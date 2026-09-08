// SPDX-License-Identifier: GPL-2.0-only
#include "device/g25_device.h"
#include "settings/user_settings.h"
#include "protocol/logitech_protocol.h"
#include "tray/gfn_bridge.h"
#include "tray/resource.h"

#include <algorithm>
#include <shellapi.h>
#include <string>

namespace {
using namespace g25;
constexpr UINT tray_callback = WM_APP + 1;
constexpr UINT timer_id = 1;
constexpr UINT icon_id = 1;
constexpr UINT cmd_rotation_base = 100;
constexpr UINT cmd_exit = 301;
constexpr UINT cmd_gfn_toggle = 302;
constexpr int rotations[]{180, 360, 540, 900};
UINT taskbar_created{};
NOTIFYICONDATAW icon{};
UserSettings settings;
std::wstring status = L"Starting...";
std::wstring applied_path;
std::wstring pending_path;
int applied_rotation{};
enum class ApplyResult { complete, retry };

void add_icon(HWND window) {
    icon = {};
    icon.cbSize = sizeof(icon);
    icon.hWnd = window;
    icon.uID = icon_id;
    icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    icon.uCallbackMessage = tray_callback;
    icon.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_G25_CONTROL));
    wcscpy_s(icon.szTip, L"G25 Control");
    Shell_NotifyIconW(NIM_ADD, &icon);
    icon.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &icon);
}

ApplyResult apply_to_wheel() {
    try {
        const auto devices = enumerate_wheels();
        const auto found = std::find_if(devices.begin(), devices.end(), [](const DeviceInfo& info) {
            return info.vid == logitech_vid && identify_model(info.pid, info.revision) == Model::g25;
        });
        if (found == devices.end()) {
            status = L"G25 not connected";
            applied_path.clear();
            pending_path.clear();
            applied_rotation = 0;
            return ApplyResult::complete;
        }
        if (found->pid != g25_pid) {
            require_g25_writer(*found, true);
            WriterLock lock;
            HidTransport transport(*found, Access::write);
            transport.send(stop_all());
            transport.send(disable_autocenter());
            transport.send(native_mode());
            status = L"Switching to G25 mode...";
            applied_path.clear();
            pending_path.clear();
            return ApplyResult::retry;
        }
        if (applied_path == found->path && applied_rotation == settings.rotation) {
            status = L"G25 ready - " + std::to_wstring(settings.rotation) + L" deg";
            pending_path.clear();
            return ApplyResult::complete;
        }
        if (pending_path != found->path) {
            pending_path = found->path;
            status = L"Waiting for G25 calibration...";
            return ApplyResult::retry;
        }
        require_g25_writer(*found, false);
        WriterLock lock;
        HidTransport transport(*found, Access::write);
        transport.send(set_range(settings.rotation));
        transport.send(stop_all());
        transport.send(disable_autocenter());
        applied_path = found->path;
        pending_path.clear();
        applied_rotation = settings.rotation;
        status = L"G25 ready - " + std::to_wstring(settings.rotation) + L" deg";
        return ApplyResult::complete;
    } catch (...) {
        status = L"G25 busy - setup pending";
        return ApplyResult::retry;
    }
}

void apply_or_retry(HWND window) {
    if (apply_to_wheel() == ApplyResult::retry) SetTimer(window, timer_id, 1000, nullptr);
    else KillTimer(window, timer_id);
}

void show_menu(HWND window) {
    settings = read_user_settings();
    HMENU menu = CreatePopupMenu();
    HMENU rotation_menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, status.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    for (UINT i = 0; i < std::size(rotations); ++i) {
        const auto text = std::to_wstring(rotations[i]) + L" deg";
        AppendMenuW(rotation_menu, MF_STRING | (settings.rotation == rotations[i] ? MF_CHECKED : 0),
                    cmd_rotation_base + i, text.c_str());
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(rotation_menu), L"Maximum rotation");

    if (gfn::presence() == gfn::Presence::installed) {
        const auto gfn_status = gfn::status();
        const bool on = gfn_status.run == gfn::RunState::running ||
                        gfn_status.run == gfn::RunState::starting;
        HMENU gfn_menu = CreatePopupMenu();
        AppendMenuW(gfn_menu, MF_STRING | MF_DISABLED, 0, gfn_status.detail.c_str());
        AppendMenuW(gfn_menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(gfn_menu, MF_STRING | (on ? MF_CHECKED : 0), cmd_gfn_toggle, L"GeForce NOW mode");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(gfn_menu), L"GeForce NOW bridge");
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, cmd_exit, L"Exit");
    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(window);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, point.x, point.y, 0, window, nullptr);
    DestroyMenu(menu);
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == taskbar_created) { add_icon(window); return 0; }
    switch (message) {
    case WM_CREATE:
        settings = read_user_settings();
        add_icon(window);
        PostMessageW(window, WM_TIMER, timer_id, 0);
        return 0;
    case WM_TIMER:
        if (wparam == timer_id) apply_or_retry(window);
        return 0;
    case WM_DEVICECHANGE:
        applied_path.clear();
        pending_path.clear();
        applied_rotation = 0;
        SetTimer(window, timer_id, 500, nullptr);
        return 0;
    case tray_callback:
        if (LOWORD(lparam) == WM_CONTEXTMENU || LOWORD(lparam) == WM_RBUTTONUP ||
            LOWORD(lparam) == NIN_SELECT || LOWORD(lparam) == NIN_KEYSELECT)
            show_menu(window);
        return 0;
    case WM_COMMAND: {
        const UINT command = LOWORD(wparam);
        if (command >= cmd_rotation_base && command < cmd_rotation_base + std::size(rotations)) {
            settings.rotation = rotations[command - cmd_rotation_base];
            applied_rotation = 0;
            pending_path = applied_path;
            (void)write_user_settings(settings);
            apply_or_retry(window);
        } else if (command == cmd_gfn_toggle) {
            const auto st = gfn::status();
            if (st.run == gfn::RunState::running || st.run == gfn::RunState::starting)
                gfn::stop();
            else
                gfn::start();
        } else if (command == cmd_exit) {
            DestroyWindow(window);
        }
        return 0;
    }
    case WM_DESTROY:
        KillTimer(window, timer_id);
        Shell_NotifyIconW(NIM_DELETE, &icon);
        PostQuitMessage(0);
        return 0;
    default: return DefWindowProcW(window, message, wparam, lparam);
    }
}
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
    const HANDLE instance_lock = CreateMutexW(nullptr, FALSE, L"Local\\g25-control-tray-v1");
    if (!instance_lock) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(instance_lock); return 0; }
    const wchar_t class_name[] = L"G25ControlTrayWindow";
    taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.lpszClassName = class_name;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_G25_CONTROL));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&wc)) { CloseHandle(instance_lock); return 1; }
    const HWND window = CreateWindowExW(0, class_name, L"G25 Control", WS_OVERLAPPED,
                                        0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!window) { CloseHandle(instance_lock); return 1; }
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    CloseHandle(instance_lock);
    return static_cast<int>(message.wParam);
}
