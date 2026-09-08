# g25-userspace

Open source **C++20 / CMake** prototype for using a Logitech G25 on Windows 11
through the standard Microsoft HID stack. License: GPL-2.0-only.

The project provides:

- `g25tool.exe`, a diagnostic and low-level control CLI.
- `g25ff.dll`, a per-user DirectInput force feedback effect driver, built for
  x64 and x86 games.
- `g25tray.exe`, a notification-area helper that switches the wheel to native
  G25 mode and applies the preferred steering range.
- An **optional** [Virtual G29 bridge](virtual-g29/README.md) that presents the
  G25 as a virtual Logitech G29 for software that only accepts a supported wheel
  from a hard-coded list - GeForce NOW filters the G25 out before the remote
  game sees it, and some local titles gate features by wheel model.
- A per-user Windows installer and GitHub Actions release pipeline.

No custom kernel driver, LGS, WinUSB/Zadig setup, Secure Boot change, Memory
Integrity/HVCI change or driver-signing bypass is required by this prototype.
The DirectInput integration adds COM/OEM registry keys under the current user;
the installer backs them up and restores/removes them on uninstall.

The Virtual G29 bridge is a separate opt-in component (it adds a virtual HID
driver and a Windows service); the core driver above stays pure per-user. See
[virtual-g29/README.md](virtual-g29/README.md).

## AI Assistance Notice

This project was developed with substantial AI assistance. The code and
documentation should be reviewed like any other community driver-adjacent
project: read the source, check the protocol notes, and test cautiously. This
notice is intentionally visible for contributors and users who do not want to
use AI-assisted software.

## Current Status

The native inputs, 180 degree stops, force commands, DirectInput loading and all
12 standard DirectInput effects have been tested on a real Logitech G25 without
the legacy Logitech drivers installed. See [validation](docs/validation.md).

The current DirectInput driver exposes these standard effects:

- Constant Force
- Ramp Force
- Square, Sine, Triangle, Sawtooth Up and Sawtooth Down
- Spring, Damper, Inertia and Friction
- Custom Force

Pedals and shifter are optional. The G25 base exposes a fixed HID descriptor, so
their axes/buttons are still visible to Windows when those accessories are not
connected. Games that support multiple controllers can bind the G25 wheel and a
separate USB pedal set independently.

## Windows Installer

Download the latest `g25-w11-driver-<version>-setup.exe` from GitHub Releases:

https://github.com/hicwic/g25-w11-driver/releases

The installer runs per user and installs into `%LOCALAPPDATA%\g25ff`. It copies
the x64/x86 DirectInput DLLs, installs `g25tray.exe`, registers the DirectInput
FFB effects for `VID_046D&PID_C299`, starts G25 Control, and adds it to user
sign-in startup.

Uninstall is available from Windows Settings, Apps, Installed apps,
**G25 Windows 11 Driver**. It runs the same cleanup path as the script:

- stops `g25tray.exe`
- removes the startup entry
- unregisters the x64/x86 COM DirectInput effect driver
- restores previous per-user OEM/COM keys when a backup existed
- removes the installed binaries

Close games before installing, updating or uninstalling so `g25ff.dll` is not
held open by a running process.

## Building With Visual Studio 2022

Install Visual Studio 2022 with Desktop development with C++, the Windows SDK
and CMake 3.24 or newer.

```powershell
cmake --preset vs2022-x64
cmake --build --preset release
ctest --preset release

cmake --preset vs2022-x86
cmake --build --preset release-x86
ctest --preset release-x86
```

The x64 CLI is built at `build/vs2022-x64/Release/g25tool.exe`. The project can
also be opened directly in Visual Studio through `CMakePresets.json`.

The CI builds with MSVC on Windows. During early local development, portable
Clang/LLVM-MinGW builds were also used under `build/portable-release` and
`build/portable-release-x86`.

## CLI Usage

In the examples below, `g25tool` means the built executable. Attach the wheel
base and any accessories you want to use. Clamp the wheel, keep its movement
clear, and close games or tools that may already command the motors.

```text
g25tool list
g25tool info
g25tool monitor
g25tool monitor --raw --seconds 30
```

