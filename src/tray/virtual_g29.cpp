// SPDX-License-Identifier: GPL-2.0-only
#include "tray/virtual_g29.h"

#include <windows.h>

#include <array>
#include <cstdlib>

namespace g25::vg29 {
namespace {
constexpr wchar_t kServiceName[] = L"g25vg29";
constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\g25vg29";

struct ScmHandle {
    SC_HANDLE h{};
    explicit ScmHandle(SC_HANDLE handle) : h(handle) {}
    ~ScmHandle() { if (h) CloseServiceHandle(h); }
    ScmHandle(const ScmHandle&) = delete;
    ScmHandle& operator=(const ScmHandle&) = delete;
    explicit operator bool() const { return h != nullptr; }
};

ScmHandle open_service(DWORD access) {
    ScmHandle scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!scm) return ScmHandle(nullptr);
    return ScmHandle(OpenServiceW(scm.h, kServiceName, access));
}

// Very small JSON field readers - the pipe payload is one flat object.
bool json_flag(const std::string& json, const char* key) {
    const auto pos = json.find(key);
    if (pos == std::string::npos) return false;
    const auto colon = json.find(':', pos);
    return colon != std::string::npos && json.compare(colon + 1, 4, "true") == 0;
}
std::string json_string(const std::string& json, const char* key) {
    auto pos = json.find(key);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos);
    if (pos == std::string::npos) return {};
    pos = json.find('"', pos);
    if (pos == std::string::npos) return {};
    const auto end = json.find('"', pos + 1);
    if (end == std::string::npos) return {};
    return json.substr(pos + 1, end - pos - 1);
}
int json_int(const std::string& json, const char* key) {
    auto pos = json.find(key);
    if (pos == std::string::npos) return 0;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return 0;
    return std::atoi(json.c_str() + pos + 1);
}
}

Presence presence() {
    auto svc = open_service(SERVICE_QUERY_STATUS);
    return svc ? Presence::installed : Presence::absent;
}

Status status() {
    Status result;

    auto svc = open_service(SERVICE_QUERY_STATUS);
    if (!svc) return result;   // absent -> stopped

    SERVICE_STATUS st{};
    if (QueryServiceStatus(svc.h, &st)) {
        switch (st.dwCurrentState) {
        case SERVICE_RUNNING:        result.run = RunState::running; break;
        case SERVICE_START_PENDING:  result.run = RunState::starting; break;
        case SERVICE_STOP_PENDING:   result.run = RunState::stopping; break;
        case SERVICE_STOPPED:        result.run = RunState::stopped; break;
        default:                     result.run = RunState::unknown; break;
        }
    }

    if (result.run == RunState::running) {
        const HANDLE pipe = CreateFileW(kPipeName, GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) {
            std::array<char, 512> buffer{};
            DWORD read = 0;
            if (ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size() - 1), &read, nullptr) && read > 0) {
                const std::string json(buffer.data(), read);
                result.ffb_active = json_flag(json, "\"ffbActive\"");
                result.wheel_range = json_int(json, "\"wheelRange\"");
                const auto state = json_string(json, "\"state\"");
                if (state == "restarting") result.run = RunState::starting;
                if (state == "wheel-lost") result.wheel_lost = true;
            }
            CloseHandle(pipe);
        }
    }

    switch (result.run) {
    case RunState::running:
        result.detail = result.wheel_lost ? L"On - G25 disconnected" : L"On";
        break;
    case RunState::starting:  result.detail = L"Starting..."; break;
    case RunState::stopping:  result.detail = L"Stopping..."; break;
    case RunState::stopped:   result.detail = L"Off"; break;
    default:                  result.detail = L"Unknown"; break;
    }
    return result;
}

bool start() {
    auto svc = open_service(SERVICE_START | SERVICE_QUERY_STATUS);
    if (!svc) return false;
    if (StartServiceW(svc.h, 0, nullptr)) return true;
    return GetLastError() == ERROR_SERVICE_ALREADY_RUNNING;
}

bool stop() {
    auto svc = open_service(SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!svc) return false;
    SERVICE_STATUS st{};
    if (ControlService(svc.h, SERVICE_CONTROL_STOP, &st)) return true;
    return GetLastError() == ERROR_SERVICE_NOT_ACTIVE;
}

}
