# Dot-sourced by the other scripts. Puts a local .NET 10 SDK on PATH if one is
# found, otherwise relies on `dotnet` already being available (CI uses
# actions/setup-dotnet).

$bridgeRoot = Split-Path -Parent $PSScriptRoot
$candidates = @(
    (Join-Path (Split-Path -Parent $bridgeRoot) '_tools\dotnet10-sdk')
    (Join-Path (Split-Path -Parent (Split-Path -Parent $bridgeRoot)) '_tools\dotnet10-sdk')
)
foreach ($c in $candidates) {
    if (Test-Path (Join-Path $c 'dotnet.exe')) {
        $env:DOTNET_ROOT = $c
        $env:PATH = "$c;$env:PATH"
        break
    }
}
$script:BridgeRoot = $bridgeRoot
$script:BridgeProj = Join-Path $bridgeRoot 'src\G25VirtualG29\G25VirtualG29.csproj'
$script:BridgeExe = Join-Path $bridgeRoot 'src\G25VirtualG29\bin\Release\net10.0-windows10.0.26100.0\win-x64\g25-virtual-g29.exe'
