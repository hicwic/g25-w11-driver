# Changelog

All notable changes to this prototype. Dates are the local test-machine dates.

The format is loosely based on Keep a Changelog. There is no released version yet;
everything below `Unreleased` is prototype iteration.

## [Unreleased]

### Fixed - the tray disturbing the wheel while a game is running (2026-09-09)

Opening the tray menu (or changing "Maximum rotation") while a game held the
wheel could send `SET_RANGE` + `STOP_ALL` + `DISABLE_AUTOCENTER` mid-session,
which yanked the wheel off centre; and it showed an alarming "G25 busy - setup
pending" retry loop.

- The tray now checks the shared output mutex first (`WriterLock::available()`).
  If a game's `g25ff.dll` or the bridge holds the wheel it does **not** write -
  status reads "G25 in use - <deg> applies when the game exits" and it re-checks
  every 4 s. The new rotation is applied the moment the game closes.
- G29 mode: changing the rotation no longer **bounces the g25vg29 service**
  (that dropped the virtual G29 mid-game). The new range applies the next time
  G29 mode starts; the tray shows "restart it for <deg>" until then
  (`BridgeStatus.wheelRange`, from the worker's startup line).

## [0.2.1] - 2026-09-08

Local-game force feedback via the virtual G29, Forza Horizon 4 support, the
tray wheel-range wired into G29 mode, and clean handling of the G25 being
unplugged mid-session.

### Added - force feedback in local games (2026-09-08)

The virtual G29 now works as a real FFB wheel for **local** DirectInput games,
not just GeForce NOW - confirmed in Wreckfest and Forza Horizon 4, with the G25
still hidden by HidHide.

- `g25ff.dll` (core driver) gains a virtual-G29 output path: when a game creates
  effects on `046D:C24F`, it writes lg4ff reports to that device instead of the
  hidden G25. The bridge relay carries them to the G25 - same route as the
  GeForce NOW client. lg4ff is one command format for G25/G27/G29, so no
  translation is added.
- `Register-G29FF.ps1` registers the per-user OEM / `OEMForceFeedback` metadata
  for `046D:C24F` (`OEMName` = `Logitech G29 Driving Force Racing Wheel USB`,
  `OEMData` = `43 00 08 10 19 00 00 00`) pointing at the g25ff class. Fixes
  Forza Horizon 4 not seeing the wheel (a game had written a broken `03...`
  entry). The installer runs it as the original user; no-op without the core
  driver.
- Local FFB depends on the core g25-driver being installed.

### Fixed - tray "Maximum rotation" ignored in G29 mode (2026-09-08)

The bridge hard-coded `--wheel-range 900`. The service (SYSTEM) now reads the
tray's `Rotation` setting from `HKEY_USERS\<sid>\Software\g25-driver` and passes
it; the tray bounces the bridge when the range changes while G29 mode is on.

### Fixed - physical G25 unplugged while G29 mode is on (2026-09-08)

The bridge held the virtual G29 for 30 s then restart-looped forever, and the
tray kept showing "G29 mode active". Now: the worker prints `WHEEL: lost` /
`WHEEL: gone`, the service shows `wheel-lost` for ~15 s (tray: "G29 mode - G25
disconnected"), then stops the service cleanly (virtual wheel removed, HidHide
reverted, SCM Stopped, tray toggle clears). Also caps start failures at 4.
The tray re-reads status when its menu opens.

### Note - first-use pedal calibration

A G25 pedal can read nothing until floored once (per power-cycle) - that is the
wheel firmware, not the bridge. The bridge prints a one-line hint at startup;
see README.

## [0.2.0] - 2026-09-08

First release with the Virtual G29 bridge as an optional component of
**g25-driver**: `g25vg29` on-demand service, tray toggle, bundled HidHide,
event-driven submit loop, and the `latency` diagnostic.

### Added - `latency` command (2026-09-08)

`g25-virtual-g29.exe latency [--seconds N]` measures the input pipeline:
report rate + inter-report gap on each side, and the G25->G29 added latency
(mid-point-crossing match on a pedal stab / wheel flick, with a steering
cross-correlation check). Prints median / p90 / p99 and a distribution
histogram. Measured on one machine: G25 ~360 Hz, G29 ~400 Hz, added latency
median ~1 ms, p90 ~11 ms. See README "Performance (measured)".

### Changed - event-driven submit loop (2026-09-08)

`PumpG25` now blocks on a new decoded G25 frame instead of polling a cached
value on a fixed `Thread.Sleep`. The virtual G29 is fed at the wheel's own
report rate (~250-400 Hz measured, tracks the Windows timer resolution) instead
of ~64 Hz (`Sleep(4)` was rounding up to the 15.6 ms Windows timer tick), which
also cut the bridge's own added latency from a ~8 ms median to ~1 ms. No
`timeBeginPeriod`, so no system-wide timer pressure. `--rate-hz` now bounds only
the idle resubmit rate. Telemetry line gains `inHz=`.

### Added - hide the physical G25 from local games (2026-09-08)

The bridge worker now drives **HidHide** (nefarius/HidHide, MIT) while it runs:
the physical G25 (`046D:C299`) is hidden from every process except the worker,
so local DirectInput games bind the virtual G29 instead of seeing two wheels.
GeForce NOW is unaffected either way.

- `HidHide.cs`: shell `HidHideCLI.exe`, hide the G25 nodes, whitelist the
  worker, `--cloak-on`; revert precisely on stop. Session-state file survives a
  hard kill; `hidhide-revert` / `cleanup` / the next start clean it up.
- Off with `hideLocalG25: false` (config) or `--no-hide-g25`. Absent HidHide =>
  one log line, bridge continues.
- Installer bundles the HidHide setup and runs it silently (`/qn /norestart`).
- Tray shows `G29 mode active (G25 hidden)` instead of `G25 not connected`.

### Working (2026-09-08)

End to end confirmed in Wreckfest over GeForce NOW: **steering and force
feedback both work.** Confirmed with **Logitech G HUB fully uninstalled** -
G HUB is not required.

Recipe: HIDMaestro driver installed, profile `logitech-g29-usbip`, exactly one
virtual G29, physical G25 in native mode. The bridge sends `F3` / `F5` /
SET_RANGE to the G25 at startup (what G HUB used to do).

- Input: G25 native HID report (`046D:C299`) -> bridge -> virtual G29
  (`046D:C24F:8900`) -> GeForce NOW (via DirectInput) -> remote game.
- FFB: the game's forces arrive as `1108 XX 80` constant-force reports (`XX`
  sweeping the full range - 198 distinct magnitudes in a no-G HUB drive test)
  plus `210C..` condition effects. `G25ForceFeedbackRelay`'s near-raw passthrough
  delivers them to the G25 well enough to feel correct. Protocol-accurate
  translation is still worth doing (`docs/ffb-protocol.md`).

