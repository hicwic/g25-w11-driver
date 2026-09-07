// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "device/hid_transport.h"
#include <iosfwd>

namespace g25 {
DeviceInfo select_device(const std::vector<DeviceInfo>& devices, std::optional<std::size_t> index);
void require_g25_writer(const DeviceInfo& info, bool switching_mode);
void print_device_info(const DeviceInfo& info, std::size_t index, bool detailed);
void print_inputs(const InputState& state, int assumed_range, bool raw, std::span<const std::uint8_t> report);

// Serializes this tool's writers within the interactive Windows session.
// Does not claim to lock out unrelated games or vendor tools.
class WriterLock {
public:
    WriterLock();
    ~WriterLock();
    WriterLock(const WriterLock&) = delete;
    WriterLock& operator=(const WriterLock&) = delete;
private:
    UniqueHandle handle_;
};
}
