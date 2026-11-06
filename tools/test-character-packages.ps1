<#
.SYNOPSIS
  Checks the character package list against the runtime consumers, the release scripts and the documents, and the
  allow-list staging on a fixture. Scratch folders only; nothing is built, launched or published.
#>
$ErrorActionPreference = 'Stop'
$Root = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'character-packages.ps1')

$failures = New-Object System.Collections.Generic.List[string]
function Check([bool]$Condition, [string]$What) { if (-not $Condition) { $failures.Add($What) } }
function Read-Tracked([string]$Relative) { [IO.File]::ReadAllText((Join-Path $Root $Relative)) }

$Packages = Get-CharacterPackages (Join-Path $Root 'src\character\character_packages.h')
$Names = @($Packages.File | Sort-Object)

Check ($Names.Count -eq 16) "the table lists $($Names.Count) packages, not 16"
Check (@($Names | Sort-Object -Unique).Count -eq $Names.Count) 'the table repeats a package'
Check (@($Packages | Where-Object { $_.Owner -notin 'kBoth', 'kEvilRyu', 'kViolentKen' -or $_.Need -notin 'kRequired', 'kOptional' }).Count -eq 0) 'the table has an unknown owner or need'
Check (($Packages | Where-Object File -eq 'violent_ken_gameplay.bin').Need -eq 'kRequired') 'violent_ken_gameplay.bin is not required'

