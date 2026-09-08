# Bridge-side integration notes

The packaging plan lives in [../../docs/virtual-g29-plugin.md](../../docs/virtual-g29-plugin.md).
This file only records the bridge's side of the contract.

## What the service / tray expects from the bridge worker

- Stable CLI: `bridge [options]` (long-running), `cleanup`, `inspect`,
  `dry-run`, `probe-virtual`.
- Exit codes: `0` clean stop (Ctrl+C / SIGTERM), non-zero with a single-line
  reason on stderr.
- Default profile `logitech-g29-usbip`; purges stale virtual G29 nodes on start
  (`--keep-existing` opts out).
- Sends `F3` / `F5` / SET_RANGE to the G25 at start (`SendWheelInit`,
  `--wheel-range`, default 900).
- Takes the shared HID writer mutex (Phase 2) so it does not fight `g25tray` /
  `g25ff.dll` on the G25 output endpoint.
- To add (Phase 3): a status named pipe `\\.\pipe\g25vg29` emitting
  newline-delimited JSON.

## Prerequisites (confirmed 2026-09-08, G HUB uninstalled)

HIDMaestro's virtual HID driver + the physical G25 in native mode. **No G HUB.**
See [../../docs/geforce-now.md](../../docs/geforce-now.md) and
[../../docs/gfn-client-internals.md](../../docs/gfn-client-internals.md).

## Shared protocol library

`G25Source` decode and `G25ForceFeedbackRelay` command bytes duplicate
`src/protocol/` in the driver. Phase 1 extracts that into `libg25` + a C-ABI
`libg25.dll`; Phase 2 has the bridge P/Invoke it instead of hand-maintaining the
byte layout. Until then, keep `G25Source.cs` in sync with `../../docs/protocol.md`
by hand and note drift in `../CHANGELOG.md`.
