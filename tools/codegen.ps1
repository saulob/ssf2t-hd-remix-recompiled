<#
.SYNOPSIS
  Downloads the pinned ReXGlue SDK and generates the recompiled C++ for the supported game versions from
  game\extracted:
    generated\base  default.xex alone                   game version 0.0.1.3, symbols base013_*
    generated\tu    default.xex with default.xexp       game version 0.0.2.3, symbols tu023_*
  The base image is always generated. The Title Update image is generated only when a supported Title Update is available: a default.xexp in game\extracted,
  or the default.xexp inside a Title Update package found under game\ (extracted to staging only). Without one the build
  holds the base game alone, which is a complete build.

.DESCRIPTION
  An image that is already current is skipped. A Title Update image that is out of date and cannot be regenerated
  without default.xexp is removed, so it cannot be built stale. ReXGlue applies a default.xexp that sits beside
  default.xex, so each image is generated from a staged copy of its inputs in out\codegen; game\extracted is only read.

.EXAMPLE
  .\tools\codegen.ps1                   # base, plus the Title Update image when a Title Update source is found
  .\tools\codegen.ps1 -Variant tu       # the Title Update image only; needs a Title Update source
  .\tools\codegen.ps1 -Check            # report only; exit 1 when a present image is out of date
  .\tools\codegen.ps1 -Force            # regenerate
#>
param(
    [ValidateSet('base', 'tu')]
    [string[]]$Variant = @('base', 'tu'),
    [switch]$Force,
    [switch]$Check
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'guest-code.ps1')

$root = $GuestRoot
$sdkZipSha256 = 'AD4485DA853FBE932E9F5DBB643F9B5FE85DC73775EBBAD4DF6879FE6DF0C11B'
$sdkDir = Join-Path $root 'tools\rexglue'
$rexglue = Join-Path $sdkDir 'win-amd64\bin\rexglue.exe'
$xex = Join-Path $root 'game\extracted\default.xex'
$xexp = Join-Path $root 'game\extracted\default.xexp'
$titleUpdateOptional = -not $PSBoundParameters.ContainsKey('Variant')

if ($Check) {
    $stale = @($Variant | Where-Object {
        $item = Get-GuestVariant $_
        $present = Test-Path (Join-Path (Get-GuestPaths $item).Output $GuestStampName)
        ($present -or -not ($item.Update -and $titleUpdateOptional)) -and -not (Test-GuestCurrent $item)
    })
    foreach ($name in $Variant) {
        $present = Test-Path (Join-Path (Get-GuestPaths (Get-GuestVariant $name)).Output $GuestStampName)
        $state = if ($stale -contains $name) { 'out of date' } elseif ($present) { 'current' } else { 'not generated (optional)' }
        Write-Host ("generated\{0}: {1}" -f $name, $state)
    }
    if ($stale.Count) { exit 1 }
    return
}

if (-not (Test-Path $rexglue)) {
    $zip = Join-Path $sdkDir "rexglue-sdk-$GuestSdkVersion-win-amd64.zip"
    if (-not (Test-Path $zip)) {
        New-Item -ItemType Directory -Force $sdkDir | Out-Null
        $url = "https://github.com/rexglue/rexglue-sdk/releases/download/v$GuestSdkVersion/rexglue-sdk-$GuestSdkVersion-win-amd64.zip"
        Write-Host "Downloading ReXGlue SDK $GuestSdkVersion..."
        Invoke-WebRequest -Uri $url -OutFile $zip
    }
    if ((Get-FileHash $zip -Algorithm SHA256).Hash -ne $sdkZipSha256) { throw "$zip is not the official ReXGlue $GuestSdkVersion release" }
    Expand-Archive -Path $zip -DestinationPath $sdkDir -Force
}
$installed = "$(& $rexglue --version)".Trim()
if ($installed -ne $GuestSdkVersion) { throw "tools\rexglue has ReXGlue $installed; expected $GuestSdkVersion (delete tools\rexglue to re-download)" }

function Assert-GameFile([string]$Path, [string]$Sha256, [string]$What, [string]$Fix) {
    if (-not (Test-Path $Path)) { throw "$What is needed to generate this image but $Path is missing. $Fix" }
    $hash = (Get-FileHash $Path -Algorithm SHA256).Hash
    if ($hash -ne $Sha256) { throw "Unsupported $Path (SHA-256 $hash). Expected $Sha256 ($What)." }
}

