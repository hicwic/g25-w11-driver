# Interim: removes HIDMaestro virtual controllers + stale virtual G29 nodes.
param()
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '_dotnet.ps1')
$dotnetRoot = $env:DOTNET_ROOT
$tmp = Join-Path $env:TEMP 'g25-virtual-g29-cleanup-admin.ps1'
@"
`$ErrorActionPreference = 'Stop'
if ('$dotnetRoot' -and (Test-Path '$dotnetRoot\dotnet.exe')) {
    `$env:DOTNET_ROOT = '$dotnetRoot'
    `$env:PATH = "$dotnetRoot;`$env:PATH"
}
& '$BridgeExe' cleanup
Read-Host 'Cleanup finished. Press Enter to close'
"@ | Set-Content -LiteralPath $tmp -Encoding UTF8
Start-Process -FilePath 'powershell.exe' -ArgumentList @('-NoExit','-ExecutionPolicy','Bypass','-File',$tmp) -Verb RunAs