`list` shows device index, PID, revision and HID path. If several wheels are
present, run `list` again and pass `--device INDEX`. `info` is diagnostic only:
it prints report sizes, HID usages and the DirectInput FFB capability flag.

If a recognized G25 is still in Driving Force / DFP compatibility mode:

```text
g25tool native
g25tool list
g25tool monitor
```

Wait for USB re-enumeration between `native` and `list`. The tool does not send
native-mode commands to a shared PID unless the real revision identifies a G25.
If an unknown descriptor is found, the tool reports it instead of guessing.

`monitor` prints the estimated wheel angle, three separate pedals, buttons
1-19, POV, an indicative shifter interpretation and raw vendor bytes. Axis
display is rate-limited to 20 Hz; button/POV changes are shown immediately.
`--raw` prints every input report.

```text
g25tool range 900 --dry-run
g25tool range 900
g25tool range 540
g25tool monitor --range 540
```

`--dry-run` does not open hardware and prints the exact Windows output buffer.
The accepted physical range is 40-900 degrees. `monitor --range` is only the
angle conversion assumption; it does not configure the wheel.

## Force Feedback Tests

Inspect reports without hardware first:

```text
g25tool test-ffb --dry-run
g25tool center --dry-run
g25tool test-ffb damper --dry-run
```

Then, with the wheel clamped and clear:

```text
g25tool test-ffb
g25tool center
g25tool test-ffb damper
g25tool stop
```

The CLI tests use bounded force for one second. `Ctrl+C` interrupts the wait and
sends stop commands. Sessions also try to stop effects on exit or exception.
There is no verified hardware watchdog; if a process or USB host crashes before
the stop reaches the wheel, force may remain active. Keep power reachable during
early tests.

## DirectInput Game Integration

Build both architectures, then register the DLLs for the current user:

```powershell
cmake -S . -B build/x64 -A x64
cmake --build build/x64 --config Release
cmake -S . -B build/x86 -A Win32
cmake --build build/x86 --config Release
powershell -ExecutionPolicy Bypass -File scripts/Register-G25FF.ps1 `
  -Action Install `
  -Dll64 build/x64/Release/g25ff.dll `
  -Dll32 build/x86/Release/g25ff.dll `
  -Tray build/x64/Release/g25tray.exe
```

The script copies files to `%LOCALAPPDATA%\g25ff\bin`, backs up existing
per-user OEM/COM keys, registers the 12 DirectInput effect GUIDs for the G25
`046d:c299`, starts G25 Control and registers it for sign-in. It does not ask
for elevation and does not install a kernel driver.

To uninstall and restore the saved per-user registry state:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/Register-G25FF.ps1 -Action Uninstall
```

## G25 Control Tray App

Click the **G25 Control** wheel icon in the notification area to choose 180, 360,
540 or 900 degrees. The choice is saved in the user profile and applied to the
wheel.

After USB connection, the tray app switches a recognized G25 from compatibility
mode to native mode, waits for re-enumeration, waits briefly for calibration to
settle, then applies the saved steering range. It sends stop and disables
autocenter after applying the range so the motors are left idle. The app reacts
to Windows device notifications and stays idle once setup is done.

The DirectInput FFB DLL does not need `g25tray.exe` once the wheel is in native
mode; games load the DLL in their own process. The tray app exists to automate
native mode and steering range after reconnects. Max FFB gain is left to games.

## Project Layout

- `src/protocol`: pure Logitech command encoders and input decoder
- `src/device`: Win32 HID transport, device selection and DirectInput probing
- `src/directinput`: DirectInput COM effect driver
- `src/app`: CLI
- `src/tray`: notification-area helper
- `src/settings`: per-user settings
- `tests`: protocol, math, COM-loading and CLI dry-run tests

Reference material in `.research` and portable tools in `.tools` are ignored by
Git and are not dependencies of the distributed source.

## Release Pipeline

GitHub Actions provides:

- CI on Windows x64, Windows x86 and Linux portable core.
- Dev artifacts named `dev-<sha>` on every push to `main`.
- A moving `nightly` prerelease.
- Versioned releases when a tag named `v*` is pushed.

Each release builds both architectures, generates a changelog, produces a
portable zip and compiles a per-user Windows installer. See
[release process](docs/release.md).
