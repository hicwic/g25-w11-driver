// SPDX-License-Identifier: GPL-2.0-only
// Exercises the C ABI in src/libg25/libg25.cpp against the same golden vectors
// the C++ protocol tests use, so the wrapper cannot drift from g25_protocol.
#include "libg25/libg25.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void check(bool value, const char *name) {
    ++checks;
    if (!value) throw std::runtime_error(name);
}

constexpr std::uint16_t dfex_pid = 0xc294;
constexpr std::uint16_t dfp_pid = 0xc298;
constexpr std::uint16_t g25_pid = 0xc299;

void run() {
    // identify_model - mirrors protocol_tests.
    check(g25_identify_model(dfex_pid, 0x1222) == G25_MODEL_G25, "compat G25");
    check(g25_identify_model(g25_pid, 0x1230) == G25_MODEL_G27, "G27 masquerade excluded");
    check(g25_identify_model(dfp_pid, 0x1352) == G25_MODEL_G29, "G29 excluded from G25");
    check(g25_identify_model(static_cast<std::uint16_t>(0xffff), 0x1222) == G25_MODEL_UNKNOWN, "unknown pid");
    check(g25_identify_model(g25_pid, 0x8900) == G25_MODEL_G29, "G29 bcdDevice 0x8900");

    // Wheel command byte layouts.
    std::array<std::uint8_t, 8> out{};
    g25_cmd_native_mode(out.data());
    check((out == std::array<std::uint8_t, 8>{0, 0xf8, 0x10, 0, 0, 0, 0, 0}), "native mode report");
    g25_cmd_stop_all(out.data());
    check(out[0] == 0 && out[1] == 0xf3, "stop all");
    g25_cmd_disable_autocenter(out.data());
    check(out[0] == 0 && out[1] == 0xf5, "disable autocenter");
    check(g25_cmd_set_range(900, out.data()) == 0, "set range 900 ok");
    check((out == std::array<std::uint8_t, 8>{0, 0xf8, 0x81, 0x84, 0x03, 0, 0, 0}), "range 900 golden vector");
    check(g25_cmd_set_range(39, out.data()) == -1, "reject range 39");
    check(g25_cmd_set_range(901, out.data()) == -1, "reject range 901");

    // Input decode - the hand-assembled bit-boundary vector from protocol_tests.
    const std::array<std::uint8_t, 12> report{0, 0x98, 0x01, 0x80, 0x02, 0x80, 0xff, 0, 0x7f, 0x12, 0x34, 0x56};
    g25_input_state st{};
    check(g25_decode_input(report.data(), 12, &st) == 0, "decode ok");
    check(st.wheel == 8192 && st.buttons == 0x19 && st.hat == 8, "decoded axes/buttons");
    check(st.throttle == 255 && st.brake == 0 && st.clutch == 127, "decoded pedals");
    check(g25_decode_input(report.data(), 11, &st) == -1, "short report rejected");
    auto bad = report;
    bad[0] = 1;
    check(g25_decode_input(bad.data(), 12, &st) == -1, "report id 1 rejected");
    check(g25_decode_input(nullptr, 12, &st) == -1, "null report rejected");

    // FFB - passthrough (mode 0).
    std::array<std::uint8_t, 8> ffb{};
    const std::array<std::uint8_t, 7> stop_all_cmd{0xf3, 0, 0, 0, 0, 0, 0};
    check(g25_ffb_translate(0, stop_all_cmd.data(), 7, ffb.data(), 1) == 1, "passthrough stop-all");
    check(ffb[0] == 0 && ffb[1] == 0xf3, "stop-all unchanged");
    // Leading report-id byte tolerated.
    const std::array<std::uint8_t, 8> cf{0x00, 0x11, 0x08, 0x1b, 0x80, 0, 0, 0};
    check(g25_ffb_translate(0, cf.data(), 8, ffb.data(), 1) == 1, "passthrough constant force");
    check(ffb[1] == 0x11 && ffb[2] == 0x08 && ffb[3] == 0x1b, "passthrough keeps bytes");
    check(g25_ffb_translate(0, nullptr, 8, ffb.data(), 1) == -1, "null in rejected");
    check(g25_ffb_translate(0, cf.data(), 8, ffb.data(), 0) == -1, "zero capacity rejected");

    // FFB - translate (mode 1).
    check(g25_ffb_translate(1, cf.data(), 8, ffb.data(), 1) == 1, "translate constant force");
    check((ffb == std::array<std::uint8_t, 8>{0, 0x11, 0x00, 0x1b, 0, 0, 0, 0}), "constant force type 0x08 -> 0x00, level kept");
    const std::array<std::uint8_t, 7> cond{0x21, 0x0c, 0x0c, 0x00, 0x0c, 0x00, 0x01};
    check(g25_ffb_translate(1, cond.data(), 7, ffb.data(), 1) == 1, "translate condition");
    check(ffb[1] == 0x41 && ffb[2] == 0x0c && ffb[3] == 0x0c && ffb[5] == 0x0c, "condition slot2 -> G25 damper slot3");
    check(g25_ffb_translate(1, stop_all_cmd.data(), 7, ffb.data(), 1) == 1, "translate stop-all");
    check(ffb[1] == 0xf3, "stop-all still passes through in translate mode");

    check(std::strlen(g25_libg25_version()) > 0, "version string");
}
}

int main() {
    try {
        run();
        std::cout << checks << " libg25 C ABI checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
