param(
    [string]$InputRoot = (Join-Path $PSScriptRoot '..\android_project\app\src\main\assets\audio'),
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\converted-audio'),
    [ValidateSet('wav', 'ogg')]
    [string]$Format = 'wav',
    [ValidateRange(8000, 192000)]
    [int]$SampleRate = 44100,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$InputRoot = (Resolve-Path -LiteralPath $InputRoot).Path
$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$outputPrefix = $OutputRoot.TrimEnd('\') + '\'
if ($OutputRoot.Equals($InputRoot, [System.StringComparison]::OrdinalIgnoreCase) -or
    $InputRoot.StartsWith($outputPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputRoot must not equal or contain InputRoot.'
}

$ffmpegCommand = Get-Command ffmpeg -ErrorAction SilentlyContinue
if ($null -eq $ffmpegCommand) {
    throw 'FFmpeg is required. Install it and add ffmpeg.exe to PATH before converting audio.'
}

$extensions = @('.caf', '.aac', '.m4a', '.mp3', '.ogg', '.aif', '.aiff', '.flac', '.opus', '.wav')
$files = Get-ChildItem -LiteralPath $InputRoot -Recurse -File |
    Where-Object {
        ($extensions -contains $_.Extension.ToLowerInvariant()) -and
        -not $_.FullName.StartsWith($outputPrefix, [System.StringComparison]::OrdinalIgnoreCase)
    }
$converted = 0

foreach ($file in $files) {
    if ($file.Extension.Equals(".$Format", [System.StringComparison]::OrdinalIgnoreCase)) {
        Write-Host "Already in $Format format: $($file.FullName)"
        continue
    }

    $relativePath = $file.FullName.Substring($InputRoot.Length).TrimStart('\')
    $target = Join-Path $OutputRoot ([System.IO.Path]::ChangeExtension($relativePath, ".$Format"))
    $targetDirectory = Split-Path -Parent $target
    New-Item -ItemType Directory -Path $targetDirectory -Force | Out-Null
    if ((Test-Path -LiteralPath $target) -and -not $Force) {
        throw "Output exists; pass -Force to replace it: $target"
    }

    $arguments = @('-hide_banner', '-nostdin', '-loglevel', 'error')
    if ($Force) {
        $arguments += '-y'
    }
    else {
        $arguments += '-n'
    }
    $arguments += @('-i', $file.FullName, '-vn', '-map_metadata', '-1')
    if ($Format -eq 'wav') {
        $arguments += @('-ar', "$SampleRate", '-ac', '2', '-c:a', 'pcm_s16le')
    }
    else {
        $arguments += @('-c:a', 'libvorbis', '-q:a', '4')
    }
    $arguments += $target

    & $ffmpegCommand.Source @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "FFmpeg failed with exit code $LASTEXITCODE while converting $($file.FullName)"
    }
    $converted++
    Write-Host "$($file.FullName) -> $target"
}

Write-Host "Converted $converted audio file(s) to $Format."
