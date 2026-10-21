<#
.SYNOPSIS
  Builds the Release configuration and packages the Windows release ZIP into dist\.
  Local only: nothing is published, tagged or uploaded.

.DESCRIPTION
  Builds into build-release\ (the normal build\ folder is not touched), checks that the runtime and GPU plugin are
  the Release files built from this branch's ReXGlue patches, stages exactly the files the program needs, checks the
  staging folder with check-release-folder.ps1, then writes dist\SSF2THDRemix-v<version>-windows-x64.zip and its
  .sha256 file. The ZIP is extracted again and checked, so the artifact itself is verified, not only the staging folder.
  The version is the project VERSION in CMakeLists.txt.

.EXAMPLE
  .\tools\package-release.ps1              # incremental Release build, then package
  .\tools\package-release.ps1 -Clean       # clean Release build first
  .\tools\package-release.ps1 -SkipBuild   # package the existing build-release\ output
  .\tools\package-release.ps1 -VerifyOnly dist\SSF2THDRemix-v0.1.0-windows-x64.zip   # check a staged folder or a ZIP
#>
param(
    [switch]$Clean,
    [switch]$SkipBuild,
    [string]$VerifyOnly  # only check a staged folder or a release ZIP, then exit
)
$ErrorActionPreference = 'Stop'

$Root = Split-Path $PSScriptRoot -Parent
$ReleaseBuild = Join-Path $Root 'build-release'
$Packaging = Join-Path $Root 'packaging'
$SdkRoot = Join-Path $Root 'tools\rexglue\win-amd64'
. (Join-Path $PSScriptRoot 'rexruntime-patch.ps1')

$Utf8NoBom = New-Object System.Text.UTF8Encoding $false
$ProgramFiles = @('ssf2t_hd_remix.exe', 'trg-updater.exe', 'rexruntime.dll', 'rexgpu-xenos.dll', 'ssf2t_hd_remix.toml')
$LicenseFiles = @('ReXGlue-LICENSE.txt', 'THIRD-PARTY-LICENSES.txt')
$AssetFiles = @('assets\launcher\header-art.png')
$CheckFolder = Join-Path $PSScriptRoot 'check-release-folder.ps1'
$ZipTimestamp = [DateTimeOffset]::new(2026, 1, 1, 0, 0, 0, [TimeSpan]::Zero)

function Write-Problems($problems) {
    Write-Host ''
    Write-Host 'RELEASE CHECK FAILED' -ForegroundColor Red
    foreach ($problem in $problems) { Write-Host "  $problem" -ForegroundColor Red }
}

function Invoke-FolderCheck([string]$Folder) {
    $global:LASTEXITCODE = 0
    & $CheckFolder -Folder $Folder
    return $LASTEXITCODE -eq 0
}

function Expand-ReleaseZip([string]$Zip) {
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $target = Join-Path ([IO.Path]::GetTempPath()) ('ssf2t_release_zip_' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory $target | Out-Null
    $archive = [IO.Compression.ZipFile]::OpenRead($Zip)
    try {
        foreach ($entry in $archive.Entries) {
            if ($entry.FullName -match '^[\\/]|\.\.|:|\\') { throw "The ZIP holds an unsafe entry name: $($entry.FullName)" }
            if (-not $entry.Name) { throw "The ZIP holds a folder entry: $($entry.FullName)" }
            $path = Join-Path $target $entry.FullName
            New-Item -ItemType Directory -Force (Split-Path $path) | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $path)
        }
    } finally { $archive.Dispose() }
    return $target
}

if ($VerifyOnly) {
    $path = (Resolve-Path -LiteralPath $VerifyOnly).Path
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        $folder = Expand-ReleaseZip $path
        try { $ok = Invoke-FolderCheck $folder } finally { Remove-Item -LiteralPath $folder -Recurse -Force -ErrorAction SilentlyContinue }
    } else {
        $ok = Invoke-FolderCheck $path
    }
    if (-not $ok) { exit 1 }
    return
}

$cmake = Get-Content (Join-Path $Root 'CMakeLists.txt') -Raw
if ($cmake -notmatch '(?m)^\s*project\(\s*ssf2t_hd_remix\s+VERSION\s+(\d+\.\d+\.\d+)') { throw 'Project VERSION not found in CMakeLists.txt' }
$Version = $Matches[1]
$Name = "SSF2THDRemix-v$Version-windows-x64"
$Dist = Join-Path $Root 'dist'
$Stage = Join-Path $Dist $Name
$Zip = Join-Path $Dist "$Name.zip"
$ShaFile = Join-Path $Dist "$Name.sha256"

