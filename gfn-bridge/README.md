# G25 GeForce NOW Wheel Bridge

`gfn-bridge/` component of **g25-driver**. Makes a real Logitech G25 drive a
virtual Logitech G29 so GeForce NOW treats it as a supported wheel. Ships as an
**optional, opt-in** component (it needs the HIDMaestro virtual HID driver and
Administrator once); the core driver stays pure per-user.

Why a bridge: the core driver fixes **local** games via a per-user DirectInput
effect driver. GeForce NOW is different - the streaming client filters local
devices by a wheel VID/PID allowlist before the remote game sees input, and the
G25 (`046D:C299`) is not on it. So a virtual supported wheel is needed.
Background and evidence: [../docs/geforce-now.md](../docs/geforce-now.md).
Packaging plan: [../docs/gfn-bridge-plugin.md](../docs/gfn-bridge-plugin.md).

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
```

`g25-gfn-wheel-bridge.exe help` lists all options. Useful ones:
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
run `Cleanup-Admin.ps1` (or `g25-gfn-wheel-bridge.exe cleanup`), confirm
`joy.cpl` shows no G29, restart the bridge, restart the GeForce NOW client.

## Repository layout

| Path | Purpose |
| --- | --- |
| `src/G25GfnWheelBridge/` | the bridge CLI (.NET 10) |
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
