// SPDX-License-Identifier: GPL-2.0-only
#include "app/console_stop.h"
#include <stdexcept>

namespace g25 {
namespace {
SRWLOCK handler_lock = SRWLOCK_INIT;
HANDLE stop_event = nullptr, finished_event = nullptr;
BOOL WINAPI handler(DWORD signal) {
    if (signal != CTRL_C_EVENT && signal != CTRL_BREAK_EVENT && signal != CTRL_CLOSE_EVENT &&
        signal != CTRL_LOGOFF_EVENT && signal != CTRL_SHUTDOWN_EVENT) return FALSE;
    AcquireSRWLockShared(&handler_lock);
    if (stop_event) SetEvent(stop_event);
    if (signal != CTRL_C_EVENT && signal != CTRL_BREAK_EVENT) {
        // Windows terminates the process after a close handler returns. Allow
        // the main stack to unwind its OutputSession first (best effort).
        if (finished_event) WaitForSingleObject(finished_event, 2500);
    }
    ReleaseSRWLockShared(&handler_lock);
    return TRUE;
}
}
ConsoleStop::ConsoleStop()
    : event_(CreateEventW(nullptr, TRUE, FALSE, nullptr)), finished_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
    if (!event_.valid() || !finished_.valid()) throw std::runtime_error(windows_error("CreateEvent(console)", GetLastError()));
    AcquireSRWLockExclusive(&handler_lock);
    stop_event = event_.get(); finished_event = finished_.get();
    ReleaseSRWLockExclusive(&handler_lock);
    if (!SetConsoleCtrlHandler(handler, TRUE)) {
        const auto error = GetLastError();
        AcquireSRWLockExclusive(&handler_lock);
        stop_event = nullptr; finished_event = nullptr;
        ReleaseSRWLockExclusive(&handler_lock);
        throw std::runtime_error(windows_error("SetConsoleCtrlHandler", error));
    }
}
ConsoleStop::~ConsoleStop() {
    SetEvent(finished_.get());
    SetConsoleCtrlHandler(handler, FALSE);
    // Wait for handlers already using these handles before RAII closes them.
    // Signal finished first, so a close handler can release its shared lock.
    AcquireSRWLockExclusive(&handler_lock);
    stop_event = nullptr; finished_event = nullptr;
    ReleaseSRWLockExclusive(&handler_lock);
}
bool ConsoleStop::requested() const { return wait(0); }
bool ConsoleStop::wait(DWORD milliseconds) const {
    const auto result = WaitForSingleObject(event_.get(), milliseconds);
    if (result == WAIT_FAILED) throw std::runtime_error(windows_error("WaitForSingleObject(console)", GetLastError()));
    return result == WAIT_OBJECT_0;
}
}
