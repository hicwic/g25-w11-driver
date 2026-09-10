# Stop G HUB from WinUSB-claiming the G25.
#
# G HUB installs logi_win_usb.inf (class LGHUBWinUSB). It matches
# VID_046D&PID_C294 (== G25 in compatibility mode) and Windows auto-binds it on
# every plug/re-enumeration, before anything can switch the wheel to native mode.
# It does NOT match VID_046D&PID_C299 (native). Removing this one INF lets the
# G25 come up as a normal HID device again; once it is native, G HUB ignores it.
#
# The INF only covers G920 / G29-PS4 / G29-PS3 / G923 and some Yeti mics - none
# of which are on this machine. Re-run this if a G HUB update reinstalls it.
#
# Run elevated.

$ErrorActionPreference = 'Continue'

# Find g25tool: next to this script in an install, then the default per-user
# install, then a local build tree. Without it the driver work below still runs;
# only the "switch to native mode" step is skipped.
$g25tool = @(
    (Join-Path $PSScriptRoot '..\bin\x64\g25tool.exe')
    (Join-Path $env:LOCALAPPDATA 'g25ff\bin\x64\g25tool.exe')
    (Join-Path $PSScriptRoot '..\build\portable-release\g25tool.exe')
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $g25tool) {
    Write-Warning 'g25tool.exe not found - the wheel will not be switched to G25 mode automatically.'
}

Write-Host "[1] Pause G HUB"
Get-Service LGHUBUpdaterService -EA SilentlyContinue | Stop-Service -Force -EA SilentlyContinue
Get-Process | Where-Object { $_.Name -match '^lghub' } | ForEach-Object { Stop-Process -Id $_.Id -Force -EA SilentlyContinue }
Start-Sleep 2

Write-Host "[2] Find and delete the G HUB WinUSB driver package(s)"
$drv = pnputil /enum-drivers
$pub = $null
for ($i = 0; $i -lt $drv.Count; $i++) {
    if ($drv[$i] -match '(oem\d+\.inf)') { $pub = $Matches[1] }
    if ($drv[$i] -match 'logi_win_usb\.inf' -and $pub) {
        Write-Host "    pnputil /delete-driver $pub /uninstall /force"
        & pnputil.exe /delete-driver $pub /uninstall /force 2>&1 | ForEach-Object { "      $_" }
        $pub = $null
    }
}

Write-Host "[3] Remove the C294 device node so it re-enumerates with the default driver"
Get-PnpDevice | Where-Object { $_.InstanceId -match 'USB\\VID_046D&PID_C294\\' } | ForEach-Object {
    & pnputil.exe /remove-device "$($_.InstanceId)" 2>&1 | ForEach-Object { "      $_" }
}
& pnputil.exe /scan-devices | Out-Null
Start-Sleep 4

Write-Host "[4] Device state now:"
Get-PnpDevice | Where-Object { $_.InstanceId -match 'VID_046D&PID_C29(9|4)' } | ForEach-Object {
    $svc=(Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName DEVPKEY_Device_Service -EA SilentlyContinue).Data
    "    {0,-8} svc={1,-10} {2}" -f $_.Status,$svc,$_.InstanceId
}

Write-Host "`n[5] Switch to native mode"
if ($g25tool) {
    & $g25tool list
    & $g25tool native
    Start-Sleep 3
    & $g25tool list
} else {
    Write-Host '    skipped (g25tool.exe not found); replug the wheel and let G25 Control do it.'
}

Write-Host "`n[6] Restart G HUB"
Start-Service LGHUBUpdaterService -EA SilentlyContinue
Start-Sleep 3
Write-Host "    Final:"
if ($g25tool) { & $g25tool list }
Get-PnpDevice | Where-Object { $_.InstanceId -match 'VID_046D&PID_C299' } | Select-Object Status,InstanceId | Format-Table -Auto

Read-Host "Press Enter to close"
