param([string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '_dotnet.ps1')
dotnet build $BridgeProj -c $Configuration
