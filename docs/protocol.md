# Logitech G25 Protocol Notes

Analysis started on 6 September 2026 and was completed with hardware tests on
7 September 2026. This document separates information taken from source code
from behavior observed on real hardware, which is recorded in
`docs/validation.md`.

## Sources And Reuse

The source revisions below are pinned so the command encoding remains
auditable:

- **new-lg4ff**, `2092db19f7b40854e0427a1b2e39eda9f8d0c3cd`:
  [hid-lg4ff.c][kernel], [hid-lg.c][hid], [hid-ids.h][ids]. This is a Linux
  HID module derived from `hid-logitech`: identification, mode switching, sysfs
  settings, Linux FFB translation, four hardware slots, hrtimer scheduling and
  effect mixing. Periodic effects are synthesized on the host and feed the
  constant-force slot. The source carries `SPDX-License-Identifier:
  GPL-2.0-or-later`, with copyrights by Simon Wood (2010) and Bernat Arlandis
  (2019).
- **lg4ff_userspace**, `d81ccb5d23716fa859b5f72d7e26d4ac8e6ba67d`:
  [configure.c][configure], [switch_mode.c][switch], [force_feedback.c][ff],
  [driver_loops.c][loops], [rd_g25][descriptor]. This is an incomplete C port
  using HIDAPI, pthreads, evdev events and `/dev/uinput`. Its license file is
  GNU GPL v2. Its README says the G25 mode was only tested on a G29 emulating a
  G25, which is not validation for every real G25 revision.

This prototype adapts the command encoding and native input layout in C++ with
attribution in the source files and in `THIRD_PARTY_NOTICES.md`, under
GPL-2.0-only. Linux-specific pieces such as uinput, evdev ioctls, sysfs and
hrtimmer scheduling are not embedded. `new-lg4ff` remains the main reference for
DirectInput translation, especially time synthesis and condition effects.

