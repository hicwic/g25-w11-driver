// SPDX-License-Identifier: GPL-2.0-only
#include "app/console_stop.h"
#include "device/g25_device.h"
#include "device/directinput_probe.h"
#include "protocol/force_feedback.h"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace g25;
void usage() {
    std::cout << "g25tool 0.1.0 - Windows HID userspace prototype\n"
        "  g25tool list\n  g25tool info [--device INDEX]\n"
        "  g25tool monitor [--device INDEX] [--range 900] [--raw] [--seconds N]\n"
        "  g25tool native [--device INDEX] [--dry-run]\n"
        "  g25tool range 40..900 [--device INDEX] [--dry-run]\n"
        "  g25tool center [--device INDEX] [--dry-run]\n"
        "  g25tool test-ffb [constant|spring|damper] [--device INDEX] [--dry-run]\n"
        "  g25tool directinput-test EFFECT\n"
        "    EFFECT: constant, ramp, square, sine, triangle, saw-up, saw-down,\n"
        "            spring, damper, inertia, friction, custom\n"
        "  g25tool stop [--device INDEX] [--dry-run]\n"
        "All effects use fixed low force for 1 second; Ctrl+C stops.\n"
        "--dry-run prints Windows reports without accessing a device.\n"
        "monitor --range is an angle assumption, not a hardware setting.\n"
        "No driver installation or Windows security changes. License: GPL-2.0-only.\n";
}
int number(std::string_view text, const char* name) {
    int result{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw std::invalid_argument(std::string("invalid integer for ") + name);
    return result;
}
struct Options {
    std::string command;
    std::optional<std::size_t> device;
    int range = 900, seconds = 0;
    bool raw = false, dry_run = false;
    TestEffect effect = TestEffect::constant;
    std::string directinput_effect = "constant";
};
Options parse(int argc, char** argv) {
    Options options;
    options.command = argc < 2 ? "help" : argv[1];
    int index = 2;
    if (options.command == "range") {
        if (argc <= index) throw std::invalid_argument("range requires an angle");
        options.range = number(argv[index++], "range"); (void)set_range(options.range);
    }
    if (options.command == "center") options.effect = TestEffect::spring;
    if (options.command == "test-ffb" && index < argc && !std::string_view(argv[index]).starts_with("--")) {
        const std::string_view effect = argv[index++];
        if (effect == "spring") options.effect = TestEffect::spring;
        else if (effect == "damper") options.effect = TestEffect::damper;
        else if (effect != "constant") throw std::invalid_argument("effect must be constant, spring or damper");
    }
    if (options.command == "directinput-test" && index < argc && !std::string_view(argv[index]).starts_with("--")) {
        options.directinput_effect = argv[index++];
        constexpr std::string_view effects[]{"constant", "ramp", "square", "sine", "triangle", "saw-up",
            "saw-down", "spring", "damper", "inertia", "friction", "custom"};
        if (std::find(std::begin(effects), std::end(effects), options.directinput_effect) == std::end(effects))
            throw std::invalid_argument("unknown DirectInput effect");
    }
    const bool write = options.command == "range" || options.command == "native" || options.command == "center" ||
        options.command == "test-ffb" || options.command == "stop";
    if (!write && options.command != "list" && options.command != "info" && options.command != "monitor" &&
        options.command != "directinput-test" && options.command != "help" && options.command != "--help")
        throw std::invalid_argument("unknown command; use --help");
    for (; index < argc; ++index) {
        const std::string_view flag = argv[index];
        if (flag == "--dry-run" && write) options.dry_run = true;
        else if (flag == "--raw" && options.command == "monitor") options.raw = true;
        else if (flag == "--device" && options.command != "list" && options.command != "help" && options.command != "--help") {
            if (++index >= argc) throw std::invalid_argument("--device requires an index");
            const int device = number(argv[index], "device");
            if (device < 0) throw std::invalid_argument("device index must be nonnegative");
            options.device = static_cast<std::size_t>(device);
        } else if (flag == "--range" && options.command == "monitor") {
            if (++index >= argc) throw std::invalid_argument("--range requires an angle");
            options.range = number(argv[index], "range"); (void)set_range(options.range);
        } else if (flag == "--seconds" && options.command == "monitor") {
            if (++index >= argc) throw std::invalid_argument("--seconds requires a duration");
            options.seconds = number(argv[index], "seconds");
            if (options.seconds < 1 || options.seconds > 3600) throw std::invalid_argument("seconds must be 1..3600");
        } else throw std::invalid_argument("unexpected option: " + std::string(flag));
    }
    return options;
}
class DryWriter : public ReportWriter {
    void send(const Command& command) override { std::cout << "DRY-RUN TX [" << hex_bytes(windows_output(command)) << "]\n"; }
};
// The same sequence is used by dry-run and hardware; only the writer/wait differ.
void perform_output(ReportWriter& writer, const Options& options, const ConsoleStop* stop,
                    Model model = Model::g25) {
    OutputSession session(writer);
    session.initialize();
    if (stop && stop->requested()) return;
    if (options.command == "native") {
        // Stop before a command that may invalidate the handle by USB detach.
        session.finish();
        try { writer.send(native_mode(model)); }
        catch (...) {
            OutputSession cleanup(writer); // Best effort if detach made the result ambiguous.
            throw;
        }
        std::cout << "Native mode command transferred. Wait for USB re-enumeration, then run list.\n";
    } else if (options.command == "range") {
        writer.send(set_range(options.range));
        session.finish();
        std::cout << "Range command transferred: " << options.range << " deg (physical verification required).\n";
    } else if (options.command == "stop") {
        session.finish();
    } else {
        std::cout << "Bounded-force test, at most 1000 ms planned. Keep wheel clear; Ctrl+C stops.\n" << std::flush;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(test_duration_ms);
        writer.send(low_force_test(options.effect));
        if (stop) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
            if (remaining > 0) (void)stop->wait(static_cast<DWORD>(remaining));
        }
        else std::cout << "DRY-RUN wait 1000 ms (not executed)\n";
        session.finish();
        std::cout << "Stop reports transferred.\n";
    }
}
int run(const Options& options) {
    if (options.command == "help" || options.command == "--help") { usage(); return 0; }
    if (options.dry_run) {
        std::cout << "DRY-RUN ONLY: no HID enumeration, no hardware I/O.\n";
        DryWriter writer; perform_output(writer, options, nullptr); return 0;
    }
    const auto devices = enumerate_wheels();
    if (options.command == "list") {
        for (std::size_t index = 0; index < devices.size(); ++index) print_device_info(devices[index], index, false);
        if (devices.empty()) std::cout << "No matching Logitech wheel HID collection detected.\n";
        return 0;
    }
    if (options.command == "info") {
        if (options.device) print_device_info(select_device(devices, options.device), *options.device, true);
        else {
            for (std::size_t index = 0; index < devices.size(); ++index) print_device_info(devices[index], index, true);
            if (devices.empty()) std::cout << "No matching Logitech wheel HID collection detected.\n";
        }
        print_directinput_inventory(); return 0;
    }
    if (options.command == "directinput-test") {
        ConsoleStop stop;
        run_directinput_test(options.directinput_effect, stop.event());
        return stop.requested() ? 130 : 0;
    }
    const auto device = select_device(devices, options.device);
    std::clog << "Selected: " << utf8(device.path) << '\n';
    ConsoleStop stop;
    if (options.command == "monitor") {
        const Model monitor_model = identify_model(device.pid, device.revision);
        if (device.pid != g25_pid && device.pid != g27_pid)
            throw std::runtime_error("monitor needs native G25/G27 mode; run native, wait, then list");
        HidTransport transport(device, Access::read);
        std::cout << "Assumed range: " << options.range << " deg (not read from device). Ctrl+C exits.\n"
                  << "Gear* is indicative Profiler mapping; confirm against raw buttons on your shifter.\n";
        const auto start = std::chrono::steady_clock::now();
        auto last_display = start - std::chrono::seconds(1);
        std::optional<InputState> last_state;
        bool received = false;
        while (!stop.requested()) {
            if (options.seconds && std::chrono::steady_clock::now() - start >= std::chrono::seconds(options.seconds)) break;
            const auto report = transport.read(stop.event(), 100);
            if (!report) continue;
            received = true;
            const auto now = std::chrono::steady_clock::now();
            const auto state = decode_windows_input(*report, monitor_model);
            // Preserve brief button/gear transitions while limiting axis-only
            // console updates to 20 Hz. --raw shows every received report.
            const bool button_change = !last_state || state.buttons != last_state->buttons || state.hat != last_state->hat;
            if (options.raw || button_change || now - last_display >= std::chrono::milliseconds(50)) {
                print_inputs(state, options.range, options.raw, *report);
                last_display = now;
            }
            last_state = state;
        }
        if (!received) std::cerr << "No input reports received; hardware milestone not verified.\n";
        return stop.requested() ? 130 : (received ? 0 : 1);
    }
    require_g25_writer(device, options.command == "native");
    const Model model = identify_model(device.pid, device.revision);
    const std::uint16_t native_pid = model == Model::g27 ? g27_pid : g25_pid;
    if (options.command == "native" && device.pid == native_pid) {
        std::cout << "Already in native mode; no reports sent.\n"; return 0;
    }
    WriterLock lock;
    HidTransport transport(device, Access::write);
    if (stop.requested()) return 130;
    perform_output(transport, options, &stop, model);
    return stop.requested() ? 130 : 0;
}
}
int main(int argc, char** argv) {
    try { return run(parse(argc, argv)); }
    catch (const std::invalid_argument& error) { std::cerr << "Argument error: " << error.what() << '\n'; return 2; }
    catch (const std::exception& error) { std::cerr << "Error: " << error.what() << '\n'; return 1; }
}
