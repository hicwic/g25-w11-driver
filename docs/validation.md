# Prototype Validation

## Hardware Tests On 7 September 2026

Tests were run on a Logitech G25 revision `1222`, using the Microsoft HID stack
after removing legacy WingMan / Logitech Gaming Software filters. These results
replace the earlier "not tested" notes from the initial audit.

| Check | Observed Result |
| --- | --- |
| `native` mode switch | Successful transfer and re-enumeration from `046d:c294` to `046d:c299` |
| Native descriptor | Windows reports **12/8/145 byte** input/output/feature buffers; decoder offsets verified with HidP on the real descriptor |
| DirectInput without LGS, before `g25ff` | **4 axes, 19 buttons, 1 POV; FFB not advertised** |
| DirectInput with `g25ff`, x64 and x86 | **4 axes, 19 buttons, 1 POV; FFB advertised; 12/12 standard effects** |
| 60-second input capture | **17,683 reports** decoded; wheel movement and all three pedals observed |
| Pedals | Throttle, brake and clutch each reached **0 to 100%** in the capture |
| Shifter | Indicative N, 1-6 and R values appeared in the capture; exhaustive physical switch mapping remains to be done |
| 540 degree range | Transfer succeeded; physical confirmation was considered hard to judge by the user |
| 180 degree range | Transfer succeeded; **user confirmed stops at about +/-90 degrees** |
| Return to 900 degrees after 180 degree test | Command transferred; full physical range not re-measured |
| Constant force at 3.1% for one second | Effect and both stop commands transferred; no force felt in two tests |
| Spring at 3.1% saturation for one second | Transfer and stop succeeded; no force felt |
| Constant force at 12.5%, repeated | Force felt, but considered very weak by the user |
| Spring at 30%, repeated | **Effect clearly felt and confirmed by the user** |
| Damper at 30%, repeated | **Effect clearly felt and confirmed by the user** |
| Console interruption during an effect | `CTRL_BREAK` injected after 250 ms; global stop and autocenter-disable transferred; exit code 130 |
| DirectInput x64, constant/spring/damper | `CreateEffect` and `Start` succeeded; expected `A6`, spring `EE/4C`, damper `0F/4C` reports, then clean stop |
| DirectInput x86, constant | Same path succeeded with the same constant report `A6`, then clean stop |
| DirectInput x64, nine added effects | Ramp, Square, Sine, Triangle, Sawtooth Up/Down, Inertia, Friction and Custom created, started, updated when time-based, and stopped successfully on the G25 |
| DirectInput x86, added effects | Inventory 12/12; Sine, Friction and Custom executed on the G25 with clean stop |
| G25 Control | Notification process starts at sign-in; 180/360/540/900 menu command path verified; final return to 900 degrees; no measurable idle CPU over two seconds |

Release LLVM-MinGW x64 and x86 builds each pass the five CTest suites. The COM
test loads the DLL directly, creates the factory and `IDirectInputEffectDriver`,
checks its version and unloads it without opening hardware. The math suite
checks waveforms, ramps and Custom sample indexing independently from real time.
After registration, a DirectInput inventory does not emit HID reports; write
access is deferred until the first FFB command.

Monitor angle values depend on the range passed as an argument. They do not
measure the real physical travel. The 180 degree validation is based on user
feedback, not only displayed angle values. The 19 buttons, every POV direction
and sequential shifter mode are not yet validated one by one. Small pedal
variations near rest appeared in captures; no dead zone is applied.

Read-only security inventory reported `VirtualizationBasedSecurityStatus=2`, but
`SecurityServicesRunning=[0]`, `HypervisorEnforcedCodeIntegrity\Enabled=0`, and
`UEFISecureBootEnabled=0`. **These tests therefore do not validate operation
with Memory Integrity and Secure Boot enabled.** None of those settings was
changed during testing or cleanup.

Local captures and diagnostics are kept under `build/hardware-tests-20260907/`,
which is ignored by Git. Console interruption was validated through transferred
stop reports and exit code; physical feel during an interrupted effect, forced
process termination and USB disconnect still need more testing. Real-game use
was confirmed with the initial DirectInput effects. The nine later-added effects
have validated DirectInput/HID transfer paths; their exact physical feel should
still be compared game by game.

