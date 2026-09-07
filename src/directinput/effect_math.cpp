// SPDX-License-Identifier: GPL-2.0-only
// Waveform conventions follow new-lg4ff's lg4ff_calculate_periodic.
#include "directinput/effect_math.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace g25::directinput {
double periodic_wave(Waveform waveform, std::uint64_t elapsed_us,
                     std::uint32_t period_us, std::uint32_t phase_hundredths) {
    if (!period_us) throw std::invalid_argument("period must be nonzero");
    constexpr double pi = std::numbers::pi_v<double>;
    constexpr double full_turn = 2.0 * pi;
    const double phase = std::fmod(full_turn * static_cast<double>(elapsed_us % period_us) /
                                   static_cast<double>(period_us) +
                                   full_turn * static_cast<double>(phase_hundredths % 36000u) / 36000.0,
                                   full_turn);
    switch (waveform) {
    case Waveform::sine: return std::sin(phase);
    case Waveform::square: return phase < pi ? 1.0 : -1.0;
    case Waveform::triangle: return 2.0 * std::abs(phase / pi - 1.0) - 1.0;
    case Waveform::sawtooth_up: return phase / pi - 1.0;
    case Waveform::sawtooth_down: return 1.0 - phase / pi;
    }
    return 0;
}
std::int64_t ramp_level(std::int32_t start, std::int32_t end,
                        std::uint64_t elapsed_us, std::uint32_t duration_us) {
    if (!duration_us) throw std::invalid_argument("duration must be nonzero");
    const auto bounded = std::min<std::uint64_t>(elapsed_us, duration_us);
    return start + (static_cast<std::int64_t>(end) - start) *
                   static_cast<std::int64_t>(bounded) / duration_us;
}
std::size_t custom_sample_index(std::uint64_t elapsed_us,
                                std::uint32_t sample_period_us, std::size_t samples) {
    if (!sample_period_us || !samples) throw std::invalid_argument("custom sample timing must be nonzero");
    return static_cast<std::size_t>((elapsed_us / sample_period_us) % samples);
}
}
