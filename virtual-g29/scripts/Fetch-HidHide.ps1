# SPDX-License-Identifier: GPL-2.0-only
# Downloads the pinned HidHide setup into a destination dir for the installer
# build. CI does the same inline; this is the local equivalent.
[CmdletBinding()]
param(
    [string]$OutDir = (Join-Path $PSScriptRoot '..\dist')
)

$ErrorActionPreference = 'Stop'

$version = 'v1.5.230.0'
$file    = 'HidHide_1.5.230_x64.exe'
$url     = "https://github.com/nefarius/HidHide/releases/download/$version/$file"
$sha256  = 'F4BBBCB82E6258641B887C74BC81C4C5F66E4AA811808DFC304347687B7605F6'

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$dest = Join-Path $OutDir $file

if (Test-Path $dest) {
    $have = (Get-FileHash -Algorithm SHA256 $dest).Hash
    if ($have -eq $sha256) { Write-Host "HidHide already present: $dest"; return }
    Write-Host "Re-downloading (hash mismatch): $dest"
}

Write-Host "Downloading $url"
Invoke-WebRequest -Uri $url -OutFile $dest

$have = (Get-FileHash -Algorithm SHA256 $dest).Hash
if ($have -ne $sha256) {
    Remove-Item $dest -Force
    throw "SHA-256 mismatch for $file : got $have, expected $sha256"
}
Write-Host "OK: $dest"
