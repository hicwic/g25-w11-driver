# G25 Virtual G29

`virtual-g29/` component of **g25-driver**. Makes a real Logitech G25 present
itself as a virtual Logitech G29, so software that only accepts a supported
wheel (GeForce NOW today) sees one. Ships as an **optional, opt-in** component
(it needs the HIDMaestro virtual HID driver and Administrator once); the core
driver stays pure per-user.

Why a bridge: **GeForce NOW** filters local devices by a hard-coded wheel
VID/PID allowlist before the remote game sees input, and the G25 (`046D:C299`)
is not on it - so a virtual supported wheel is needed. Some **local** titles
also gate features by wheel model (Forza Horizon 4 only accepts a known wheel;
some sims tune FFB per model). Background: [../docs/geforce-now.md](../docs/geforce-now.md).
Packaging plan: [../docs/virtual-g29-plugin.md](../docs/virtual-g29-plugin.md).

> **Status: working.** Steering + force feedback confirmed over GeForce NOW
> (Wreckfest, BeamNG) and in **local** games (Wreckfest, Forza Horizon 4), with
> Logitech G HUB uninstalled. Needs the `logitech-g29-usbip` profile (default),
> the HIDMaestro driver, exactly one virtual G29, the G25 in native mode, and -
> for local FFB - the core g25-driver installed. FFB translation for conditions
> beyond constant force is not yet protocol-accurate (`docs/ffb-protocol.md`).

## How it works

```
Physical G25 (046D:C299)                          Virtual G29 (046D:C24F)
   HID input  ──► G25Source ──► bridge ──► HIDMaestro ──► GeForce NOW client
                                                     └──► local game (DirectInput)
   HID output ◄── G25ForceFeedbackRelay ◄── OutputReceived ◄──┘  (both sources)
```

- `G25Source` reads the native G25 HID input report directly (wheel, 3 pedals,
  buttons, hat).
- HIDMaestro creates the virtual G29 from a profile (`logitech-g29-usbip` by
  default - the USB/IP backend that GeForce NOW recognises).
