// SPDX-License-Identifier: GPL-2.0-only
#include "directinput/effect_math.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
int checks{};
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
void near(double actual, double expected, const char* message) {
    check(std::abs(actual - expected) < 1e-9, message);
}
}
int main() {
    using namespace g25::directinput;
    try {
        near(periodic_wave(Waveform::sine, 0, 1000, 0), 0, "sine origin");
        near(periodic_wave(Waveform::sine, 250, 1000, 0), 1, "sine quarter");
        near(periodic_wave(Waveform::square, 499, 1000, 0), 1, "square positive");
        near(periodic_wave(Waveform::square, 500, 1000, 0), -1, "square negative");
        near(periodic_wave(Waveform::triangle, 0, 1000, 0), 1, "triangle peak");
        near(periodic_wave(Waveform::triangle, 500, 1000, 0), -1, "triangle trough");
        near(periodic_wave(Waveform::sawtooth_up, 0, 1000, 0), -1, "saw up start");
        near(periodic_wave(Waveform::sawtooth_down, 0, 1000, 0), 1, "saw down start");
        near(periodic_wave(Waveform::sine, 0, 1000, 9000), 1, "phase offset");
        check(ramp_level(-3000, 3000, 0, 1000) == -3000, "ramp start");
        check(ramp_level(-3000, 3000, 500, 1000) == 0, "ramp midpoint");
        check(ramp_level(-3000, 3000, 1000, 1000) == 3000, "ramp end");
        check(custom_sample_index(0, 250, 4) == 0 && custom_sample_index(1000, 250, 4) == 0,
              "custom sample looping");
        bool rejected{};
        try { (void)periodic_wave(Waveform::sine, 0, 0, 0); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "zero period rejected");
        std::cout << checks << " effect synthesis checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
