# How the GeForce NOW client handles wheels

Findings from `Geronimo.dll` / `GameStreamClientAgent.dll` strings, `geronimo.log`,
and a Process Monitor capture of `GeForceNOW.exe` during a session with the
virtual G29 (2026-09-08). Goal: understand whether G HUB is really required.

## The client path

`GeForceNOW.exe` (CEF app, loads `Geronimo.dll`) does the wheel work itself:

1. **Enumerates game controllers through DirectInput.** In the capture,
   `GeForceNOW.exe` reads
   `HKCU\...\MediaProperties\PrivateProperties\DirectInput\VID_046D&PID_C24F`
   **and** `...\VID_046D&PID_C299` - it sees both the virtual G29 and the
   physical G25.
2. **Filters by a supported VID/PID list** it calls "GSHID", from
   **`RIDevices.json`**. `geronimo.log`:
   ```
   GSHID: Supporting {046D:C24F}   (also C262 C266 C268 C26E C272 C276,
                                    044F:0402/0404/B67C/B687/B68D/B68F/B697,
                                    06A3:075C/0762, 0738:2221/A221)
   ```
   `046D:C299` (G25 native) is **not** in the list, so the G25 is enumerated
   but dropped.
3. **Needs a per-device definition** keyed `VID:PID:bcdDevice:interface`. Missing
   definition -> `No known device with interface number %d in %04X:%04X:%04X`.
   The real G29 `:8900` has one; the stale UMDF `:0100` did not.
4. For a matched device: `Plugging 046D:C24F:8900`, then
   `HIDDevice - Beginning HID reading loop`, and `RIDeviceMediator` decides
   forwarding (`shouldForwardHid`, `configureHid`, `handleHidChangedEvent`).
5. Input and FFB go over the stream via DirectInput - no direct `CreateFile` on
   `\\?\HID#...` by `GeForceNOW.exe` was seen; the DirectInput registry access
   is the tell.

## `RIDevices.json`

- **Network-delivered.** `GeForceNOW.exe` reads a fixed set of local JSON configs
  (`GeForceNOW.json`, `config.json`, `GEAR.json`, `overrides.json`,
  `storage.json`, `data/configs/*` ...). **None is `RIDevices.json`.** The CEF web
  layer fetches it and hands it to native ("Writing metadata key 'RIDevices.json'
  with value of size ..."). `overrides.json` and `storage.json` are empty /
  window geometry - not a device hook.
- **Bundled fallback** compiled into `Geronimo.dll`
  (`GSHID: added %zu vidPids from local RIDevices.json`).
- Exact URL / schema not captured yet (needs a TLS-intercepting proxy during a
  session, or unpacking the 50 MB CEF cache block).

## G HUB

- **No `lghub` / `ghub` / Logitech-software string, and no IPC to G HUB, in the
  GFN client binaries.** GFN does not talk to G HUB.
- G HUB's likely real job: **initialise the physical wheel** (mode switch,
  rotation range, autocenter, LEDs). The `13` / `F3` output reports at session
  start came from G HUB. `g25tray.exe` already does mode + range + autocenter for
  the G25.
- NVIDIA's "G HUB running" requirement may be partly a support/QA statement, or a
  check that lives server-side or in the CEF web app (not found in the native
  code).

## Consequences

Two ways to drop G HUB, both plausible:

### A. Virtual G29, no G HUB
GFN uses DirectInput, which works without G HUB. The bridge + HIDMaestro already
present a valid `:8900` G29. If the bridge sends the classic init (`F3`/`F5`)
itself at start, G HUB's bring-up is covered. **Never tested cleanly** - every
no-G HUB run so far used the wrong profile or had the stale `:0100` device.
Cheap to try.

### B. No virtual device at all - teach GFN the real G25
GFN already enumerates `046D:C299` via DirectInput and only drops it for not
being in `RIDevices.json`. If `046D:C299` + a G25 device definition can be added
(via the local/bundled RIDevices.json, or an override path if one exists), GFN
would forward the **real G25** directly. No HIDMaestro, no virtual device, no
G HUB - `g25tray` just keeps the wheel native. The G25 report layout and FFB
command format are already in `src/protocol/`. Blocker: whether the local
RIDevices.json can be edited / merged and whether it is signature-checked.

## Next probes

1. Clean no-G HUB test (path A) - fixed bridge, single `:8900` G29, usbip
   profile, G HUB uninstalled.
2. Capture the `RIDevices.json` request (Fiddler / mitmproxy with cert, or
   `pktmon` + TLS key log) during a session to get URL + schema.
3. Check whether `Geronimo.dll`'s bundled RIDevices.json can be located and
   whether GFN merges a local file over the downloaded one.
