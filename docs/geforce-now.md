# GeForce NOW wheel support notes

This branch tracks the feasibility work for using a Logitech G25 with GeForce NOW.

## Current evidence

NVIDIA's public support article, updated 2026-05-15, says GeForce NOW supports a selected list of racing wheels with force feedback on the latest Windows native app. The Windows requirements are Windows 11 or newer and Logitech G HUB running in the background for Logitech wheels. The supported Logitech devices listed by NVIDIA are PRO Racing Wheel, RS50, G923, G920, G29, and the Driving Force Shifter. The Logitech G25 is not listed.

Local GeForce NOW 2026-08 binaries on the test machine contain strings for these Logitech USB IDs in `Geronimo.dll` and `nvc/localuser/GFN/GameStreamClientAgent.dll`:

- `046d:c24f` - Logitech G29 Driving Force Racing Wheel
- `046d:c262` - Logitech G920 Driving Force Racing Wheel
- `046d:c266` - Logitech G923 PlayStation/PC family
- `046d:c26e` - Logitech G923 Xbox/PC family
- plus related `046d:c268`, `046d:c272`, `046d:c276` strings

The same targeted search did not find `046d:c299` in those GeForce NOW streaming components. `046d:c299` is the Logitech G25 native USB product ID.

This points to GeForce NOW deciding wheel eligibility in the local client before the remote game sees input. The existing `g25ff.dll` DirectInput effect driver helps local games because they load it in their own process. It does not make the physical G25 look like another USB/HID device to the GeForce NOW client.

## Why the FH4 OEMData fix is not enough for GeForce NOW

The FH4 fix registers DirectInput/OEM metadata for the real G25 under the current user. That is enough for local DirectInput games that enumerate Windows game controllers and inspect the OEM joystick registry metadata.

GeForce NOW is different: the game runs on NVIDIA's remote machine. The local app must capture local input, serialize it through the streaming protocol, and recreate or inject it remotely. If the local app only forwards selected wheel VID/PID devices, registry metadata for `VID_046D&PID_C299` cannot make the physical USB HID device enumerate as `046d:c24f` or `046d:c262`.

## Feasible implementation paths

### Path A: Gamepad bridge

Read the real G25 locally and expose an Xbox-compatible virtual gamepad. GeForce NOW already supports normal gamepads. This can be done with an existing virtual gamepad bus such as ViGEm-compatible drivers.

Pros:

- Most likely to work with GeForce NOW without depending on private wheel protocol details.
- User-mode bridge can reuse this project's G25 HID input code.
- No need to patch or inject into GeForce NOW.

Cons:

- The remote game sees a gamepad, not a wheel.
- Steering is limited by thumbstick semantics.
- Force feedback can only be approximated from gamepad rumble, if GeForce NOW exposes rumble back to the virtual gamepad.
- Wheel rotation and detailed DirectInput FFB effects are lost.

### Path B: Virtual supported Logitech wheel

Expose a virtual HID/USB wheel that looks like a supported Logitech G29/G920/G923 to the GeForce NOW client, then bridge physical G25 inputs to that virtual device. If GeForce NOW sends FFB to the virtual wheel, translate those output reports back to the real G25.

Pros:

- This is the path that could make GeForce NOW treat the device as a real supported wheel.
- It can preserve wheel semantics and potentially force feedback.

Cons:

- Requires a signed virtual HID/bus driver on Windows. A normal user-mode DirectInput DLL cannot change a physical USB VID/PID.
- The virtual wheel HID descriptor and output report protocol must match a supported Logitech wheel closely enough for GeForce NOW and G HUB.
- Logitech G HUB may be part of NVIDIA's validation path, so a virtual device may also need to satisfy G HUB's expectations.
- Capturing and translating FFB from a virtual G29/G920/G923 to the G25 is a separate protocol mapping project.

### Path C: GeForce NOW client integration

Patch, hook, or extend the GeForce NOW client so it accepts `046d:c299` directly.

Pros:

- Could avoid writing a virtual HID wheel.

Cons:

- Fragile across GeForce NOW updates.
- Likely not appropriate for an open-source driver project.
- Could violate service/client integrity expectations.

## Proposed next experiment

1. Start GeForce NOW with the G25 connected in native mode and collect fresh `geronimo.log` / `CxNative_GeForceNOW.log` entries around HID enumeration.
2. Confirm whether the local client logs the G25 as `046d:c299` and ignores it, or never enumerates it through the wheel path.
3. If GeForce NOW ignores `046d:c299`, build a small local diagnostic that reports whether a device would match the current known supported VID/PID list.
4. Decide between:
   - a practical gamepad bridge prototype for immediate usability; or
   - a longer virtual-HID-wheel research branch for true wheel support.

## References

- NVIDIA support: "Does GeForce NOW support racing wheels and pedals?" updated 2026-05-15.
- Local GeForce NOW install inspected on 2026-09-07 under `%LOCALAPPDATA%\NVIDIA Corporation\GeForceNOW`.
- Device ID references: Logitech G29 `046d:c24f`, G920 `046d:c262`, G25 `046d:c299`.
