# G25 Control icon

`g25-control-source.png` is the artwork supplied by the project author on
7 September 2026. `g25-control.ico` is generated from it with:

```powershell
.\scripts\Build-TrayIcon.ps1 `
  -InputPng .\assets\g25-control-source.png `
  -OutputIco .\assets\g25-control.ico
```

The ICO contains 16, 20, 24, 32, 40, 48, 64, 128 and 256 pixel frames.
