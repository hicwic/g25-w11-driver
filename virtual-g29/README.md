# G25 Virtual G29

`virtual-g29/` component of **g25-driver**. Makes a real Logitech G25 present
itself as a virtual Logitech G29, so software that only accepts a supported
wheel (GeForce NOW today) sees one. Ships as an **optional, opt-in** component
(it needs the HIDMaestro virtual HID driver and Administrator once); the core
driver stays pure per-user.

Why a bridge: the core driver fixes **local** games via a per-user DirectInput
effect driver. GeForce NOW is different - the streaming client filters local
devices by a wheel VID/PID allowlist before the remote game sees input, and the
G25 (`046D:C299`) is not on it. So a virtual supported wheel is needed.
Background and evidence: [../docs/geforce-now.md](../docs/geforce-now.md).
Packaging plan: [../docs/virtual-g29-plugin.md](../docs/virtual-g29-plugin.md).

> **Status: working prototype.** Steering and force feedback both confirmed in
> Wreckfest over GeForce NOW (2026-09-08), **with Logitech G HUB uninstalled -
> it is not required.** Needs the `logitech-g29-usbip` profile (now the
> default), the HIDMaestro driver, exactly one virtual G29, and the G25 in
> native mode. FFB is still a raw passthrough (feels right, not
> protocol-accurate). See `CHANGELOG.md` and `docs/`.

## How it works

```
Physical G25 (046D:C299)                     Virtual G29 (046D:C24F)
   HID input  ──► G25Source ──► bridge ──► HIDMaestro ──► GeForce NOW client
   HID output ◄── G25ForceFeedbackRelay ◄── OutputReceived ◄──┘
```

- `G25Source` reads the native G25 HID input report directly (wheel, 3 pedals,
  buttons, hat).
- HIDMaestro creates the virtual G29 from a profile (`logitech-g29-usbip` by
  default - the USB/IP backend that GeForce NOW recognises).
- `G25ForceFeedbackRelay` forwards output reports from the virtual G29 back to
  the G25. Raw passthrough - it works (real forces come through), but is not a
  protocol-accurate translation (`docs/ffb-protocol.md`). At startup it also
  sends `F3` / `F5` / SET_RANGE to the G25 (the bring-up G HUB used to do).

GeForce NOW talks to the virtual G29 through **DirectInput**; it has no
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