Write-Host ''
Write-Host "Super Street Fighter II Turbo HD Remix PC port $Version - Windows release package (local only)"

if (-not $SkipBuild) {
    $build = @{ Config = 'Release'; BuildDir = $ReleaseBuild }
    if ($Clean) { $build.Clean = $true }
    & (Join-Path $PSScriptRoot 'build.ps1') @build
}

# tools\rexglue must hold the Release runtime and GPU plugin built from this branch's patches, and the build must use them.
$runtimeProblems = @(Test-PatchedRuntime $SdkRoot) + @(Test-StagedRuntime $SdkRoot $ReleaseBuild 'Release')
if ($runtimeProblems.Count) { Write-Problems $runtimeProblems; throw 'tools\rexglue or the release build does not hold the runtime and GPU plugin this branch needs (build without -SkipBuild)' }
$cache = Join-Path $ReleaseBuild 'CMakeCache.txt'
if (-not (Select-String -Path $cache -SimpleMatch 'CMAKE_BUILD_TYPE:STRING=Release' -Quiet)) { throw "$ReleaseBuild is not a Release build" }
$stamp = Read-RuntimeStamp $SdkRoot

foreach ($leaf in $ProgramFiles) {
    if (-not (Test-Path (Join-Path $ReleaseBuild $leaf))) { throw "Release build output is missing $leaf (run without -SkipBuild)" }
}
if ((Get-Item (Join-Path $ReleaseBuild 'ssf2t_hd_remix.exe')).Length -lt 1MB) { throw 'ssf2t_hd_remix.exe is implausibly small' }

New-Item -ItemType Directory -Force $Dist | Out-Null
if (Test-Path -LiteralPath $Stage) { Remove-Item -LiteralPath $Stage -Recurse -Force }
New-Item -ItemType Directory $Stage, (Join-Path $Stage 'licenses') | Out-Null

foreach ($leaf in $ProgramFiles) { Copy-Item (Join-Path $ReleaseBuild $leaf) $Stage }
$AssetDir = Join-Path $Stage 'assets\launcher'
New-Item -ItemType Directory -Force $AssetDir | Out-Null
foreach ($asset in $AssetFiles) {
    Copy-Item -LiteralPath (Join-Path $Root $asset) $AssetDir
}
foreach ($leaf in @('rexruntime.dll', 'rexgpu-xenos.dll')) {
    $staged = Get-FileSha256 (Join-Path $Stage $leaf)
    if ($staged -ne (Get-FileSha256 (Join-Path $SdkRoot "bin\$leaf"))) { throw "$leaf is not the file installed in tools\rexglue" }
    if ($staged -ne $stamp.Hashes["bin/$leaf"]) { throw "$leaf does not match the hash recorded in $RuntimeStampName" }
}
$RootLicense = Join-Path $Root 'LICENSE'
if (-not (Test-Path -LiteralPath $RootLicense -PathType Leaf)) { throw "Missing project license: $RootLicense" }
Copy-Item -LiteralPath $RootLicense $Stage
foreach ($leaf in $LicenseFiles) {
    $source = Join-Path $Packaging "licenses\$leaf"
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing license file: $source" }
    Copy-Item -LiteralPath $source (Join-Path $Stage 'licenses')
}
$readme = (Get-Content (Join-Path $Packaging 'README.txt') -Raw).Replace('@VERSION@', $Version) -replace "\r?\n", "`r`n"
[IO.File]::WriteAllText((Join-Path $Stage 'README.txt'), $readme, $Utf8NoBom)

