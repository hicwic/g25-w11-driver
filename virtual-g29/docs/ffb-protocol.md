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

## Cross-check with `hid-lg4ff.c`

The Linux driver (`berarma/new-lg4ff`, rev `2092db1`) uses **one** command
format for G25 / G27 / G29 - it does **not** branch on model for FFB bytes
(only `set_range` and LED init differ). `lg4ff_update_slot`:

```c
cmd[0] = (0x10 << slot_id) | cmd_op;         // op 0x01 = download-and-play, 0x03 = stop
// constant force:
cmd[1] = 0x00;
cmd[2 + slot_id] = (clamp_s16(level) + 0x8000) >> 8;
// spring:  cmd[1] = 0x0b; ...
// damper:  cmd[1] = 0x0c; cmd[2]=coeff_l cmd[3]=sign_l cmd[4]=coeff_r cmd[5]=sign_r cmd[6]=clip
// autocenter on  = { 0x14, 0, ... }   off = { 0xf5, 0, ... }
// stop all       = { 0xf3, 0, ... }
```

So, comparing to the GFN capture:

| GFN sends | lg4ff form | verdict |
| --- | --- | --- |
| `F3`, `F5`, `13` | `0xf3`, `0xf5`, `0x13` | identical - passthrough |
| `14 00` | autocenter **on** | identical - passthrough (it *is* a command, not keepalive) |
| `21 0C 0N 00 0N 00 01` | damper, slot 1, `coeff/sign/coeff/sign/clip` | **already correct** - passthrough |
| `11 08 XX 80` | constant force wants `cmd[1]=0x00`, `cmd[3..]=0` | **only real deviation** |

## The translation (`g25_ffb_translate`, mode 1)

Given the above, `mode 1` does exactly one thing: normalise the constant-force
report.

| GFN | -> G25 |
| --- | --- |
| `X1 08 .. ` / `X1 00 ..` (download-and-play, constant) | `cmd[1]=0x00`, force byte kept at `cmd[2 + slot]`, rest zero |
| everything else | passthrough (already lg4ff form) |

Off by default (`bridge --ffb-translate`, or `ffbTranslate` in the service
config) until the A/B feel test confirms it is at least as good as passthrough.

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
