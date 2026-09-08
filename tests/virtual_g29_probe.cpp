// SPDX-License-Identifier: GPL-2.0-only
// Manual probe: exercises tray/virtual_g29 from an UNPRIVILEGED process to check
// the service SDDL actually lets the tray query/start/stop. Not a ctest.
//   virtual_g29_probe            -> print presence + status
//   virtual_g29_probe start|stop -> drive the service
#include "tray/virtual_g29.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

static const char *run_name(g25::vg29::RunState s) {
    using R = g25::vg29::RunState;
    switch (s) {
    case R::stopped: return "stopped";
    case R::starting: return "starting";
    case R::running: return "running";
    case R::stopping: return "stopping";
    default: return "unknown";
    }
}

int main(int argc, char **argv) {
    using namespace g25::vg29;
    std::cout << "presence: " << (presence() == Presence::installed ? "installed" : "absent") << "\n";

    if (argc > 1 && std::strcmp(argv[1], "start") == 0) {
        std::cout << "start(): " << (start() ? "ok" : "FAILED") << "\n";
        std::this_thread::sleep_for(std::chrono::seconds(8));
    } else if (argc > 1 && std::strcmp(argv[1], "stop") == 0) {
        std::cout << "stop(): " << (stop() ? "ok" : "FAILED") << "\n";
        std::this_thread::sleep_for(std::chrono::seconds(6));
    }

    const auto s = status();
    std::wcout << L"status: run=" << run_name(s.run)
               << L" ffb=" << (s.ffb_active ? L"yes" : L"no")
               << L" detail=\"" << s.detail << L"\"\n";
    return 0;
}
