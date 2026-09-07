// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "device/output_session.h"
#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <memory>
#include <optional>
#include <vector>

namespace g25 {
std::string utf8(const std::wstring& value);
std::string windows_error(const std::string& operation, DWORD error);

class UniqueHandle {
public:
    explicit UniqueHandle(HANDLE value = INVALID_HANDLE_VALUE) noexcept : value_(value) {}
    ~UniqueHandle() { if (valid()) CloseHandle(value_); }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    HANDLE get() const noexcept { return value_; }
    bool valid() const noexcept { return value_ != nullptr && value_ != INVALID_HANDLE_VALUE; }
private:
    HANDLE value_;
};

struct DeviceInfo {
    std::wstring path, name;
    std::uint16_t vid{}, pid{}, revision{};
    HIDP_CAPS caps{};
    std::vector<HIDP_VALUE_CAPS> input_values, output_values;
    std::vector<HIDP_BUTTON_CAPS> input_buttons;
    std::string diagnostic;
    bool caps_valid{};
    bool native_offsets_verified{};
};
std::vector<DeviceInfo> enumerate_wheels();
bool native_input_layout(const DeviceInfo& info);
bool logitech_output_layout(const DeviceInfo& info);

enum class Access { read, write };
class HidTransport : public ReportWriter {
public:
    HidTransport(const DeviceInfo& expected, Access access);
    ~HidTransport() override;
    HidTransport(const HidTransport&) = delete;
    HidTransport& operator=(const HidTransport&) = delete;
    const DeviceInfo& info() const noexcept { return info_; }
    // nullopt means timeout/cancel. Other errors throw, including unplug.
    std::optional<std::vector<std::uint8_t>> read(HANDLE stop_event, DWORD timeout_ms);
    void send(const Command& command) override;
private:
    UniqueHandle handle_;
    Access access_;
    DeviceInfo info_;
};
}
