# GeForce NOW bridge - packaging plan

Status: planning. Branch `feature/geforce-now-wheel-support`.

Background on *why* a bridge is needed: [geforce-now.md](geforce-now.md). How the
GFN client actually works: [gfn-client-internals.md](gfn-client-internals.md).

## Decisions (2026-09-08)

- **Monorepo.** The bridge moves into `g25-driver/gfn-bridge/`. The standalone
  `g25-gfn-wheel-bridge` repo is archived. One CI pipeline, one version number,
  tray and bridge always compatible. Everything is **GPL-2.0-only** (the bridge
  had no LICENSE, so no history conflict).
- **Optional component, not default.** The core driver stays pure per-user, no
  admin. The GFN bridge is a separate opt-in payload with its own elevation.
- **Elevation via an on-demand Windows service.** The tray runs per-user and
  cannot elevate silently. A service (`g25gfnbridge`, `start=demand`) does the
  privileged HIDMaestro work; its SDDL grants the interactive user
  START/STOP/QUERY so the unprivileged tray can drive it via the SCM. One UAC,
  at component-install time only.
- **No G HUB.** Confirmed unnecessary (see geforce-now.md). Prerequisites are
  just the HIDMaestro driver + the G25 in native mode (which `g25tray` already
  ensures).

## Target architecture

```mermaid
flowchart LR
    subgraph User session
        T[g25tray.exe]
        DI[Local DirectInput games] --> FF[g25ff.dll]
    end
    subgraph Service (LocalSystem, on-demand)
        S[g25gfnbridge service] --> B[bridge worker]
    end
    G[Physical G25 046D:C299] <--> H[Microsoft HID]
    FF --> H
    B <--> H
    B <--> HM[HIDMaestro virtual G29 046D:C24F]
    HM <--> GFN[GeForce NOW client] <--> R[Remote game]
    T -- SCM start/stop/query --> S
    S -- status pipe --> T
    T --> H
    L[libg25.dll] -.-> FF
    L -.-> T
    L -.-> B
```

Shared writer mutex `Local\g25ff-hid-writer` (rename TBD) serialises HID output
between `g25tray`, `g25ff.dll` and the bridge worker.

## Phased plan

### Phase 0 - monorepo move
- Copy the bridge tree into `gfn-bridge/` (`src/`, `profiles/`, `scripts/`,
  `third_party/HIDMaestro/`, `docs/`, `CHANGELOG.md`, `README.md`, `.sln`).
  Not `artifacts/` (git-ignored test output).
- Add SPDX `GPL-2.0-only` headers to the `.cs` files; drop the "no LICENSE" note.
- Fix relative paths: local SDK `..\..\_tools\dotnet10-sdk` -> `..\..\..\_tools`;
  doc cross-links `../g25-driver/docs/x` -> `../../docs/x`.
- Root `.gitignore`: add `gfn-bridge/**/bin/`, `obj/`, `gfn-bridge/artifacts/`.
- Archive `g25-gfn-wheel-bridge` on GitHub; leave a README pointer.

### Phase 1 - `libg25` (done)
- `g25_protocol` already is the shared static core (`src/protocol/*`), linked by
  `g25tool` / `g25ff` / `g25tray`. No change there.
- New `src/libg25/` -> `libg25.dll` (`G25_BUILD_LIBG25`, C ABI in `libg25.h`):
  `g25_identify_model`, `g25_decode_input`, `g25_cmd_native_mode` /
  `_set_range` / `_stop_all` / `_disable_autocenter`, `g25_ffb_translate`,
  `g25_libg25_version`.
- `g25_ffb_translate` is Phase 1 passthrough (matches the shipping bridge);
  protocol-accurate translation with `force_feedback.h` encoders is Phase 2+.
- `tests/libg25_tests.cpp` - golden vectors shared with `protocol_tests`, runs
  on the ubuntu `portable` CI job.

### Phase 2 - bridge on `libg25`
- `gfn-bridge/src/.../Libg25.cs`: P/Invoke wrapper over `libg25.dll`.
- `G25Source` decode -> `libg25`. `G25ForceFeedbackRelay` translation ->
  `libg25` (`SendWheelInit` too).
- Bridge takes the shared writer mutex.
- Ship `libg25.dll` (x64) with the bridge payload.

