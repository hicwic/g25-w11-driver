# SPDX-License-Identifier: GPL-2.0-only
# Registers the per-user OEM / force-feedback metadata for the virtual G29
# (046D:C24F) so local DirectInput games recognise it (Forza) and route FFB
# effects through the g25ff.dll effect driver. g25ff, seeing the C24F device,
# writes lg4ff reports to the virtual G29; the bridge relays them to the G25.
#
# The g25ff COM class is registered by the core g25-driver installer - this
# script only adds the OEM key for C24F. Run as the interactive user (HKCU).
[CmdletBinding()]
param([ValidateSet('Install', 'Uninstall')] [string]$Action = 'Install')

$ErrorActionPreference = 'Stop'

# Same CLSID the core installer registers for g25ff.dll.
$driverClsid = '{D7A3D8CB-8B3C-4C35-A552-73A4BEB529E0}'
$oemPath = 'System\CurrentControlSet\Control\MediaProperties\PrivateProperties\Joystick\OEM\VID_046D&PID_C24F'
$comPath = "Software\Classes\CLSID\$driverClsid\InProcServer32"
$stateRoot = Join-Path $env:LOCALAPPDATA 'g25vg29'
$stateFile = Join-Path $stateRoot 'oem-registration.json'

function Open-Base([Microsoft.Win32.RegistryView]$View) {
    [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser, $View)
}
function Test-Key([Microsoft.Win32.RegistryView]$View, [string]$Path) {
    $base = Open-Base $View
    try { $k = $base.OpenSubKey($Path, $false); try { return $null -ne $k } finally { if ($k) { $k.Dispose() } } }
    finally { $base.Dispose() }
}
function DWords([uint32[]]$Values) {
    $bytes = [System.Collections.Generic.List[byte]]::new()
    foreach ($v in $Values) { $bytes.AddRange([BitConverter]::GetBytes($v)) }
    Write-Output -NoEnumerate $bytes.ToArray()
}
function Remove-Key([Microsoft.Win32.RegistryView]$View, [string]$Path) {
    $base = Open-Base $View
    try { $base.DeleteSubKeyTree($Path, $false) } finally { $base.Dispose() }
}

function Set-Registration([Microsoft.Win32.RegistryView]$View) {
    $base = Open-Base $View
    try {
        $oem = $base.CreateSubKey($oemPath, $true)
        try {
            # LGS-era name (the one Forza Horizon 4 accepts; the G HUB name is not).
            $oem.SetValue('OEMName', 'Logitech G29 Driving Force Racing Wheel USB',
                          [Microsoft.Win32.RegistryValueKind]::String)
            # Byte 4 = button count (0x19 = 25 for the G29; the G25 uses 0x13 = 19).
            $oem.SetValue('OEMData', [byte[]](0x43, 0x00, 0x08, 0x10, 0x19, 0x00, 0x00, 0x00),
                          [Microsoft.Win32.RegistryValueKind]::Binary)
        } finally { $oem.Dispose() }

        $axis = $base.CreateSubKey("$oemPath\Axes\0", $true)
        try {
            $axis.SetValue('', 'Wheel axis', [Microsoft.Win32.RegistryValueKind]::String)
            $axis.SetValue('Attributes', [byte[]](0x01, 0x81, 0x00, 0x00), [Microsoft.Win32.RegistryValueKind]::Binary)
            $axis.SetValue('FFAttributes', [byte[]](0x0a, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00),
                           [Microsoft.Win32.RegistryValueKind]::Binary)
        } finally { $axis.Dispose() }

        $ff = $base.CreateSubKey("$oemPath\OEMForceFeedback", $true)
        try {
            $ff.SetValue('Attributes', (DWords @(0, 4000, 4000)), [Microsoft.Win32.RegistryValueKind]::Binary)
            $ff.SetValue('CLSID', $driverClsid, [Microsoft.Win32.RegistryValueKind]::String)
        } finally { $ff.Dispose() }

        $effects = @(
            @('{13541C20-8E33-11D0-9AD0-00A0C9A06E35}', 'Constant', 0, 0x8601, 0x3ed),
            @('{13541C21-8E33-11D0-9AD0-00A0C9A06E35}', 'Ramp Force', 1, 0x8602, 0x3ef),
            @('{13541C22-8E33-11D0-9AD0-00A0C9A06E35}', 'Square Wave', 2, 0x8603, 0x3ef),
            @('{13541C23-8E33-11D0-9AD0-00A0C9A06E35}', 'Sine Wave', 3, 0x8603, 0x3ef),
            @('{13541C24-8E33-11D0-9AD0-00A0C9A06E35}', 'Triangle Wave', 4, 0x8603, 0x3ef),
            @('{13541C25-8E33-11D0-9AD0-00A0C9A06E35}', 'Sawtooth Up Wave', 5, 0x8603, 0x3ef),
            @('{13541C26-8E33-11D0-9AD0-00A0C9A06E35}', 'Sawtooth Down Wave', 6, 0x8603, 0x3ef),
            @('{13541C27-8E33-11D0-9AD0-00A0C9A06E35}', 'Spring',   7, 0xd804, 0x36d),
            @('{13541C28-8E33-11D0-9AD0-00A0C9A06E35}', 'Damper',   8, 0xd804, 0x36d),
            @('{13541C29-8E33-11D0-9AD0-00A0C9A06E35}', 'Inertia',  9, 0xd804, 0x36d),
            @('{13541C2A-8E33-11D0-9AD0-00A0C9A06E35}', 'Friction', 10, 0xd804, 0x36d),
            @('{13541C2B-8E33-11D0-9AD0-00A0C9A06E35}', 'Custom Force', 11, 0x8605, 0x3ef)
        )
        foreach ($e in $effects) {
            $key = $base.CreateSubKey("$oemPath\OEMForceFeedback\Effects\$($e[0])", $true)
            try {
                $key.SetValue('', $e[1], [Microsoft.Win32.RegistryValueKind]::String)
                $key.SetValue('Attributes', (DWords @([uint32]$e[2], [uint32]$e[3],
                    [uint32]$e[4], [uint32]$e[4], 0x30)), [Microsoft.Win32.RegistryValueKind]::Binary)
            } finally { $key.Dispose() }
        }
    } finally { $base.Dispose() }
}

