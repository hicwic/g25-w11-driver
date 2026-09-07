# SPDX-License-Identifier: GPL-2.0-only
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$InputPng,
    [Parameter(Mandatory)][string]$OutputIco
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$source = [System.Drawing.Bitmap]::FromFile((Resolve-Path -LiteralPath $InputPng))
try {
    $sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
    $frames = [System.Collections.Generic.List[byte[]]]::new()
    foreach ($size in $sizes) {
        $bitmap = [System.Drawing.Bitmap]::new($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
            try {
                $graphics.Clear([System.Drawing.Color]::Transparent)
                $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
                $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
                $scale = [Math]::Min($size / $source.Width, $size / $source.Height)
                $width = [int][Math]::Round($source.Width * $scale)
                $height = [int][Math]::Round($source.Height * $scale)
                $destination = [System.Drawing.Rectangle]::new(
                    [int](($size - $width) / 2), [int](($size - $height) / 2), $width, $height)
                $graphics.DrawImage($source, $destination)
            } finally { $graphics.Dispose() }
            $stream = [System.IO.MemoryStream]::new()
            try {
                $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
                $frames.Add($stream.ToArray())
            } finally { $stream.Dispose() }
        } finally { $bitmap.Dispose() }
    }
    $outputDirectory = Split-Path -Parent $OutputIco
    if ($outputDirectory) { New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null }
    $file = [System.IO.File]::Open($OutputIco, [System.IO.FileMode]::Create)
    $writer = [System.IO.BinaryWriter]::new($file)
    try {
        $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$frames.Count)
        $offset = 6 + 16 * $frames.Count
        for ($index = 0; $index -lt $frames.Count; ++$index) {
            $size = $sizes[$index]
            $dimension = if ($size -eq 256) { 0 } else { $size }
            $writer.Write([byte]$dimension)
            $writer.Write([byte]$dimension)
            $writer.Write([byte]0); $writer.Write([byte]0)
            $writer.Write([uint16]1); $writer.Write([uint16]32)
            $writer.Write([uint32]$frames[$index].Length)
            $writer.Write([uint32]$offset)
            $offset += $frames[$index].Length
        }
        foreach ($frame in $frames) { $writer.Write($frame) }
    } finally { $writer.Dispose(); $file.Dispose() }
} finally { $source.Dispose() }