- **FFB in** comes from either the GeForce NOW client (raw G29 HID reports) or,
  for a local game, `g25ff.dll` (the core driver's DirectInput effect driver):
  when a game creates effects on the virtual G29, `g25ff` writes lg4ff reports
  to it. Both land on `OutputReceived`.
- `G25ForceFeedbackRelay` forwards those to the physical G25. At startup it also
  sends `F3` / `F5` / SET_RANGE (the bring-up G HUB used to do); the range is
  the tray's "Maximum rotation" setting.

The G25 stays hidden from local games (HidHide), so they bind the virtual G29
and its FFB flows back through the relay - no whitelisting of game executables.

Local FFB needs the core g25-driver installed. The vg29 installer registers the
per-user OEM / force-feedback metadata for `046D:C24F` (`Register-G29FF.ps1`,
`OEMName` = `Logitech G29 Driving Force Racing Wheel USB`, `OEMData` =
`43 00 08 10 19 00 00 00`) pointing `OEMForceFeedback` at the g25ff class.

GeForce NOW and local games talk to the virtual G29 through **DirectInput**; no
dependency on Logitech G HUB (confirmed with G HUB fully uninstalled).

## Performance (measured)

`g25-virtual-g29.exe latency` reads both HID input streams on their own threads,
timestamps every report with `QueryPerformanceCounter`, and - while you stab a
pedal / flick the wheel - matches the mid-point crossing of the step on each
side to get the added latency. Numbers below are from one machine (Windows 11
26200, 2026-09-08), G29 mode on.

**Report rate**

| | rate | inter-report gap |
| --- | --- | --- |
| physical G25 (`046D:C299`) | ~220 Hz idle → ~360 Hz under load | min 2.0 ms (500 Hz endpoint), median 2–4 ms |
| virtual G29 (`046D:C24F`) | ~245 → ~400 Hz | median 2–4 ms |

The observed G25 rate tracks the Windows timer resolution (2 ms endpoint, but
~4 ms effective on a quiet desktop, ~2 ms once a game / the GFN client raises it
to 1 ms). The virtual G29 is always **at or above** the G25 rate - the bridge
never throttles the wheel.

**Added latency** — physical G25 HID report in → same change observable on the
virtual G29:

| | value |
| --- | --- |
| median | **~1 ms** |
| p90 | ~11 ms |
| shape | ~85–90 % under 3 ms; a thin tail to ~15 ms under CPU load; effectively nothing past ~20 ms |

The ~1 ms median is dominated by endpoint phase (half of a 2 ms slot); the tail
is scheduler contention. This is **on top of** the wheel's own sensor→USB delay
(~1–3 ms, fixed, not measured here) and **before** anything the game or GeForce
NOW adds (client sampling + network + remote frame - tens of ms).

The event-driven submit loop matters here: the earlier fixed-`Sleep` poll ran at
~64 Hz effective and added a ~8 ms median. See `CHANGELOG.md`.

## Requirements

- Windows 11.
- Administrator for the `bridge` and `cleanup` commands (HIDMaestro installs a
  UMDF2 virtual HID driver with a self-signed test certificate).
- .NET 10 SDK. The scripts use a local SDK at
  `..\_tools\dotnet10-sdk` when present.
- HIDMaestro v1.7.3 runtime binaries are vendored under
  `third_party/HIDMaestro/v1.7.3` (MIT, see `LICENSE-HIDMaestro.txt`).
- **Logitech G HUB is *not* required.** If it is installed for other devices,
  see `../docs/ghub-coexistence.md` - it will otherwise WinUSB-claim
  the G25.

## Commands

```powershell
.\scripts\Build.ps1

# Read the G25 only, create nothing
.\scripts\Run-DryRun.ps1 -Args '--duration 10'

# Create the virtual G29 and bridge into it (elevates)
.\scripts\Run-Bridge-Admin.ps1 -BridgeArgs '--install-driver'

# Remove HIDMaestro virtual devices (elevates)
.\scripts\Cleanup-Admin.ps1

# Report rate + G25->G29 latency (needs G29 mode / the bridge running)
g25-virtual-g29.exe latency --seconds 20
```

`g25-virtual-g29.exe help` lists all options. Useful ones:
`--trace-output` (print output reports), `--no-ffb`, `--no-buttons`,
`--no-hat`, `--invert-accelerator|brake|clutch`, `--profile <id>`,
`--profiles-dir <path>`, `--rate-hz <n>`, `--duration <seconds>`,
`--keep-existing` (skip the stale-virtual-G29 purge).

## First test

1. Make sure the G25 is in native mode (`046D:C299`) - `g25tool list` from
   g25-driver, or its tray. Close GeForce NOW for now.
2. `Run-DryRun.ps1`, move the wheel and pedals, check the printed axes move the
   right way. Add `--invert-*` for any reversed pedal.
3. `Run-Bridge-Admin.ps1 --install-driver`, accept UAC. It uses the
   `logitech-g29-usbip` profile, purges any stale virtual G29, and sends the
   wheel bring-up commands.
4. `joy.cpl` should list **exactly one** `G29 Driving Force Racing Wheel` and its
   axes should move with the physical wheel.
5. Launch GeForce NOW and a racing game. Test steering, then force feedback.
6. `Cleanup-Admin.ps1` when done.

### A pedal reads nothing, then jumps to full when floored (once per pedal)

Expected on the first use after the wheel is (re)plugged. The **G25 firmware**
auto-calibrates each pedal's travel and only knows the endpoints once the pedal
has been to its mechanical stop; G HUB / Profiler used to pre-load this.
**Press the accelerator, brake and clutch fully to the floor once** after
enabling G29 mode and they track normally for the rest of the session.
`g25-virtual-g29.exe dry-run` shows the raw axes if you want to confirm it is
the wheel and not calibration.

### The physical G25 was unplugged while G29 mode was on

The bridge holds the virtual G29 for ~15 s in case it is a USB glitch
(tray shows `G29 mode - G25 disconnected`), then stops the service and removes
the virtual wheel. Re-plug the G25 and toggle G29 mode back on.

### A local game sees the virtual G29 but there is no force feedback

Local FFB routes through `g25ff.dll` from the **core g25-driver** - install it
first. Then check the per-user registration:
`HKCU\...\Joystick\OEM\VID_046D&PID_C24F\OEMForceFeedback\CLSID` should be
`{D7A3D8CB-8B3C-4C35-A552-73A4BEB529E0}`. If a game overwrote the OEM entry
(`OEMName` back to `G29 Driving Force Racing Wheel`, `OEMData` starting `03`),
re-run `scripts\Register-G29FF.ps1 -Action Install` and restart the game.

### If the wheel is detected in-game but does nothing

Almost always a second, stale virtual G29. GeForce NOW's `geronimo.log` shows
`No known device with interface number 0 in 046D:C24F:0100`. Stop the bridge,
run `Cleanup-Admin.ps1` (or `g25-virtual-g29.exe cleanup`), confirm
`joy.cpl` shows no G29, restart the bridge, restart the GeForce NOW client.

## Repository layout

| Path | Purpose |
| --- | --- |
| `src/G25VirtualG29/` | the bridge CLI (.NET 10) |
| `profiles/` | extra HIDMaestro profiles (`logitech-g29-usbip.json`) |
| `scripts/` | build and elevated run/cleanup helpers |
| `third_party/HIDMaestro/` | vendored HIDMaestro runtime |
| `docs/ffb-protocol.md` | force-feedback state and the translation work needed |
| `docs/plugin-integration.md` | plan to ship this as an optional g25-driver component |
| `docs/test-history.md` | manual test log (artifacts are not tracked) |
| `docs/hidmaestro.md` | why the G29 profile / HIDMaestro |
| `artifacts/` | local test outputs, git-ignored |

## Safety

AI-assisted experimental project. It creates and can install virtual HID devices
when the elevated scripts are run. Keep testing scoped, close the bridge after
use, run `Cleanup-Admin.ps1` if anything looks wrong.

## License

Not decided yet. Until a `LICENSE` file is added, treat this as "all rights
reserved" for redistribution purposes. Vendored HIDMaestro binaries keep their
own MIT license (`third_party/HIDMaestro/v1.7.3/LICENSE-HIDMaestro.txt`).
