# Force feedback: current state and the translation work

## What happens today - and it works

`G25ForceFeedbackRelay` (`src/G25GfnWheelBridge/G25ForceFeedbackRelay.cs`) is a
near-raw passthrough:

1. The virtual G29 raises `OutputReceived` for each host output report.
2. The first 7 bytes are copied into an 8-byte G25 output report (leading `0x00`
   report id) and written to the physical G25 HID output endpoint.
3. On shutdown the relay sends `F3` (stop all forces) and `F5` (disable
   autocenter).

Confirmed 2026-09-08 in Wreckfest over GeForce NOW: the forces come through and
feel correct. So the passthrough is good enough to play with. It is still not a
protocol-accurate translation.

## Observed output reports

With `--trace-output`, `logitech-g29-usbip` profile:

| Bytes | Source | Meaning |
| --- | --- | --- |
| `13 00..`, `F3 00..`, `FE 0D ..` / `14 00..` | **G HUB** `logi_joy_hid` filter | device bring-up / keepalive. **Gone once G HUB is uninstalled** - not needed. |
| `11 08 XX 80 00..` | **game** | constant force, `XX` = magnitude (198 distinct values in a no-G HUB drive test) |
| `21 0C 0X 00 0X 00 01 ..` | **game** | condition effect (spring / damper), slot index `0X` |

The `1108`/`210C` reports are the real road/collision forces and are all that
flows with G HUB removed. `1108 XX 80` reads as: command `0x11` (slot 1, play),
effect type `0x08`, level `XX`, offset `0x80`.

## Why passthrough is still not enough

- The G29 in native mode uses an **extended** command set; the G25/G27 use the
  older **classic** set. `F3` / `F5` overlap, but constant-force level encoding,
  spring/damper coefficients, autocenter and range commands differ between
  generations. The passthrough happens to land close enough for constant force
  to feel right; conditions and directions are not verified.
- No effect-slot bookkeeping, magnitude scaling, clamping or rate limiting.
- `XX` around `0x80`: is `0x80` really centre for the G25's classic constant
  force, or is the G25 expecting a signed 8-bit level? Needs checking against
  `lg4ff`.

## Plan

1. **Capture a known force.** In a streamed session run a wheel FFB test
   (constant force left/right, then a spring) and capture the exact reports with
   `--trace-output`. Drive the physical G25 with the g25-driver DirectInput path
   for the same effects and diff.
2. **Build a real translation table** mapping virtual-G29 reports (`11xx`,
   `21xx`, `Fx`) to G25 classic commands: constant force, spring, damper,
   autocenter, stop.
3. **Reference:** `drivers/hid/hid-lg4ff.c` (Linux kernel) - per-model command
   differences (Driving Force, DFP, DFGT, G25, G27, G29). The g25-driver
   `docs/protocol.md` and `g25ff.dll` encoders cover the G25 side.
4. **Share the encoder.** The G25 command encoding should come from the shared
   protocol library in `docs/plugin-integration.md`, not be re-implemented here.

## Open questions

- The game reports are classic-format and G HUB is not involved, so it is
  HIDMaestro's G29 profile / DirectInput that presents a classic FFB interface.
- Is `0x80` the correct centre for the G25 classic constant-force level?
- Condition-effect (`21 0C ..`) fidelity - untested beyond "feels right".