if ($Action -eq 'Install') {
    if (-not (Test-Key Registry64 $comPath)) {
        Write-Warning 'g25ff.dll is not registered (core g25-driver not installed). Skipping virtual G29 FFB registration; GeForce NOW still works. Re-run this script after installing the core driver for local-game force feedback.'
        exit 0
    }
    if (Test-Path -LiteralPath $stateFile) {
        Write-Host 'Virtual G29 OEM metadata already registered.'; exit 0
    }
    New-Item -ItemType Directory -Force -Path $stateRoot | Out-Null
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $backupDir = Join-Path $stateRoot "oem-backup-$stamp"
    New-Item -ItemType Directory -Force -Path $backupDir | Out-Null
    $state = [ordered]@{
        version = 1; backup = $backupDir
        oem64 = (Test-Key Registry64 $oemPath); oem32 = (Test-Key Registry32 $oemPath)
    }
    foreach ($bits in 64, 32) {
        if ($state."oem$bits") {
            & reg.exe export "HKCU\$oemPath" (Join-Path $backupDir "oem-$bits.reg") /y "/reg:$bits" | Out-Null
        }
    }
    $state | ConvertTo-Json | Set-Content -LiteralPath $stateFile -Encoding utf8
    # A local game may have written its own (broken) C24F entry - "G29 Driving
    # Force Racing Wheel" / OEMData 03.... Rebuild it clean.
    foreach ($bits in 64, 32) {
        $view = if ($bits -eq 64) { [Microsoft.Win32.RegistryView]::Registry64 } else { [Microsoft.Win32.RegistryView]::Registry32 }
        if ($state."oem$bits") { Remove-Key $view $oemPath }
    }
    Set-Registration Registry64
    Set-Registration Registry32
    Write-Host 'Virtual G29 (046D:C24F) OEM / force-feedback metadata registered.'
    Write-Host "Backup: $backupDir"
    exit 0
}

if (-not (Test-Path -LiteralPath $stateFile)) { Write-Host 'Nothing to unregister.'; exit 0 }
$state = Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json
foreach ($bits in 64, 32) {
    $view = if ($bits -eq 64) { [Microsoft.Win32.RegistryView]::Registry64 } else { [Microsoft.Win32.RegistryView]::Registry32 }
    Remove-Key $view $oemPath
    if ($state."oem$bits") {
        & reg.exe import (Join-Path $state.backup "oem-$bits.reg") "/reg:$bits" | Out-Null
    }
}
Remove-Item -LiteralPath $stateFile
Write-Host 'Virtual G29 OEM metadata unregistered and the previous state restored.'
