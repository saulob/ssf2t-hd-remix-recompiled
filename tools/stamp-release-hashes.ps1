<#
.SYNOPSIS
  Writes the SHA-256 of the built release artifacts (Windows ZIP and Linux AppImage) into the Checksums table of the release notes.
  Local only: nothing is committed, tagged or published.

.DESCRIPTION
  Run it after tools\package-release.ps1 and tools/package-linux.sh have built the artifacts. It recomputes their hashes in dist\, checks them
  against the .sha256 file, and fills the Checksums table of .github\release-notes-v<version>.md. The notes are not an
  input of the build, so committing them afterwards does not change the artifact. It stops when any other file has
  uncommitted changes: the artifact would then not match the commit it is published for.
  The version is the project VERSION in CMakeLists.txt.

.EXAMPLE
  .\tools\stamp-release-hashes.ps1
#>
param(
    [switch]$AllowOtherChanges  # fill the table although other files have uncommitted changes
)
$ErrorActionPreference = 'Stop'

$Root = Split-Path $PSScriptRoot -Parent
$cmake = Get-Content (Join-Path $Root 'CMakeLists.txt') -Raw
if ($cmake -notmatch '(?m)^\s*project\(\s*ssf2t_hd_remix\s+VERSION\s+(\d+\.\d+\.\d+)') { throw 'Project VERSION not found in CMakeLists.txt' }
$Version = $Matches[1]
$Dist = Join-Path $Root 'dist'
$Artifacts = @("SSF2THDRemix-v$Version-windows-x64.zip", "SSF2THDRemix-v$Version-x86_64.AppImage")
$Notes = @(Join-Path $Root ".github\release-notes-v$Version.md")
$Utf8NoBom = New-Object System.Text.UTF8Encoding $false

if (-not $AllowOtherChanges) {
    $status = @(git -C $Root status --porcelain)
    if ($LASTEXITCODE -ne 0) { throw 'git status failed' }
    $other = @($status | Where-Object { $_ -notmatch '^.. "?\.github/release-notes-v\d+\.\d+\.\d+\.md"?$' })
    if ($other.Count) {
        $other | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
        throw 'Other files have uncommitted changes. Commit the release source and build the artifact from that commit first.'
    }
}

$hashes = @{}
foreach ($name in $Artifacts) {
    $file = Join-Path $Dist $name
    $shaFile = Join-Path $Dist ($name -replace '\.(zip|AppImage)$', '.sha256')
    foreach ($path in @($file, $shaFile)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing $path (build the artifact first)" }
    }
    $hash = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
    $recorded = ((Get-Content -LiteralPath $shaFile -Raw).Trim() -split '\s+')[0].ToLowerInvariant()
    if ($recorded -ne $hash) { throw "$shaFile does not match $name" }
    $hashes[$name] = $hash
}

foreach ($path in $Notes) {
    $text = [IO.File]::ReadAllText($path)
    foreach ($name in $Artifacts) {
        $pattern = '(\| `' + [regex]::Escape($name) + '` \| `)[^`]*(` \|)'
        if ($text -notmatch $pattern) { throw "$path has no Checksums row for $name" }
        $text = $text -replace $pattern, ('${1}' + $hashes[$name] + '${2}')
    }
    [IO.File]::WriteAllText($path, $text, $Utf8NoBom)
}

Write-Host ''
Write-Host "Release $Version checksum written to the release notes:"
foreach ($name in $Artifacts) { Write-Host "  $($hashes[$name])  $name" }
Write-Host "Commit .github\release-notes-v$Version.md on its own; the artifact does not change."
