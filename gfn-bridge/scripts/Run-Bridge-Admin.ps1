# Interim: launches the bridge in an elevated console (UAC each time).
# Superseded by the g25gfnbridge service - see ../../docs/gfn-bridge-plugin.md.
param([string]$BridgeArgs = '--install-driver')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '_dotnet.ps1')
$dotnetRoot = $env:DOTNET_ROOT
$tmp = Join-Path $env:TEMP 'g25-gfn-bridge-admin.ps1'
@"
`$ErrorActionPreference = 'Stop'
if ('$dotnetRoot' -and (Test-Path '$dotnetRoot\dotnet.exe')) {
    `$env:DOTNET_ROOT = '$dotnetRoot'
    `$env:PATH = "$dotnetRoot;`$env:PATH"
}
Set-Location '$BridgeRoot'
& '$BridgeExe' bridge $BridgeArgs
Read-Host 'Bridge stopped. Press Enter to close'
"@ | Set-Content -LiteralPath $tmp -Encoding UTF8
Start-Process -FilePath 'powershell.exe' -ArgumentList @('-NoExit','-ExecutionPolicy','Bypass','-File',$tmp) -Verb RunAs
