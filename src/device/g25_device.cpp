// SPDX-License-Identifier: GPL-2.0-only
#include "device/g25_device.h"
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace g25 {
DeviceInfo select_device(const std::vector<DeviceInfo>& devices, std::optional<std::size_t> index) {
    if (index) {
        if (*index >= devices.size()) throw std::runtime_error("device index not present; run g25tool list");
        return devices[*index];
    }
    std::optional<DeviceInfo> selected;
    for (const auto& device : devices) {
        if (device.pid != g25_pid && identify_model(device.pid, device.revision) != Model::g25) continue;
        if (selected) throw std::runtime_error("multiple candidate wheels/collections; use --device INDEX from list");
        selected = device;
    }
    if (!selected) throw std::runtime_error("no G25 detected; connect it and run list (compatibility devices need a known G25 revision)");
    return *selected;
}
void require_g25_writer(const DeviceInfo& info, bool switching_mode) {
    if (info.vid != logitech_vid || identify_model(info.pid, info.revision) != Model::g25)
        throw std::runtime_error("writes require a recognized real G25 revision; no reports sent");
    if (!switching_mode && info.pid != g25_pid)
        throw std::runtime_error("wheel is in compatibility mode; run native, wait for re-enumeration, then list");
    if (!logitech_output_layout(info)) throw std::runtime_error("unverified HID output layout; inspect info before any writes");
    if (!switching_mode && (!native_input_layout(info) || !info.native_offsets_verified))
        throw std::runtime_error("unverified native G25 descriptor; no reports sent");
}
void print_device_info(const DeviceInfo& info, std::size_t index, bool detailed) {
    std::cout << '[' << index << "] " << utf8(info.name) << " VID:PID=" << std::hex << std::setfill('0')
              << std::setw(4) << info.vid << ':' << std::setw(4) << info.pid
              << " rev=" << std::setw(4) << info.revision << std::dec << std::setfill(' ') << '\n'
              << "    Mode: " << mode_name(info.pid) << "; model: ";
    switch (identify_model(info.pid, info.revision)) {
    case Model::g25: std::cout << "G25 revision recognized"; break;
    case Model::g27: std::cout << "G27"; break;
    case Model::g29: std::cout << "G29"; break;
    default: std::cout << "unverified"; break;
    }
    std::cout << "\n    Path: " << utf8(info.path) << '\n';
    if (!info.diagnostic.empty()) std::cout << "    Diagnostic: " << info.diagnostic << '\n';
    if (!detailed || !info.caps_valid) return;
    std::cout << "    HID page/usage: " << std::hex << info.caps.UsagePage << '/' << info.caps.Usage << std::dec
              << "\n    Windows report lengths input/output/feature (including ID): "
              << info.caps.InputReportByteLength << '/' << info.caps.OutputReportByteLength << '/' << info.caps.FeatureReportByteLength
              << "\n    Known native input: " << (native_input_layout(info) ? "yes" : "no")
              << "; offsets verified by HidP: " << (info.native_offsets_verified ? "yes" : "no")
              << "; known Logitech output: " << (logitech_output_layout(info) ? "yes" : "no") << '\n';
    const auto print_values = [](const auto& values, const char* label) {
        for (const auto& cap : values) {
            std::cout << "    " << label << " id=" << static_cast<unsigned>(cap.ReportID)
                      << " page=" << std::hex << cap.UsagePage << " usage="
                      << (cap.IsRange ? cap.Range.UsageMin : cap.NotRange.Usage);
            if (cap.IsRange) std::cout << ".." << cap.Range.UsageMax;
            std::cout << std::dec << " bits=" << cap.BitSize << " count=" << cap.ReportCount
                      << " logical=" << cap.LogicalMin << ".." << cap.LogicalMax << '\n';
        }
    };
    print_values(info.input_values, "input"); print_values(info.output_values, "output");
    for (const auto& cap : info.input_buttons) {
        std::cout << "    buttons id=" << static_cast<unsigned>(cap.ReportID) << " page=" << std::hex << cap.UsagePage
                  << std::dec << " usage=" << (cap.IsRange ? cap.Range.UsageMin : cap.NotRange.Usage)
                  << ".." << (cap.IsRange ? cap.Range.UsageMax : cap.NotRange.Usage) << '\n';
    }
    std::cout << "    Current physical range: unknown (no verified query command)\n";
}
void print_inputs(const InputState& state, int assumed_range, bool raw, std::span<const std::uint8_t> report) {
    // One complete line per sample also works when redirected to a log file.
    std::cout << std::fixed << std::setprecision(1)
              << "Wheel: " << std::setw(7) << state.angle(assumed_range) << " deg"
              << " | Throttle: " << std::setw(5) << InputState::pedal_percent(state.throttle) << '%'
              << " | Brake: " << std::setw(5) << InputState::pedal_percent(state.brake) << '%'
              << " | Clutch: " << std::setw(5) << InputState::pedal_percent(state.clutch) << '%'
              << " | Gear*: " << state.indicated_gear() << " | POV: " << static_cast<unsigned>(state.hat) << " | Buttons:";
    for (unsigned bit = 0; bit < 19; ++bit) if ((state.buttons & (1u << bit)) != 0) std::cout << ' ' << bit + 1;
    if (!state.buttons) std::cout << " -";
    std::cout << " | Shifter/vendor: " << hex_bytes(state.vendor_bytes) << " / " << static_cast<unsigned>(state.vendor_bits);
    if (raw) std::cout << " | RX: " << hex_bytes(report);
    std::cout << '\n';
}
WriterLock::WriterLock() : handle_(CreateMutexW(nullptr, FALSE, L"Local\\g25tool-output-v1")) {
    if (!handle_.valid()) throw std::runtime_error(windows_error("CreateMutex(output)", GetLastError()));
    const auto result = WaitForSingleObject(handle_.get(), 0);
    if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED)
        throw std::runtime_error("another g25tool writer is active; stop it before changing wheel output");
    if (result == WAIT_ABANDONED) std::clog << "Previous writer exited unexpectedly; resetting effects before use\n";
}
WriterLock::~WriterLock() { ReleaseMutex(handle_.get()); }
bool WriterLock::available() noexcept {
    UniqueHandle handle(CreateMutexW(nullptr, FALSE, L"Local\\g25tool-output-v1"));
    if (!handle.valid()) return false;
    const auto result = WaitForSingleObject(handle.get(), 0);
    const bool got = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
    if (got) ReleaseMutex(handle.get());
    return got;
}
}
