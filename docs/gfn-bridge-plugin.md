# GeForce NOW bridge - packaging plan

Status: **all phases done and validated** (2026-09-08). Branch
`feature/geforce-now-wheel-support`. Installed end to end from the real
`g25-gfn-bridge-<ver>-setup.exe` (Dev Build artifact): HIDMaestro driver +
`g25gfnbridge` service + tray toggle + steering & FFB in GeForce NOW.

Left: archive the old standalone repo, drop `tools/gfn-g25-bridge-poc/`, open a
PR to `main`, cut a `v*` release, and Phase 7 (local G29 emulation + HidHide).

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

### Phase 2 - bridge on `libg25` (done)
- `gfn-bridge/src/.../Libg25.cs`: `LibraryImport` over `libg25.dll`.
- `G25Source.ReadLoop` decodes via `g25_decode_input`.
- `G25ForceFeedbackRelay`: `SendWheelInit` uses `g25_cmd_*`, `Enqueue` uses
  `g25_ffb_translate`. Hand-rolled byte literals removed.
- Bridge takes `Local\g25tool-output-v1` (5 s timeout, then best-effort).
- `libg25.dll` copied next to the exe via the `Libg25Dll` MSBuild property;
  `Build` errors if it is missing.
- Verified: `dry-run` decodes the real G25 through the DLL.

### Phase 3 - `g25gfnbridge` service (done, needs an elevated test)
- `gfn-bridge/src/G25GfnBridgeService/` - .NET Worker Service (`g25gfnbridge.exe`,
  LocalSystem, `start=demand`).
  - `install` / `uninstall` - `sc.exe` wrappers; `sdset` grants the interactive
    user group (`IU`) START/STOP/QUERY so the unprivileged tray drives it.
  - `run` - the SCM entry point; `BridgeWorker` spawns and supervises
    `g25-gfn-wheel-bridge.exe bridge --stop-event <name> ...`, restarts it with
    backoff (3/10/30 s) on crash, stops it cleanly on service stop, and turns
    its stdout into `BridgeStatus`.
  - `StatusServer` serves `\\.\pipe\g25gfnbridge` (newline JSON: `state`,
    `virtualDevice`, `ffbActive`, `wheel`, `restarts`, `lastError`), readable by
    any authenticated user. `status` verb prints one line.
  - config: `%ProgramData%\g25gfnbridge\config.json` (profile, range, inverts).
- Bridge: `--stop-event <name>` opens a named event and triggers the same clean
  shutdown as Ctrl+C; stop signalling refactored to a `CancellationTokenSource`.
- HIDMaestro driver install still happens via the bridge's `--install-driver`
  (config flag on first run); the installer can also do it up front.

### Phase 4 - tray integration (done, GUI click still to try)
- `src/tray/gfn_bridge.{h,cpp}` (`g25_gfn_bridge` lib): `presence()` (is the
  `g25gfnbridge` service registered?), `status()` (SCM state + a read of
  `\\.\pipe\g25gfnbridge`), `start()` / `stop()` via the SCM.
- `main.cpp`: when the service is present, `show_menu` adds a **GeForce NOW
  bridge** submenu - status line + a checked/unchecked **GeForce NOW mode** item
  that toggles the service. Absent -> no item.
- Verified from an **unprivileged** `gfn_bridge_probe`: `start()` / `stop()`
  succeed (the `IU` SDDL grant works), `status()` reads the pipe, the worker and
  virtual G29 are gone after stop.
- Still TODO: "Install GeForce NOW bridge..." entry when the payload is present
  but the service is not; "start automatically with GeForce NOW"; Phase 7
  "local games" checkbox.

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

### Phase 5 - installers (done, ISCC / real install still to run)
- Core installer unchanged (`installer/g25-w11-driver.iss`, per-user).
  `Package-Release.ps1` copies named files only, so `libg25.dll` does not leak
  into the core package.
- New `installer/g25-gfn-bridge.iss` - **elevated** (`PrivilegesRequired=admin`),
  own AppId, installs to `{autopf}\G25 GeForce NOW Bridge`:
  - `[Files]` the `dotnet publish` payload (`G25_GFN_PAYLOAD_DIR`).
  - `[Run]` `g25-gfn-wheel-bridge.exe install-driver` then `g25gfnbridge.exe
    install`.
  - `[UninstallRun]` `g25gfnbridge.exe uninstall` + `g25-gfn-wheel-bridge.exe
    cleanup`.
  - `[Code]` advises about G HUB if `{commonpf}\LGHUB` exists.
- New bridge verb `install-driver` (install/refresh HIDMaestro, then exit).
- The tray shows no GFN item until the service is registered (user's choice) -
  discovery is via the release page / docs, not an in-tray installer link.

### Phase 6 - CI/CD (done, needs a run)
- `release.yml`: `actions/setup-dotnet@v4` (10.x); after the C++ build, publish
  the bridge + service self-contained into `dist/gfn-bridge` (with
  `-p:Libg25Dll=dist/stage-x64/bin/libg25.dll`), `ISCC` both `.iss` files,
  attach `g25-gfn-bridge-<ver>-setup.exe` to the release.
- `ci.yml`: x64 job also `dotnet build`s the bridge + service against the
  freshly built `libg25.dll`.
- One `G25_VERSION` drives both installers.
- Unsigned - SmartScreen prompts; a signing cert is out of scope.

## Open questions

1. **FFB translation fidelity.** Passthrough feels right; a real table
   (`g25_ffb_translate`) is Phase 1/2 work. `docs/ffb-protocol.md` in the bridge.
2. **.NET 10 on CI runners.** windows-2022 needs `setup-dotnet` with an explicit
   10.x SDK (still new). Confirm availability or pin a version.
3. **Service worker: separate exe vs in-process.** Start with separate exe
   (current bridge CLI), revisit if the pipe/supervision gets awkward.
4. **G27 / other wheels.** Profile system + `libg25` model identification already
   generalise; a later milestone.