## Driver Cleanup Performed On 7 September 2026

At the user's request, WingMan 5.09.129.0 packages `oem25.inf` (WmJoyHid),
`oem26.inf` (WmVirHid) and `oem27.inf` (WmBEnum) were exported and removed with
PnPUtil. Removal succeeded without `/force` and without a requested reboot.
Related virtual devices, old wheel instances and OEM/FFB registrations for
C294/C298/C299 modes were removed after backup. Other Logitech devices were not
targeted. No Windows security setting was changed.

Post-cleanup PnP inventory: both USB and HID nodes for the G25 use `input.inf`,
provider **Microsoft**, status OK, with no WingMan filter. The USB stack includes
`HidUsb`; the joystick stack includes `hidgamepad` and `HidUsb`. Services
WmHidLo/WmFilter/WmBEnum/WmXlCore/WmVirHid no longer appear in system-driver
inventory.

The wheel then reported **046d:c294, revision 1222** in compatibility mode.
`g25tool info` correctly detected the G25 and reported:

- Windows input/output/feature buffers: **8/8/0 bytes**
- 10-bit X wheel axis, combined Y axis, 12 buttons and one POV
- vendor output `FF00:03`, seven bytes with report ID zero
- DirectInput: **2 axes, 12 buttons, 1 POV, FFB not advertised**

No `native`, `range` or FFB command was sent during cleanup. The next step was
the controlled native switch, followed by a fresh descriptor inventory and input
tests.

Local backup:
`build/driver-cleanup-20260907/backup-20260907-085114/` (32 files). The parent
folder keeps `cleanup.ps1`, `cleanup.log`, `result.json`, inventories and CLI
output before/after cleanup. It is ignored by Git. The cleanup script was
machine-specific and checked the hashes of the three INF packages before any
mutation; do not reuse it on another machine or after package numbers change.

## Earlier Audit: G25 Present With Legacy Logitech Driver

Before cleanup, the connected G25 was detected as `046d:c299`, revision `1222`,
but that did **not** validate operation without the Logitech driver:

- USB/HID nodes used Logitech package `oem25.inf`, version `5.9.129.0`.
- `pnputil /enum-devices /instanceid ... /stack` confirmed `WmHidLo` in the USB
  stack and `WmFilter` in the HID stack, alongside `HidUsb`.
- DirectInput reported five axes, 19 buttons, one POV and
  `DIDC_FORCEFEEDBACK=yes`; OEMForceFeedback was still registered with Logitech
  CLSID `{8D533A4D-7A5F-11D3-8297-0050DA1A72D3}`.
- HID capabilities reported **13/8/145 byte** input/output/feature buffers,
  including report ID, with an additional Slider axis. The original decoder
  expected 12 input bytes and rejected that descriptor.

Only diagnostic requests were run during that audit: no driver change, no
registry change, no security change and no motor command.

Reference:
[PnPUtil `/stack`](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/pnputil-command-syntax).

## Initial Audit On 6 September 2026

The first audit was run before hardware validation. A working executable did not
validate hardware behavior.

