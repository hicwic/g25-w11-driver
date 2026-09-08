# G HUB dependency test

Goal: find out whether the GeForce NOW bridge needs Logitech G HUB, or only
HIDMaestro. NVIDIA's docs say Logitech wheels need "G HUB running in the
background"; we want to know if that applies to the virtual G29 the bridge
creates. Context: `../../docs/geforce-now.md`.

## Files

| File | What |
| --- | --- |
| `capture-footprint.ps1` | snapshot Logitech/G HUB/HIDMaestro drivers, services, devices, folders |
| `baseline-before.txt` | snapshot taken 2026-09-08 with G HUB `2026.5.939708` installed |
| `uninstall-ghub.ps1` | elevated: stop G HUB, run its uninstaller, remove residuals, snapshot after |
| `baseline-after.txt` | written by `uninstall-ghub.ps1` |
| `uninstall-ghub.transcript.txt` | transcript of the uninstall run |

## Run

```powershell
# elevated PowerShell
cd tools\ghub-uninstall
powershell -ExecutionPolicy Bypass -File .\uninstall-ghub.ps1
```

Click **Uninstall** in the G HUB window when it appears, let it finish, then
press Enter in the console for residual cleanup. Reboot afterwards.

## What gets removed

- `LGHUBUpdaterService`
- `HKCU\...\Run\LGHUB`
- G HUB gaming virtual driver stack: `logi_joy_bus_enum.inf`,
  `logi_joy_hid.inf`, `logi_joy_vir_hid.inf` (NOT `logitechble.inf`)
- Phantom `Logitech G HUB Virtual *` devices and the Virtual Bus Enumerator
- `C:\Program Files\LGHUB`, `C:\ProgramData\LGHUB`,
  `C:\Users\steve\AppData\Local\LGHUB`

Not touched: HIDMaestro drivers (`hidmaestro.inf`, `hidmaestro_xusb.inf`), the
physical G25, the `g25-driver` per-user install.

## Then test

1. Reboot.
2. `virtual-g29/scripts/Run-Bridge-Admin.ps1 --install-driver --trace-output`.
3. Launch GeForce NOW + a racing game.
4. Record in `../../docs/geforce-now.md` ("G HUB dependency test", Result):
   - virtual G29 still detected by GFN? (Y/N)
   - output reports still arrive (`OUT ...` lines)? (Y/N)
   - steering works in game? (Y/N)

## Reinstall G HUB

Download from <https://www.logitechg.com/software/g-hub> and run the installer.
The `logi_joy_*` drivers and virtual devices come back with it.
