# Remove every virtual G29 device node so the bridge can create exactly one.
# A stuck HIDMaestro UMDF device (ROOT\HIDCLASS, oem25) from an earlier
# default-profile run reports bcdDevice 0100 and makes GeForce NOW spam
# "No known device with interface number 0 in 046D:C24F:0100".
# Run elevated. Bridge must be stopped.

$ErrorActionPreference = 'Continue'
$bridgeRepo = 'C:\Users\steve\Documents\Project\g25-gfn-wheel-bridge'
$exe = Join-Path $bridgeRepo 'src\G25GfnWheelBridge\bin\Release\net10.0-windows10.0.26100.0\win-x64\g25-gfn-wheel-bridge.exe'
$dotnetRoot = 'C:\Users\steve\Documents\Project\_tools\dotnet10-sdk'
if (Test-Path (Join-Path $dotnetRoot 'dotnet.exe')) { $env:DOTNET_ROOT = $dotnetRoot; $env:PATH = "$dotnetRoot;$env:PATH" }

Get-Process g25-gfn-wheel-bridge -EA SilentlyContinue | ForEach-Object { Write-Host "killing bridge $($_.Id)"; Stop-Process -Id $_.Id -Force }
Start-Sleep 1

Write-Host "`n[1] HIDMaestro RemoveAllVirtualControllers"
& $exe cleanup 2>&1 | Out-Host
Start-Sleep 2

Write-Host "`n[2] Removing any remaining virtual-G29 nodes"
$targets = Get-PnpDevice | Where-Object {
    ($_.FriendlyName -match 'G29|Driving Force' -or $_.InstanceId -match 'VID_046D&PID_C24F') -and
    $_.InstanceId -notmatch 'VID_046D&PID_C299'
}
foreach ($t in $targets) {
    Write-Host "    $($t.Status)  $($t.InstanceId)"
    & pnputil.exe /remove-device "$($t.InstanceId)" 2>&1 | ForEach-Object { "      $_" } | Out-Host
}
& pnputil.exe /scan-devices | Out-Null

Write-Host "`n[3] Remaining G29 / C24F devices (want: none):"
Get-PnpDevice | Where-Object { $_.FriendlyName -match 'G29|Driving Force' -or $_.InstanceId -match 'VID_046D&PID_C24F' } |
    Select-Object Status, FriendlyName, InstanceId | Format-Table -Auto -Wrap | Out-String -Width 200 | Out-Host

Write-Host "`n[4] Physical G25 (want: OK on c299):"
Get-PnpDevice | Where-Object { $_.InstanceId -match 'VID_046D&PID_C299' } | Select-Object Status, InstanceId | Format-Table -Auto | Out-Host

Write-Host "Done. If [3] is empty and [4] is OK, restart the bridge." -ForegroundColor Green
Read-Host "Press Enter to close"