Upstream Linux references are
[drivers/hid/hid-lg4ff.c](https://github.com/torvalds/linux/blob/v6.12/drivers/hid/hid-lg4ff.c),
[hid-lg.c](https://github.com/torvalds/linux/blob/v6.12/drivers/hid/hid-lg.c)
and
[hid-ids.h](https://github.com/torvalds/linux/blob/v6.12/drivers/hid/hid-ids.h).
The links at the bottom point to the exact fork revisions that were read.

## USB Identity And Modes

Logitech VID is `046d`. In this project, PID means USB Product ID, not the HID
Physical Interface Device class used by standard HID FFB.

| PID | Reported Mode | Project Behavior |
| --- | --- | --- |
| `c294` | Driving Force / Formula EX, possible compatibility mode | Listed; switched only when the revision identifies a G25 |
| `c298` | Driving Force Pro, another possible G25 compatibility mode | Same caution |
| `c299` | Native G25 layout, also emulatable by G27/G29 | Native inputs; writes only for recognized G25 revisions |
| `c29b` | Native G27 | Diagnostic only; no G25 commands |

Sources: [USB constants][ids], `lg4ff_multimode_wheels`,
`lg4ff_main_checklist` and `lg4ff_identify_multimode_wheel` in
[hid-lg4ff.c][kernel].

A G25 is identified by `(bcdDevice & 0xff00) == 0x1200`, after excluding G27
`(bcdDevice & 0xfff0) == 0x1230`. Known G29 signatures include
`(rev & 0xfff8) == 0x1350` and `(rev & 0xff00) == 0x8900`. A compatibility PID
alone never proves that the device is a G25. Windows exposes the revision as
`HIDD_ATTRIBUTES.VersionNumber`.

Switching to native G25 mode sends `F8 10 00 00 00 00 00`
(`lg4ff_mode_switch_ext16_g25`). Switching to DFP mode uses `F8 01 ...`. The G25
does not support arbitrary back-and-forth mode changes; Linux refuses to switch
back to a lower PID after native mode. Unplugging/replugging may be required to
return to the startup mode. The `F8 09 ...` command family belongs to other
models such as G27/DFGT/G29 and is not sent to a real G25.

Mode switching can invalidate the handle and change PID/descriptor. `native`
sends the command and closes the handle; run `list` and `monitor` again after
USB re-enumeration. The CLI never silently switches modes from `list`, `info` or
`monitor`.

## Native HID Reports

The [rd_g25][descriptor] descriptor exposes a Joystick collection
(page `01`, usage `04`) without an explicit Report ID:

| Type | USB Payload | Windows HID Buffer |
| --- | --- | --- |
| Input | 11 bytes, 88 bits | 12 bytes, first byte `00` |
| Output | 7 vendor bytes | 8 bytes, first byte `00` |
| Feature | 144 vendor bytes | 145 bytes, first byte `00` |

Sizes are checked with `HidP_GetCaps` before decoding or sending. Usages and
logical ranges are checked too. Unknown descriptors must be analyzed instead of
being interpreted with approximate offsets. The Windows parser
`HidP_GetUsageValue/GetUsages` is exercised on local single-bit buffers to
verify that offsets match the decoder; those buffers are never sent to the
wheel. Feature reports are not read or written because their meaning is not
established by the studied sources.

Compatibility modes expose other output usages: `01:03` for Driving Force and
`01:02` for DFP in `df_rdesc_fixed/dfp_rdesc_fixed` from [hid-lg.c][hid]. The
output filter treats those modes separately, always with seven bytes and Report
ID 0.

Offsets below are in the **USB payload**, without the leading Windows `00`. Bits
are numbered from the least significant bit of the first byte:

| Bits | Field | Range / Meaning |
| --- | --- | --- |
| 0-3 | POV | 0-7 directions, 8 idle, other values reserved |
| 4-22 | Buttons | 19 bits, page `09`, usages 1-19 |
| 23-25 | Vendor | Kept as raw bits |
| 26-39 | Wheel X | 14 bits, 0-16383; `(p[3] >> 2) | (p[4] << 6)` |
| 40-47 | Throttle Z | `p[5]`, 255 released, 0 pressed |
| 48-55 | Brake Rz | `p[6]`, same convention |
| 56-63 | Clutch Y | `p[7]`, same convention |
| 64-87 | Vendor | `p[8..10]`, kept raw |

The layout comes from `uinput_g25_g27_emit` in [driver_loops.c][loops]. Pedals
are separate in the native report; combined pedals are a software transform.
`monitor` prints an estimated angle from the range passed through `--range`
(900 by default). The studied protocol does not provide a verified readback for
the current steering range.

**G27 native (`046D:C29B`, revision `0x123x`).** Wheel, pedals and POV are byte
-identical to the table above. The button field is wider: bits 4-25 are buttons
1-22 (page `09`), there is no 3-bit vendor field before the wheel, and bit 80
(`p[10]` bit 0) is button 23 (usage `0x17`). Native-mode switch is
`f8 09 04 01 00 00 00` (vs the G25's `f8 10`); FFB, range and the compat-mode
descriptors are the same. Bytes from new-lg4ff `lg4ff_mode_switch_ext09_g27`
and lg4ff_userspace `rd_g27`.

### Shifter

[Logitech documents][gears] gears 1-6 and R as DirectX buttons 8-14 using
zero-based numbering, which appear as displayed buttons 9-15. The CLI exposes
that mapping as **indicative** and also prints all buttons, POV and vendor bytes.
Several active gear bits produce `?`; none produce `N`. This XP/Vista Profiler
mapping must be compared with raw HID input without LGS. Linux passes buttons
and does not name gears.

## Commands Sent

Rows in this table are the **seven Logitech payload bytes**. The Windows
transport prepends `00`, sends with `WriteFile`, and checks the transferred byte
count. There is no automatic fallback to WinUSB, `SetFeature` or another
command.

| Function | Payload | Exact Source In [hid-lg4ff.c][kernel] |
| --- | --- | --- |
| G25 mode | `F8 10 00 00 00 00 00` | `lg4ff_mode_switch_ext16_g25`, `lg4ff_get_mode_switch_command` |
| Range | `F8 81 LL HH 00 00 00` | `lg4ff_set_range_g25` |
| 900 degrees | `F8 81 84 03 00 00 00` | same function; 900 = `0384` |
| 540 degrees | `F8 81 1C 02 00 00 00` | same function; 540 = `021C` |
| Stop four slots | `F3 00 00 00 00 00 00` | `lg4ff_stop_effects` |
| Disable autocenter | `F5 00 00 00 00 00 00` | `lg4ff_set_autocenter_default`, magnitude 0 |
| Constant slot 0 | `11 00 XX 00 00 00 00` | `lg4ff_update_slot`, `FF_CONSTANT` branch |
| Spring slot 1 | `21 0B D1 D2 KK SS CL` | same function, `FF_SPRING` branch |
| Damper slot 2 | `41 0C K1 S1 K2 S2 CL` | same function, `FF_DAMPER` branch |
| Friction slot 3 | `81 0E K1 S1 K2 S2 CL` | same function, `FF_FRICTION` branch |
| Stop slots 0/1/2/3 | `13` / `23` / `43` / `83`, then zeros | stop operation in `lg4ff_update_slot` |

Accepted range is 40-900 degrees inclusive (`lg4ff_devices`). The prototype
rejects out-of-range values instead of silently clamping them. A successful
write confirms Windows transfer, not firmware acknowledgement or a measured
stop position.

### Force Feedback

Byte 0 combines the slot mask in the high nibble and the operation in the low
nibble: `1` downloads/plays, `C` updates, `3` stops. The four hardware slot
masks are `10`, `20`, `40`, `80`. `new-lg4ff` supports more software effects
than hardware slots by mixing constants and periodic effects, then assigning
condition effects to available slots.

Constant force uses `XX = (signed_16_bit_force + 32768) >> 8`. `80` is neutral;
`00` and `FF` are near extremes, not stop commands.

Spring converts dead-zone start/end positions from signed 16-bit space to
11-bit space. Damper and Inertia share the damper slot; the strongest active
effect wins. Friction uses its own slot. Ramp, Square, Sine, Triangle, Sawtooth
Up, Sawtooth Down and Custom are synthesized on the host every 4 ms and mixed
into the constant-force slot.

Manufacturer autocenter uses `FE 0D A A B 00 00`, followed by `14 00 ...`, with
non-linear A/B calculation in `lg4ff_set_autocenter_default`. The project
documents that path but keeps autocenter disabled after setup and after bounded
tests so the wheel is not left applying force.

The 12 standard DirectInput effect GUIDs are registered and advertised. The CLI
`directinput-test` offers a bounded one-second test for each one.

### Initialization And Stop

1. Enumerate without changing drivers; check VID/PID/revision/descriptor.
2. If needed, send `native`, then enumerate again after the PID changes.
3. Read inputs without FFB initialization for the first milestone.
4. For an output session, prepare the stop guard before the first transfer, send
   stop + disable autocenter, then send the requested command.
5. For an effect, use a one-second CLI maximum, interruptible wait, explicit
   stop, then RAII fallback stop.
6. On session exit/error, try stop and autocenter-disable independently, even if
   one write fails.

`Ctrl+C` wakes the wait immediately. Overlapped writes have a guard timeout; a
cancelled write is completed before freeing its buffer. Real stop latency
depends on Windows and USB. The sources do not prove a hardware watchdog:
process crash, host failure or unplugged USB can prevent a stop command from
reaching the wheel. Keep the wheel clear and power reachable during early tests.
CTest never sends motor commands.

[kernel]: https://github.com/berarma/new-lg4ff/blob/2092db19f7b40854e0427a1b2e39eda9f8d0c3cd/hid-lg4ff.c
[hid]: https://github.com/berarma/new-lg4ff/blob/2092db19f7b40854e0427a1b2e39eda9f8d0c3cd/hid-lg.c
[ids]: https://github.com/berarma/new-lg4ff/blob/2092db19f7b40854e0427a1b2e39eda9f8d0c3cd/hid-ids.h
[configure]: https://github.com/Kethen/lg4ff_userspace/blob/d81ccb5d23716fa859b5f72d7e26d4ac8e6ba67d/configure.c
[switch]: https://github.com/Kethen/lg4ff_userspace/blob/d81ccb5d23716fa859b5f72d7e26d4ac8e6ba67d/switch_mode.c
[ff]: https://github.com/Kethen/lg4ff_userspace/blob/d81ccb5d23716fa859b5f72d7e26d4ac8e6ba67d/force_feedback.c
[loops]: https://github.com/Kethen/lg4ff_userspace/blob/d81ccb5d23716fa859b5f72d7e26d4ac8e6ba67d/driver_loops.c
[descriptor]: https://github.com/Kethen/lg4ff_userspace/blob/d81ccb5d23716fa859b5f72d7e26d4ac8e6ba67d/rd_g25
[gears]: https://support.logi.com/hc/en-us/articles/360023207554-Programming-the-G25-Shifter-positions-with-Logitech-Profiler
