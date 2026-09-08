# Dev helper: build libg25, publish the bridge + service into one folder, and
# install the g25vg29 service pointing at it. Elevates for the install.
#
#   .\scripts\Dev-Install-Service.ps1            # build + publish + install
#   .\scripts\Dev-Install-Service.ps1 -Uninstall # remove the service
#
# After install:
#   sc start g25vg29      /  net start g25vg29   (or the tray, Phase 4)
#   dist\g25vg29.exe status
#   sc stop g25vg29
param([switch]$Uninstall)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '_dotnet.ps1')
$repo = Split-Path -Parent (Split-Path -Parent $BridgeRoot)   # g25-driver
$dist = Join-Path $BridgeRoot 'dist'
$svcExe = Join-Path $dist 'g25vg29.exe'

if ($Uninstall) {
    Start-Process $svcExe 'uninstall' -Verb RunAs -Wait
    return
}

Write-Host '[1] Building libg25.dll'
$cmake = Get-ChildItem (Join-Path $repo '.tools\cmake-*\bin\cmake.exe') -EA SilentlyContinue | Select-Object -First 1
if (-not $cmake) { throw 'cmake not found under .tools (see docs/validation.md)' }
& $cmake.FullName --build (Join-Path $repo 'build\portable-release') --target g25_libg25 --parallel 4
$libg25 = Join-Path $repo 'build\portable-release\libg25.dll'

Write-Host "`n[2] Publishing bridge + service -> $dist"
Remove-Item $dist -Recurse -Force -EA SilentlyContinue
dotnet publish (Join-Path $BridgeRoot 'src\G25VirtualG29\G25VirtualG29.csproj') -c Release -r win-x64 --self-contained -o $dist "-p:Libg25Dll=$libg25"
dotnet publish (Join-Path $BridgeRoot 'src\G25VirtualG29Service\G25VirtualG29Service.csproj') -c Release -r win-x64 --self-contained -o $dist

Write-Host "`n[3] Installing the service (elevates)"
Start-Process $svcExe 'install' -Verb RunAs -Wait
Write-Host "`nDone. dist: $dist"
Write-Host "Start:  sc start g25vg29      Status: & '$svcExe' status      Stop: sc stop g25vg29"
