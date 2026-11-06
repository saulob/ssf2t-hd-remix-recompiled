<#
.SYNOPSIS
  The character package list and checks shared by build.ps1, package-release.ps1 and check-release-folder.ps1. Dot-source it;
  src\character\character_packages.h is the list.
#>

$CharacterPackageLine = '^\s*\{"(?<file>[a-z0-9_]+\.bin)", (?<size>\d+), "(?<sha256>[0-9a-f]{64})", PackageOwner::(?<owner>\w+), PackageNeed::(?<need>\w+)\},'

function Get-CharacterPackages([string]$Table) {
    $packages = foreach ($line in Get-Content -LiteralPath $Table) {
        if ($line -match $CharacterPackageLine) {
            [pscustomobject]@{ File = $Matches.file; Size = [long]$Matches.size; Sha256 = $Matches.sha256; Owner = $Matches.owner; Need = $Matches.need }
        }
    }
    if (-not $packages) { throw "No character packages found in $Table" }
    return @($packages)
}

# One line per package that is missing or is not the file the table names.
function Test-CharacterPackages([string]$Folder, $Packages) {
    foreach ($package in $Packages) {
        $path = Join-Path $Folder $package.File
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            "missing character package: $($package.File)"
        } elseif ((Get-Item -LiteralPath $path).Length -ne $package.Size -or
            (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $package.Sha256) {
            "character package differs from character_packages.h: $($package.File)"
        }
    }
}

# Copies the listed packages and nothing else; whatever else sits in the source folder stays out.
function Copy-CharacterPackages([string]$From, [string]$To, $Packages) {
    $problems = @(Test-CharacterPackages $From $Packages)
    if ($problems.Count) { throw "Character packages in ${From}: $($problems -join '; ')" }
    New-Item -ItemType Directory -Force $To | Out-Null
    foreach ($package in $Packages) { Copy-Item -LiteralPath (Join-Path $From $package.File) $To }
}
