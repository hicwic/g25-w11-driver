# HIDMaestro notes

HIDMaestro is MIT-licensed and currently used as a prototype dependency
(vendored under `third_party/HIDMaestro/v1.7.3`).

## Why a G29 (`046D:C24F`)

- NVIDIA lists the Logitech G29 as supported by GeForce NOW on Windows;
- GeForce NOW's `geronimo.log` prints its supported-wheel list and `046D:C24F`
  is in it;
- a virtual G29 is detected by Wreckfest inside GeForce NOW, and forwards input
  + FFB - confirmed with G HUB uninstalled, so no G HUB dependency.

## Why the `logitech-g29-usbip` profile, not `logitech-g29`

HIDMaestro has two backends:

- **UMDF** (`logitech-g29`, the built-in profile): a `ROOT\HIDCLASS` virtual HID
  device. It enumerates as `046D:C24F:0100` (bcdDevice `0x0100`). GeForce NOW
  keys wheels on `VID:PID:bcdDevice` and does **not** recognise `:0100` -
  `geronimo.log` spams `No known device with interface number 0 in
  046D:C24F:0100` and wheel input never works.
- **USB/IP** (`logitech-g29-usbip`, bundled in `profiles/`): a full USB
  device/configuration descriptor with `bcdDevice 0x8900`. GeForce NOW
  enumerates it as `046D:C24F:8900`, matches it, and forwards input + FFB.

`logitech-g29-usbip` is the default. Never run both backends at once: a leftover
`:0100` device from a UMDF run will jam the `:8900` one. `bridge` and `cleanup`
purge stale virtual G29 nodes for this reason.

## Force feedback

Works via `G25ForceFeedbackRelay`'s raw passthrough - the game's forces (`1108
XX 80` constant force, `210C..` conditions) reach the G25 and feel correct. A
protocol-accurate translation is still worth doing; see `ffb-protocol.md`.

## Not G HUB

GeForce NOW talks to the virtual G29 through DirectInput. With G HUB installed,
its `logi_joy_hid` filter attaches to the device and sends `13` / `F3` / `FE0D`
bring-up reports - these are not needed and disappear when G HUB is removed. The
bridge does its own G25 bring-up (`SendWheelInit`). G HUB is not a dependency.
