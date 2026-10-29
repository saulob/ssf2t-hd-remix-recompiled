# Shared by codegen.ps1 and build.ps1: the two recompiled guest images, their input hashes, the symbol prefix step
# and the currency check of generated\<variant>.
#   base  default.xex alone                    game version 0.0.1.3  symbols base013_*
#   tu    default.xex with default.xexp        game version 0.0.2.3  symbols tu023_*

$GuestRoot = Split-Path $PSScriptRoot -Parent
$GuestSdkVersion = '0.10.0'
$GuestXexSha256 = '6E1C2F9ED7D1102A311A5F5626563527EDA5F85B2FBE8DB8405B866DED3F31EE'
$GuestXexpSha256 = 'A2E8E66B482C5D3A0D3A5D3F67A3CECB7A12CE1F6189196694FD3E765910606C'
$GuestStampName = 'ssf2t-guest.stamp'

$GuestVariants = @(
    @{ Name = 'base'; Prefix = 'base013_'; Version = '0.0.1.3'; Update = $false },
    @{ Name = 'tu'; Prefix = 'tu023_'; Version = '0.0.2.3'; Update = $true }
)

# Everything the generator names that two images in one executable would define twice. Imports (__imp__X) stay as is.
$GuestSymbolPatterns = @(
    '(?<![A-Za-z0-9])sub_[0-9A-F]{8}\b',
    '(?<![A-Za-z0-9_])(?:xstart|PPCImageConfig|PPCFuncMappings|ssf2t_hd_remix_RegisterFunctions|__(?:save|rest)(?:gprlr|fpr|vmx)_[0-9]+)\b'
)

function Get-GuestVariant([string]$Name) {
    $variant = $GuestVariants | Where-Object { $_.Name -eq $Name }
    if (-not $variant) { throw "Unknown guest image '$Name'" }
    return $variant
}

function Get-GuestPaths($Variant) {
    $config = Join-Path $GuestRoot "config\guest\$($Variant.Name)"
    $work = Join-Path $GuestRoot "out\codegen\$($Variant.Name)"
    return @{
        Config = $config
        ManifestTemplate = Join-Path $config 'manifest.template.toml'
        Manifest = Join-Path $work 'manifest.toml'
        Work = $work
        Input = Join-Path $work 'input'
        Raw = Join-Path $work 'raw'
        Output = Join-Path $GuestRoot "generated\$($Variant.Name)"
    }
}

function Get-TextSha256([string]$Text) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Text))) -replace '-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}

# Line endings are normalized so the same sources hash alike on Windows and Linux.
function Get-ConfigSha256([string]$Path) {
    return Get-TextSha256 ([IO.File]::ReadAllText($Path).Replace("`r", ''))
}

