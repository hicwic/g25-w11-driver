// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "device/hid_transport.h"
#include <chrono>
#include <iosfwd>

namespace g25 {
DeviceInfo select_device(const std::vector<DeviceInfo>& devices, std::optional<std::size_t> index);
void require_g25_writer(const DeviceInfo& info, bool switching_mode);
void print_device_info(const DeviceInfo& info, std::size_t index, bool detailed);
void print_inputs(const InputState& state, int assumed_range, bool raw, std::span<const std::uint8_t> report);

// Serializes this tool's writers within the interactive Windows session.
// Does not claim to lock out unrelated games or vendor tools.
//
// This wraps a named Win32 mutex, so it carries thread affinity: the thread
// that constructs a WriterLock must be the thread that destroys it. Releasing
// from another thread silently fails (ERROR_NOT_OWNER) and leaves the mutex
// held until the process exits.
class WriterLock {
public:
    // Fail fast: throws immediately if another writer holds the wheel. Right for
    // interactive tools (the CLI, the tray) that should yield to a running game.
    WriterLock();
    // Wait up to `timeout` before giving up. For a long-lived claim - a game's
    // effect driver - where losing to the tray's few-millisecond range write
    // would cost the whole session its force feedback.
    explicit WriterLock(std::chrono::milliseconds timeout);
    ~WriterLock();
    WriterLock(const WriterLock&) = delete;
    WriterLock& operator=(const WriterLock&) = delete;

    // Non-throwing peek: false when another writer (a game's g25ff.dll, the
    // bridge worker, another g25tool) currently holds the output mutex.
    static bool available() noexcept;

private:
    UniqueHandle handle_;
};
}
