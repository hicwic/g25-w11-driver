# HidHide 1.5.230.0

<https://github.com/nefarius/HidHide> — MIT (`LICENSE-HidHide.txt`).

The Virtual G29 bridge uses HidHide to hide the physical G25 from **local**
DirectInput games while the bridge runs, so they bind the virtual G29 instead of
seeing two wheels. GeForce NOW is unaffected (it already ignores `046D:C299`).

- **Runtime:** the bridge shells the installed `HidHideCLI.exe` (found via the
  `HKLM\SOFTWARE\Nefarius Software Solutions e.U.\HidHide` `Path` value, or
  `%ProgramW6432%\Nefarius Software Solutions\HidHide`). Nothing from HidHide is
  linked or bundled into the bridge binaries.
- **Installer:** `installer/g25-virtual-g29.iss` bundles the HidHide **setup**
  and runs it silently (`/qn /norestart`).

## The setup binary is not committed

`HidHide_1.5.230_x64.exe` (~7.7 MB) is fetched, not stored in git:

- CI (`dev.yml`, `release.yml`) downloads it into the installer payload dir.
- Locally: `virtual-g29/scripts/Fetch-HidHide.ps1` downloads it into
  `virtual-g29/dist/` before you build the installer.

Pinned URL:
<https://github.com/nefarius/HidHide/releases/download/v1.5.230.0/HidHide_1.5.230_x64.exe>
SHA-256 is checked by `Fetch-HidHide.ps1`.
