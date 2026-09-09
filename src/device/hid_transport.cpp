// SPDX-License-Identifier: GPL-2.0-only
#include "device/hid_transport.h"
#include <setupapi.h>
#include <algorithm>
#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace g25 {
namespace {
struct DeviceSet {
    HDEVINFO value;
    ~DeviceSet() { if (value != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(value); }
};
struct Preparsed {
    PHIDP_PREPARSED_DATA value{};
    ~Preparsed() { if (value) HidD_FreePreparsedData(value); }
};
void hid_status(NTSTATUS status, const char* operation) {
    if (status != HIDP_STATUS_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed, NTSTATUS=" + std::to_string(status));
}
bool verify_native_offsets(PHIDP_PREPARSED_DATA preparsed, Model model) {
    // Exercise the Windows parser with walking bits in LOCAL buffers. This
    // verifies the hardcoded decoder's offsets, not just sizes/usages. These
    // synthetic reports are NEVER sent to the wheel.
    const USAGE max_button = model == Model::g27 ? 23 : 19;
    for (unsigned bit = 0; bit < 88; ++bit) {
        std::array<std::uint8_t, 12> report{};
        report[1 + bit / 8] = static_cast<std::uint8_t>(1u << (bit % 8));
        const auto expected = decode_windows_input(report, model);
        const std::array<USAGE, 5> usages{0x30, 0x32, 0x35, 0x31, 0x39};
        const std::array<ULONG, 5> values{expected.wheel, expected.throttle, expected.brake, expected.clutch, expected.hat};
        for (std::size_t index = 0; index < usages.size(); ++index) {
            ULONG value{};
            if (HidP_GetUsageValue(HidP_Input, 1, 0, usages[index], &value, preparsed,
                                  reinterpret_cast<PCHAR>(report.data()), static_cast<ULONG>(report.size())) != HIDP_STATUS_SUCCESS ||
                value != values[index]) return false;
        }
        std::array<USAGE, 24> buttons{};
        ULONG count = static_cast<ULONG>(buttons.size());
        if (HidP_GetUsages(HidP_Input, 9, 0, buttons.data(), &count, preparsed,
                          reinterpret_cast<PCHAR>(report.data()), static_cast<ULONG>(report.size())) != HIDP_STATUS_SUCCESS) return false;
        std::uint32_t mask{};
        for (ULONG index = 0; index < count; ++index) {
            if (buttons[index] < 1 || buttons[index] > max_button) return false;
            mask |= 1u << (buttons[index] - 1);
        }
        if (mask != expected.buttons) return false;
    }
    return true;
}
void describe(HANDLE handle, DeviceInfo& info) {
    info.caps_valid = false;
    info.native_offsets_verified = false;
    HIDD_ATTRIBUTES attr{}; attr.Size = sizeof(attr);
    if (!HidD_GetAttributes(handle, &attr)) throw std::runtime_error(windows_error("HidD_GetAttributes", GetLastError()));
    info.vid = attr.VendorID; info.pid = attr.ProductID; info.revision = attr.VersionNumber;
    wchar_t name[128]{};
    if (HidD_GetProductString(handle, name, sizeof(name))) info.name = name;
    Preparsed preparsed;
    if (!HidD_GetPreparsedData(handle, &preparsed.value))
        throw std::runtime_error(windows_error("HidD_GetPreparsedData", GetLastError()));
    hid_status(HidP_GetCaps(preparsed.value, &info.caps), "HidP_GetCaps");
    auto count = info.caps.NumberInputValueCaps;
    info.input_values.resize(count);
    if (count != 0) {
        hid_status(HidP_GetValueCaps(HidP_Input, info.input_values.data(), &count, preparsed.value), "HidP_GetValueCaps(input)");
        info.input_values.resize(count);
    }
    count = info.caps.NumberOutputValueCaps;
    info.output_values.resize(count);
    if (count != 0) {
        hid_status(HidP_GetValueCaps(HidP_Output, info.output_values.data(), &count, preparsed.value), "HidP_GetValueCaps(output)");
        info.output_values.resize(count);
    }
    count = info.caps.NumberInputButtonCaps;
    info.input_buttons.resize(count);
    if (count != 0) {
        hid_status(HidP_GetButtonCaps(HidP_Input, info.input_buttons.data(), &count, preparsed.value), "HidP_GetButtonCaps");
        info.input_buttons.resize(count);
    }
    info.caps_valid = true;
    if (native_input_layout(info))
        info.native_offsets_verified =
            verify_native_offsets(preparsed.value, identify_model(info.pid, info.revision));
}
bool usage_matches(const HIDP_VALUE_CAPS& cap, USAGE page, USAGE usage) {
    return cap.UsagePage == page &&
        (cap.IsRange ? usage >= cap.Range.UsageMin && usage <= cap.Range.UsageMax : cap.NotRange.Usage == usage);
}
bool axis_matches(const DeviceInfo& info, USAGE usage, USHORT bits, LONG maximum) {
    return std::any_of(info.input_values.begin(), info.input_values.end(), [&](const auto& cap) {
        return usage_matches(cap, 1, usage) && cap.ReportID == 0 && cap.BitSize == bits &&
            cap.LogicalMin == 0 && cap.LogicalMax == maximum && cap.IsAbsolute;
    });
}

// An OVERLAPPED and its buffer must outlive cancellation completion. CancelIoEx
// alone is insufficient. The caller keeps both alive until this returns.
void cancel_and_drain(HANDLE handle, OVERLAPPED& operation) noexcept {
    CancelIoEx(handle, &operation);
    DWORD ignored{};
    GetOverlappedResult(handle, &operation, &ignored, TRUE);
}
bool await_io(HANDLE handle, OVERLAPPED& operation, HANDLE stop_event, DWORD timeout, DWORD& transferred) {
    HANDLE events[]{operation.hEvent, stop_event};
    const DWORD result = WaitForMultipleObjects(stop_event ? 2u : 1u, events, FALSE, timeout);
    if (result == WAIT_TIMEOUT || result == WAIT_OBJECT_0 + 1) {
        cancel_and_drain(handle, operation);
        return false;
    }
    if (result != WAIT_OBJECT_0) {
        const auto error = GetLastError();
        cancel_and_drain(handle, operation);
        throw std::runtime_error(windows_error("WaitForMultipleObjects(HID)", error));
    }
    if (!GetOverlappedResult(handle, &operation, &transferred, FALSE))
        throw std::runtime_error(windows_error("GetOverlappedResult(HID)", GetLastError()));
    return true;
}
}
std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) throw std::runtime_error("string too long");
    const auto length = static_cast<int>(value.size());
    const int needed = WideCharToMultiByte(CP_UTF8, 0, value.data(), length, nullptr, 0, nullptr, nullptr);
    if (!needed) throw std::runtime_error("WideCharToMultiByte failed");
    std::string output(static_cast<std::size_t>(needed), '\0');
    if (!WideCharToMultiByte(CP_UTF8, 0, value.data(), length, output.data(), needed, nullptr, nullptr))
        throw std::runtime_error("WideCharToMultiByte failed");
    return output;
}
std::string windows_error(const std::string& operation, DWORD error) {
    wchar_t message[512]{};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, error, 0, message, 512, nullptr);
    return operation + " (Win32 " + std::to_string(error) + "): " + utf8(message);
}
std::vector<DeviceInfo> enumerate_wheels() {
    GUID guid{}; HidD_GetHidGuid(&guid);
    DeviceSet set{SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)};
    if (set.value == INVALID_HANDLE_VALUE) throw std::runtime_error(windows_error("SetupDiGetClassDevs", GetLastError()));
    std::vector<DeviceInfo> devices;
    for (DWORD index = 0; ; ++index) {
        SP_DEVICE_INTERFACE_DATA iface{}; iface.cbSize = sizeof(iface);
        if (!SetupDiEnumDeviceInterfaces(set.value, nullptr, &guid, index, &iface)) {
            const auto error = GetLastError();
            if (error == ERROR_NO_MORE_ITEMS) break;
            throw std::runtime_error(windows_error("SetupDiEnumDeviceInterfaces", error));
        }
        DWORD bytes{};
        SetupDiGetDeviceInterfaceDetailW(set.value, &iface, nullptr, 0, &bytes, nullptr);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))
            throw std::runtime_error(windows_error("SetupDiGetDeviceInterfaceDetail(size)", GetLastError()));
        // max_align_t backing avoids an unaligned structure reinterpretation.
        std::vector<std::max_align_t> storage((bytes + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set.value, &iface, detail, bytes, nullptr, nullptr))
            throw std::runtime_error(windows_error("SetupDiGetDeviceInterfaceDetail", GetLastError()));
        DeviceInfo info; info.path = detail->DevicePath;
        UniqueHandle handle(CreateFileW(info.path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       nullptr, OPEN_EXISTING, 0, nullptr));
        if (!handle.valid()) {
            const auto error = GetLastError();
            // Do not emit unrelated device paths. A Logitech path is only a
            // diagnostic hint, never sufficient authorization to send commands.
            if (info.path.find(L"vid_046d&pid_c29") != std::wstring::npos)
                std::cerr << windows_error("HID query " + utf8(info.path), error);
            continue;
        }
        HIDD_ATTRIBUTES attr{}; attr.Size = sizeof(attr);
        if (!HidD_GetAttributes(handle.get(), &attr)) continue;
        if (attr.VendorID != logitech_vid || !supported_pid(attr.ProductID)) continue;
        info.vid = attr.VendorID; info.pid = attr.ProductID; info.revision = attr.VersionNumber;
        try { describe(handle.get(), info); }
        catch (const std::exception& error) { info.diagnostic = error.what(); }
        devices.push_back(std::move(info));
    }
    std::sort(devices.begin(), devices.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
    return devices;
}
bool native_input_layout(const DeviceInfo& info) {
    if (!info.caps_valid || (info.pid != g25_pid && info.pid != g27_pid) ||
        info.caps.InputReportByteLength != 12 || info.caps.UsagePage != 1 || info.caps.Usage != 4)
        return false;
    if (!axis_matches(info, 0x30, 14, 16383) || !axis_matches(info, 0x32, 8, 255) ||
        !axis_matches(info, 0x35, 8, 255) || !axis_matches(info, 0x31, 8, 255) ||
        !axis_matches(info, 0x39, 4, 7)) return false;
    // G25: button range 1..19. G27: range 1..22, plus a separate cap for
    // button 23 (usage 0x17). Accept either.
    const auto max = info.pid == g27_pid ? USAGE{22} : USAGE{19};
    return std::any_of(info.input_buttons.begin(), info.input_buttons.end(), [&](const auto& cap) {
        return cap.ReportID == 0 && cap.UsagePage == 9 && cap.IsRange &&
            cap.Range.UsageMin == 1 && cap.Range.UsageMax == max;
    });
}
bool logitech_output_layout(const DeviceInfo& info) {
    if (!info.caps_valid || info.vid != logitech_vid || info.caps.OutputReportByteLength != 8 || info.caps.UsagePage != 1 ||
        (info.caps.Usage != 4 && info.caps.Usage != 5))
        return false;
    // hid-lg.c df_rdesc_fixed / dfp_rdesc_fixed have Generic Desktop
    // output usages 3 / 2. They must not be mistaken for the native FF00:2.
    // The G27 native output report is identical to the G25's (FF00:2, 7 bytes).
    return std::any_of(info.output_values.begin(), info.output_values.end(), [&](const auto& cap) {
        const bool usage = ((info.pid == g25_pid || info.pid == g27_pid) && usage_matches(cap, 0xff00, 2)) ||
                           (info.pid == dfp_pid && usage_matches(cap, 1, 2)) ||
                           (info.pid == dfex_pid && (usage_matches(cap, 1, 3) || usage_matches(cap, 0xff00, 3)));
        return usage && cap.ReportID == 0 && cap.BitSize == 8 && cap.ReportCount == 7;
    });
}
HidTransport::HidTransport(const DeviceInfo& expected, Access access, Trace trace)
    : handle_(CreateFileW(expected.path.c_str(), access == Access::read ? GENERIC_READ : GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr)),
      access_(access), trace_(trace), info_(expected) {
    if (!handle_.valid()) {
        const auto error = GetLastError();
        throw std::runtime_error(windows_error("CreateFile " + utf8(expected.path), error));
    }
    describe(handle_.get(), info_);
    if (info_.vid != expected.vid || info_.pid != expected.pid || info_.revision != expected.revision)
        throw std::runtime_error("HID identity changed since enumeration; run list again");
    if (access == Access::write && !logitech_output_layout(info_))
        throw std::runtime_error("unsupported HID output layout; no reports sent (run info)");
    if (access == Access::read && (!native_input_layout(info_) || !info_.native_offsets_verified))
        throw std::runtime_error("unsupported native G25 input layout; run info before using a new descriptor");
}
HidTransport::~HidTransport() = default;
std::optional<std::vector<std::uint8_t>> HidTransport::read(HANDLE stop_event, DWORD timeout_ms) {
    if (access_ != Access::read) throw std::logic_error("transport is not open for input");
    if (stop_event && WaitForSingleObject(stop_event, 0) == WAIT_OBJECT_0) return std::nullopt;
    std::vector<std::uint8_t> buffer(info_.caps.InputReportByteLength);
    UniqueHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event.valid()) throw std::runtime_error(windows_error("CreateEvent(read)", GetLastError()));
    OVERLAPPED operation{}; operation.hEvent = event.get();
    DWORD transferred{};
    if (!ReadFile(handle_.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &transferred, &operation)) {
        const auto error = GetLastError();
        if (error != ERROR_IO_PENDING) throw std::runtime_error(windows_error("ReadFile " + utf8(info_.path), error));
        if (!await_io(handle_.get(), operation, stop_event, timeout_ms, transferred)) return std::nullopt;
    }
    if (transferred != buffer.size()) throw std::runtime_error("short HID input report; device may have disconnected");
    return buffer;
}
void HidTransport::send(const Command& command) {
    if (access_ != Access::write) throw std::logic_error("transport is not open for output");
    const auto report = windows_output(command);
    if (trace_ == Trace::transmit) std::clog << "TX [" << hex_bytes(report) << "]\n";
    UniqueHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event.valid()) throw std::runtime_error(windows_error("CreateEvent(write)", GetLastError()));
    OVERLAPPED operation{}; operation.hEvent = event.get();
    DWORD transferred{};
    if (!WriteFile(handle_.get(), report.data(), static_cast<DWORD>(report.size()), &transferred, &operation)) {
        const auto error = GetLastError();
        if (error != ERROR_IO_PENDING) throw std::runtime_error(windows_error("WriteFile " + utf8(info_.path), error));
        // Stop reports must still be sent after Ctrl+C; no shared stop event here.
        if (!await_io(handle_.get(), operation, nullptr, 250, transferred))
            throw std::runtime_error("HID write timed out after 250 ms: " + utf8(info_.path));
    }
    if (transferred != report.size()) throw std::runtime_error("short HID output write: " + utf8(info_.path));
}
}
