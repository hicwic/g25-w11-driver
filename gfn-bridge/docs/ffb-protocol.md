# Force feedback: GeForce NOW -> G25 translation

## What GeForce NOW sends to a virtual G29

Captured with `--trace-output`, `logitech-g29-usbip` profile, **no G HUB**
(so this is GFN + the game only). 1755 output reports over one Wreckfest
session (`tools/ghub-uninstall/bridge-trace-no-ghub-working.txt`):

| First 2 bytes | Count | Payload shape | Reading |
| --- | --- | --- | --- |
| `11 08` | 1577 | `11 08 XX 80 00 00 00` | **Constant force.** `0x11` = slot 1, download-and-play. `XX` = level, 0x1B..0xE5 (centre 0x80, so roughly +/-100). `0x80` in byte 3 = fixed. |
| `21 0C` | 29 | `21 0C 0N 00 0N 00 01` | **Condition effect** on slot 2. `0N` symmetric, 0x07..0x0C. Trailing `0x01`. |
| `23 0C` | 2 | `23 0C 0C 00 0C 00 01` | slot 2 stop + condition params (effect swap?). |
| `FE 0D` | 65 | all zero | GFN wheel keepalive / RPM-LED refresh. |
| `14 00` | 60 | all zero | slot 1, opcode 4 - "refresh" / default. |
| `13 00` | 16 | all zero | slot 1, opcode 3 = **stop slot 1**. |
| `F5 00` | 5 | all zero | all slots, opcode 5 = default spring / autocentre. |
| `F3 00` | 1 | all zero | all slots, opcode 3 = **stop all forces**. |

The `F3` / `F5` bytes are exactly what `g25::stop_all()` / `disable_autocenter()`
produce, so those pass through unchanged. `13` (stop slot 1) is also valid G25.

## What the G25 expects (`src/protocol/force_feedback.cpp`)

- Constant force: `{0x11, 0x00, level, 0, 0, 0, 0}` - byte 1 is `0x00`;
  `level = (force + 32768) >> 8` (0x80 = centre).
- Spring: `{0x21, 0x0b, d1>>3, d2>>3, (coeff4(k2)<<4)|coeff4(k1),
  ((d2&7)<<5)|((d1&7)<<1)|signs, clip>>8}` on slot 2.
- Damper: `{0x41, 0x0c, coeff4(kl), kl<0, coeff4(kr), kr<0, clip>>8}` on slot 3.
- Stop slot n: `{(1u << (n+4)) | 0x03, 0, ...}` (slot 0 = 0x13, slot 1 = 0x23...).

## Why passthrough already "feels right"

GFN's format is Logitech-classic-shaped: same slot bytes (`0x11` / `0x21`),
same `F3` / `F5`. Constant force dominates (90% of reports) and the G25 clearly
reacts to `11 08 XX 80` as sent. The likely errors are subtle:

1. **Constant force type byte.** GFN sends `0x08` where the G25 driver uses
   `0x00`. If `0x08` selects a different level scaling on the G25 the magnitude
   is slightly off; if the G25 ignores unknown types some forces are dropped.
   Hypothesis: `11 08 XX 80` -> `11 00 XX 00 00 00 00` (keep the level byte).
2. **Condition effects.** `21 0C` on slot 2 with symmetric small coefficients
   and no deadband reads as a **damper**, but the G25's damper is `0x41 0x0c`
   on slot 3. Passthrough leaves it as `21 0C` (slot 2, wrong type) - probably
   why centring / understeer feel is off. Hypothesis: decode `0N` as a
   coefficient (`k ~= 0N << 11`) and emit `g25::damper(k, k, clip)`.
3. **Direction / sign.** Not verified either way.

## The translation (`g25_ffb_translate`, mode 1)

`libg25` gains a translating path, off by default (`bridge --ffb-translate`):

| GFN | -> G25 |
| --- | --- |
| `11 08 XX ..` | `constant_force`-shaped: `11 00 XX 00 00 00 00` |
| `21 0C 0N 00 0N 00 ..` | `damper(N<<11, N<<11, 0xFFFF)` -> `41 0C ...` |
| `23 0C ..` | `stop_force_slot(2)` -> `43 00 ...` |
| `13`, `14`, `FE 0D` | passthrough (init / keepalive) |
| `F3`, `F5` | passthrough (identical) |

These mappings are **hypotheses**. They need the A/B test below before becoming
the default.

## A/B capture plan (hardware)

1. **GFN side.** In a streamed game with a deterministic FFB effect (a
   wheel-alignment screen; or force a constant pull by steering into a wall),
   capture `bridge --trace-output` output.
2. **Local reference.** For the same DirectInput effect, drive the real G25
   through `g25ff.dll` (any local FFB game or `g25tool directinput-test
   constant` / `spring` / `damper`) and capture its HID output
   (`g25tool monitor --raw` on the output endpoint, or add a trace to
   `output_session`).
3. **Diff** the two byte streams for constant / spring / damper.
4. Tune `g25_ffb_translate`, then feel-test: bridge-driven G25 vs
   `g25ff`-driven G25 for the same effect should feel the same.

Reference: `drivers/hid/hid-lg4ff.c` `lg4ff_update_slot` (per-model classic
command format).