### Phase 3 - `g25gfnbridge` service
- `gfn-bridge/service/` - thin Windows service that supervises the bridge worker
  (restart on crash / USB re-enum), logs to the Event Log + a rolling file.
- Control: SCM start/stop for enable/disable; a status named pipe
  (`\\.\pipe\g25gfnbridge`) emitting newline JSON (`state`, `virtual`, `ffb`,
  `inputHz`, `lastError`).
- Install (admin, once): `sc create g25gfnbridge start= demand`, `sc sdset` to
  grant `RP` (start) `WP` (stop) `LC` (query) to `IU` (interactive users),
  register + install the HIDMaestro driver.
- The worker can stay the current CLI exe run by the service, or be folded into
  the service process. Start with the service spawning the exe (least churn).

### Phase 4 - tray integration
- Probe on menu open: `gfn-bridge/` payload present AND `g25gfnbridge` service
  registered.
- Not present -> single item "Install GeForce NOW bridge..." opening the docs /
  release page. Present -> submenu "Virtual G29":
  - status line
  - "GeForce NOW mode" checkbox (StartService / ControlService STOP)
  - "Start automatically with GeForce NOW"
  - (Phase 7) "Local games mode (hides the G25)" checkbox
- The tray already keeps the G25 native - reuse that; just don't fight the
  bridge for the writer mutex.

### Phase 7 - local G29 emulation (follow-up, after 1-6 ship)
The virtual G29 is a normal DirectInput/HID FFB device, so it also works for
**local** games - useful for sims that gate FFB profiles by wheel model (ACC,
F1, sometimes iRacing) or that accept a G29 but not a G25.

The catch is only local: with both the physical G25 and the virtual G29 present,
local DirectInput games see two wheels. (GFN does not - it filters the G25 out,
`046D:C299` is not in `RIDevices.json`.)

Hiding the G25 from local games while the bridge still reads it needs a HID
filter driver. Use **HidHide** (the ViGEm-ecosystem tool for exactly this
pattern): install its driver, configure it to cloak `046D:C299` and allowlist
the bridge worker process. `pnputil /disable-device` does not work - it would
kill the bridge's handle too.

Not in the initial scope because: it is unproven (does ACC/F1 actually behave
better as a G29?) and HidHide is its own dependency + install. Ship GFN mode
first; add this as a second checkbox once validated.

### Phase 5 - installers
- Core installer unchanged (`installer/g25-w11-driver.iss`, per-user).
- New `installer/g25-gfn-bridge.iss` - **elevated** (`PrivilegesRequired=admin`):
  drops `gfn-bridge/` payload + `libg25.dll`, installs the HIDMaestro driver,
  creates + configures the service, its own uninstall (`sc delete`, remove
  driver, `bridge cleanup`).
- The core installer / tray offers "Install GeForce NOW bridge" which launches
  `g25-gfn-bridge-<ver>-setup.exe`.
- If G HUB is detected, the bridge installer warns and offers to run
  `block-ghub-winusb.ps1` ([ghub-coexistence.md](ghub-coexistence.md)).

### Phase 6 - CI/CD
- `release.yml`: after the C++ build, `actions/setup-dotnet@v4` (10.x),
  `dotnet publish -c Release -r win-x64` the bridge (self-contained), bundle
  HIDMaestro + profiles + `libg25.dll`, run `ISCC` on the second `.iss`, attach
  `g25-gfn-bridge-<ver>-setup.exe` to the release.
- `ci.yml` / `dev.yml`: add `dotnet build` + `dotnet test` for the bridge.
- One `G25_VERSION` drives both installers.
- Unsigned for now - note SmartScreen; a signing cert is out of scope.

## Open questions

1. **FFB translation fidelity.** Passthrough feels right; a real table
   (`g25_ffb_translate`) is Phase 1/2 work. `docs/ffb-protocol.md` in the bridge.
2. **.NET 10 on CI runners.** windows-2022 needs `setup-dotnet` with an explicit
   10.x SDK (still new). Confirm availability or pin a version.
3. **Service worker: separate exe vs in-process.** Start with separate exe
   (current bridge CLI), revisit if the pipe/supervision gets awkward.
4. **G27 / other wheels.** Profile system + `libg25` model identification already
   generalise; a later milestone.
