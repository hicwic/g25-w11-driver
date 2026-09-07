// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "device/hid_transport.h"

namespace g25 {
class ConsoleStop {
public:
    ConsoleStop();
    ~ConsoleStop();
    ConsoleStop(const ConsoleStop&) = delete;
    ConsoleStop& operator=(const ConsoleStop&) = delete;
    HANDLE event() const noexcept { return event_.get(); }
    bool requested() const;
    bool wait(DWORD milliseconds) const;
private:
    UniqueHandle event_, finished_;
};
}
