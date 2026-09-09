// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace g25 {
using Command = std::array<std::uint8_t, 7>;
using WindowsOutput = std::array<std::uint8_t, 8>;
inline constexpr std::uint16_t logitech_vid = 0x046d;
inline constexpr std::uint16_t dfex_pid = 0xc294;
inline constexpr std::uint16_t dfp_pid = 0xc298;
inline constexpr std::uint16_t g25_pid = 0xc299;
inline constexpr std::uint16_t g27_pid = 0xc29b;

enum class Model { unknown, g25, g27, g29 };
Model identify_model(std::uint16_t pid, std::uint16_t revision) noexcept;
bool supported_pid(std::uint16_t pid) noexcept;
std::string mode_name(std::uint16_t pid);
// Compatibility-mode -> native-mode switch. G25 uses EXT_CMD16 (0xf8 0x10);
// the G27 needs EXT_CMD9 (0xf8 0x09 0x04 0x01) for its full 23-button layout.
Command native_mode(Model model);
inline Command native_mode() { return native_mode(Model::g25); }
Command set_range(int degrees);
Command stop_all();
Command disable_autocenter();
WindowsOutput windows_output(const Command& command);
std::string hex_bytes(std::span<const std::uint8_t> bytes);

struct InputState {
    std::uint16_t wheel{};
    std::uint8_t throttle{}, brake{}, clutch{}, hat{};
    std::uint32_t buttons{}; // HID usages 1..19 (G25) or 1..23 (G27), bit 0 = button 1.
    std::uint8_t vendor_bits{};
    std::array<std::uint8_t, 3> vendor_bytes{};
    double angle(int assumed_range) const;
    static double pedal_percent(std::uint8_t value) noexcept;
    // Indicative XP/Vista Profiler mapping. See docs/protocol.md.
    std::string indicated_gear() const;
};

// Native payload only (transport framing must be removed explicitly). The wheel,
// pedals and hat decode identically for G25/G27; only the button field differs
// (G25: 19 bits + 3 vendor bits; G27: 22 bits + button 23 at report bit 80).
InputState decode_native_payload(std::span<const std::uint8_t> payload, Model model = Model::g25);
InputState decode_windows_input(std::span<const std::uint8_t> report, Model model = Model::g25);
}
