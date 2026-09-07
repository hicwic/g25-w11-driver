# SPDX-License-Identifier: GPL-2.0-only
[CmdletBinding()]
param(
    [string]$GeForceNowRoot = (Join-Path $env:LOCALAPPDATA 'NVIDIA Corporation\GeForceNOW')
)
$ErrorActionPreference = 'Stop'

$knownLogitechWheels = [ordered]@{
    '046d:c299' = 'Logitech G25 Racing Wheel'
    '046d:c29b' = 'Logitech G27 Racing Wheel'
    '046d:c24f' = 'Logitech G29 Driving Force Racing Wheel'
    '046d:c260' = 'Logitech G29 Driving Force Racing Wheel PS4 mode'
    '046d:c262' = 'Logitech G920 Driving Force Racing Wheel'
    '046d:c266' = 'Logitech G923 Racing Wheel PlayStation/PC family'
    '046d:c267' = 'Logitech G923 Racing Wheel PlayStation/PC family'
    '046d:c26d' = 'Logitech G923 Racing Wheel Xbox/PC family'
    '046d:c26e' = 'Logitech G923 Racing Wheel Xbox/PC family'
}

if (-not (Test-Path -LiteralPath $GeForceNowRoot)) {
    throw "GeForce NOW root not found: $GeForceNowRoot"
}

$targets = @(
    Join-Path $GeForceNowRoot 'CEF\Geronimo.dll'
    Join-Path $GeForceNowRoot 'CEF\nvc\localuser\GFN\GameStreamClientAgent.dll'
)

Write-Host "GeForce NOW root: $GeForceNowRoot"
Write-Host ''
foreach ($target in $targets) {
    if (-not (Test-Path -LiteralPath $target)) {
        Write-Host "Missing: $target"
        continue
    }
    $bytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $target).Path)
    $ascii = [Text.Encoding]::ASCII.GetString($bytes).ToLowerInvariant()
    Write-Host "[$target]"
    foreach ($id in $knownLogitechWheels.Keys) {
        $present = $ascii.Contains($id)
        $mark = if ($present) { 'present' } else { 'absent ' }
        Write-Host ("  {0}  {1}  {2}" -f $mark, $id, $knownLogitechWheels[$id])
    }
    Write-Host ''
}

Write-Host 'Recent GeForce NOW wheel/gamepad log lines:'
$logs = Get-ChildItem -LiteralPath $GeForceNowRoot -File -Filter '*.log' -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 8
foreach ($log in $logs) {
    Select-String -LiteralPath $log.FullName -Pattern 'wheel','steering','logitech','046d','c299','c24f','c262','c266','c26e','gamepad','controller','GSHID' -SimpleMatch -ErrorAction SilentlyContinue |
        Select-Object -First 40 |
        ForEach-Object { "{0}:{1}: {2}" -f $log.Name, $_.LineNumber, $_.Line }
}
