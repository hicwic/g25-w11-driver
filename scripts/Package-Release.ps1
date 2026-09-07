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
if (-not $Build64) { $Build64 = Join-Path $repoRoot 'build\vs2022-x64\Release' }
if (-not $Build32) { $Build32 = Join-Path $repoRoot 'build\vs2022-x86\Release' }
if (-not $OutputRoot) { $OutputRoot = Join-Path $repoRoot 'dist' }

$Build64 = (Resolve-Path -LiteralPath $Build64).Path
$Build32 = (Resolve-Path -LiteralPath $Build32).Path
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$packageName = "g25-w11-driver-$Version"
$packageRoot = Join-Path $OutputRoot $packageName
$zipPath = Join-Path $OutputRoot "$packageName.zip"

$required = @(
    (Join-Path $Build64 'g25ff.dll'),
    (Join-Path $Build64 'g25tool.exe'),
    (Join-Path $Build64 'g25tray.exe'),
    (Join-Path $Build32 'g25ff.dll'),
    (Join-Path $Build32 'g25tool.exe')
)
foreach ($path in $required) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing build output: $path" }
}

if (Test-Path -LiteralPath $packageRoot) { Remove-Item -LiteralPath $packageRoot -Recurse -Force }
New-Item -ItemType Directory -Force -Path (Join-Path $packageRoot 'bin\x64'), (Join-Path $packageRoot 'bin\x86'), (Join-Path $packageRoot 'scripts'), (Join-Path $packageRoot 'docs'), (Join-Path $packageRoot 'assets') | Out-Null

Copy-Item -LiteralPath (Join-Path $Build64 'g25ff.dll') -Destination (Join-Path $packageRoot 'bin\x64\g25ff.dll')
Copy-Item -LiteralPath (Join-Path $Build64 'g25tool.exe') -Destination (Join-Path $packageRoot 'bin\x64\g25tool.exe')
Copy-Item -LiteralPath (Join-Path $Build64 'g25tray.exe') -Destination (Join-Path $packageRoot 'bin\g25tray.exe')
Copy-Item -LiteralPath (Join-Path $Build32 'g25ff.dll') -Destination (Join-Path $packageRoot 'bin\x86\g25ff.dll')
Copy-Item -LiteralPath (Join-Path $Build32 'g25tool.exe') -Destination (Join-Path $packageRoot 'bin\x86\g25tool.exe')
Copy-Item -LiteralPath (Join-Path $repoRoot 'scripts\Register-G25FF.ps1') -Destination (Join-Path $packageRoot 'scripts\Register-G25FF.ps1')
Copy-Item -LiteralPath (Join-Path $repoRoot 'README.md') -Destination (Join-Path $packageRoot 'README.md')
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination (Join-Path $packageRoot 'LICENSE')
Copy-Item -LiteralPath (Join-Path $repoRoot 'THIRD_PARTY_NOTICES.md') -Destination (Join-Path $packageRoot 'THIRD_PARTY_NOTICES.md')
Copy-Item -LiteralPath (Join-Path $repoRoot 'assets\g25-control.ico') -Destination (Join-Path $packageRoot 'assets\g25-control.ico')
Copy-Item -LiteralPath (Join-Path $repoRoot 'docs\*') -Destination (Join-Path $packageRoot 'docs') -Recurse

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
