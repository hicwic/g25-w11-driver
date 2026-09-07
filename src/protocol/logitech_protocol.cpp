// SPDX-License-Identifier: GPL-2.0-only
// Protocol adapted from new-lg4ff hid-lg4ff.c (GPL-2.0-or-later).
// Copyright (c) 2010 Simon Wood <simon@mungewell.org>
// Copyright (c) 2019 Bernat Arlandis <berarma@hotmail.com>
// Input layout: Kethen/lg4ff_userspace driver_loops.c, uinput_g25_g27_emit.
// Modified for C++20 / Windows framing; source revisions in THIRD_PARTY_NOTICES.md.
#include "protocol/logitech_protocol.h"
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace g25 {
bool supported_pid(std::uint16_t pid) noexcept {
    return pid == dfex_pid || pid == dfp_pid || pid == g25_pid || pid == g27_pid;
}
Model identify_model(std::uint16_t pid, std::uint16_t revision) noexcept {
    if (!supported_pid(pid)) return Model::unknown;
    // Match the more specific G27 signature before the G25's 0x12xx.
    if ((revision & 0xfff8) == 0x1350 || (revision & 0xff00) == 0x8900) return Model::g29;
    if ((revision & 0xfff0) == 0x1230) return Model::g27;
    if (pid != g27_pid && (revision & 0xff00) == 0x1200) return Model::g25;
    return Model::unknown;
}
std::string mode_name(std::uint16_t pid) {
    switch (pid) {
    case dfex_pid: return "Driving Force / Formula EX";
    case dfp_pid: return "Driving Force Pro";
    case g25_pid: return "G25 native layout";
    case g27_pid: return "G27 (diagnostic only)";
    default: return "unknown";
    }
}
Command native_mode() { return {0xf8, 0x10, 0, 0, 0, 0, 0}; }
Command set_range(int degrees) {
    if (degrees < 40 || degrees > 900) throw std::invalid_argument("range must be 40..900 degrees");
    return {0xf8, 0x81, static_cast<std::uint8_t>(degrees & 0xff),
            static_cast<std::uint8_t>(degrees >> 8), 0, 0, 0};
}
Command stop_all() { return {0xf3, 0, 0, 0, 0, 0, 0}; }
Command disable_autocenter() { return {0xf5, 0, 0, 0, 0, 0, 0}; }
WindowsOutput windows_output(const Command& command) {
    WindowsOutput report{};
    for (std::size_t i = 0; i < command.size(); ++i) report[i + 1] = command[i];
    return report;
}
std::string hex_bytes(std::span<const std::uint8_t> bytes) {
    std::ostringstream out;
    out << std::hex << std::uppercase << std::setfill('0');
    bool first = true;
    for (auto byte : bytes) {
        if (!first) out << ' ';
        first = false;
        out << std::setw(2) << static_cast<unsigned>(byte);
    }
    return out.str();
}
InputState decode_native_payload(std::span<const std::uint8_t> p) {
    if (p.size() != 11) throw std::invalid_argument("native G25 payload must contain exactly 11 bytes");
    InputState state;
    state.hat = p[0] & 0x0f;
    const auto packed = static_cast<std::uint32_t>(p[0]) |
        (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) |
        (static_cast<std::uint32_t>(p[3]) << 24);
    state.buttons = (packed >> 4) & 0x7ffff;
    state.vendor_bits = static_cast<std::uint8_t>((packed >> 23) & 7);
    state.wheel = static_cast<std::uint16_t>((p[3] >> 2) | (static_cast<unsigned>(p[4]) << 6));
    state.throttle = p[5]; state.brake = p[6]; state.clutch = p[7];
    state.vendor_bytes = {p[8], p[9], p[10]};
    return state;
}
InputState decode_windows_input(std::span<const std::uint8_t> report) {
    if (report.size() != 12 || report[0] != 0)
        throw std::invalid_argument("expected 12-byte Windows G25 input with Report ID 0");
    return decode_native_payload(report.subspan(1));
}
double InputState::angle(int assumed_range) const {
    (void)set_range(assumed_range);
    return (static_cast<double>(wheel) / 16383.0 - 0.5) * assumed_range;
}
double InputState::pedal_percent(std::uint8_t value) noexcept {
    return (255.0 - value) * 100.0 / 255.0;
}
std::string InputState::indicated_gear() const {
    const auto mask = (buttons >> 8) & 0x7f;
    if (mask == 0) return "N";
    if ((mask & (mask - 1)) != 0) return "?";
    for (unsigned i = 0; i < 7; ++i)
        if ((mask & (1u << i)) != 0) return i == 6 ? "R" : std::to_string(i + 1);
    return "?";
}
}