Root cause of the earlier "detected but no input": GeForce NOW keys wheels on
`VID:PID:bcdDevice`. Test runs with the default `logitech-g29` profile
(HIDMaestro's UMDF backend) left a stale virtual G29 at `ROOT\HIDCLASS`
enumerating as `046D:C24F:0100`; GFN could not match it
(`No known device with interface number 0`) and it jammed the wheel input path.

### Added
- **`g25vg29` service** (`src/G25VirtualG29Service/`): on-demand Windows
  service (LocalSystem) that supervises the bridge worker - starts it, restarts
  with backoff on crash, stops it cleanly, and exposes status on
  `\\.\pipe\g25vg29`. `install` / `uninstall` / `status` / `run` verbs.
  The unprivileged tray drives it through the SCM.
- Bridge `--stop-event <name>`: a supervising service signals a named event for
  a clean shutdown (removes the virtual G29, stops G25 forces) instead of
  Ctrl+C. Stop signalling is now a `CancellationTokenSource`.
- **`Libg25.cs`**: P/Invoke over `libg25.dll` (driver `src/libg25/`). `G25Source`
  decodes the input report and `G25ForceFeedbackRelay` builds the wheel commands
  and translates FFB through it, instead of hand-maintaining the byte layout.
  `libg25.dll` is copied next to the exe (`Libg25Dll` MSBuild property; build
  errors if missing).
- The bridge takes the shared HID writer mutex `Local\g25tool-output-v1` (same
  one g25tool / g25ff.dll / g25tray use) so it does not fight them on the G25
  output endpoint.
- Split `Program.cs` into `Program.cs`, `BridgeOptions.cs`, `G25Source.cs`,
  `G25ForceFeedbackRelay.cs`, `PnpSnapshot.cs`.
- Default profile is now `logitech-g29-usbip` (was `logitech-g29`).
- `bridge` purges stale virtual G29 nodes - including the `:0100` UMDF one -
  before creating its own; `cleanup` does the same. `--keep-existing` opts out.
- `G25ForceFeedbackRelay.SendWheelInit`: sends `F3` (stop) + `F5` (autocenter
  off) + SET_RANGE to the G25 at startup, replacing G HUB's device bring-up.
  `--wheel-range <deg>` (default 900, 0 to skip).
- `probe-virtual` command: reads the raw HID input report of the virtual G29
  from a second process.
- `G25Source` / `G25ForceFeedbackRelay` reconnect (30 s window) after a USB
  re-enumeration instead of exiting the bridge.
- `.editorconfig`, this `CHANGELOG.md`, `docs/ffb-protocol.md`,
  `docs/plugin-integration.md`, `docs/test-history.md`.
- `profiles/logitech-g29-usbip.json` is now tracked.

### Confirmed / resolved
- **G HUB is not required** - full steering + FFB with G HUB uninstalled and
  rebooted. GeForce NOW handles the wheel via DirectInput; no G HUB hooks.
- The `13` / `F3` / `FE0D` / `14` output reports seen earlier came from G HUB's
  `logi_joy_hid` filter driver, not from the game; gone once G HUB was removed.

### Still open
- Protocol-accurate FFB translation instead of passthrough.
- The USB/IP backend re-enumerates the USB tree on start, briefly disturbing the
  G25 handle (survived by the reconnect logic).
- If G HUB is present for other devices, `logi_win_usb.inf` still grabs the G25
  (`../docs/ghub-coexistence.md`).

## [0.0.1] - 2026-09-07 - Initial prototype (commit a0465ac)

### Added
- `bridge` / `dry-run` / `inspect` / `cleanup` commands.
- HIDMaestro `logitech-g29` virtual wheel creation, admin-gated.
- G25 input read through SharpDX DirectInput (later replaced by a direct HID
  reader).
- Axes-only forwarding by default, buttons and hat opt-in, per-pedal invert
  flags, `Ctrl+C` disposes the virtual controller.

### Post-commit iteration (uncommitted, reconstructed from `artifacts/`)
- Input path moved from DirectInput to a direct HID reader (`HidSharp`).
- `G25ForceFeedbackRelay` added (passthrough).
- Options added: `--profiles-dir`, `--trace-output`, `--no-ffb`; buttons/hat
  flipped to on-by-default (`--no-buttons` / `--no-hat` to disable).
- `logitech-g29-usbip` profile added: full USB configuration descriptor for a
  USB/IP backend topology experiment; `inputDefaults` bytes tuned across v3..v7.
