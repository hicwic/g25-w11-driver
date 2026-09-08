# Manual test history

Reconstructed from `artifacts/` (which is not tracked in git). Kept here so the
knowledge survives. Machine-local dates.

## 2026-09-07

- HIDMaestro generic `emulate` self-test was tried first and rejected: it sends
  an automatic input pattern and caused confusing desktop behaviour.
- With the constrained `bridge` app: GeForce NOW + Wreckfest detect the
  HIDMaestro virtual Logitech G29 (`046D:C24F`). The physical G25 only drives it
  while the bridge process runs.
- Targeted string search of local GeForce NOW binaries: `046d:c24f` (G29),
  `c262` (G920), `c266`/`c26e` (G923) present; `046d:c299` (G25) absent. See
  `../../docs/geforce-now.md`.

## 2026-09-08 - USB/IP topology profile iterations

The `logitech-g29-usbip` profile carries a full USB configuration descriptor so
HIDMaestro's USB/IP backend presents a device topology close to a real G29.
`inputDefaults` bytes 9/10/11 were tuned across runs.

| Artifact | Profile bytes 9/10/11 | Output reports seen | Notes |
| --- | --- | --- | --- |
| `usbip-test` | - | - | exit code -1, empty stderr; environment repair scripts present |
| `usbip-test-v2` | - | - | USB read trace only (`HIDMAESTRO_DIAG_READS`) |
| `usbip-test-v3` | 128 / 128 / 148 | yes (`0x13 0xF3 0xFE0D 0x14 0xF5`, zero payloads) | `--trace-output`, safety defaults (axes only) |
| `usbip-test-v4` | (as v3) | yes, plus moving wheel/pedal axes | last run with output-report traffic |
| `usbip-test-v5` | - | none | input axes static in capture |
| `usbip-test-v6` | - | none | input axes static in capture |
| `usbip-test-v7` | 129 / 128 / 156 | none | input axes static in capture |

### Open items

- Root-cause the loss of output reports after v4.
- Confirm whether v5-v7 static input is a real regression or just an untouched
  wheel during those captures.
- Re-run v4-style capture with a deliberate in-game force to get a non-zero
  payload (see `ffb-protocol.md`).
