// SPDX-License-Identifier: GPL-2.0-only
#include "app/console_stop.h"
#include "device/g25_device.h"
#include "settings/user_settings.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
// WriterLock's semantics do not depend on which mutex it guards, so the tests
// use their own name. Running them against the shipping one made the suite fail
// whenever a game legitimately held the wheel.
constexpr wchar_t test_mutex[] = L"Local\\g25tool-output-tests";
constexpr auto fail_fast = std::chrono::milliseconds(0);
int checks{};
void check(bool ok, const char* name) {
    ++checks;
    if (!ok) throw std::runtime_error(name);
}
template<class F> void rejects(F&& fn, const char* name) {
    bool threw = false;
    try { fn(); } catch (const std::exception&) { threw = true; }
    check(threw, name);
}
g25::DeviceInfo native_caps() {
    // Semantic fixture from rd_g25, no live device and no fabricated preparsed
    // data. Offset verification itself needs a real Windows HID collection.
    g25::DeviceInfo info;
    info.vid = g25::logitech_vid; info.pid = g25::g25_pid; info.revision = 0x1222;
    info.caps_valid = true; info.native_offsets_verified = true;
    info.caps.UsagePage = 1; info.caps.Usage = 4;
    info.caps.InputReportByteLength = 12; info.caps.OutputReportByteLength = 8;
    for (auto usage : {0x30, 0x32, 0x35, 0x31, 0x39}) {
        HIDP_VALUE_CAPS cap{};
        cap.UsagePage = 1; cap.NotRange.Usage = static_cast<USAGE>(usage);
        cap.BitSize = static_cast<USHORT>(usage == 0x30 ? 14 : usage == 0x39 ? 4 : 8);
        cap.LogicalMax = usage == 0x30 ? 16383 : usage == 0x39 ? 7 : 255;
        cap.IsAbsolute = TRUE;
        info.input_values.push_back(cap);
    }
    HIDP_VALUE_CAPS output{};
    output.UsagePage = 0xff00; output.NotRange.Usage = 2; output.BitSize = 8; output.ReportCount = 7;
    info.output_values.push_back(output);
    HIDP_BUTTON_CAPS buttons{};
    buttons.UsagePage = 9; buttons.IsRange = TRUE; buttons.Range.UsageMin = 1; buttons.Range.UsageMax = 19;
    info.input_buttons.push_back(buttons);
    return info;
}
void run() {
    using namespace g25;
    auto info = native_caps();
    check(native_input_layout(info) && logitech_output_layout(info), "known native capabilities");
    require_g25_writer(info, false);
    info.native_offsets_verified = false;
    rejects([&] { require_g25_writer(info, false); }, "do not write with unverified offsets");
    info = native_caps(); info.revision = 0x1230;
    rejects([&] { require_g25_writer(info, false); }, "G27 shares G25 PID");
    info = native_caps(); info.revision = 0x0000;
    rejects([&] { require_g25_writer(info, false); }, "unknown revision cannot write");
    info = native_caps(); info.vid = 0x1111;
    rejects([&] { require_g25_writer(info, false); }, "non-Logitech cannot write");
    info = native_caps(); info.caps.OutputReportByteLength = 7;
    check(!logitech_output_layout(info), "Windows length must include report ID");
    info = native_caps(); info.output_values[0].ReportID = 1;
    check(!logitech_output_layout(info), "unknown output report ID");
    info = native_caps(); info.input_values[0].BitSize = 16;
    check(!native_input_layout(info), "reject wrong wheel resolution");
    info = native_caps(); info.input_buttons[0].Range.UsageMax = 22;
    check(!native_input_layout(info), "reject G27 native button count");
    info = native_caps(); info.pid = dfp_pid; info.output_values[0].UsagePage = 1;
    check(logitech_output_layout(info), "DFP output is Generic Desktop usage 2");
    require_g25_writer(info, true);
    rejects([&] { require_g25_writer(info, false); }, "FFB cannot use compatibility mode");
    info.pid = dfex_pid; info.output_values[0].NotRange.Usage = 3;
    check(logitech_output_layout(info), "DF output is Generic Desktop usage 3");
    info.output_values[0].UsagePage = 0xff00;
    check(logitech_output_layout(info), "DF output vendor usage 3 variant");
    info.caps.OutputReportByteLength = 16;
    check(!logitech_output_layout(info), "do not infer offsets for oversized DFEX outputs");
    rejects([] { (void)select_device({}, {}); }, "no hardware");
    rejects([] { (void)select_device({native_caps(), native_caps()}, {}); }, "ambiguous devices");
    check(select_device({native_caps(), native_caps()}, 1).pid == g25_pid, "explicit device index");
    rejects([] { (void)select_device({native_caps()}, 2); }, "invalid device index");
    check(supported_rotation(180) && supported_rotation(360) && supported_rotation(540) &&
          supported_rotation(900), "tray rotation presets");
    check(!supported_rotation(179) && !supported_rotation(901), "reject unsupported tray rotations");
    {
        WriterLock lock(test_mutex, fail_fast);
        // A mutex is recursive in its owning thread: contention must be tested
        // from another thread, as it would be from another CLI process.
        bool blocked = false;
        std::thread contender([&] {
            try { WriterLock other(test_mutex, fail_fast); } catch (const std::exception&) { blocked = true; }
        });
        contender.join();
        check(blocked, "concurrent writers are refused");
    }
    {
        // g25ff.dll claims the wheel for a whole game session, so it waits for the
        // current writer instead of failing fast the way the CLI and tray do.
        std::atomic<bool> holding{false};
        std::thread holder([&] {
            WriterLock held(test_mutex, std::chrono::milliseconds(5000));
            holding = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
        });
        while (!holding) std::this_thread::yield();
        const auto start = std::chrono::steady_clock::now();
        bool acquired = false;
        try { WriterLock waited(test_mutex, std::chrono::milliseconds(5000)); acquired = true; }
        catch (const std::exception&) {}
        const auto elapsed = std::chrono::steady_clock::now() - start;
        holder.join();
        check(acquired, "timeout constructor waits for the current writer");
        check(elapsed >= std::chrono::milliseconds(40), "timeout constructor really blocked");
    }
    {
        // ...but it still gives up rather than hanging when the wheel stays busy.
        std::atomic<bool> holding{false};
        std::atomic<bool> release{false};
        std::thread holder([&] {
            WriterLock held(test_mutex, std::chrono::milliseconds(5000));
            holding = true;
            while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        });
        while (!holding) std::this_thread::yield();
        bool refused = false;
        try { WriterLock waited(test_mutex, std::chrono::milliseconds(30)); }
        catch (const std::exception&) { refused = true; }
        release = true;
        holder.join();
        check(refused, "timeout constructor gives up when the wheel stays busy");
    }
    {
        ConsoleStop stop;
        check(!stop.requested() && !stop.wait(1), "unsignaled wait times out");
        const auto start = std::chrono::steady_clock::now();
        std::jthread trigger([&] { std::this_thread::sleep_for(std::chrono::milliseconds(30)); SetEvent(stop.event()); });
        check(stop.wait(1000), "stop event interrupts effect wait");
        check(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(900), "cancellation does not wait full duration");
        check(stop.requested(), "stop remains latched");
    }
}
}
int main() {
    try { run(); std::cout << checks << " Windows descriptor/selection/cancellation checks passed (no HID I/O)\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
