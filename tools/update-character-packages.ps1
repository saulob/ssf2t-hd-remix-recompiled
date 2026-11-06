<#
.SYNOPSIS
  Rewrites the sizes and hashes in src\character\character_packages.h from assets\characters, after the packages were
  regenerated on purpose. The executable and the packages stay matched only through that table.
#>
$ErrorActionPreference = 'Stop'

$Root = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'character-packages.ps1')
$Table = Join-Path $Root 'src\character\character_packages.h'
$Folder = Join-Path $Root 'assets\characters'

$text = [IO.File]::ReadAllText($Table)
foreach ($package in Get-CharacterPackages $Table) {
    $path = Join-Path $Folder $package.File
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing $path" }
    $size = (Get-Item -LiteralPath $path).Length
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    $text = $text.Replace("`"$($package.File)`", $($package.Size), `"$($package.Sha256)`"", "`"$($package.File)`", $size, `"$hash`"")
}
[IO.File]::WriteAllText($Table, $text, (New-Object System.Text.UTF8Encoding $false))
Write-Host "Updated $Table"
