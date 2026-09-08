# Clean uninstall of Logitech G HUB for the GeForce NOW bridge dependency test.
# Run elevated. See docs/geforce-now.md "G HUB dependency test".
#
#   powershell -ExecutionPolicy Bypass -File .\uninstall-ghub.ps1
#
# It will:
#   1. stop G HUB processes and the updater service
#   2. launch the official G HUB uninstaller (GUI - click "Uninstall")
#   3. after you confirm, remove residual services, drivers, devices, folders,
#      autostart entries
#   4. write baseline-after.txt next to this script

$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$log  = Join-Path $here 'uninstall-ghub.transcript.txt'
$userLocalAppData = 'C:\Users\steve\AppData\Local\LGHUB'

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error 'Run this script elevated (Administrator).'; exit 1
}

Start-Transcript -Path $log -Append | Out-Null
Write-Host "=== G HUB uninstall - $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') ===" -ForegroundColor Cyan

# 1. stop processes + service ------------------------------------------------
Write-Host "`n[1] Stopping G HUB processes and service"
Get-Service -Name 'LGHUBUpdaterService' -EA SilentlyContinue | Stop-Service -Force -EA SilentlyContinue
Get-Process | Where-Object { $_.Name -match '^lghub|^logi_' } | ForEach-Object {
    Write-Host "    kill $($_.Name) ($($_.Id))"; Stop-Process -Id $_.Id -Force -EA SilentlyContinue
}
Start-Sleep 2

# 2. official uninstaller ----------------------------------------------------
$mgr = 'C:\Program Files\LGHUB\lghub_software_manager.exe'
if (Test-Path $mgr) {
    Write-Host "`n[2] Launching G HUB uninstaller GUI. Click 'Uninstall' and wait for it to finish."
    Start-Process -FilePath $mgr -Wait
} else {
    Write-Host "`n[2] $mgr not found - already removed? Continuing with residual cleanup."
}

Read-Host "`n>>> When the G HUB uninstaller window has closed, press Enter to run residual cleanup"

# 3. residual cleanup -----------------------------------------------------------
Write-Host "`n[3] Residual cleanup"

# 3a. service
if (Get-Service -Name 'LGHUBUpdaterService' -EA SilentlyContinue) {
    Write-Host "    sc delete LGHUBUpdaterService"
    & sc.exe delete LGHUBUpdaterService | Out-Host
}

# 3b. autostart
foreach ($rk in 'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Run','HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Run') {
    Get-Item $rk -EA SilentlyContinue | ForEach-Object {
        $_.Property | Where-Object { $_ -match 'LGHUB|logi' } | ForEach-Object {
            Write-Host "    remove Run value $rk\$_"; Remove-ItemProperty -Path $rk -Name $_ -EA SilentlyContinue
        }
    }
}

# 3c. scheduled tasks
Get-ScheduledTask | Where-Object { $_.TaskName -match 'LGHUB|logi' -or $_.TaskPath -match 'LGHUB|Logi' } | ForEach-Object {
    Write-Host "    unregister task $($_.TaskPath)$($_.TaskName)"
    Unregister-ScheduledTask -TaskName $_.TaskName -TaskPath $_.TaskPath -Confirm:$false -EA SilentlyContinue
}

# 3d. G HUB gaming virtual driver stack (logi_joy_*). Leaves logitechble.inf alone.
$drv = pnputil /enum-drivers
$pub = $null
for ($i = 0; $i -lt $drv.Count; $i++) {
    if ($drv[$i] -match 'Nom publi.*:\s*(oem\d+\.inf)') { $pub = $Matches[1] }
    if ($drv[$i] -match 'logi_joy_(bus_enum|hid|vir_hid)\.inf' -and $pub) {
        Write-Host "    pnputil /delete-driver $pub /uninstall /force"
        & pnputil.exe /delete-driver $pub /uninstall /force | Out-Host
        $pub = $null
    }
}

# 3e. leftover phantom G HUB virtual input devices
& pnputil.exe /enum-devices /class HIDClass /disconnected 2>$null | Out-Null
Get-PnpDevice -EA SilentlyContinue | Where-Object {
    $_.InstanceId -match 'LGHUBDEVICE|ROOT\\SYSTEM' -and $_.FriendlyName -match 'Logitech G HUB|Virtual Bus Enumerator'
} | ForEach-Object {
    Write-Host "    remove device $($_.FriendlyName) [$($_.InstanceId)]"
    & pnputil.exe /remove-device "$($_.InstanceId)" 2>$null | Out-Host
}

# 3f. folders
foreach ($d in 'C:\Program Files\LGHUB','C:\ProgramData\LGHUB',$userLocalAppData) {
    if (Test-Path $d) {
        Write-Host "    remove folder $d"
        Remove-Item -LiteralPath $d -Recurse -Force -EA SilentlyContinue
        if (Test-Path $d) { Write-Host "      (still present - a file is locked, retry after reboot)" -ForegroundColor Yellow }
    }
}

# 4. after snapshot -----------------------------------------------------------
Write-Host "`n[4] Writing baseline-after.txt"
& (Join-Path $here 'capture-footprint.ps1') -OutFile (Join-Path $here 'baseline-after.txt')

Write-Host "`nDone. Reboot before testing the bridge without G HUB." -ForegroundColor Green
Stop-Transcript | Out-Null
Read-Host "Press Enter to close"
