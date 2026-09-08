// SPDX-License-Identifier: GPL-2.0-only
#include "libg25/libg25.h"

#include "protocol/force_feedback.h"
#include "protocol/logitech_protocol.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <span>

namespace {
void write_output(std::uint8_t *out8, const g25::Command &command) {
    const auto report = g25::windows_output(command);
    std::memcpy(out8, report.data(), report.size());
}
}

extern "C" {

int32_t g25_identify_model(uint16_t pid, uint16_t revision) {
    switch (g25::identify_model(pid, revision)) {
    case g25::Model::g25: return G25_MODEL_G25;
    case g25::Model::g27: return G25_MODEL_G27;
    case g25::Model::g29: return G25_MODEL_G29;
    case g25::Model::unknown: break;
    }
    return G25_MODEL_UNKNOWN;
}

int32_t g25_decode_input(const uint8_t *report, int32_t len, g25_input_state *out) {
    if (report == nullptr || out == nullptr || len != 12) return -1;
    try {
        const auto state = g25::decode_windows_input(std::span<const std::uint8_t>(report, static_cast<std::size_t>(len)));
        out->wheel = state.wheel;
        out->throttle = state.throttle;
        out->brake = state.brake;
        out->clutch = state.clutch;
        out->hat = state.hat;
        out->buttons = state.buttons;
        return 0;
    } catch (...) {
        return -1;
    }
}

void g25_cmd_native_mode(uint8_t *out8) {
    if (out8) write_output(out8, g25::native_mode());
}

void g25_cmd_stop_all(uint8_t *out8) {
    if (out8) write_output(out8, g25::stop_all());
}

void g25_cmd_disable_autocenter(uint8_t *out8) {
    if (out8) write_output(out8, g25::disable_autocenter());
}

int32_t g25_cmd_set_range(int32_t degrees, uint8_t *out8) {
    if (out8 == nullptr) return -1;
    try {
        write_output(out8, g25::set_range(degrees));
        return 0;
    } catch (...) {
        return -1;
    }
}

namespace {
g25::Command passthrough(std::span<const std::uint8_t> payload) {
    g25::Command command{};
    const auto n = std::min<std::size_t>(payload.size(), command.size());
    for (std::size_t i = 0; i < n; ++i) command[i] = payload[i];
    return command;
}

// GeForce NOW's G29 report -> G25 classic command.
//
// hid-lg4ff.c uses ONE format for G25/G27/G29: cmd[0] = (0x10 << slot) | op,
// cmd[1] = effect type (0x00 constant, 0x0b spring, 0x0c damper, 0x0e friction),
// force at cmd[2 + slot] as (clamp_s16(level) + 0x8000) >> 8. F3/F5/13/14 and
// the 0x21 0x0c condition GFN sends are already in that format, so they pass
// through unchanged.
//
// The one deviation observed: GFN's constant-force report is
//   11 08 <level> 80 00 00 00
// where lg4ff would send   11 00 <level> 00 00 00 00 . Normalise it.
g25::Command translate(std::span<const std::uint8_t> p) {
    const bool download = (p[0] & 0x0F) == 0x01;           // op 1 = download-and-play
    const bool constant_type = p.size() > 1 && (p[1] == 0x00 || p[1] == 0x08);
    if (download && constant_type && p.size() >= 3) {
        const std::uint8_t slot = (p[0] >> 4) & 0x0F;      // 1,2,4,8 -> byte 2..5
        const std::uint8_t idx = slot == 0x02 ? 3 : slot == 0x04 ? 4 : slot == 0x08 ? 5 : 2;
        g25::Command cmd{p[0], 0x00, 0, 0, 0, 0, 0};
        cmd[idx] = p[2];
        return cmd;
    }
    return passthrough(p);
}
}

int32_t g25_ffb_translate(int32_t mode, const uint8_t *in, int32_t in_len, uint8_t *out, int32_t out_cap) {
    if (in == nullptr || out == nullptr || in_len < 1 || out_cap < 1) return -1;

    // Tolerate a leading report-id byte: the HIDMaestro USB/IP backend sometimes
    // exposes byte 0 as a report id even though this G29 descriptor has none.
    std::span<const std::uint8_t> payload(in, static_cast<std::size_t>(in_len));
    if (payload.size() >= 8 && payload[0] == 0x00) payload = payload.subspan(1);
    if (payload.empty()) return -1;

    write_output(out, mode == 1 ? translate(payload) : passthrough(payload));
    return 1;
}

const char *g25_libg25_version(void) { return "0.1.0"; }

}
