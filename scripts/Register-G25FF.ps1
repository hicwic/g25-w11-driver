# SPDX-License-Identifier: GPL-2.0-only
[CmdletBinding()]
param(
    [ValidateSet('Install', 'Uninstall')]
    [string]$Action = 'Install',
    [string]$Dll64 = '',
    [string]$Dll32 = '',
    [string]$Tray = ''
)
$ErrorActionPreference = 'Stop'
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Dll64) { $Dll64 = Join-Path $scriptDir '..\build\portable-release\g25ff.dll' }
if (-not $Dll32) { $Dll32 = Join-Path $scriptDir '..\build\portable-release-x86\g25ff.dll' }
if (-not $Tray) { $Tray = Join-Path $scriptDir '..\build\portable-release\g25tray.exe' }

$driverClsid = '{D7A3D8CB-8B3C-4C35-A552-73A4BEB529E0}'
$oemPath = 'System\CurrentControlSet\Control\MediaProperties\PrivateProperties\Joystick\OEM\VID_046D&PID_C299'
$comPath = "Software\Classes\CLSID\$driverClsid"
$stateRoot = Join-Path $env:LOCALAPPDATA 'g25ff'
$stateFile = Join-Path $stateRoot 'installation.json'
$runPath = 'Software\Microsoft\Windows\CurrentVersion\Run'
$runName = 'G25 Control'

