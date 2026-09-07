// SPDX-License-Identifier: GPL-2.0-only
#include "settings/user_settings.h"
#include <windows.h>

namespace g25 {
namespace {
constexpr wchar_t settings_path[] = L"Software\\g25-driver";

int read_dword(HKEY key, const wchar_t* name, int fallback) noexcept {
    DWORD value{};
    DWORD size = sizeof(value);
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS)
        return fallback;
    return static_cast<int>(value);
}
}

bool supported_rotation(int degrees) noexcept {
    return degrees == 180 || degrees == 360 || degrees == 540 || degrees == 900;
}

UserSettings read_user_settings() noexcept {
    UserSettings result;
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, settings_path, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return result;
    const int rotation = read_dword(key, L"Rotation", result.rotation);
    RegCloseKey(key);
    if (supported_rotation(rotation)) result.rotation = rotation;
    return result;
}

bool write_user_settings(const UserSettings& settings) noexcept {
    if (!supported_rotation(settings.rotation)) return false;
    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, settings_path, 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    const DWORD rotation = static_cast<DWORD>(settings.rotation);
    const auto a = RegSetValueExW(key, L"Rotation", 0, REG_DWORD,
                                  reinterpret_cast<const BYTE*>(&rotation), sizeof(rotation));
    RegCloseKey(key);
    return a == ERROR_SUCCESS;
}
}
