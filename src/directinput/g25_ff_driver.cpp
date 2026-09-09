// SPDX-License-Identifier: GPL-2.0-only
#include "directinput/g25_ff_driver.h"
#include "directinput/effect_math.h"
#include "device/g25_device.h"
#include "device/output_session.h"
#include "protocol/force_feedback.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cwctype>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace g25::directinput {
namespace {
using Clock = std::chrono::steady_clock;
std::atomic<long> live_objects{};
std::atomic<long> server_locks{};

constexpr IID effect_driver_iid{0x02538130, 0x898f, 0x11d0,
    {0x9a, 0xd0, 0x00, 0xa0, 0xc9, 0xa0, 0x6e, 0x35}};

enum class Kind {
    constant, ramp, square, sine, triangle, sawtooth_up, sawtooth_down,
    spring, damper, inertia, friction, custom
};
struct Effect {
    Kind kind{Kind::constant};
    DWORD duration{INFINITE};
    DWORD gain{DI_FFNOMINALMAX};
    DWORD start_delay{};
    LONG direction{1};
    DICONSTANTFORCE constant{};
    DIRAMPFORCE ramp{};
    DIPERIODIC periodic{};
    DICONDITION condition{};
    DWORD custom_sample_period{};
    std::vector<LONG> custom_samples;
    DIENVELOPE envelope{};
    bool has_envelope{};
    bool playing{};
    DWORD iterations{1};
    Clock::time_point started{};
};

// Writes lg4ff force-feedback reports to the virtual G29 (046D:C24F) HID
// output. The bridge's OutputReceived relays them to the physical G25, so a
// local game gets FFB even while HidHide hides the G25 from the game process.
// The command bytes are identical to the G25 path - lg4ff is one format for
// G25/G27/G29 - only the destination differs.
class G29Output {
public:
    explicit G29Output(const std::wstring& interface_path)
        : handle_(CreateFileW(interface_path.c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr)) {
        if (!handle_.valid())
            throw std::runtime_error(windows_error("CreateFile(virtual G29)", GetLastError()));
        PHIDP_PREPARSED_DATA preparsed{};
        if (!HidD_GetPreparsedData(handle_.get(), &preparsed))
            throw std::runtime_error("HidD_GetPreparsedData(virtual G29) failed");
        HIDP_CAPS caps{};
        const auto status = HidP_GetCaps(preparsed, &caps);
        HidD_FreePreparsedData(preparsed);
        if (status != HIDP_STATUS_SUCCESS || caps.OutputReportByteLength < 8)
            throw std::runtime_error("virtual G29 has no usable HID output report");
        report_.assign(caps.OutputReportByteLength, std::uint8_t{0});
    }

    void send(const Command& command) {
        std::fill(report_.begin(), report_.end(), std::uint8_t{0});
        std::copy(command.begin(), command.end(), report_.begin() + 1);  // [0] = report id 0
        UniqueHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event.valid())
            throw std::runtime_error(windows_error("CreateEvent(G29 write)", GetLastError()));
        OVERLAPPED operation{}; operation.hEvent = event.get();
        DWORD transferred{};
        if (WriteFile(handle_.get(), report_.data(), static_cast<DWORD>(report_.size()),
                      &transferred, &operation))
            return;
        const auto error = GetLastError();
        if (error != ERROR_IO_PENDING)
            throw std::runtime_error(windows_error("WriteFile(virtual G29)", error));
        if (WaitForSingleObject(event.get(), 250) != WAIT_OBJECT_0) {
            CancelIoEx(handle_.get(), &operation);
            throw std::runtime_error("virtual G29 HID write timed out");
        }
        if (!GetOverlappedResult(handle_.get(), &operation, &transferred, FALSE))
            throw std::runtime_error(windows_error("GetOverlappedResult(virtual G29)", GetLastError()));
    }

private:
    UniqueHandle handle_;
    std::vector<std::uint8_t> report_;
};

// Watches HKCU\Software\g25-driver "Rotation" while a game holds the wheel, so a
// change to the tray's "Maximum rotation" is applied live (SET_RANGE only)
// instead of waiting for the game to exit. One thread, blocked in
// RegNotifyChangeKeyValue - no polling. Only used on the physical-G25 path; in
// G29 mode the bridge worker owns the live range.
class RangeWatcher {
public:
    explicit RangeWatcher(std::function<void(int)> on_change)
        : on_change_(std::move(on_change)),
          stop_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
        if (stop_.valid()) thread_ = std::thread(&RangeWatcher::run, this);
    }
    ~RangeWatcher() {
        if (stop_.valid()) SetEvent(stop_.get());
        if (thread_.joinable()) thread_.join();
    }
    RangeWatcher(const RangeWatcher&) = delete;
    RangeWatcher& operator=(const RangeWatcher&) = delete;

