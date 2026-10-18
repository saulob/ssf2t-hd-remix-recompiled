<#
.SYNOPSIS
  Extracts an Xbox 360 STFS (LIVE, PIRS or CON) package into an empty folder.
  The package must carry the Geometry Wars: Retro Evolved 2 title ID.

.EXAMPLE
  .\tools\extract_stfs.ps1 -Package 'C:\path\to\package' -OutDir game\extracted
#>
param(
    [Parameter(Mandatory = $true)][string]$Package,
    [Parameter(Mandatory = $true)][string]$OutDir,
    [uint32]$TitleId = 0x584108FF
)
$ErrorActionPreference = 'Stop'
Add-Type -Path (Join-Path $PSScriptRoot 'StfsExtract.cs')

$pkg = (Resolve-Path -LiteralPath $Package).Path
$found = [StfsExtract]::TitleId($pkg)
if ($found -ne $TitleId) { throw ('Package title ID 0x{0:X8} is not the expected 0x{1:X8}.' -f $found, $TitleId) }

$out = [IO.Path]::GetFullPath($OutDir)
if ((Test-Path -LiteralPath $out) -and (Get-ChildItem -LiteralPath $out -Force | Select-Object -First 1)) {
    throw "$out is not empty; extract into a new folder."
}
[StfsExtract]::Run($pkg, $out) | Out-Null
