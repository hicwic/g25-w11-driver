# SPDX-License-Identifier: GPL-2.0-only
[CmdletBinding()]
param(
    [string]$Version = '',
    [string]$OutputPath = '',
    [string]$PreviousRef = ''
)
$ErrorActionPreference = 'Stop'
$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')
function Git-Lines([string[]]$Arguments) {
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = @(& git -C $repoRoot @Arguments 2>$null)
        if ($LASTEXITCODE -ne 0) { return @() }
        return $output
    } finally {
        $ErrorActionPreference = $previousPreference
    }
}
if (-not $Version) {
    $Version = (Git-Lines @('describe', '--tags', '--always', '--dirty') | Select-Object -First 1)
    if (-not $Version) { $Version = 'unreleased' }
}
if (-not $OutputPath) { $OutputPath = Join-Path $repoRoot 'dist\CHANGELOG.md' }
if (-not $PreviousRef) {
    $PreviousRef = (Git-Lines @('describe', '--tags', '--abbrev=0', "$Version^") | Select-Object -First 1)
    if (-not $PreviousRef) { $PreviousRef = (Git-Lines @('rev-list', '--max-parents=0', 'HEAD') | Select-Object -First 1) }
}

$range = if ($PreviousRef) { "$PreviousRef..HEAD" } else { 'HEAD' }
$commits = @(Git-Lines @('log', $range, '--pretty=format:%h%x09%s'))
if (-not $commits) { $commits = @(Git-Lines @('log', '--pretty=format:%h%x09%s')) }

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("# Changelog")
$lines.Add("")
$lines.Add("## $Version")
$lines.Add("")
if ($commits.Count -eq 0) {
    $lines.Add("- Initial source package.")
} else {
    foreach ($commit in $commits) {
        $parts = $commit -split "`t", 2
        if ($parts.Count -eq 2) { $lines.Add("- ``$($parts[0])`` $($parts[1])") }
        else { $lines.Add("- $commit") }
    }
}
$parent = Split-Path -Parent $OutputPath
if ($parent) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
$lines | Set-Content -LiteralPath $OutputPath -Encoding utf8
Write-Host $OutputPath
