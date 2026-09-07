// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "protocol/logitech_protocol.h"

namespace g25 {
// Raw encoders for a future effect engine. CLI uses only the fixed low-force
// vectors below. Signed coefficients are -32768..32767, clip is 0..65535.
Command constant_force(std::int16_t force);
Command spring(std::int16_t left_boundary, std::int16_t right_boundary,
               std::int16_t left_coefficient, std::int16_t right_coefficient,
               std::uint16_t clip);
Command damper(std::int16_t left_coefficient, std::int16_t right_coefficient,
               std::uint16_t clip);
Command friction(std::int16_t left_coefficient, std::int16_t right_coefficient,
                 std::uint16_t clip);
// Stops one of the G25's four hardware effect slots without disturbing the
// others. Slot 0 carries synthesized forces; slots 1..3 carry conditions.
Command stop_force_slot(unsigned slot);
enum class TestEffect { constant, spring, damper };
Command low_force_test(TestEffect effect);
inline constexpr unsigned test_duration_ms = 1000;
}