# What the generated code depends on. Equal text means generated\<variant> was written for the current sources.
function Get-GuestStamp($Variant) {
    $paths = Get-GuestPaths $Variant
    $lines = @(
        "sdk=$GuestSdkVersion",
        "prefix=$($Variant.Prefix)",
        "xex=$($GuestXexSha256.ToLowerInvariant())",
        "xexp=$(if ($Variant.Update) { $GuestXexpSha256.ToLowerInvariant() } else { 'none' })",
        "manifest=$(Get-ConfigSha256 $paths.ManifestTemplate)",
        "overrides=$(Get-ConfigSha256 (Join-Path $paths.Config 'overrides.toml'))",
        "discovered=$(Get-ConfigSha256 (Join-Path $paths.Config 'discovered-functions.toml'))",
        "prefix-rules=$(Get-TextSha256 ($GuestSymbolPatterns -join "`n"))"
    )
    return ($lines -join "`n") + "`n"
}

# The manifest names every path absolutely: ReXGlue resolves relative ones against the manifest's own folder.
function Write-GuestManifest($Variant) {
    $paths = Get-GuestPaths $Variant
    $rootForToml = $GuestRoot.Replace('\', '/')
    $text = [IO.File]::ReadAllText($paths.ManifestTemplate).Replace('@ROOT@', $rootForToml)
    New-Item -ItemType Directory -Force $paths.Work | Out-Null
    [IO.File]::WriteAllText($paths.Manifest, $text, (New-Object Text.UTF8Encoding $false))
}

# The staged default.xex and the one the manifest hands to ReXGlue must be the same existing file.
function Assert-GuestStaging($Variant) {
    $paths = Get-GuestPaths $Variant
    $staged = [IO.Path]::GetFullPath((Join-Path $paths.Input 'default.xex'))
    if (-not (Test-Path -LiteralPath $staged)) { throw "Staged $staged is missing for the $($Variant.Name) image" }
    $manifest = [IO.File]::ReadAllText($paths.Manifest)
    if ($manifest -notmatch '(?m)^file_path\s*=\s*"([^"]+)"') { throw "$($paths.Manifest) has no entrypoint file_path" }
    $entrypoint = [IO.Path]::GetFullPath($Matches[1])
    if ($entrypoint -ne $staged) { throw "The $($Variant.Name) manifest names $entrypoint but codegen staged $staged" }
}

function Test-GuestCurrent($Variant) {
    $paths = Get-GuestPaths $Variant
    $stamp = Join-Path $paths.Output $GuestStampName
    if (-not ((Test-Path (Join-Path $paths.Output 'sources.cmake')) -and (Test-Path $stamp))) { return $false }
    if (@(Get-MissingGuestFiles $paths.Output).Count) { return $false }
    return ([IO.File]::ReadAllText($stamp).Replace("`r", '')) -eq (Get-GuestStamp $Variant)
}

# Copies the generated sources, headers and sources.cmake from $SourceDir to $TargetDir with every guest symbol of
# the image prefixed, and fails if one slipped through.
function Add-GuestSymbolPrefix([string]$SourceDir, [string]$TargetDir, [string]$Prefix) {
    New-Item -ItemType Directory -Force $TargetDir | Out-Null
    $utf8 = New-Object Text.UTF8Encoding $false
    $replacements = @($GuestSymbolPatterns | ForEach-Object { New-Object Text.RegularExpressions.Regex $_, 'Compiled, CultureInvariant' })
    $leftover = New-Object Text.RegularExpressions.Regex ('^DEFINE_REX_FUNC\((?!' + [regex]::Escape($Prefix) + ')'), 'Compiled, CultureInvariant, Multiline'
    $files = Get-ChildItem -LiteralPath $SourceDir -File | Where-Object { $_.Extension -in '.h', '.cpp', '.cmake' }
    foreach ($file in $files) {
        $target = Join-Path $TargetDir $file.Name
        if ($file.Extension -eq '.cmake') { Copy-Item -LiteralPath $file.FullName $target -Force; continue }
        $text = [IO.File]::ReadAllText($file.FullName, $utf8)
        foreach ($rule in $replacements) { $text = $rule.Replace($text, "$Prefix`$0") }
        if ($leftover.IsMatch($text)) { throw "$($file.Name): a guest function kept its unprefixed name" }
        [IO.File]::WriteAllText($target, $text, $utf8)
    }
    return @($files).Count
}

# Package metadata decides, never the file or folder name: LIVE/PIRS/CON, this title, Title Update content type, and
# a default.xexp whose SHA-256 is the supported one. Returns the extracted default.xexp inside $TempDir, or $null.
function Find-TitleUpdatePackageXexp([string]$SearchRoot, [string]$TempDir) {
    if (-not (Test-Path -LiteralPath $SearchRoot)) { return $null }
    if (-not ('StfsExtract' -as [type])) { Add-Type -Path (Join-Path $PSScriptRoot 'StfsExtract.cs') }
    $skip = @('extracted', 'analysis') | ForEach-Object { Join-Path $SearchRoot $_ }
    $files = Get-ChildItem -LiteralPath $SearchRoot -Recurse -File -Depth 4 -ErrorAction SilentlyContinue |
        Where-Object { $path = $_.FullName; $_.Length -gt 0x1000 -and $_.Length -lt 64MB -and -not ($skip | Where-Object { $path.StartsWith($_, [StringComparison]::OrdinalIgnoreCase) }) }
    foreach ($file in $files) {
        if ([StfsExtract]::TitleId($file.FullName) -ne 0x584108A4 -or [StfsExtract]::ContentType($file.FullName) -ne 0x000B0000) { continue }
        Remove-Item -LiteralPath $TempDir -Recurse -Force -ErrorAction SilentlyContinue
        try { [StfsExtract]::Run($file.FullName, $TempDir) | Out-Null } catch { continue }
        $patch = Join-Path $TempDir 'default.xexp'
        if ((Test-Path -LiteralPath $patch) -and (Get-FileHash $patch -Algorithm SHA256).Hash -eq $GuestXexpSha256) { return $patch }
    }
    return $null
}

# The supported default.xexp for generating the Title Update image: the installed one when present, otherwise the
# one inside a local Title Update package. Never installs anything into game\extracted.
function Find-TitleUpdateSource([string]$Extracted, [string]$SearchRoot, [string]$TempDir) {
    $installed = Join-Path $Extracted 'default.xexp'
    if ((Test-Path -LiteralPath $installed) -and (Get-FileHash $installed -Algorithm SHA256).Hash -eq $GuestXexpSha256) { return $installed }
    return Find-TitleUpdatePackageXexp $SearchRoot $TempDir
}

# Files a generated image needs: the fixed ones, every source its sources.cmake lists, and the declarations of each
# recompiled part. Returns the names that are missing.
function Get-MissingGuestFiles([string]$Dir) {
    $required = @('sources.cmake', 'ssf2t_hd_remix_pch.h', 'ssf2t_hd_remix_init.h', 'ssf2t_hd_remix_funcs.h',
                  'ssf2t_hd_remix_init.cpp', 'ssf2t_hd_remix_register.cpp')
    $list = Join-Path $Dir 'sources.cmake'
    if (Test-Path -LiteralPath $list) {
        foreach ($match in [regex]::Matches([IO.File]::ReadAllText($list), '[A-Za-z0-9_.]+\.cpp')) {
            $required += $match.Value
            if ($match.Value -match '^(.+)_recomp\.(\d+)\.cpp$') { $required += "$($Matches[1])_funcs.$($Matches[2]).h" }
        }
    }
    return @($required | Sort-Object -Unique | Where-Object { -not (Test-Path -LiteralPath (Join-Path $Dir $_)) })
}

# Builds the prefixed image next to $Output and swaps it in only when it is complete; the stamp is written last.
# On any failure the previous $Output is left as it was and no stamp exists for the new tree.
function Publish-GuestImage($Variant, [string]$Raw, [string]$Output) {
    $staging = "$Output.new"
    $previous = "$Output.old"
    Remove-Item -LiteralPath $staging, $previous -Recurse -Force -ErrorAction SilentlyContinue
    try {
        $count = Add-GuestSymbolPrefix $Raw $staging $Variant.Prefix
        $missing = @(Get-MissingGuestFiles $staging)
        if ($missing.Count) { throw "generated $($Variant.Name) image is missing $($missing.Count) file(s), e.g. $($missing[0])" }
        [IO.File]::WriteAllText((Join-Path $staging $GuestStampName), (Get-GuestStamp $Variant))
    } catch {
        Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
        throw
    }
    if (Test-Path -LiteralPath $Output) { Move-Item -LiteralPath $Output -Destination $previous }
    try { Move-Item -LiteralPath $staging -Destination $Output }
    catch {
        if (Test-Path -LiteralPath $previous) { Move-Item -LiteralPath $previous -Destination $Output }
        throw
    }
    Remove-Item -LiteralPath $previous -Recurse -Force -ErrorAction SilentlyContinue
    return $count
}
