# Take the physical G25 back from G HUB's WinUSB claim and put it in native mode.
# G HUB's logi_win_usb.inf (oem63) matches VID_046D&PID_C294 (G29 PS3 == G25 in
# compatibility mode) but NOT VID_046D&PID_C299 (G25 native). So once the wheel
# is native, G HUB leaves it alone.
# Run elevated.

$ErrorActionPreference = 'Continue'
$g25tool = 'C:\Users\steve\Documents\Project\g25-driver\build\portable-release\g25tool.exe'

Write-Host "[1] Pausing G HUB so it does not re-grab during recovery"
Get-Service LGHUBUpdaterService -EA SilentlyContinue | Stop-Service -Force -EA SilentlyContinue
Get-Process | Where-Object { $_.Name -match '^lghub' } | ForEach-Object { Stop-Process -Id $_.Id -Force -EA SilentlyContinue }
Start-Sleep 2

Write-Host "[2] Removing the WinUSB binding on the C294 device"
Get-PnpDevice | Where-Object { $_.InstanceId -match 'USB\\VID_046D&PID_C294\\' } | ForEach-Object {
    Write-Host "    remove $($_.InstanceId)"
    & pnputil.exe /remove-device "$($_.InstanceId)" 2>&1 | ForEach-Object { "      $_" }
}
& pnputil.exe /scan-devices | Out-Null
Start-Sleep 3

Write-Host "[3] Device state after rescan:"
Get-PnpDevice | Where-Object { $_.InstanceId -match 'VID_046D&PID_C29(9|4)' } | ForEach-Object {
    $svc=(Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName DEVPKEY_Device_Service -EA SilentlyContinue).Data
    "    {0,-8} svc={1,-8} {2}" -f $_.Status,$svc,$_.InstanceId
}

Write-Host "`n[4] Switching to native mode (g25tool native)"
& $g25tool list
& $g25tool native
Start-Sleep 3
& $g25tool list

Write-Host "`n[5] Final state (want: C299 OK, g25tool sees native):"
Get-PnpDevice | Where-Object { $_.InstanceId -match 'VID_046D&PID_C299' } | Select-Object Status,InstanceId | Format-Table -Auto

Write-Host "[6] Restarting G HUB (it will not touch C299)"
Start-Service LGHUBUpdaterService -EA SilentlyContinue

Write-Host "`nDone." -ForegroundColor Green
Read-Host "Press Enter to close"