| Check | Result |
| --- | --- |
| Local system | Windows 11 Professional, build 26200, x64 |
| Local compiler | LLVM-MinGW 20260826, Clang 23.1.0, UCRT x64 |
| Generator | CMake 4.4.3, MinGW Makefiles |
| Debug build of core then CLI | Successful, no warnings with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion` |
| Release CLI build | Successful, no warnings with the same options |
| Protocol and stop-guard tests | 69 checks passed |
| Windows capability, selection, writer-exclusion and cancellation tests | 23 checks passed |
| CLI dry-run and invalid argument scenarios | 18 scenarios passed |
| Debug and Release CTest | 3 suites of 3 passed in each configuration |
| `g25tool list` on the PC | No matching Logitech wheel collection |
| `g25tool info` on the PC | No matching HID or DirectInput wheel |
| Portable binary dependencies | Windows/HID/SetupAPI/DirectInput DLLs and UCRT; no HIDAPI/libusb/LLVM DLL to deploy |
| MSVC / GitHub workflow build | Configuration provided; not run locally without Visual Studio |
| Real G25 inputs | **Not tested: wheel absent** |
| USB switch, stops, physical effect, motor stop | **Not tested: wheel absent** |
| FFB in games | **Not implemented at that milestone** |

Tests do not enumerate or open HID devices. Input fixtures are synthetic vectors
built from the published descriptor, not captures presented as coming from a
real G25.

The Windows interruption test triggers a stop event during a one-second wait and
checks that the wait wakes early. It does not simulate forced system shutdown
and does not measure USB motor-stop latency.

## Reproducing The Portable Build On The Test Machine

Tools were downloaded from official
[LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw/releases/tag/20260826) and
[CMake](https://github.com/Kitware/CMake/releases/tag/v4.4.3) releases, then
extracted under `.tools` without system installation. The Authenticode signature
of `cmake.exe` was verified as valid. The global `PATH` was not modified.

```powershell
$cmake = Join-Path $PWD '.tools/cmake-4.4.3-windows-x86_64/bin/cmake.exe'
$ctest = Join-Path $PWD '.tools/cmake-4.4.3-windows-x86_64/bin/ctest.exe'
$compiler = Join-Path $PWD '.tools/llvm-mingw-20260826-ucrt-x86_64/bin/clang++.exe'
$make = Join-Path $PWD '.tools/llvm-mingw-20260826-ucrt-x86_64/bin/mingw32-make.exe'
& $cmake -S . -B build/portable-release -G 'MinGW Makefiles' "-DCMAKE_CXX_COMPILER=$compiler" "-DCMAKE_MAKE_PROGRAM=$make" -DCMAKE_BUILD_TYPE=Release
& $cmake --build build/portable-release --parallel 4
& $ctest --test-dir build/portable-release --output-on-failure
```

For MSVC, use the commands from the README. Keep build directories separate to
avoid reusing a CMake cache from another compiler.

## Hardware Test Procedure

Keep the command output together with the Windows build/version, wheel revision,
active driver provider and Secure Boot / Memory Integrity state. Do not change
those protections just to make a test pass.

1. **Before writes**: clamp the wheel, attach wanted accessories, keep rotation
   clear, close games and other FFB controllers. Run `list`, then `info`. Record
   VID/PID, revision and report lengths. Check that the Microsoft driver is
   active in Device Manager and keep the DirectInput diagnostic.
2. **Mode**: if a recognized G25 is in `c294/c298`, inspect `native --dry-run`,
   then run `native`; wait and rerun `list`/`info`. Expected result: PID `c299`
   and valid new handles.
3. **Inputs**: run `monitor --raw --seconds 30`, then `monitor`. Turn the wheel
   slowly, actuate each pedal through full travel, press every button and POV
   direction, and test N, 1-6, R and sequential shifter mode.
4. **Range**: inspect `range 540 --dry-run`, send `range 540`, then run
   `monitor --range 540`. Measure physical travel and stops instead of relying
   on the displayed conversion. Repeat with 900 and after reconnect.
5. **Constant force**: inspect `test-ffb --dry-run`, then run `test-ffb`. Expect
   weak force for about one second and a return to idle. Keep power reachable.
6. **Conditions**: run `center` for temporary spring, then `test-ffb damper`.
   Damper is felt while moving, not necessarily at rest. Verify stop behavior.

A milestone is validated only after the physical result is recorded. If
`WriteFile` fails, record the Win32 code, PID, capabilities and active driver.
The project does not recommend disabling HVCI, replacing HID with Zadig or
installing unsigned drivers to bypass failures.

## Limits To Keep Visible

- The reference descriptor comes from G25 mode on a G29 in `lg4ff_userspace`.
  Its offsets were later verified on a real G25 revision `1222` with the
  Microsoft stack; that does not cover every revision.
- Unknown firmware is an analysis case. There is no "force" option to blindly
  send commands to an unrecognized revision.
- The Profiler shifter mapping may differ from raw HID; raw fields remain
  visible to avoid hiding that uncertainty.
- Timeouts and Ctrl+C are software controls. No wheel watchdog has been proven,
  and cancelling I/O does not guarantee the hardware received a stop command.
- The prototype keeps the requested range but leaves effects stopped and
  autocenter disabled; it cannot restore settings that were never read.
- No virtual backend or kernel driver is included.