# Application identity: the version and name in the executable, the PerMonitorV2 manifest and no debug runtime.
$exe = Join-Path $Stage 'ssf2t_hd_remix.exe'
$info = (Get-Item $exe).VersionInfo
$identity = @()
if ($info.ProductVersion -ne $Version -or $info.FileVersion -ne $Version) { $identity += "ssf2t_hd_remix.exe reports version $($info.FileVersion) / $($info.ProductVersion), not $Version" }
if ($info.ProductName -ne 'Super Street Fighter II Turbo HD Remix') { $identity += "ssf2t_hd_remix.exe product name is '$($info.ProductName)'" }
$exeText = [Text.Encoding]::GetEncoding(28591).GetString([IO.File]::ReadAllBytes($exe))
if ($exeText.IndexOf('PerMonitorV2', [StringComparison]::Ordinal) -lt 0) { $identity += 'ssf2t_hd_remix.exe has no PerMonitorV2 manifest' }
foreach ($debugDll in 'ucrtbased.dll', 'vcruntime140d.dll', 'msvcp140d.dll', 'rexruntimed.dll', 'rexruntimerd.dll', 'rexgpu-xenosd.dll', 'rexgpu-xenosrd.dll') {
    if ($exeText.IndexOf($debugDll, [StringComparison]::OrdinalIgnoreCase) -ge 0) { $identity += "ssf2t_hd_remix.exe refers to $debugDll" }
}
if ($identity.Count) { Write-Problems $identity; throw 'The executable does not carry the release identity' }

# The update helper runs alone from a temporary folder, so it must not need any runtime library from the installation.
$helperText = [Text.Encoding]::GetEncoding(28591).GetString([IO.File]::ReadAllBytes((Join-Path $Stage 'trg-updater.exe')))
$helper = @()
foreach ($dependency in 'vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll', 'ucrtbase.dll', 'libc++.dll', 'rexruntime.dll', 'ssf2t_hd_remix.exe') {
    if ($helperText.IndexOf($dependency, [StringComparison]::OrdinalIgnoreCase) -ge 0) { $helper += "trg-updater.exe refers to $dependency" }
}
if ($helper.Count) { Write-Problems $helper; throw 'trg-updater.exe is not self-contained' }

if (-not (Invoke-FolderCheck $Stage)) { throw 'The staging folder holds files that must not be released' }

Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
if (Test-Path -LiteralPath $Zip) { Remove-Item -LiteralPath $Zip }
$files = Get-ChildItem -LiteralPath $Stage -Recurse -File | Sort-Object { $_.FullName.Substring($Stage.Length + 1).Replace('\', '/') }
$stream = [IO.File]::Open($Zip, [IO.FileMode]::CreateNew)
try {
    $archive = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in $files) {
            $entry = $archive.CreateEntry($file.FullName.Substring($Stage.Length + 1).Replace('\', '/'), [IO.Compression.CompressionLevel]::Optimal)
            $entry.LastWriteTime = $ZipTimestamp
            $in = [IO.File]::OpenRead($file.FullName)
            $out = $entry.Open()
            try { $in.CopyTo($out) } finally { $out.Dispose(); $in.Dispose() }
        }
    } finally { $archive.Dispose() }
} finally { $stream.Dispose() }

$entries = @($files | ForEach-Object { $_.FullName.Substring($Stage.Length + 1).Replace('\', '/') })
$extracted = Expand-ReleaseZip $Zip
try {
    $listed = @(Get-ChildItem -LiteralPath $extracted -Recurse -File | ForEach-Object { $_.FullName.Substring($extracted.Length + 1).Replace('\', '/') } | Sort-Object)
    if (Compare-Object $entries $listed) { throw 'The ZIP does not hold exactly the staged files' }
    foreach ($entry in $entries) {
        $relative = $entry.Replace('/', '\')
        if ((Get-FileSha256 (Join-Path $Stage $relative)) -ne (Get-FileSha256 (Join-Path $extracted $relative))) { throw "$entry in the ZIP differs from the staged file" }
    }
    if (-not (Invoke-FolderCheck $extracted)) { throw 'The ZIP holds files that must not be released' }
} finally { Remove-Item -LiteralPath $extracted -Recurse -Force -ErrorAction SilentlyContinue }

$hash = Get-FileSha256 $Zip
[IO.File]::WriteAllText($ShaFile, "$hash  $Name.zip`n", $Utf8NoBom)

Write-Host ''
Write-Host '===================================================='
Write-Host ' RELEASE PACKAGE READY (nothing was published)'
Write-Host '===================================================='
Write-Host "  ZIP:       $Zip ($([math]::Round((Get-Item $Zip).Length / 1MB, 1)) MB)"
Write-Host "  SHA-256:   $hash"
Write-Host "  Hash file: $ShaFile"
Write-Host "  Runtime:   ReXGlue $($stamp.Values['sdk_version']) ($($stamp.Values['sdk_commit'].Substring(0, 12))), patch fingerprint $($stamp.Values['inputs_sha256'].Substring(0, 16))"
Write-Host '  Contents:'
foreach ($entry in $entries) { Write-Host "    $entry" }
Write-Host ''
