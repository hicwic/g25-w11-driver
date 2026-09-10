# Coexisting with Logitech G HUB

GeForce NOW requires G HUB running for Logitech wheels, so anyone using the
`g25-virtual-g29` will have G HUB installed. G HUB and this driver both want
the physical G25, and out of the box G HUB wins.

## The conflict

The G25 boots in **compatibility mode** (`USB\VID_046D&PID_C294`, "Driving Force
EX") and is switched to **native mode** (`USB\VID_046D&PID_C299`) by
`g25tool native` / `g25tray.exe`.

G HUB installs **`logi_win_usb.inf`** (driver class `LGHUBWinUSB`,
`{9164FBF8-1925-4B46-B3A8-206547B3A587}`). Its device list:

```
USB\VID_046D&PID_C261   G920 (GIP)
USB\VID_046D&PID_C260   G29 (PS4)
USB\VID_046D&PID_C294   G29 (PS3)   <-- also matches the G25 in compat mode
USB\VID_046D&PID_C26D   G923 (GIP)
USB\VID_B58E&PID_0008   Yeti X bootloader
... more Yeti mic bootloaders
```

It does **not** list `VID_046D&PID_C299` (G25 native).

So: on every plug / reboot / USB re-enumeration the G25 appears as `C294` for a
moment, and Windows binds `logi_win_usb.inf` (a device-specific INF outranks the
generic USB composite driver). The wheel becomes a WinUSB device named
"Logitech G29 Driving Force Racing Wheel (PS3)", its HID interface disappears,
and `g25tool` / `g25ff.dll` / `g25tray` can no longer talk to it - they need the
HID interface, and they can't even send the native-mode switch to get out.

Pausing G HUB does not help: the INF is in the driver store and Windows binds it
on its own.

## The fix

Remove `logi_win_usb.inf` from the driver store:

```powershell
# elevated (Terminal as administrator)
powershell -ExecutionPolicy Bypass -File "$env:LOCALAPPDATA\g25ff\scripts\Block-GHubWinUsb.ps1"
```

It stops G HUB, `pnputil /delete-driver`s the `logi_win_usb.inf` package,
re-enumerates the wheel (now a normal HID device), runs `g25tool native`, and
restarts G HUB.

This is safe here: the INF only covers G920 / G29-PS4 / G29-PS3 / G923 and Yeti
mic bootloaders, none of which is a real device on a G25 setup. G HUB keeps
working for the **virtual** G29 the bridge creates - that path uses
`logi_joy_hid.inf`, not WinUSB.

Once the G25 is native (`C299`), G HUB ignores it.

## Making it stick

A G HUB update reinstalls `logi_win_usb.inf`. Options, roughly in order of
effort:

1. **Manual:** re-run `Block-GHubWinUsb.ps1` after a G HUB update. Fine for a
   single test machine.
2. **Scheduled task** (installer sets it up, runs elevated): on logon and on a
   G HUB install/update, delete `logi_win_usb.inf` if present and re-switch the
   wheel to native.
3. **Tray, extended:** `g25tray.exe` detects the G25 sitting on `C294` with the
   `WINUSB` service and triggers the elevated cleanup (needs an elevated helper;
   the tray itself is per-user).

Option 2 is the intended long-term answer and ties into the optional-component
installer described in `virtual-g29-plugin.md`.

## Note for the bridge

`g25-virtual-g29` reads the physical G25 from its native HID interface
(`C299`). If G HUB has WinUSB-claimed the wheel, the bridge's `G25Source` fails
with `Win32Exception 1167`. The bridge now retries, but the real fix is to keep
`logi_win_usb.inf` off the machine.