private:
    static std::optional<int> read_rotation(HKEY key) {
        DWORD value{};
        DWORD size = sizeof(value);
        if (RegGetValueW(key, nullptr, L"Rotation", RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS)
            return std::nullopt;
        const int degrees = static_cast<int>(value);
        if (degrees == 180 || degrees == 360 || degrees == 540 || degrees == 900) return degrees;
        return std::nullopt;
    }
    void run() {
        int last_applied = 0;
        while (WaitForSingleObject(stop_.get(), 0) != WAIT_OBJECT_0) {
            HKEY key{};
            if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\g25-driver", 0,
                              KEY_NOTIFY | KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
                if (WaitForSingleObject(stop_.get(), 2000) == WAIT_OBJECT_0) return;
                continue;
            }
            if (const auto degrees = read_rotation(key)) last_applied = *degrees;
            UniqueHandle changed(CreateEventW(nullptr, FALSE, FALSE, nullptr));
            bool reopen = false;
            while (!reopen && WaitForSingleObject(stop_.get(), 0) != WAIT_OBJECT_0) {
                if (RegNotifyChangeKeyValue(key, FALSE, REG_NOTIFY_CHANGE_LAST_SET,
                                            changed.get(), TRUE) != ERROR_SUCCESS) {
                    reopen = true;
                    break;
                }
                const HANDLE handles[]{stop_.get(), changed.get()};
                if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0) {
                    RegCloseKey(key);
                    return;
                }
                if (const auto degrees = read_rotation(key); degrees && *degrees != last_applied) {
                    last_applied = *degrees;
                    on_change_(*degrees);
                }
            }
            RegCloseKey(key);
        }
    }

    std::function<void(int)> on_change_;
    UniqueHandle stop_;
    std::thread thread_;
};

// The shared writer mutex is NOT held here: it has thread affinity, so worker()
// owns it for the whole session (see EffectDriver::worker).
struct Hardware {
    DeviceInfo info;
    std::wstring g29_path;                  // non-empty => drive the virtual G29
    std::unique_ptr<HidTransport> transport;
    std::unique_ptr<OutputSession> output;
    std::unique_ptr<G29Output> g29;
};

bool is_virtual_g29_path(const wchar_t* path) {
    if (!path) return false;
    std::wstring lower(path);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return lower.find(L"vid_046d&pid_c24f") != std::wstring::npos;
}

std::optional<Kind> effect_kind(DWORD id) {
    if (id == constant_effect_id) return Kind::constant;
    if (id == ramp_effect_id) return Kind::ramp;
    if (id == square_effect_id) return Kind::square;
    if (id == sine_effect_id) return Kind::sine;
    if (id == triangle_effect_id) return Kind::triangle;
    if (id == sawtooth_up_effect_id) return Kind::sawtooth_up;
    if (id == sawtooth_down_effect_id) return Kind::sawtooth_down;
    if (id == spring_effect_id) return Kind::spring;
    if (id == damper_effect_id) return Kind::damper;
    if (id == inertia_effect_id) return Kind::inertia;
    if (id == friction_effect_id) return Kind::friction;
    if (id == custom_effect_id) return Kind::custom;
    return std::nullopt;
}
bool same_path(const std::wstring& a, const wchar_t* b) {
    return b && CompareStringOrdinal(a.c_str(), -1, b, -1, TRUE) == CSTR_EQUAL;
}
template<class T> T clamp_cast(std::int64_t value) {
    return static_cast<T>(std::clamp<std::int64_t>(value,
        std::numeric_limits<T>::min(), std::numeric_limits<T>::max()));
}
std::int64_t scale(std::int64_t value, DWORD effect_gain, DWORD device_gain) {
    return value * std::min(effect_gain, DWORD{DI_FFNOMINALMAX}) *
           std::min(device_gain, DWORD{DI_FFNOMINALMAX}) /
           (std::int64_t{DI_FFNOMINALMAX} * DI_FFNOMINALMAX);
}

