# Changelog

All notable changes to this prototype. Dates are the local test-machine dates.

The format is loosely based on Keep a Changelog. There is no released version yet;
everything below `Unreleased` is prototype iteration.

## [Unreleased]

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
