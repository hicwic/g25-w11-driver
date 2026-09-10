# SPDX-License-Identifier: GPL-2.0-only
[CmdletBinding()]
param(
    [string]$Version = '',
    [string]$Build64 = '',
    [string]$Build32 = '',
    [string]$OutputRoot = ''
)
$ErrorActionPreference = 'Stop'
$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')
function Git-Text([string[]]$Arguments) {
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & git -C $repoRoot @Arguments 2>$null
        if ($LASTEXITCODE -ne 0) { return '' }
        return ($output -join "`n").Trim()
    } finally {
        $ErrorActionPreference = $previousPreference
    }
}
if (-not $Version) {
    $Version = Git-Text @('describe', '--tags', '--always', '--dirty')
    if (-not $Version) { $Version = '0.1.0-local' }
}
if (-not $Build64) { $Build64 = Join-Path $repoRoot 'build\vs2022-x64' }
if (-not $Build32) { $Build32 = Join-Path $repoRoot 'build\vs2022-x86' }
if (-not $OutputRoot) { $OutputRoot = Join-Path $repoRoot 'dist' }

$Build64 = (Resolve-Path -LiteralPath $Build64).Path
$Build32 = (Resolve-Path -LiteralPath $Build32).Path
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$packageName = "g25-w11-driver-$Version"
$packageRoot = Join-Path $OutputRoot $packageName
$zipPath = Join-Path $OutputRoot "$packageName.zip"

function Find-Artifact([string]$Root, [string]$Name) {
    $direct = Join-Path $Root $Name
    if (Test-Path -LiteralPath $direct) { return (Resolve-Path -LiteralPath $direct).Path }
    $match = Get-ChildItem -LiteralPath $Root -Filter $Name -Recurse -File | Select-Object -First 1
    if (-not $match) { throw "Missing build output under ${Root}: $Name" }
    return $match.FullName
}

$g25ff64 = Find-Artifact $Build64 'g25ff.dll'
$g25tool64 = Find-Artifact $Build64 'g25tool.exe'
$g25tray = Find-Artifact $Build64 'g25tray.exe'
$g25ff32 = Find-Artifact $Build32 'g25ff.dll'
$g25tool32 = Find-Artifact $Build32 'g25tool.exe'

if (Test-Path -LiteralPath $packageRoot) { Remove-Item -LiteralPath $packageRoot -Recurse -Force }
New-Item -ItemType Directory -Force -Path (Join-Path $packageRoot 'bin\x64'), (Join-Path $packageRoot 'bin\x86'), (Join-Path $packageRoot 'scripts'), (Join-Path $packageRoot 'docs'), (Join-Path $packageRoot 'assets') | Out-Null

Copy-Item -LiteralPath $g25ff64 -Destination (Join-Path $packageRoot 'bin\x64\g25ff.dll')
Copy-Item -LiteralPath $g25tool64 -Destination (Join-Path $packageRoot 'bin\x64\g25tool.exe')
Copy-Item -LiteralPath $g25tray -Destination (Join-Path $packageRoot 'bin\g25tray.exe')
Copy-Item -LiteralPath $g25ff32 -Destination (Join-Path $packageRoot 'bin\x86\g25ff.dll')
Copy-Item -LiteralPath $g25tool32 -Destination (Join-Path $packageRoot 'bin\x86\g25tool.exe')
Copy-Item -LiteralPath (Join-Path $repoRoot 'scripts\Register-G25FF.ps1') -Destination (Join-Path $packageRoot 'scripts\Register-G25FF.ps1')
Copy-Item -LiteralPath (Join-Path $repoRoot 'scripts\Block-GHubWinUsb.ps1') -Destination (Join-Path $packageRoot 'scripts\Block-GHubWinUsb.ps1')
Copy-Item -LiteralPath (Join-Path $repoRoot 'README.md') -Destination (Join-Path $packageRoot 'README.md')
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination (Join-Path $packageRoot 'LICENSE')
Copy-Item -LiteralPath (Join-Path $repoRoot 'THIRD_PARTY_NOTICES.md') -Destination (Join-Path $packageRoot 'THIRD_PARTY_NOTICES.md')
Copy-Item -LiteralPath (Join-Path $repoRoot 'assets\g25-control.ico') -Destination (Join-Path $packageRoot 'assets\g25-control.ico')
Copy-Item -Path (Join-Path $repoRoot 'docs\*') -Destination (Join-Path $packageRoot 'docs') -Recurse

$manifest = [ordered]@{
    name = 'g25-w11-driver'
    version = $Version
    builtAt = (Get-Date).ToUniversalTime().ToString('o')
    binaries = @(
        'bin/x64/g25ff.dll',
        'bin/x64/g25tool.exe',
        'bin/g25tray.exe',
        'bin/x86/g25ff.dll',
        'bin/x86/g25tool.exe'
    )
    install = 'powershell -ExecutionPolicy Bypass -File scripts/Register-G25FF.ps1 -Action Install -Dll64 bin\x64\g25ff.dll -Dll32 bin\x86\g25ff.dll -Tray bin\g25tray.exe'
    uninstall = 'powershell -ExecutionPolicy Bypass -File scripts/Register-G25FF.ps1 -Action Uninstall'
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $packageRoot 'manifest.json') -Encoding utf8

if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
Compress-Archive -Path (Join-Path $packageRoot '*') -DestinationPath $zipPath

[pscustomobject]@{
    Version = $Version
    PackageRoot = $packageRoot
    Zip = $zipPath
} | ConvertTo-Json
