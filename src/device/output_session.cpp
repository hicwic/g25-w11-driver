// SPDX-License-Identifier: GPL-2.0-only
#include "device/output_session.h"
#include <exception>
#include <iostream>

namespace g25 {
void OutputSession::initialize() {
    writer_.send(stop_all());
    writer_.send(disable_autocenter());
}
void OutputSession::finish() {
    std::exception_ptr failure;
    try { writer_.send(stop_all()); } catch (...) { failure = std::current_exception(); }
    // Both writes must be attempted, even when the first one fails.
    try { writer_.send(disable_autocenter()); } catch (...) { if (!failure) failure = std::current_exception(); }
    if (failure) std::rethrow_exception(failure);
    finished_ = true;
}
OutputSession::~OutputSession() {
    if (finished_) return;
    try { finish(); }
    catch (const std::exception& error) { std::cerr << "STOP FAILED: " << error.what() << '\n'; }
    catch (...) { std::cerr << "STOP FAILED: unknown error\n"; }
}
}
