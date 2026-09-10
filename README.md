# g25-driver

Use a **Logitech G25** racing wheel on **Windows 11** - without the old Logitech
software, and without changing any Windows security setting.

The G25 is from 2006. Windows 11 sees it as a generic controller: the steering
range is wrong and most games get no force feedback. The old Logitech driver that
fixed that no longer installs cleanly on Windows 11. This replaces it.

## What you get

- **Force feedback in games.** The wheel pushes back again.
- **Your own steering angle** - 180, 360, 540 or 900 degrees, from a small menu
  next to the clock. You can change it at any time, even mid-race.
- **Nothing to set up each time.** Plug the wheel in and it is configured for you.
- **Optional: games that refuse a G25.** Some titles and **GeForce NOW** only
  accept wheels from a fixed list. A separate component makes your G25 appear as
  a Logitech G29 so they accept it - see [Virtual G29](virtual-g29/README.md).

## What you need

- Windows 11, 64-bit
- A Logitech G25
- **The old Logitech software removed** - see the next section

That last point is the one people get wrong: if the old driver is still there,
the wheel will not be detected at all.

**Logitech G HUB** is a different thing and you can keep it - but it gets in the
way of the wheel and needs one extra step. See
[If you use Logitech G HUB](#if-you-use-logitech-g-hub) below.

## Step 1 - remove the old Logitech software

> **The old Logitech software for the G25 must be removed *completely*, driver
> included.** That is *Logitech Gaming Software*, *Logitech Profiler* or
> *WingMan* - anything Logitech from before G HUB, including the versions that
> ask you to turn off a Windows security check in order to install.
> **Uninstalling the app is not enough.**

Removing the app leaves the driver itself behind, tucked away inside Windows
where you will not see it. It stays attached to your wheel and keeps it in the
old format, so this driver never finds it. The steps below get rid of it.

(For the curious: the leftovers are Logitech's WingMan filter drivers - `WmHidLo`
on the USB stack, `WmFilter` on the HID stack - from `oem*.inf` packages such as
`WmJoyHid` / `WmVirHid` / `WmBEnum`.)

### Removing it - the beginner (GUI) way

1. **Settings -> Apps -> Installed apps** (or Control Panel -> Programs). Uninstall
   anything Logitech that is **not** G HUB: *Logitech Gaming Software*,
   *Logitech Profiler*, *Logitech WingMan*. **Reboot.**
2. Plug in the wheel. Open **Device Manager** (right-click Start -> Device Manager).
   Menu **View -> Show hidden devices**.
3. Under **Human Interface Devices** and **Sound, video and game controllers**,
   for every Logitech wheel entry (including greyed-out / hidden ones):
   right-click -> **Uninstall device** -> tick **"Delete the driver software for
   this device"** / "Attempt to remove the driver" -> OK.
4. **Unplug and replug** the wheel (or reboot). It should come back as a plain
   **HID-compliant game controller**, driver provider **Microsoft**.

Verify: in Device Manager the wheel's **Driver -> Driver Details** should list
Microsoft `hidusb.sys` / `hidclass.sys`, not any `Wm*` file. `g25tool info`
should report an **8/8/0** byte input/output/feature buffer.

### If the wheel keeps coming back on the old driver

The `oem*.inf` package is still in the Windows driver store. Remove it from an
**elevated** prompt, then replug:

```powershell
pnputil /enum-drivers                 # find the Logitech oem*.inf (provider Logitech, WingMan/WmXxx)
pnputil /delete-driver oemNN.inf /uninstall   # for each matching package
```

See [docs/validation.md](docs/validation.md) for a full before/after.

(This is separate from **G HUB**, which is fine to keep for other devices - see
[docs/ghub-coexistence.md](docs/ghub-coexistence.md).)
## If you use Logitech G HUB

> ⚠️ **G HUB stops this driver from finding the wheel.** It installs a driver of
> its own that claims the G25 the instant you plug it in, and closing or pausing
> G HUB does not help. The wheel then does nothing at all.
>
> If G HUB is only there for the wheel, uninstall it - nothing else to do.
>
> If you need G HUB for a mouse, keyboard or headset, keep it and remove just
> that one driver:
>
> 1. Right-click the **Start** button, choose **Terminal (Admin)** (on older
>    Windows: *Windows PowerShell (Admin)*), and accept the permission box.
> 2. Copy this line, paste it in, press Enter:
>
> ```powershell
> powershell -ExecutionPolicy Bypass -File "$env:LOCALAPPDATA\g25ff\scripts\Block-GHubWinUsb.ps1"
> ```
>
> A G HUB update puts the driver back, so you may have to do this again.
>
> Why, and what the script does: [docs/ghub-coexistence.md](docs/ghub-coexistence.md).

## Step 2 - install

1. Download `g25-w11-driver-<version>-setup.exe` from the
   [Releases page](https://github.com/hicwic/g25-w11-driver/releases) and run it.
   It installs for your account only and does **not** ask for administrator rights.
2. Plug in the wheel. A small **G25 Control** icon appears next to the clock -
   that is where you pick the steering angle.

**Close your games first**, so the driver file is not in use.

Want GeForce NOW, or a game that refuses the G25, to accept it? Also run
`g25-virtual-g29-<version>-setup.exe` from the same page. That one **does** need
administrator rights, because it installs a driver: Windows will pop up a
permission box when it starts. It does not switch off any Windows protection.
It is entirely optional - the wheel works in local games without it.

To uninstall: **Settings -> Apps -> Installed apps -> G25 Windows 11 Driver**.

## What gets installed

- `g25tray.exe` - the icon next to the clock. Switches the wheel to its native
  G25 mode, applies your steering range, and toggles the Virtual G29 bridge.
- `g25ff.dll` - the force feedback driver games talk to, built for both 64-bit
  and 32-bit games.
- `g25tool.exe` - a command-line tool for diagnostics (see [CLI Usage](#cli-usage)).
- Optionally, the [Virtual G29 bridge](virtual-g29/README.md): a virtual HID
  driver and an on-demand Windows service.

The core driver is **per user**: no kernel driver, no LGS, no WinUSB/Zadig, no
Secure Boot or Memory Integrity change, no driver-signing bypass. It adds COM and
OEM registry keys under your own account; the installer backs them up and
restores them when you uninstall. Open source, **C++20 / CMake**, GPL-2.0-only.

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

## Installer details

The core installer runs per user into `%LOCALAPPDATA%\g25ff`. It copies the
x64/x86 DirectInput DLLs, installs `g25tray.exe`, registers the DirectInput FFB
effects for `VID_046D&PID_C299`, starts G25 Control and adds it to sign-in
startup.

Uninstalling runs the same cleanup path as the script:

- stops `g25tray.exe`
- removes the startup entry
- unregisters the x64/x86 COM DirectInput effect driver
- restores previous per-user OEM/COM keys when a backup existed
- removes the installed binaries

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