# Every package a consumer names is in the table, and every table entry has a consumer.
$consumers = Get-ChildItem (Join-Path $Root 'src\character') -File | Where-Object { $_.Name -ne 'character_packages.h' -and $_.Extension -in '.h', '.cpp' }
$named = @($consumers | Select-String -Pattern '"([a-z0-9_]+\.bin)"' -AllMatches | ForEach-Object { $_.Matches } | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
$difference = Compare-Object $Names $named
Check (-not $difference) "runtime consumers and the table differ: $($difference.InputObject -join ', ')"

# Allow-list staging: only the listed files leave the source folder, and an incomplete or stale set stops it.
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('ssf2t_character_packages_' + [Guid]::NewGuid().ToString('N'))
try {
    $from = Join-Path $fixture 'from'
    New-Item -ItemType Directory -Force $from | Out-Null
    $content = [Text.Encoding]::ASCII.GetBytes('package')
    $hash = (Get-FileHash -InputStream ([IO.MemoryStream]::new($content)) -Algorithm SHA256).Hash.ToLowerInvariant()
    $table = @([pscustomobject]@{ File = 'a.bin'; Size = $content.Length; Sha256 = $hash }, [pscustomobject]@{ File = 'b.bin'; Size = $content.Length; Sha256 = $hash })
    foreach ($leaf in 'a.bin', 'b.bin', 'notes.txt', 'default.xex', 'other.bin') { [IO.File]::WriteAllBytes((Join-Path $from $leaf), $content) }
    $to = Join-Path $fixture 'to'
    Copy-CharacterPackages $from $to $table
    $copied = @(Get-ChildItem -LiteralPath $to -File -Name | Sort-Object)
    Check (($copied -join ',') -eq 'a.bin,b.bin') "staging copied: $($copied -join ', ')"

    $refused = { param($Target) try { Copy-CharacterPackages $from $Target $table; $false } catch { $true } }
    [IO.File]::WriteAllBytes((Join-Path $from 'b.bin'), [Text.Encoding]::ASCII.GetBytes('packagf'))
    Check (& $refused (Join-Path $fixture 'stale')) 'staging accepted a stale package'
    Check (-not (Test-Path (Join-Path $fixture 'stale'))) 'staging started on a stale set'
    Remove-Item -LiteralPath (Join-Path $from 'b.bin')
    Check (& $refused (Join-Path $fixture 'missing')) 'staging accepted a missing package'

    $parsed = @(Get-CharacterPackages (Join-Path $Root 'src\character\character_packages.h'))
    Check ($parsed.Count -eq $Packages.Count) 'the table parses differently on a second read'
} finally {
    Remove-Item -LiteralPath $fixture -Recurse -Force -ErrorAction SilentlyContinue
}

# The packages themselves, when this checkout has them.
$inputs = Join-Path $Root 'assets\characters'
if (Test-Path -LiteralPath $inputs) {
    foreach ($problem in Test-CharacterPackages $inputs $Packages) { Check $false $problem }
    $unknown = @(Get-ChildItem -LiteralPath $inputs -Force | Where-Object { $Names -notcontains $_.Name })
    Check ($unknown.Count -eq 0) "assets\characters holds other files: $($unknown.Name -join ', ')"
}

# The release scripts stage and check through the shared list, and the runtime reads from the program's own folder.
$windows = Read-Tracked 'tools\package-release.ps1'
Check ($windows.Contains("Copy-CharacterPackages (Join-Path `$ReleaseBuild 'assets\characters') (Join-Path `$Stage 'assets\characters')")) 'package-release.ps1 does not stage the packages'
Check ($windows.Contains('Test-CharacterPackages $CharacterInputs')) 'package-release.ps1 does not check its inputs first'
Check ((Read-Tracked 'tools\check-release-folder.ps1').Contains('Test-CharacterPackages (Join-Path $base $CharacterFolder)')) 'check-release-folder.ps1 does not verify the packages'
$policy = Read-Tracked 'tools\linux-policy.sh'
Check ($policy.Contains('usr/bin/assets/characters/%s')) 'linux-policy.sh does not expect the packages in the AppDir'
Check ($policy.Contains('cp "$bin/assets/characters/$package" "$appdir/usr/bin/assets/characters/"')) 'linux-policy.sh does not stage the packages'
Check (([regex]::Matches((Read-Tracked 'tools\package-linux.sh'), 'check_character_packages ')).Count -ge 4) 'package-linux.sh does not check inputs, AppDir, AppImage and --verify-only'
$app = Read-Tracked 'src\ssf2t_hd_remix_app.h'
Check ($app.Contains('character::CharacterPackageFolder(rex::filesystem::GetExecutableFolder())')) 'the program does not read the packages from its own folder'
Check ($app -notmatch 'character::Set\w+Directory\(game_data_root\(\)\)') 'a package consumer still reads the game folder'
Check ((Read-Tracked 'CMakeLists.txt').Contains('stage_character_packages.cmake')) 'CMake does not stage the packages'
Check ((Read-Tracked 'src\game_files.cpp') -notmatch '\.bin"') 'game_files.cpp should not need to know the packages'

# The packages are local inputs and never enter the repository.
Check ((Read-Tracked '.gitignore') -match '(?m)^/assets/characters/\r?$') '.gitignore does not cover assets/characters'
Check (-not (& git -C $Root ls-files -- 'assets/characters')) 'assets/characters holds tracked files'

# The documents describe a release that holds the fighters.
$documents = @('README.md', 'packaging\README.txt') + @(Get-ChildItem (Join-Path $Root 'docs') -Filter *.md | ForEach-Object { "docs\$($_.Name)" })
$stale = 'not part of the release', 'not in the allow-list', 'neither the ZIP nor the AppImage contains', 'no generated packages', 'Current development limitation', 'not part of any install'
foreach ($document in $documents) {
    $text = Read-Tracked $document
    foreach ($phrase in $stale) { Check ($text.IndexOf($phrase, [StringComparison]::OrdinalIgnoreCase) -lt 0) "$document still says '$phrase'" }
}
$fighters = Read-Tracked 'docs\ultra-fighters.md'
foreach ($name in $Names) { Check ($fighters.Contains($name)) "docs\ultra-fighters.md does not list $name" }
Check ($fighters.Contains('assets/characters')) 'docs\ultra-fighters.md does not name the package folder'

foreach ($failure in $failures) { Write-Host "  FAIL $failure" }
Write-Host "character package checks: $($failures.Count) failures"
if ($failures.Count) { throw 'Character package checks failed' }
$global:LASTEXITCODE = 0
