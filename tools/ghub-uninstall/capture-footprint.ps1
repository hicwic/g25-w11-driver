# Snapshot the Logitech / G HUB / HIDMaestro footprint to a text file.
# Used for before/after comparison around the G HUB dependency test.
#
#   powershell -ExecutionPolicy Bypass -File .\capture-footprint.ps1 -OutFile baseline-before.txt

param([Parameter(Mandatory)][string]$OutFile)

$ts = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
$sb = [System.Text.StringBuilder]::new()
[void]$sb.AppendLine("Logitech / G HUB / HIDMaestro footprint - $ts")
function Section($t, $body) {
    [void]$sb.AppendLine()
    [void]$sb.AppendLine("=== $t ===")
    [void]$sb.AppendLine(($body | Out-String).TrimEnd())
}

Section 'Uninstall entries' (
    Get-ChildItem HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall, HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall -EA SilentlyContinue |
        ForEach-Object { $p = Get-ItemProperty $_.PSPath; if ($p.DisplayName -match 'Logitech|G HUB|LGHUB|LGS') { $p | Select-Object DisplayName, DisplayVersion, Publisher, InstallLocation, UninstallString } } |
        Format-List
)

Section 'Services' (
    Get-CimInstance Win32_Service | Where-Object { $_.Name -match 'LGHUB|logi|Logitech' } |
        Select-Object Name, State, StartMode, PathName | Format-List
)

$d = pnputil /enum-drivers
$drvLines = for ($i = 0; $i -lt $d.Count; $i++) {
    if ($d[$i] -match 'logi_joy|logitechble|hidmaestro') { ($d[($i - 1)..($i + 6)] -join "`n"); "" }
}
Section 'Drivers (pnputil, logi / hidmaestro)' $drvLines

Section 'PnP devices - Logitech / G HUB / HIDMaestro / virtual' (
    Get-PnpDevice | Where-Object {
        $_.InstanceId -match 'VID_046D|logi|LGHUB|HIDMAESTRO|USBIP' -or
        $_.FriendlyName -match 'Logitech|G HUB|G25|G29|HIDMaestro|Virtual Bus'
    } | Select-Object Status, Class, FriendlyName, InstanceId | Sort-Object Class, FriendlyName |
        Format-Table -Auto -Wrap | Out-String -Width 4096
)

Section 'Run keys (logi / LGHUB)' (
    Get-Item 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Run', 'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Run' -EA SilentlyContinue |
        ForEach-Object { $k = $_; $_.Property | ForEach-Object { "{0} :: {1} = {2}" -f $k.Name, $_, (Get-ItemPropertyValue $k.PSPath $_) } } |
        Where-Object { $_ -match 'logi|LGHUB' }
)

Section 'Scheduled tasks (logi / LGHUB)' (
    Get-ScheduledTask | Where-Object { $_.TaskName -match 'logi|LGHUB' -or $_.TaskPath -match 'logi|LGHUB' } |
        Select-Object TaskName, TaskPath, State | Format-Table -Auto
)

Section 'Folders' (
    @('C:\Program Files\LGHUB', 'C:\ProgramData\LGHUB', 'C:\Users\steve\AppData\Local\LGHUB', 'C:\Program Files\Logitech') |
        ForEach-Object { "{0}  exists={1}" -f $_, (Test-Path $_) }
)

Section 'Processes (lghub / logi)' (
    Get-Process | Where-Object { $_.Name -match 'lghub|logi' } | Select-Object Name, Id, Path | Format-Table -Auto
)

$sb.ToString() | Set-Content $OutFile -Encoding UTF8
Write-Host "wrote $OutFile"
