<#
.SYNOPSIS
  Extracts an Xbox 360 STFS (LIVE, PIRS or CON) package into an empty folder.
  The package must carry the Super Street Fighter II Turbo HD Remix title ID (0x584108A4).
  A Title Update package only adds its default.xexp, so it can go into the folder that holds the game.

.EXAMPLE
  .\tools\extract_stfs.ps1 -Package 'C:\path\to\package' -OutDir game\extracted
  .\tools\extract_stfs.ps1 -Package 'C:\path\to\title-update' -OutDir game\extracted
#>
param(
    [Parameter(Mandatory = $true)][string]$Package,
    [Parameter(Mandatory = $true)][string]$OutDir,
    [uint32]$TitleId = 0x584108A4
)
$ErrorActionPreference = 'Stop'
Add-Type -Path (Join-Path $PSScriptRoot 'StfsExtract.cs')

$pkg = (Resolve-Path -LiteralPath $Package).Path
$found = [StfsExtract]::TitleId($pkg)
if ($found -ne $TitleId) { throw ('Package title ID 0x{0:X8} is not the expected 0x{1:X8}.' -f $found, $TitleId) }

$out = [IO.Path]::GetFullPath($OutDir)
if ([StfsExtract]::ContentType($pkg) -eq 0x000B0000) {
    $temp = Join-Path ([IO.Path]::GetTempPath()) ('ssf2t_title_update_' + [Guid]::NewGuid().ToString('N'))
    try {
        [StfsExtract]::Run($pkg, $temp) | Out-Null
        $patch = Join-Path $temp 'default.xexp'
        if (-not (Test-Path -LiteralPath $patch)) { throw 'The Title Update package has no default.xexp.' }
        New-Item -ItemType Directory -Force $out | Out-Null
        Copy-Item -LiteralPath $patch -Destination (Join-Path $out 'default.xexp') -Force
    } finally { Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue }
    return
}
if ((Test-Path -LiteralPath $out) -and (Get-ChildItem -LiteralPath $out -Force | Select-Object -First 1)) {
    throw "$out is not empty; extract into a new folder."
}
[StfsExtract]::Run($pkg, $out) | Out-Null