$searchRoot = Join-Path $root 'game'
$tuTemp = Join-Path $root 'out\codegen\tu\package'

function Build-GuestImage($Item, $PatchSource) {
    $paths = Get-GuestPaths $Item
    Assert-GameFile $xex $GuestXexSha256 'default.xex (Xbox Live Arcade 0.0.1.3)' 'Extract the XBLA package into game\extracted with tools\extract_stfs.ps1.'

    New-Item -ItemType Directory -Force $paths.Input | Out-Null
    Copy-Item -LiteralPath $xex -Destination (Join-Path $paths.Input 'default.xex') -Force
    $stagedPatch = Join-Path $paths.Input 'default.xexp'
    if ($Item.Update) { Copy-Item -LiteralPath $PatchSource -Destination $stagedPatch -Force }
    else { Remove-Item -LiteralPath $stagedPatch -Force -ErrorAction SilentlyContinue }
    Write-GuestManifest $Item
    Assert-GuestStaging $Item

    $log = Join-Path $paths.Work 'codegen.log'
    $extra = if ($Force -or @(Get-MissingGuestFiles $paths.Raw).Count) { @('--ignore-stamp') } else { @() }
    Push-Location $root
    try {
        # rexglue logs to stderr; in Windows PowerShell 5.1 that would trip 'Stop', so judge by exit code.
        $ErrorActionPreference = 'Continue'
        $timer = [Diagnostics.Stopwatch]::StartNew()
        & $rexglue codegen @extra $paths.Manifest 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $log
        $exitCode = $LASTEXITCODE
        Write-Host ("codegen exit code {0} after {1:N1} s (log: {2})" -f $exitCode, $timer.Elapsed.TotalSeconds, $log)
        if ($exitCode -ne 0) { throw "codegen failed for the $($Item.Name) image ($exitCode)" }
    } finally { Pop-Location; $ErrorActionPreference = 'Stop' }

    $rawMissing = @(Get-MissingGuestFiles $paths.Raw)
    if ($rawMissing.Count) { throw "ReXGlue output for the $($Item.Name) image is missing $($rawMissing[0])" }
    Write-Host "Prefixing guest symbols with $($Item.Prefix)..."
    $count = Publish-GuestImage $Item $paths.Raw $paths.Output
    Write-Host "generated\$($Item.Name): $count files, game version $($Item.Version)"
}

foreach ($name in $Variant) {
    $item = Get-GuestVariant $name
    $existing = (Get-GuestPaths $item).Output
    if (-not $Force -and (Test-Path (Join-Path $existing $GuestStampName)) -and @(Get-MissingGuestFiles $existing).Count) {
        Write-Host "generated\$name is incomplete; regenerating."
    }
    if (-not $Force -and (Test-GuestCurrent $item)) {
        Write-Host "generated\$name is current (game version $($item.Version))."
        continue
    }
    $patchSource = $null
    if ($item.Update) {
        $patchSource = Find-TitleUpdateSource (Join-Path $root 'game\extracted') $searchRoot $tuTemp
        if (-not $patchSource) {
            if (-not $titleUpdateOptional) { throw 'No supported Title Update was found to generate the 0.0.2.3 image: neither game\extracted\default.xexp nor a Title Update package under game.' }
            $stale = (Get-GuestPaths $item).Output
            if (Test-Path $stale) {
                Remove-Item -LiteralPath (Join-Path $stale $GuestStampName) -Force -ErrorAction SilentlyContinue
                Remove-Item -LiteralPath $stale -Recurse -Force
            }
            Write-Host 'Title Update not present: building base game support only (0.0.1.3)'
            continue
        }
    }
    try { Build-GuestImage $item $patchSource }
    finally { Remove-Item -LiteralPath $tuTemp -Recurse -Force -ErrorAction SilentlyContinue }
}

# The single-image layout this replaces.
$old = Join-Path $root 'generated\default'
if ((Test-Path (Join-Path $old 'codegen.stamp')) -and (Test-GuestCurrent (Get-GuestVariant 'base'))) {
    Remove-Item -LiteralPath $old, (Join-Path $root 'generated\rexglue.cmake'), (Join-Path $root 'generated\codegen.log') -Recurse -Force -ErrorAction SilentlyContinue
}
