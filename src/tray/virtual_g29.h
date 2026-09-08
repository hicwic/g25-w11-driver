// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <string>

// Thin view of the optional g25vg29 service for the tray. All calls are
// safe when the service is not installed.
namespace g25::vg29 {

enum class Presence { absent, installed };
enum class RunState { stopped, starting, running, stopping, unknown };

Presence presence();

struct Status {
    RunState run{RunState::stopped};
    bool ffb_active{false};
    std::wstring detail;   // short line for the menu, e.g. "Running - force feedback"
};

Status status();

// Returns true on success. The service SDDL grants interactive users START/STOP.
bool start();
bool stop();

}