class EffectDriver final : public IDirectInputEffectDriver {
public:
    EffectDriver() { ++live_objects; }
    ~EffectDriver() { shutdown(); --live_objects; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, effect_driver_iid)) {
            *object = static_cast<IDirectInputEffectDriver*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(++refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto refs = --refs_;
        if (!refs) delete this;
        return static_cast<ULONG>(refs);
    }

    HRESULT STDMETHODCALLTYPE DeviceID(DWORD version, DWORD external_id, DWORD begin,
                                       DWORD, LPVOID init_data) override {
        if (version < 0x0500) return DIERR_UNSUPPORTED;
        if (!begin) {
            if (active_external_id_ == external_id) shutdown();
            return S_OK;
        }
        if (!init_data) return E_POINTER;
        const auto* init = static_cast<const DIHIDFFINITINFO*>(init_data);
        if (init->dwSize != sizeof(DIHIDFFINITINFO) || !init->pwszDeviceInterface)
            return E_INVALIDARG;
        shutdown();
        const bool drive_g29 = is_virtual_g29_path(init->pwszDeviceInterface);
        try {
            auto hardware = std::make_unique<Hardware>();
            if (drive_g29) {
                // Local game asking for FFB on our virtual G29. Drive its HID
                // output; the bridge relays to the physical G25.
                hardware->g29_path = init->pwszDeviceInterface;
            } else {
                const auto devices = enumerate_wheels();
                const auto found = std::find_if(devices.begin(), devices.end(), [&](const DeviceInfo& info) {
                    return same_path(info.path, init->pwszDeviceInterface);
                });
                if (found == devices.end()) return DIERR_DEVICENOTREG;
                require_g25_writer(*found, false);
                hardware->info = *found;
            }
            {
                std::lock_guard lock(mutex_);
                hardware_ = std::move(hardware);
                active_external_id_ = external_id;
                quitting_ = false;
                hardware_failed_ = false;
                lock_settled_ = false;
                pending_range_.reset();
                last_.fill(std::nullopt);
            }
            worker_ = std::thread(&EffectDriver::worker, this);
            // The worker takes the shared writer mutex; wait for that to settle so
            // "another writer owns the wheel" is still reported synchronously.
            {
                std::unique_lock lock(mutex_);
                lock_ready_.wait(lock, [this] { return lock_settled_; });
                if (hardware_failed_) {
                    lock.unlock();
                    shutdown();
                    return DIERR_INPUTLOST;
                }
            }
            if (!drive_g29) {
                range_watcher_ = std::make_unique<RangeWatcher>([this](int degrees) {
                    std::lock_guard lock(mutex_);
                    pending_range_ = degrees;
                    wake_.notify_one();
                });
            }
            return S_OK;
        } catch (...) {
            shutdown();
            return DIERR_GENERIC;
        }
    }
    HRESULT STDMETHODCALLTYPE GetVersions(LPDIDRIVERVERSIONS versions) override {
        if (!versions) return E_POINTER;
        if (versions->dwSize != sizeof(DIDRIVERVERSIONS)) return E_INVALIDARG;
        versions->dwFirmwareRevision = 0x1222;
        versions->dwHardwareRevision = 0x1222;
        versions->dwFFDriverVersion = 0x00010000;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Escape(DWORD, DWORD, LPDIEFFESCAPE) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetGain(DWORD external_id, DWORD gain) override {
        std::lock_guard lock(mutex_);
        if (!valid_device(external_id)) return DIERR_NOTINITIALIZED;
        const auto bounded = std::min(gain, DWORD{DI_FFNOMINALMAX});
        device_gain_ = bounded;
        wake_.notify_one();
        return bounded == gain ? S_OK : DI_TRUNCATED;
    }
    HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD external_id, DWORD command) override {
        std::lock_guard lock(mutex_);
        if (!valid_device(external_id)) return DIERR_NOTINITIALIZED;
        if (!activate_locked()) return DIERR_INPUTLOST;
        switch (command) {
        case DISFFC_RESET:
            effects_.clear();
            stopped_ = true; paused_ = false;
            break;
        case DISFFC_STOPALL:
            for (auto& [handle, effect] : effects_) { (void)handle; effect.playing = false; }
            stopped_ = true; paused_ = false;
            break;
        case DISFFC_PAUSE:
            if (!paused_) { paused_ = true; paused_at_ = Clock::now(); }
            break;
        case DISFFC_CONTINUE:
            if (paused_) {
                const auto delta = Clock::now() - paused_at_;
                for (auto& [handle, effect] : effects_) { (void)handle; if (effect.playing) effect.started += delta; }
                paused_ = false;
            }
            break;
        case DISFFC_SETACTUATORSON: actuators_ = true; break;
        case DISFFC_SETACTUATORSOFF: actuators_ = false; break;
        default: return E_NOTIMPL;
        }
        wake_.notify_one();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetForceFeedbackState(DWORD external_id, LPDIDEVICESTATE state) override {
        if (!state) return E_POINTER;
        if (state->dwSize != sizeof(DIDEVICESTATE)) return E_INVALIDARG;
        std::lock_guard lock(mutex_);
        if (!valid_device(external_id)) return DIERR_NOTINITIALIZED;
        state->dwState = DIGFFS_POWERON | DIGFFS_SAFETYSWITCHOFF | DIGFFS_USERFFSWITCHON;
        state->dwState |= effects_.empty() ? DIGFFS_EMPTY : 0;
        state->dwState |= stopped_ ? DIGFFS_STOPPED : 0;
        state->dwState |= paused_ ? DIGFFS_PAUSED : 0;
        state->dwState |= actuators_ ? DIGFFS_ACTUATORSON : DIGFFS_ACTUATORSOFF;
        state->dwLoad = static_cast<DWORD>(std::min<std::size_t>(100, effects_.size() * 2));
        return hardware_failed_ ? DIERR_INPUTLOST : S_OK;
    }
    HRESULT STDMETHODCALLTYPE DownloadEffect(DWORD external_id, DWORD type, LPDWORD handle,
                                             LPCDIEFFECT input, DWORD flags) override {
        try { return download_effect(external_id, type, handle, input, flags); }
        catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
        catch (...) { return DIERR_GENERIC; }
    }
    HRESULT download_effect(DWORD external_id, DWORD type, LPDWORD handle,
                            LPCDIEFFECT input, DWORD flags) {
        if (!handle || !input) return E_POINTER;
        const auto kind = effect_kind(type);
        if (!kind) return DIERR_UNSUPPORTED;
        if (input->dwSize < sizeof(DIEFFECT_DX5)) return E_INVALIDARG;
        if (input->cAxes != 1 || !input->rgdwAxes || !input->rglDirection) return DIERR_INVALIDPARAM;
        if ((input->dwFlags & DIEFF_POLAR) || (input->dwFlags & DIEFF_SPHERICAL)) return DIERR_UNSUPPORTED;
        if (input->lpEnvelope && input->lpEnvelope->dwSize != sizeof(DIENVELOPE)) return E_INVALIDARG;

        Effect candidate;
        candidate.kind = *kind;
        candidate.duration = input->dwDuration;
        candidate.gain = std::min(input->dwGain, DWORD{DI_FFNOMINALMAX});
        candidate.start_delay = input->dwSize >= sizeof(DIEFFECT) ? input->dwStartDelay : 0;
        candidate.direction = input->rglDirection[0] < 0 ? -1 : 1;
        if (input->lpEnvelope) { candidate.envelope = *input->lpEnvelope; candidate.has_envelope = true; }
        if (!input->lpvTypeSpecificParams) return DIERR_INVALIDPARAM;
        if (*kind == Kind::constant) {
            if (input->cbTypeSpecificParams < sizeof(DICONSTANTFORCE)) return DIERR_INVALIDPARAM;
            candidate.constant = *static_cast<const DICONSTANTFORCE*>(input->lpvTypeSpecificParams);
            candidate.constant.lMagnitude = std::clamp(candidate.constant.lMagnitude,
                                                       -LONG{DI_FFNOMINALMAX}, LONG{DI_FFNOMINALMAX});
        } else if (*kind == Kind::ramp) {
            if (input->cbTypeSpecificParams < sizeof(DIRAMPFORCE)) return DIERR_INVALIDPARAM;
            candidate.ramp = *static_cast<const DIRAMPFORCE*>(input->lpvTypeSpecificParams);
            candidate.ramp.lStart = std::clamp(candidate.ramp.lStart,
                -LONG{DI_FFNOMINALMAX}, LONG{DI_FFNOMINALMAX});
            candidate.ramp.lEnd = std::clamp(candidate.ramp.lEnd,
                -LONG{DI_FFNOMINALMAX}, LONG{DI_FFNOMINALMAX});
        } else if (*kind == Kind::square || *kind == Kind::sine || *kind == Kind::triangle ||
                   *kind == Kind::sawtooth_up || *kind == Kind::sawtooth_down) {
            if (input->cbTypeSpecificParams < sizeof(DIPERIODIC)) return DIERR_INVALIDPARAM;
            candidate.periodic = *static_cast<const DIPERIODIC*>(input->lpvTypeSpecificParams);
            if (!candidate.periodic.dwPeriod) return DIERR_INVALIDPARAM;
            candidate.periodic.dwMagnitude = std::min(candidate.periodic.dwMagnitude,
                DWORD{DI_FFNOMINALMAX});
            candidate.periodic.lOffset = std::clamp(candidate.periodic.lOffset,
                -LONG{DI_FFNOMINALMAX}, LONG{DI_FFNOMINALMAX});
            candidate.periodic.dwPhase %= 360 * DI_DEGREES;
        } else if (*kind == Kind::spring || *kind == Kind::damper ||
                   *kind == Kind::inertia || *kind == Kind::friction) {
            if (input->cbTypeSpecificParams < sizeof(DICONDITION)) return DIERR_INVALIDPARAM;
            candidate.condition = static_cast<const DICONDITION*>(input->lpvTypeSpecificParams)[0];
            candidate.condition.lOffset = std::clamp(candidate.condition.lOffset,
                -LONG{DI_FFNOMINALMAX}, LONG{DI_FFNOMINALMAX});
            candidate.condition.lPositiveCoefficient = std::clamp(candidate.condition.lPositiveCoefficient,
                -LONG{DI_FFNOMINALMAX}, LONG{DI_FFNOMINALMAX});
            candidate.condition.lNegativeCoefficient = std::clamp(candidate.condition.lNegativeCoefficient,
                -LONG{DI_FFNOMINALMAX}, LONG{DI_FFNOMINALMAX});
            candidate.condition.dwPositiveSaturation = std::min(candidate.condition.dwPositiveSaturation,
                DWORD{DI_FFNOMINALMAX});
            candidate.condition.dwNegativeSaturation = std::min(candidate.condition.dwNegativeSaturation,
                DWORD{DI_FFNOMINALMAX});
            candidate.condition.lDeadBand = std::clamp(candidate.condition.lDeadBand,
                LONG{0}, LONG{DI_FFNOMINALMAX * 2});
        } else {
            if (input->cbTypeSpecificParams < sizeof(DICUSTOMFORCE)) return DIERR_INVALIDPARAM;
            const auto& custom = *static_cast<const DICUSTOMFORCE*>(input->lpvTypeSpecificParams);
            if (custom.cChannels != 1 || !custom.dwSamplePeriod || !custom.cSamples ||
                custom.cSamples > 65536 || !custom.rglForceData) return DIERR_INVALIDPARAM;
            candidate.custom_sample_period = custom.dwSamplePeriod;
            candidate.custom_samples.assign(custom.rglForceData, custom.rglForceData + custom.cSamples);
            for (auto& sample : candidate.custom_samples)
                sample = std::clamp(sample, -LONG{DI_FFNOMINALMAX}, LONG{DI_FFNOMINALMAX});
        }
        if (flags & DIEP_NODOWNLOAD) return S_OK;

        std::lock_guard lock(mutex_);
        if (!valid_device(external_id)) return DIERR_NOTINITIALIZED;
        if (*handle) {
            const auto existing = effects_.find(*handle);
            if (existing == effects_.end()) return static_cast<HRESULT>(DIERR_NOTDOWNLOADED);
            candidate.playing = existing->second.playing;
            candidate.iterations = existing->second.iterations;
            candidate.started = existing->second.started;
            existing->second = candidate;
        } else {
            if (effects_.size() >= 64) return static_cast<HRESULT>(DIERR_DEVICEFULL);
            do { ++next_handle_; } while (!next_handle_ || effects_.contains(next_handle_));
            *handle = next_handle_;
            effects_.emplace(*handle, candidate);
        }
        if ((flags & DIEP_START) && !activate_locked()) return DIERR_INPUTLOST;
        if (flags & DIEP_START) start_locked(*handle, 1, false);
        wake_.notify_one();
        return candidate.gain == input->dwGain ? S_OK : DI_TRUNCATED;
    }
    HRESULT STDMETHODCALLTYPE DestroyEffect(DWORD external_id, DWORD handle) override {
        std::lock_guard lock(mutex_);
        if (!valid_device(external_id)) return DIERR_NOTINITIALIZED;
        if (!effects_.erase(handle)) return static_cast<HRESULT>(DIERR_NOTDOWNLOADED);
        wake_.notify_one();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE StartEffect(DWORD external_id, DWORD handle, DWORD mode, DWORD count) override {
        std::lock_guard lock(mutex_);
        if (!valid_device(external_id)) return DIERR_NOTINITIALIZED;
        if (!activate_locked()) return DIERR_INPUTLOST;
        return start_locked(handle, count, (mode & DIES_SOLO) != 0);
    }
    HRESULT STDMETHODCALLTYPE StopEffect(DWORD external_id, DWORD handle) override {
        std::lock_guard lock(mutex_);
        if (!valid_device(external_id)) return DIERR_NOTINITIALIZED;
        const auto found = effects_.find(handle);
        if (found == effects_.end()) return static_cast<HRESULT>(DIERR_NOTDOWNLOADED);
        found->second.playing = false;
        wake_.notify_one();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEffectStatus(DWORD external_id, DWORD handle, LPDWORD status) override {
        if (!status) return E_POINTER;
        std::lock_guard lock(mutex_);
        if (!valid_device(external_id)) return DIERR_NOTINITIALIZED;
        const auto found = effects_.find(handle);
        if (found == effects_.end()) return static_cast<HRESULT>(DIERR_NOTDOWNLOADED);
        *status = found->second.playing ? DIEGES_PLAYING : 0;
        return S_OK;
    }

private:
    bool valid_device(DWORD id) const { return hardware_ && id == active_external_id_; }
    bool activate_locked() {
        if (!hardware_ || hardware_failed_) return false;
        if (hardware_->output || hardware_->g29) return true;
        try {
            if (!hardware_->g29_path.empty()) {
                auto g29 = std::make_unique<G29Output>(hardware_->g29_path);
                g29->send(stop_all());
                g29->send(disable_autocenter());
                hardware_->g29 = std::move(g29);
            } else {
                // worker() already holds the shared writer mutex for this session.
                hardware_->transport = std::make_unique<HidTransport>(hardware_->info, Access::write);
                auto output = std::make_unique<OutputSession>(*hardware_->transport);
                output->initialize();
                hardware_->output = std::move(output);
            }
            last_.fill(std::nullopt);
            return true;
        } catch (...) {
            hardware_failed_ = true;
            hardware_->output.reset();
            hardware_->g29.reset();
            hardware_->transport.reset();
            return false;
        }
    }
    HRESULT start_locked(DWORD handle, DWORD count, bool solo) {
        const auto found = effects_.find(handle);
        if (found == effects_.end()) return static_cast<HRESULT>(DIERR_NOTDOWNLOADED);
        if (solo) for (auto& [id, effect] : effects_) if (id != handle) effect.playing = false;
        found->second.playing = true;
        found->second.iterations = count ? count : 1;
        found->second.started = Clock::now();
        stopped_ = false;
        wake_.notify_one();
        return S_OK;
    }
    std::int64_t apply_envelope(std::int64_t level, const Effect& effect,
                                std::uint64_t in_cycle_us) const {
        if (!effect.has_envelope || level == 0) return level;
        const auto sign = level < 0 ? -1 : 1;
        const auto magnitude = std::abs(level);
        if (effect.envelope.dwAttackTime && in_cycle_us < effect.envelope.dwAttackTime) {
            const auto start = std::min<std::int64_t>(effect.envelope.dwAttackLevel, DI_FFNOMINALMAX);
            return sign * (start + (magnitude - start) * static_cast<std::int64_t>(in_cycle_us) /
                           effect.envelope.dwAttackTime);
        }
        if (effect.duration != INFINITE && effect.envelope.dwFadeTime &&
            in_cycle_us + effect.envelope.dwFadeTime > effect.duration) {
            const auto elapsed = in_cycle_us + effect.envelope.dwFadeTime - effect.duration;
            const auto end = std::min<std::int64_t>(effect.envelope.dwFadeLevel, DI_FFNOMINALMAX);
            return sign * (magnitude + (end - magnitude) * static_cast<std::int64_t>(elapsed) /
                           effect.envelope.dwFadeTime);
        }
        return level;
    }
    std::int64_t synthesized_level(const Effect& effect, std::uint64_t in_cycle_us) const {
        std::int64_t level{};
        switch (effect.kind) {
        case Kind::constant:
            level = apply_envelope(effect.constant.lMagnitude, effect, in_cycle_us);
            break;
        case Kind::ramp:
            if (effect.duration == INFINITE) level = effect.ramp.lStart;
            else level = ramp_level(effect.ramp.lStart, effect.ramp.lEnd, in_cycle_us, effect.duration);
            level = apply_envelope(level, effect, in_cycle_us);
            break;
        case Kind::square:
        case Kind::sine:
        case Kind::triangle:
        case Kind::sawtooth_up:
        case Kind::sawtooth_down: {
            Waveform waveform = Waveform::square;
            if (effect.kind == Kind::sine) waveform = Waveform::sine;
            else if (effect.kind == Kind::triangle) waveform = Waveform::triangle;
            else if (effect.kind == Kind::sawtooth_up) waveform = Waveform::sawtooth_up;
            else if (effect.kind == Kind::sawtooth_down) waveform = Waveform::sawtooth_down;
            const double wave = periodic_wave(waveform, in_cycle_us, effect.periodic.dwPeriod,
                                              effect.periodic.dwPhase);
            const auto magnitude = apply_envelope(effect.periodic.dwMagnitude, effect, in_cycle_us);
            level = effect.periodic.lOffset + static_cast<std::int64_t>(std::llround(wave * static_cast<double>(magnitude)));
            break;
        }
        case Kind::custom: {
            const auto index = custom_sample_index(in_cycle_us, effect.custom_sample_period,
                                                   effect.custom_samples.size());
            level = apply_envelope(effect.custom_samples[index], effect, in_cycle_us);
            break;
        }
        default: break;
        }
        return std::clamp<std::int64_t>(level * effect.direction, -DI_FFNOMINALMAX, DI_FFNOMINALMAX);
    }
    void build_commands(Clock::time_point now, std::array<std::optional<Command>, 4>& desired) {
        if (!actuators_ || paused_ || stopped_) return;
        std::int64_t constant_total{};
        bool synthesized_active{};
        const Effect* best_spring{};
        const Effect* best_damper{};
        const Effect* best_friction{};
        for (auto& [handle, effect] : effects_) {
            (void)handle;
            if (!effect.playing) continue;
            const auto elapsed_signed = std::chrono::duration_cast<std::chrono::microseconds>(now - effect.started).count();
            if (elapsed_signed < effect.start_delay) continue;
            const auto elapsed = static_cast<std::uint64_t>(elapsed_signed - effect.start_delay);
            std::uint64_t in_cycle = elapsed;
            if (effect.duration == 0) { effect.playing = false; continue; }
            if (effect.duration != INFINITE && effect.duration != 0) {
                const auto cycle = elapsed / effect.duration;
                if (effect.iterations != INFINITE && cycle >= effect.iterations) {
                    effect.playing = false;
                    continue;
                }
                in_cycle %= effect.duration;
            }
            if (effect.kind == Kind::constant || effect.kind == Kind::ramp || effect.kind == Kind::square ||
                effect.kind == Kind::sine || effect.kind == Kind::triangle || effect.kind == Kind::sawtooth_up ||
                effect.kind == Kind::sawtooth_down || effect.kind == Kind::custom) {
                synthesized_active = true;
                constant_total += scale(synthesized_level(effect, in_cycle), effect.gain, device_gain_);
            } else if (std::max(effect.condition.dwPositiveSaturation,
                                effect.condition.dwNegativeSaturation) == 0) {
                continue;
            } else if (effect.kind == Kind::spring) {
                const auto strength = std::max(std::abs(effect.condition.lPositiveCoefficient),
                                               std::abs(effect.condition.lNegativeCoefficient)) *
                                      static_cast<std::int64_t>(effect.gain);
                const auto previous = best_spring ? std::max(std::abs(best_spring->condition.lPositiveCoefficient),
                    std::abs(best_spring->condition.lNegativeCoefficient)) * static_cast<std::int64_t>(best_spring->gain) : -1;
                if (strength > previous) best_spring = &effect;
            } else if (effect.kind == Kind::friction) {
                const auto strength = std::max(std::abs(effect.condition.lPositiveCoefficient),
                                               std::abs(effect.condition.lNegativeCoefficient)) *
                                      static_cast<std::int64_t>(effect.gain);
                const auto previous = best_friction ? std::max(std::abs(best_friction->condition.lPositiveCoefficient),
                    std::abs(best_friction->condition.lNegativeCoefficient)) * static_cast<std::int64_t>(best_friction->gain) : -1;
                if (strength > previous) best_friction = &effect;
            } else {
                const auto strength = std::max(std::abs(effect.condition.lPositiveCoefficient),
                                               std::abs(effect.condition.lNegativeCoefficient)) *
                                      static_cast<std::int64_t>(effect.gain);
                const auto previous = best_damper ? std::max(std::abs(best_damper->condition.lPositiveCoefficient),
                    std::abs(best_damper->condition.lNegativeCoefficient)) * static_cast<std::int64_t>(best_damper->gain) : -1;
                if (strength > previous) best_damper = &effect;
            }
        }
        if (synthesized_active) {
            const auto direct = std::clamp<std::int64_t>(constant_total, -DI_FFNOMINALMAX, DI_FFNOMINALMAX);
            desired[0] = constant_force(clamp_cast<std::int16_t>(direct * 32767 / DI_FFNOMINALMAX));
        }
        const auto condition_command = [&](const Effect& effect) {
            const auto coefficient = [&](LONG value) {
                const auto direct = scale(value, effect.gain, device_gain_);
                return clamp_cast<std::int16_t>(direct * 32767 / DI_FFNOMINALMAX);
            };
            const auto saturation = [&](DWORD a, DWORD b) {
                const auto direct = scale(std::max(a, b), effect.gain, device_gain_);
                return static_cast<std::uint16_t>(std::clamp<std::int64_t>(direct * 65535 / DI_FFNOMINALMAX, 0, 65535));
            };
            const auto& c = effect.condition;
            if (effect.kind == Kind::friction)
                return friction(coefficient(c.lNegativeCoefficient), coefficient(c.lPositiveCoefficient),
                                saturation(c.dwNegativeSaturation, c.dwPositiveSaturation));
            if (effect.kind != Kind::spring)
                return damper(coefficient(c.lNegativeCoefficient), coefficient(c.lPositiveCoefficient),
                              saturation(c.dwNegativeSaturation, c.dwPositiveSaturation));
            const auto left = std::clamp<std::int64_t>(c.lOffset - static_cast<std::int64_t>(c.lDeadBand) / 2,
                                                       -DI_FFNOMINALMAX, DI_FFNOMINALMAX);
            const auto right = std::clamp<std::int64_t>(c.lOffset + static_cast<std::int64_t>(c.lDeadBand) / 2,
                                                        -DI_FFNOMINALMAX, DI_FFNOMINALMAX);
            return spring(clamp_cast<std::int16_t>(left * 32767 / DI_FFNOMINALMAX),
                          clamp_cast<std::int16_t>(right * 32767 / DI_FFNOMINALMAX),
                          coefficient(c.lNegativeCoefficient), coefficient(c.lPositiveCoefficient),
                          saturation(c.dwNegativeSaturation, c.dwPositiveSaturation));
        };
        if (best_spring) desired[1] = condition_command(*best_spring);
        if (best_damper) desired[2] = condition_command(*best_damper);
        if (best_friction) desired[3] = condition_command(*best_friction);
    }
    void worker() noexcept {
        // A Win32 mutex may only be released by the thread that took it, and this
        // thread outlives every COM call that touches the wheel - so the shared
        // writer lock is taken and dropped here, not in activate_locked(). Taking
        // it on the game's DirectInput thread and dropping it on whichever thread
        // ran shutdown() left it held for the life of the game process.
        // Wait briefly rather than failing fast: the tray holds the mutex for a
        // few milliseconds when it applies a rotation, and losing that race would
        // cost the game force feedback for its whole session.
        std::unique_ptr<WriterLock> writer_lock;
        bool needs_wheel;
        {
            std::lock_guard guard(mutex_);
            needs_wheel = hardware_ && hardware_->g29_path.empty();
        }
        bool lock_failed = false;
        if (needs_wheel) {
            try { writer_lock = std::make_unique<WriterLock>(std::chrono::milliseconds{500}); }
            catch (...) { lock_failed = true; }
        }
        {
            std::lock_guard guard(mutex_);
            if (lock_failed) hardware_failed_ = true;
            lock_settled_ = true;
        }
        lock_ready_.notify_all();

        std::unique_lock lock(mutex_);
        while (!quitting_) {
            // Live "Maximum rotation" change from the tray (SET_RANGE only, so
            // running forces and wheel centre are undisturbed). Applied on this
            // thread, which already owns the output endpoint.
            if (pending_range_ && hardware_ && hardware_->transport && !hardware_failed_) {
                const int degrees = *pending_range_;
                pending_range_.reset();
                auto* transport = hardware_->transport.get();
                lock.unlock();
                bool failed = false;
                try { transport->send(set_range(degrees)); }
                catch (...) { failed = true; }
                lock.lock();
                if (failed) hardware_failed_ = true;
            }
            std::array<std::optional<Command>, 4> desired;
            build_commands(Clock::now(), desired);
            for (unsigned slot = 0; slot < desired.size() && !hardware_failed_; ++slot) {
                if (desired[slot] == last_[slot]) continue;
                auto command = desired[slot].value_or(stop_force_slot(slot));
                if (desired[slot] && last_[slot])
                    command[0] = static_cast<std::uint8_t>((command[0] & 0xf0u) | 0x0cu);
                auto* transport = hardware_ && hardware_->output ? hardware_->transport.get() : nullptr;
                auto* g29 = hardware_ && hardware_->g29 ? hardware_->g29.get() : nullptr;
                lock.unlock();
                try {
                    if (transport) transport->send(command);
                    else if (g29) g29->send(command);
                }
                catch (...) { lock.lock(); hardware_failed_ = true; break; }
                lock.lock();
                last_[slot] = desired[slot];
            }
            wake_.wait_for(lock, std::chrono::milliseconds(4));
        }
    }
    void shutdown() noexcept {
        range_watcher_.reset();   // joins the watcher thread before we tear down
        {
            std::lock_guard lock(mutex_);
            quitting_ = true;
            wake_.notify_all();
        }
        if (worker_.joinable()) worker_.join();
        std::unique_ptr<Hardware> hardware;
        {
            std::lock_guard lock(mutex_);
            effects_.clear();
            hardware = std::move(hardware_);
            active_external_id_ = std::numeric_limits<DWORD>::max();
        }
        if (hardware && hardware->output) {
            try { hardware->output->finish(); } catch (...) {}
        }
        if (hardware && hardware->g29) {
            try { hardware->g29->send(stop_all()); hardware->g29->send(disable_autocenter()); } catch (...) {}
        }
    }

    std::atomic<long> refs_{1};
    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable lock_ready_;   // worker() has settled the shared writer mutex
    bool lock_settled_{true};
    std::thread worker_;
    std::unique_ptr<RangeWatcher> range_watcher_;
    std::optional<int> pending_range_;   // guarded by mutex_; set by the watcher, applied by worker()
    std::unique_ptr<Hardware> hardware_;
    std::unordered_map<DWORD, Effect> effects_;
    std::array<std::optional<Command>, 4> last_{};
    DWORD active_external_id_{std::numeric_limits<DWORD>::max()};
    DWORD next_handle_{};
    DWORD device_gain_{DI_FFNOMINALMAX};
    bool quitting_{true};
    bool stopped_{true};
    bool paused_{};
    bool actuators_{true};
    bool hardware_failed_{};
    Clock::time_point paused_at_{};
};

class ClassFactory final : public IClassFactory {
public:
    ClassFactory() { ++live_objects; }
    ~ClassFactory() { --live_objects; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, IID_IClassFactory)) {
            *object = static_cast<IClassFactory*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(++refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto refs = --refs_; if (!refs) delete this; return static_cast<ULONG>(refs);
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** object) override {
        if (outer) return CLASS_E_NOAGGREGATION;
        if (!object) return E_POINTER;
        auto* driver = new (std::nothrow) EffectDriver();
        if (!driver) return E_OUTOFMEMORY;
        const auto result = driver->QueryInterface(iid, object);
        driver->Release();
        return result;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
        if (lock) ++server_locks; else --server_locks;
        return S_OK;
    }
private:
    std::atomic<long> refs_{1};
};
}
}

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID iid, void** object) {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (!IsEqualCLSID(clsid, g25::directinput::class_id)) return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory = new (std::nothrow) g25::directinput::ClassFactory();
    if (!factory) return E_OUTOFMEMORY;
    const auto result = factory->QueryInterface(iid, object);
    factory->Release();
    return result;
}
extern "C" HRESULT __stdcall DllCanUnloadNow() {
    return g25::directinput::live_objects.load() == 0 && g25::directinput::server_locks.load() == 0 ? S_OK : S_FALSE;
}
