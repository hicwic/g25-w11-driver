// SPDX-License-Identifier: GPL-2.0-only
// Adapted from new-lg4ff hid-lg4ff.c, lg4ff_update_slot (GPL-2.0-or-later).
// Copyright (c) 2010 Simon Wood <simon@mungewell.org>
// Copyright (c) 2019 Bernat Arlandis <berarma@hotmail.com>
// Modified: pure C++20 encoders, explicit slots, bounded CLI test vectors.
#include "protocol/force_feedback.h"
#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace g25 {
namespace {
std::uint8_t coeff4(int value) {
    return static_cast<std::uint8_t>(std::min(std::abs(value) * 2, 65535) >> 12);
}
std::uint8_t coeff8(int value) {
    return static_cast<std::uint8_t>(std::min(std::abs(value) * 2, 65535) >> 8);
}
}
Command constant_force(std::int16_t force) {
    return {0x11, 0x00, static_cast<std::uint8_t>((static_cast<int>(force) + 32768) >> 8), 0, 0, 0, 0};
}
Command spring(std::int16_t left_boundary, std::int16_t right_boundary,
               std::int16_t left_coefficient, std::int16_t right_coefficient, std::uint16_t clip) {
    if (left_boundary > right_boundary) throw std::invalid_argument("spring boundaries are reversed");
    if (clip == 0) return {0x23, 0, 0, 0, 0, 0, 0};
    int d1 = (static_cast<int>(left_boundary) + 32768) >> 5;
    int d2 = (static_cast<int>(right_boundary) + 32768) >> 5;
    int k1 = std::abs(static_cast<int>(left_coefficient));
    int k2 = std::abs(static_cast<int>(right_coefficient));
    if (k1 < 2048) d1 = 0; else k1 -= 2048;
    if (k2 < 2048) d2 = 2047; else k2 -= 2048;
    const int signs = (left_coefficient < 0 ? 1 : 0) | (right_coefficient < 0 ? 16 : 0);
    return {0x21, 0x0b, static_cast<std::uint8_t>(d1 >> 3), static_cast<std::uint8_t>(d2 >> 3),
            static_cast<std::uint8_t>((coeff4(k2) << 4) | coeff4(k1)),
            static_cast<std::uint8_t>(((d2 & 7) << 5) | ((d1 & 7) << 1) | signs),
            static_cast<std::uint8_t>(clip >> 8)};
}
Command damper(std::int16_t left_coefficient, std::int16_t right_coefficient, std::uint16_t clip) {
    if (clip == 0) return {0x43, 0, 0, 0, 0, 0, 0};
    return {0x41, 0x0c, coeff4(left_coefficient), static_cast<std::uint8_t>(left_coefficient < 0),
            coeff4(right_coefficient), static_cast<std::uint8_t>(right_coefficient < 0),
            static_cast<std::uint8_t>(clip >> 8)};
}
Command friction(std::int16_t left_coefficient, std::int16_t right_coefficient, std::uint16_t clip) {
    if (clip == 0) return {0x83, 0, 0, 0, 0, 0, 0};
    const auto signs = static_cast<std::uint8_t>((left_coefficient < 0 ? 1 : 0) |
                                                 (right_coefficient < 0 ? 16 : 0));
    return {0x81, 0x0e, coeff8(left_coefficient), coeff8(right_coefficient),
            static_cast<std::uint8_t>(clip >> 8), signs, 0};
}
Command stop_force_slot(unsigned slot) {
    if (slot > 3) throw std::invalid_argument("force-feedback slot must be 0, 1, 2, or 3");
    return {static_cast<std::uint8_t>((1u << (slot + 4u)) | 0x03u), 0, 0, 0, 0, 0, 0};
}
Command low_force_test(TestEffect effect) {
    switch (effect) {
    case TestEffect::constant: return constant_force(9830);
    case TestEffect::spring: return spring(0, 0, 32767, 32767, 19660);
    case TestEffect::damper: return damper(32767, 32767, 19660);
    }
    throw std::invalid_argument("unknown test effect");
}
}
