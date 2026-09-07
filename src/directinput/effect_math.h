// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstddef>
#include <cstdint>

namespace g25::directinput {
enum class Waveform { square, sine, triangle, sawtooth_up, sawtooth_down };

// DirectInput periods are microseconds and phases are hundredths of a degree.
double periodic_wave(Waveform waveform, std::uint64_t elapsed_us,
                     std::uint32_t period_us, std::uint32_t phase_hundredths);
std::int64_t ramp_level(std::int32_t start, std::int32_t end,
                        std::uint64_t elapsed_us, std::uint32_t duration_us);
std::size_t custom_sample_index(std::uint64_t elapsed_us,
                                std::uint32_t sample_period_us, std::size_t samples);
}
