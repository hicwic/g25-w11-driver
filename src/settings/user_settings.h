// SPDX-License-Identifier: GPL-2.0-only
#pragma once

namespace g25 {
struct UserSettings {
    int rotation = 900;
};

bool supported_rotation(int degrees) noexcept;
UserSettings read_user_settings() noexcept;
bool write_user_settings(const UserSettings& settings) noexcept;
}