function Open-Base([Microsoft.Win32.RegistryView]$View) {
    [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser, $View)
}
function Test-Key([Microsoft.Win32.RegistryView]$View, [string]$Path) {
    $base = Open-Base $View
    try {
        $key = $base.OpenSubKey($Path, $false)
        try { return $null -ne $key } finally { if ($key) { $key.Dispose() } }
    } finally { $base.Dispose() }
}
function DWords([uint32[]]$Values) {
    $bytes = [System.Collections.Generic.List[byte]]::new()
    foreach ($value in $Values) { $bytes.AddRange([BitConverter]::GetBytes($value)) }
    Write-Output -NoEnumerate $bytes.ToArray()
}
function Set-Registration([Microsoft.Win32.RegistryView]$View, [string]$Dll) {
    $base = Open-Base $View
    try {
        $server = $base.CreateSubKey("$comPath\InProcServer32", $true)
        try {
            $server.SetValue('', $Dll, [Microsoft.Win32.RegistryValueKind]::String)
            $server.SetValue('ThreadingModel', 'Both', [Microsoft.Win32.RegistryValueKind]::String)
        } finally { $server.Dispose() }

        $oem = $base.CreateSubKey($oemPath, $true)
        try {
            $oem.SetValue('OEMName', 'Logitech G25 Racing Wheel USB', [Microsoft.Win32.RegistryValueKind]::String)
            $oem.SetValue('OEMData', [byte[]](0x43, 0x00, 0x88, 0x10, 0x13, 0x00, 0x00, 0x00),
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
        foreach ($effect in $effects) {
            $key = $base.CreateSubKey("$oemPath\OEMForceFeedback\Effects\$($effect[0])", $true)
            try {
                $key.SetValue('', $effect[1], [Microsoft.Win32.RegistryValueKind]::String)
                $key.SetValue('Attributes', (DWords @([uint32]$effect[2], [uint32]$effect[3],
                    [uint32]$effect[4], [uint32]$effect[4], 0x30)), [Microsoft.Win32.RegistryValueKind]::Binary)
            } finally { $key.Dispose() }
        }
    } finally { $base.Dispose() }
}
function Remove-Key([Microsoft.Win32.RegistryView]$View, [string]$Path) {
    $base = Open-Base $View
    try { $base.DeleteSubKeyTree($Path, $false) } finally { $base.Dispose() }
}
function Copy-IfDifferent([string]$Source, [string]$Destination) {
    $sourceFull = [IO.Path]::GetFullPath($Source)
    $destinationFull = [IO.Path]::GetFullPath($Destination)
    if ([string]::Equals($sourceFull, $destinationFull, [StringComparison]::OrdinalIgnoreCase)) { return }
    Copy-Item -LiteralPath $sourceFull -Destination $destinationFull
}

if ($Action -eq 'Install') {
    if (Test-Path -LiteralPath $stateFile) { throw "g25ff is already registered; uninstall it before reinstalling." }
    $source64 = (Resolve-Path -LiteralPath $Dll64).Path
    $source32 = (Resolve-Path -LiteralPath $Dll32).Path
    $sourceTray = (Resolve-Path -LiteralPath $Tray).Path
    if (-not [Environment]::Is64BitOperatingSystem) { throw 'This package expects 64-bit Windows.' }
    New-Item -ItemType Directory -Force -Path $stateRoot | Out-Null
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $backupDir = Join-Path $stateRoot "backup-$stamp"
    New-Item -ItemType Directory -Force -Path $backupDir | Out-Null
    $bin64 = Join-Path $stateRoot 'bin\x64'
    $bin32 = Join-Path $stateRoot 'bin\x86'
    New-Item -ItemType Directory -Force -Path $bin64, $bin32 | Out-Null
    $resolved64 = Join-Path $bin64 'g25ff.dll'
    $resolved32 = Join-Path $bin32 'g25ff.dll'
    $resolvedTray = Join-Path $stateRoot 'bin\g25tray.exe'
    Copy-IfDifferent $source64 $resolved64
    Copy-IfDifferent $source32 $resolved32
    Copy-IfDifferent $sourceTray $resolvedTray
    $runBase = Open-Base Registry64
    try {
        $runKey = $runBase.OpenSubKey($runPath, $false)
        try {
            $previousRun = if ($runKey) { $runKey.GetValue($runName, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames) } else { $null }
            $previousRunKind = if ($runKey -and $null -ne $previousRun) { [string]$runKey.GetValueKind($runName) } else { $null }
        } finally { if ($runKey) { $runKey.Dispose() } }
    } finally { $runBase.Dispose() }
    $state = [ordered]@{
        version = 3; clsid = $driverClsid; dll64 = $resolved64; dll32 = $resolved32; tray = $resolvedTray
        backup = $backupDir; oem64 = (Test-Key Registry64 $oemPath); oem32 = (Test-Key Registry32 $oemPath)
        com64 = (Test-Key Registry64 $comPath); com32 = (Test-Key Registry32 $comPath)
        runExisted = ($null -ne $previousRun); runValue = $previousRun; runKind = $previousRunKind
    }
    foreach ($bits in 64, 32) {
        if ($state."oem$bits") { & reg.exe export "HKCU\$oemPath" (Join-Path $backupDir "oem-$bits.reg") /y "/reg:$bits" | Out-Null }
        if ($state."com$bits") { & reg.exe export "HKCU\$comPath" (Join-Path $backupDir "com-$bits.reg") /y "/reg:$bits" | Out-Null }
    }
    $state | ConvertTo-Json | Set-Content -LiteralPath $stateFile -Encoding utf8
    try {
        Set-Registration Registry64 $resolved64
        Set-Registration Registry32 $resolved32
        $runBase = Open-Base Registry64
        try {
            $runKey = $runBase.CreateSubKey($runPath, $true)
            try { $runKey.SetValue($runName, ('"' + $resolvedTray + '"'), [Microsoft.Win32.RegistryValueKind]::String) }
            finally { $runKey.Dispose() }
        } finally { $runBase.Dispose() }
    } catch {
        Write-Warning 'Registration failed; run this script with -Action Uninstall to restore the saved state.'
        throw
    }
    Write-Host "g25ff registered for the current user (64-bit and 32-bit)."
    Start-Process -FilePath $resolvedTray -WindowStyle Hidden
    Write-Host 'G25 Control started and registered for sign-in.'
    Write-Host "Backup: $backupDir"
    exit 0
}

if (-not (Test-Path -LiteralPath $stateFile)) { throw 'No g25ff installation state was found.' }
$state = Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json
if ($state.version -ge 3) {
    Get-Process -Name 'g25tray' -ErrorAction SilentlyContinue | Stop-Process
    $runBase = Open-Base Registry64
    try {
        $runKey = $runBase.CreateSubKey($runPath, $true)
        try {
            $runKey.DeleteValue($runName, $false)
            if ($state.runExisted) {
                $kind = [Enum]::Parse([Microsoft.Win32.RegistryValueKind], [string]$state.runKind)
                $runKey.SetValue($runName, $state.runValue, $kind)
            }
        } finally { $runKey.Dispose() }
    } finally { $runBase.Dispose() }
}
foreach ($bits in 64, 32) {
    $view = if ($bits -eq 64) { [Microsoft.Win32.RegistryView]::Registry64 } else { [Microsoft.Win32.RegistryView]::Registry32 }
    Remove-Key $view $comPath
    if ($state."com$bits") { & reg.exe import (Join-Path $state.backup "com-$bits.reg") "/reg:$bits" | Out-Null }
    if (-not $state."oem$bits") { Remove-Key $view $oemPath }
    else {
        Remove-Key $view $oemPath
        & reg.exe import (Join-Path $state.backup "oem-$bits.reg") "/reg:$bits" | Out-Null
    }
}
Remove-Item -LiteralPath $stateFile
if ($state.version -ge 2) {
    $safeRoot = [IO.Path]::GetFullPath($stateRoot) + [IO.Path]::DirectorySeparatorChar
    $binaries = @($state.dll64, $state.dll32)
    if ($state.version -ge 3) { $binaries += $state.tray }
    foreach ($binary in $binaries) {
        $full = [IO.Path]::GetFullPath([string]$binary)
        if (-not $full.StartsWith($safeRoot, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove a binary outside $safeRoot"
        }
        if (Test-Path -LiteralPath $full) { Remove-Item -LiteralPath $full }
    }
    foreach ($directory in @((Split-Path -Parent $state.dll64), (Split-Path -Parent $state.dll32))) {
        if (Test-Path -LiteralPath $directory) { [IO.Directory]::Delete($directory, $false) }
    }
    if ($state.version -ge 3 -and (Test-Path -LiteralPath (Split-Path -Parent $state.tray))) {
        # The shared bin directory still contains architecture subdirectories at this point.
        # It is removed only when empty after those directories and the tray executable are gone.
        try { [IO.Directory]::Delete((Split-Path -Parent $state.tray), $false) } catch [IO.IOException] {}
    }
}
Write-Host 'g25ff unregistered and the previous per-user registry state restored.'
