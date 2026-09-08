# Clean-slate G HUB reinstall for the bridge recovery test.
# Removes stale virtual-G29 / compat phantom device nodes first so a fresh
# G HUB does not inherit a confused wheel topology, then runs the installer.
# Run elevated. Reboot afterwards.

$ErrorActionPreference = 'Continue'
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error 'Run elevated.'; exit 1
}

Write-Host "[1] Physical G25 (must stay on native c299):"
Get-PnpDevice | Where-Object { $_.InstanceId -match 'VID_046D&PID_C299' } | Select-Object Status, InstanceId | Format-Table -Auto | Out-Host

Write-Host "`n[2] Removing phantom virtual-G29 / compat nodes"
Get-PnpDevice | Where-Object {
    $_.Status -ne 'OK' -and $_.InstanceId -match 'VID_046D&PID_C24F|VID_046D&PID_C294'
} | ForEach-Object {
    Write-Host "    $($_.InstanceId)"
    & pnputil.exe /remove-device "$($_.InstanceId)" 2>&1 | ForEach-Object { "      $_" } | Out-Host
}
& pnputil.exe /scan-devices | Out-Null

$installer = Join-Path $env:LOCALAPPDATA 'Temp\claude\c--Users-steve-Documents-Project-g25-driver\debdd326-ca78-4bd6-b7ec-76a851f5f909\scratchpad\lghub_installer.exe'
if (-not (Test-Path $installer)) {
    $installer = Read-Host "lghub_installer.exe not at the expected path. Paste full path (or download from logitechg.com/software/g-hub)"
}

Write-Host "`n[3] Launching G HUB installer: $installer"
Write-Host "    Click through Install. Do NOT open games yet."
Start-Process -FilePath $installer -Wait

Write-Host "`n[4] G25 state right after install (before reboot):"
Get-PnpDevice | Where-Object { $_.InstanceId -match 'VID_046D&PID_C(299|294)' } | Select-Object Status, InstanceId | Format-Table -Auto | Out-Host

Write-Host "`nReboot now. Then tell Claude - we check whether G HUB grabbed the G25." -ForegroundColor Green
Read-Host "Press Enter to close"
