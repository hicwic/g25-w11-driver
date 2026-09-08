param([string]$Args = '--duration 10')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '_dotnet.ps1')
if (-not (Test-Path $BridgeExe)) { & (Join-Path $PSScriptRoot 'Build.ps1') }
& $BridgeExe dry-run @($Args -split ' ')
