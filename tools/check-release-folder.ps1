<#
.SYNOPSIS
  Checks a staged release folder: only the files the program needs, no game files, packages, player data, logs,
  debug output or developer paths. Reads the folder only; exits 1 and lists the problems when it is not clean.

.EXAMPLE
  .\tools\check-release-folder.ps1 dist\SSF2THDRemix-v0.1.0-windows-x64
#>
param(
    [Parameter(Mandatory)][string]$Folder
)
$ErrorActionPreference = 'Stop'

$Root = Split-Path $PSScriptRoot -Parent
$RequiredFiles = @('ssf2t_hd_remix.exe', 'trg-updater.exe', 'rexruntime.dll', 'rexgpu-xenos.dll', 'ssf2t_hd_remix.toml', 'README.txt', 'LICENSE')
$LicenseFiles = @('ReXGlue-LICENSE.txt', 'THIRD-PARTY-LICENSES.txt')
$AssetFiles = @('assets\launcher\header-art.png')
# Names that mean game data, a package, saves, settings, caches or logs.
$ForbiddenNames = @('default.xex', 'game', 'game-install-temp', 'game-install-backup', 'user_data', 'user_data.migrating',
    'content', 'cache', 'logs', 'settings.toml', 'settings.toml.bak', 'flog_x360.fpg', '584108A4', 'renderdoc', 'renderdoc.dll',
    'vulkan-1.dll')
$ForbiddenPatterns = @('*.xex', '*.fpg', '*.log', '*.dmp', '*.pdb', '*.xsh', '*.xpso', '*.bak', '*.tmp', '*.sav', 'VkLayer_*', 'renderdoc*',
    'TracyClient*', '*.pyc', '*.obj', '*.lib', '*.ilk', '*.orig', '*.rej', '*.rdc', 'Thumbs.db', 'desktop.ini', '.git*')
$PackageMagic = @('LIVE', 'PIRS', 'CON ', 'XEX2')
$Markers = @('DEBUG_SAULO')

$Latin1 = [Text.Encoding]::GetEncoding(28591)
function Get-Needles([string[]]$Text) {
    foreach ($item in $Text) {
        $item
        $Latin1.GetString([Text.Encoding]::Unicode.GetBytes($item))
    }
}
# The repository and profile folders in either slash style, as text or UTF-16.
$LocalPaths = @(Get-Needles @($Root, $Root.Replace('\', '/'), $env:USERPROFILE, $env:USERPROFILE.Replace('\', '/')) | Where-Object { $_ })
$MarkerNeedles = @(Get-Needles $Markers)
# Any user profile folder or Visual Studio repository folder; matched against the file text with the NUL bytes
# removed, so UTF-16 text is found too.
$LocalPathRegexes = @(
    '[A-Za-z]:[\\/]Users[\\/][^\\/\x00-\x1f"<>|*?]+',
    '[\\/]VisualStudio[\\/]repos[\\/]',
    '[A-Za-z]:[\\/]msys64'
) | ForEach-Object { [regex]::new($_, 'IgnoreCase') }

function Test-ReleaseFolder([string]$Dir) {
    $problems = New-Object System.Collections.Generic.List[string]
    if (-not (Test-Path -LiteralPath $Dir -PathType Container)) { throw "Not a folder: $Dir" }
    $base = (Resolve-Path -LiteralPath $Dir).Path.TrimEnd('\')
    foreach ($item in Get-ChildItem -LiteralPath $base -Recurse -Force) {
        $relative = $item.FullName.Substring($base.Length + 1)
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { $problems.Add("reparse point: $relative") }
        if ($ForbiddenNames -contains $item.Name) { $problems.Add("forbidden name: $relative") }
        foreach ($pattern in $ForbiddenPatterns) {
            if ($item.Name -like $pattern) { $problems.Add("forbidden file type: $relative") }
        }
        if (-not $item.PSIsContainer) {
            $bytes = [IO.File]::ReadAllBytes($item.FullName)
            if ($bytes.Length -ge 4 -and [Text.Encoding]::ASCII.GetString($bytes, 0, 4) -in $PackageMagic) {
                $problems.Add("Xbox 360 package or executable: $relative")
            }
            $text = $Latin1.GetString($bytes)
            foreach ($path in $LocalPaths) {
                if ($text.IndexOf($path, [StringComparison]::OrdinalIgnoreCase) -ge 0) { $problems.Add("local path in file: $relative"); break }
            }
            $stripped = $text.Replace([string][char]0, '')
            foreach ($regex in $LocalPathRegexes) {
                if ($regex.IsMatch($stripped)) { $problems.Add("user or repository path in file: $relative"); break }
            }
            foreach ($marker in $MarkerNeedles) {
                if ($text.IndexOf($marker, [StringComparison]::Ordinal) -ge 0) { $problems.Add("debug marker in file: $relative"); break }
            }
        }
        $parts = $relative -split '\\'
        if ($parts.Count -eq 1) {
            if ($item.PSIsContainer) { if (@('licenses', 'assets') -notcontains $item.Name) { $problems.Add("unexpected folder: $relative") } }
            elseif ($RequiredFiles -notcontains $item.Name) { $problems.Add("unexpected file: $relative") }
        } elseif ($parts[0] -eq 'licenses') {
            if ($parts.Count -gt 2 -or $item.PSIsContainer -or $LicenseFiles -notcontains $item.Name) { $problems.Add("unexpected item: $relative") }
        } elseif ($parts[0] -eq 'assets') {
            if ($item.PSIsContainer) {
                if ($relative -ne 'assets\launcher') { $problems.Add("unexpected folder: $relative") }
            } elseif ($AssetFiles -notcontains $relative) {
                $problems.Add("unexpected item: $relative")
            }
        } else {
            $problems.Add("unexpected item: $relative")
        }
    }
    foreach ($leaf in $RequiredFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $base $leaf) -PathType Leaf)) { $problems.Add("missing: $leaf") }
    }
    foreach ($leaf in $LicenseFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $base "licenses\$leaf") -PathType Leaf)) { $problems.Add("missing: licenses\$leaf") }
    }
    foreach ($asset in $AssetFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $base $asset) -PathType Leaf)) { $problems.Add("missing: $asset") }
    }
    return $problems
}

$problems = Test-ReleaseFolder $Folder
if ($problems.Count) {
    Write-Host ''
    Write-Host 'RELEASE CHECK FAILED' -ForegroundColor Red
    foreach ($problem in $problems) { Write-Host "  $problem" -ForegroundColor Red }
    exit 1
}
Write-Host "Release folder is clean: $Folder"
