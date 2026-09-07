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

## Existing signed or installable virtual HID options

This section tracks option 1 from the investigation: reuse an existing virtual input driver instead of writing and signing a new kernel driver.

### vJoy

vJoy is a signed virtual joystick driver. It can expose a generic joystick with configurable axes, buttons, and POV hats, and a feeder application can write input state into it.

For this project it is probably not enough for GeForce NOW wheel mode. The device identity remains a generic vJoy device rather than a Logitech G29/G920/G923 VID/PID, and GeForce NOW appears to allowlist specific supported wheel IDs locally. vJoy can still be useful for local-game compatibility experiments or as a reference for DirectInput/HID PID force-feedback behavior.

### libvirtualhid

libvirtualhid is a C++ virtual HID library with a Windows UMDF2 control driver backed by Microsoft's Virtual HID Framework. Its public documentation says compatible applications can create virtual HID gamepads, keyboards, and mice, with custom descriptors and HID output callbacks.

It is interesting architecturally because VHF is the right Windows mechanism for virtual HID devices, and it avoids a custom kernel transport minidriver. The current public product surface, however, is described around gamepads/keyboards/mice rather than racing wheels with HID PID force feedback. Treat it as a possible building block only if a small prototype proves that a custom wheel descriptor plus output reports can be created and seen by DirectInput/GeForce NOW.

### HIDMaestro

HIDMaestro is the strongest current candidate. Its repository includes Logitech wheel profiles for:

- `logitech-g29` - VID/PID `046D:C24F`
- `logitech-g920` - VID/PID `046D:C262`
- `logitech-g923-ps` - VID/PID `046D:C266`
- `logitech-g923-xbox` - VID/PID `046D:C26E`

Those IDs match the Logitech wheels currently listed by NVIDIA for GeForce NOW, and the same IDs were found in the local GeForce NOW binaries. HIDMaestro also claims HID PID 1.0 DirectInput force-feedback support: the virtual driver accepts FFB output reports and raises them to the consumer application, which is exactly the bridge shape needed here.

Important caveat: HIDMaestro is not a preinstalled Microsoft WHQL wheel driver. It installs a UMDF2 virtual HID driver and uses a locally trusted self-signed certificate, requiring administrator privileges. That is much lighter than writing and signing our own kernel driver, but it is still a system driver install and must be tested carefully.

### Preferred prototype

The first true-wheel prototype should use HIDMaestro with the `logitech-g29` profile:

1. Real G25 stays hidden from games/GeForce NOW if needed.
2. A small bridge app reads the real G25 input through this project's existing HID/DirectInput code.
3. The bridge creates a virtual `046D:C24F` Logitech G29 through HIDMaestro.
4. The bridge maps G25 wheel/buttons/pedals/shifter to the virtual G29 state.
5. If GeForce NOW writes FFB to the virtual G29, the bridge receives HID PID output packets and translates them to the real G25.

The first pass can ignore force feedback and only prove detection plus steering/pedal input in GeForce NOW. If GeForce NOW detects the virtual G29, then FFB routing becomes the next milestone.

## Additional references for virtual HID research

- vJoy: https://sourceforge.net/projects/vjoystick/
- Microsoft Virtual HID Framework: https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/virtual-hid-framework--vhf-
- libvirtualhid Windows driver package: https://docs.lizardbyte.dev/projects/libvirtualhid/latest/md_docs_2windows-driver.html
- HIDMaestro: https://github.com/hifihedgehog/HIDMaestro
- HIDMaestro public site: https://hidmaestro.org/

## References

- NVIDIA support: "Does GeForce NOW support racing wheels and pedals?" updated 2026-05-15.
- Local GeForce NOW install inspected on 2026-09-07 under `%LOCALAPPDATA%\NVIDIA Corporation\GeForceNOW`.
- Device ID references: Logitech G29 `046d:c24f`, G920 `046d:c262`, G25 `046d:c299`.
