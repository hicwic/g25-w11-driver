// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "protocol/force_feedback.h"
#include <windows.h>
#include <string_view>
namespace g25 { void print_directinput_inventory(); }
namespace g25 { void run_directinput_test(std::string_view effect, HANDLE stop_event); }
