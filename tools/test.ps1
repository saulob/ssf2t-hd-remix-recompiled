<#
.SYNOPSIS
  The headless parity gate for Windows and Linux: every check that needs no window, audio, controller, game run or
  Internet peer, for both game images (0.0.1.3 base013, Title Update 0.0.2.3 tu023) and both platforms, then one parity
  summary. This is the only command needed for deterministic parity.

.DESCRIPTION
  The Windows suites run here. The Linux suites run inside WSL by tools/test.sh (Ubuntu 20.04, glibc 2.31): this script
  finds the distro, hands it this checkout (the real game images are read in place), and folds its results into the
  summary. That stage compiles the real production sources, builds and runs the C++ suites, the Worker and a local
  lobby (also behind TLS), stages and scans a fixture AppDir, runs a fixture AppImage in extract-and-run mode, and
  inspects the glibc versions, RUNPATH and dependencies of the Linux binaries. The wire vectors each platform
  produces are checked by the other platform and by the Worker.

  Every check runs even after one fails; the script exits 1 when any failed. A check that cannot run fails: nothing
  required for parity is reported NOT RUN, and a missing WSL environment is "PARITY FAIL: Linux validation environment
  is unavailable". Only the explicit -SkipRuntime and -SkipLinux switches leave checks out; such a run is partial and
  exits 2 when nothing failed.

  The C++ suites are built into the configured build folder against the patched runtime in tools\rexglue, so run
  tools\build.ps1 once first. The online integration suite talks to a local lobby on 127.0.0.1 that this script starts
  and stops. The updater suites run the real install transaction and trg-updater.exe against a marker program (the
  release CLANG64 helper is built here when it is missing or stale); nothing starts the game or the launcher.

.EXAMPLE
  .\tools\test.ps1
  .\tools\test.ps1 -SkipLinux       # Windows checks only (partial)
  .\tools\test.ps1 -SkipRuntime     # script, Node and source checks only (partial)
  $env:SSF2T_WSL_DISTRO = 'Ubuntu-20.04'; .\tools\test.ps1    # choose the WSL distro
#>
param(
    [ValidateSet('RelWithDebInfo', 'Release')]
    [string]$Config = 'RelWithDebInfo',
    [string]$BuildDir,  # relative to the repository root; default: build
    [switch]$SkipRuntime,
    [switch]$SkipLinux
)
$ErrorActionPreference = 'Stop'

$Root = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'rexruntime-patch.ps1')
$BuildDir = if (-not $BuildDir) { Join-Path $Root 'build' } elseif ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $Root $BuildDir }
$NotRun = 77

# The summary areas, in order. A check counts for every area it names.
$Areas = @(
    'Base013', 'TU023', 'Base013/TU023 pairs', 'Hooks and patches', 'Windows platform paths', 'Linux platform paths',
    'Online protocol', 'Direct / Relay', 'Player Match', 'Rollback', 'Frame Zero', 'Multiple fights / matches',
    'Launcher controls', 'Local multiplayer', 'Overlays / hotkeys', 'Settings', 'Display / Graphics', 'Achievements',
    'Saves / user data', 'Updates', 'Packaging structure', 'Core runtime', 'Global feature parity',
    'Windows/Base013 feature set', 'Windows/TU023 feature set', 'Localization pt-BR', 'Online parity', 'Fight telemetry', 'Worker',
    'Dashboard', 'Recent matches'
)
$AreaLabels = @{
    'Base013' = 'Windows Base013 structural parity'
    'TU023' = 'Windows TU023 structural parity'
    'Linux platform paths' = 'Linux platform paths (static, Windows run)'
}
$TargetAreas = [ordered]@{
    'windows/base013' = 'Windows/Base013 feature set'
    'windows/tu023' = 'Windows/TU023 feature set'
}
$CrossAreas = @('Windows/Linux wire compatibility')
$Results = [Collections.Generic.List[object]]::new()
$LinuxAreas = [Collections.Generic.List[string]]::new()
$Partial = [bool]($SkipRuntime -or $SkipLinux)

function Add-Result([string]$What, [string[]]$In, [string]$Status, [string]$Why = '', [string]$Platform = 'Windows') {
    foreach ($area in $In) {
        $known = if ($Platform -eq 'Windows') { $Areas -contains $area -or $CrossAreas -contains $area } else { $LinuxAreas -contains $area -or $CrossAreas -contains $area }
        if (-not $known) { throw "test.ps1: unknown area '$area' for $What" }
    }
    if ($Status -eq 'NOT RUN' -and -not $Partial) { $Status = 'FAIL' }
    $Results.Add([pscustomobject]@{ What = $What; Areas = $In; Status = $Status; Why = $Why; Platform = $Platform })
    if ($Platform -ne 'Windows') { return }
    switch ($Status) {
        'FAIL' { Write-Host "PARITY FAIL: $What ($($In -join ', '))$(if ($Why) { ": $Why" })" -ForegroundColor Red }
        'NOT RUN' { Write-Host "   NOT RUN: $What$(if ($Why) { " ($Why)" })" -ForegroundColor Yellow }
    }
}

# Runs one check; exit 0 is PASS, 77 (a missing local fixture) and anything else FAIL.
function Invoke-Check([string]$What, [string[]]$In, [scriptblock]$Command) {
    Write-Host "== $What"
    $global:LASTEXITCODE = 0
    $script:LastOutput = @()
    try {
        & $Command | Tee-Object -Variable captured
        $code = $LASTEXITCODE
        $script:LastOutput = @($captured)
    } catch {
        Write-Host "  $_" -ForegroundColor Red
        $code = 1
    }
    $script:LastCode = $code
    if ($code -eq $NotRun) { Add-Result $What $In 'NOT RUN' 'its local fixture is missing' }
    elseif ($code) { Add-Result $What $In 'FAIL' "exit $code" }
    else { Add-Result $What $In 'PASS' }
}

# The global feature parity suite prints one "TARGET <platform>/<build> PASS|FAIL <features>" line per target.
function Add-TargetResults([string]$What, [int]$Code, [string[]]$Output, [string]$Why = '') {
    foreach ($target in $TargetAreas.Keys) {
        $area = $TargetAreas[$target]
        $line = @($Output | Where-Object { "$_" -match "^TARGET $target (PASS|FAIL) " }) | Select-Object -First 1
        if ($Code -eq $NotRun -or $Why) { Add-Result "$What ($area)" @($area) 'NOT RUN' $Why }
        elseif ($line -and "$line" -match " PASS ") { Add-Result "$What ($area)" @($area) 'PASS' }
        else { Add-Result "$What ($area)" @($area) 'FAIL' 'a feature is missing on this target' }
    }
}

function Skip-Check([string]$What, [string[]]$In, [string]$Why) {
    Write-Host "== $What"
    Add-Result $What $In 'NOT RUN' $Why
}

# ---- Scripts, the online service and the scanner: no build needed ----------------------------------------------

$python = Get-Command python -ErrorAction SilentlyContinue
$scanner = Join-Path $PSScriptRoot 'find-missing-functions.py'
if ($python) {
    Invoke-Check 'codegen scanner self-test' @('Core runtime') { & $python.Source -I $scanner --self-test }
    if ((Test-Path (Join-Path $Root 'game\analysis\default.image.bin')) -and (Test-Path (Join-Path $Root 'out\codegen\tu\raw\sources.cmake'))) {
        Invoke-Check 'codegen scanner verify (TU023 image)' @('TU023', 'Hooks and patches') { & $python.Source -I $scanner --verify }
    } else {
        Skip-Check 'codegen scanner verify (TU023 image)' @('TU023') 'needs out\codegen\tu\raw and game\analysis\default.image.bin'
    }
} else {
    Skip-Check 'codegen scanner self-test' @('Core runtime') 'python not found'
    Skip-Check 'codegen scanner verify (TU023 image)' @('TU023') 'python not found'
}
Invoke-Check 'guest symbol prefix (base013_/tu023_)' @('Base013', 'TU023', 'Hooks and patches') { & (Join-Path $PSScriptRoot 'test-guest-code.ps1') }
Invoke-Check 'release folder check cases' @('Packaging structure', 'Windows platform paths') { & (Join-Path $PSScriptRoot 'test-release-check.ps1') }
Invoke-Check 'online instance script' @('Online protocol') { & (Join-Path $PSScriptRoot 'test-online-instance.ps1') }

$node = Get-Command node -ErrorAction SilentlyContinue
if ($node) {
    Invoke-Check 'online service (Worker, Node)' @('Online protocol', 'Direct / Relay', 'Player Match', 'Multiple fights / matches', 'Online parity', 'Worker') {
        Push-Location (Join-Path $Root 'server\ssf2t-lobby')
        try { & $node.Source --test 'test/*.test.js' } finally { Pop-Location }
    }
    Invoke-Check 'fight telemetry (Worker, Node)' @('Fight telemetry', 'Worker', 'Online parity', 'Multiple fights / matches') {
        Push-Location (Join-Path $Root 'server\ssf2t-lobby')
        try { & $node.Source --test test/fights.test.js test/wire.test.js } finally { Pop-Location }
    }
    Invoke-Check 'recent matches (Worker, Node)' @('Recent matches', 'Worker') {
        Push-Location (Join-Path $Root 'server\ssf2t-lobby')
        try { & $node.Source --test test/recent.test.js } finally { Pop-Location }
    }
    Invoke-Check 'dashboard (page and admin API, Node)' @('Dashboard', 'Worker') {
        Push-Location (Join-Path $Root 'server\ssf2t-lobby')
        try { & $node.Source --test test/dashboard.test.js test/admin.test.js } finally { Pop-Location }
    }
    Invoke-Check 'fight telemetry mutations (Worker and dashboard)' @('Fight telemetry', 'Worker', 'Dashboard', 'Recent matches') {
        Push-Location (Join-Path $Root 'server\ssf2t-lobby')
        try { & $node.Source test/mutations.mjs node } finally { Pop-Location }
    }
    Invoke-Check 'fight telemetry mutations (client, C++ compiler)' @('Fight telemetry', 'Base013', 'TU023') {
        Push-Location (Join-Path $Root 'server\ssf2t-lobby')
        try { & $node.Source test/mutations.mjs cpp } finally { Pop-Location }
    }
} else {
    Skip-Check 'online service (Worker, Node)' @('Online protocol', 'Direct / Relay') 'node not found'
    foreach ($what in 'fight telemetry (Worker, Node)', 'recent matches (Worker, Node)', 'dashboard (page and admin API, Node)',
        'fight telemetry mutations (Worker and dashboard)', 'fight telemetry mutations (client, C++ compiler)') {
        Skip-Check $what @('Fight telemetry', 'Worker') 'node not found'
    }
}

# ---- C++ suites ------------------------------------------------------------------------------------------------

$Suites = @(
    # Game images and hooks
    @('guest parity: counterparts in source', 'guest_parity source', 'Base013', 'TU023', 'Base013/TU023 pairs', 'Hooks and patches'),
    @('guest parity: address pairs on the real images', 'guest_parity pairs', 'Base013/TU023 pairs', 'Hooks and patches'),
    @('guest parity: Base013 fingerprints on the real image', 'guest_parity base013', 'Base013', 'Hooks and patches'),
    @('guest parity: TU023 fingerprints on the real image', 'guest_parity tu023', 'TU023', 'Hooks and patches'),
    @('global feature parity: source (every build and platform)', 'global_feature_parity static', 'Global feature parity', 'Base013', 'TU023', 'Hooks and patches', 'Windows platform paths', 'Linux platform paths'),
    @('global feature parity: real images', 'global_feature_parity images', 'Global feature parity', 'Base013', 'TU023', 'Hooks and patches'),
    @('guest build (both images in one program)', 'guest_build', 'Base013', 'TU023'),
    @('game files', 'game_files', 'Base013', 'TU023'),
    @('game files, base-only build', 'game_files_base_only', 'Base013'),
    @('menu text', 'menu_text', 'Hooks and patches', 'Player Match'),
    @('localization pt-BR: registration, tables, placeholders, encoding (source)', 'localization source', 'Localization pt-BR', 'Settings', 'Windows platform paths', 'Linux platform paths'),
    @('localization pt-BR: real game string tables and fonts', 'localization game', 'Localization pt-BR', 'Base013', 'TU023', 'Hooks and patches'),
    @('localization pt-BR: runtime literals of both guest images and their reachability', 'localization runtime', 'Localization pt-BR', 'Base013', 'TU023', 'Hooks and patches'),
    @('fight area', 'fight_area', 'Hooks and patches', 'Display / Graphics'),
    @('infinite time', 'infinite_time', 'Hooks and patches', 'Overlays / hotkeys', 'Settings'),
    # Platforms and wire formats
    @('wire bytes (tests/wire-vectors.txt)', 'wire_parity', 'Online protocol', 'Direct / Relay', 'Windows platform paths', 'Linux platform paths', 'Online parity'),
    @('platform parity', 'platform_parity', 'Windows platform paths', 'Linux platform paths', 'Packaging structure', 'Launcher controls'),
    @('user data', 'user_data', 'Saves / user data', 'Linux platform paths'),
    @('relaunch', 'relaunch', 'Windows platform paths', 'Linux platform paths'),
    # Online
    @('netplay packets', 'netplay_packet', 'Online protocol', 'Rollback'),
    @('netplay rollback', 'netplay_rollback', 'Rollback'),
    @('netplay session', 'netplay_session', 'Rollback', 'Frame Zero', 'Multiple fights / matches'),
    @('netplay guest', 'netplay_guest', 'Rollback', 'Frame Zero', 'Base013', 'TU023'),
    @('netplay input', 'netplay_input', 'Rollback', 'Local multiplayer'),
    @('netplay state', 'netplay_state', 'Rollback', 'Frame Zero'),
    @('netplay online game socket', 'netplay_online_game_socket', 'Direct / Relay'),
    @('netplay setting', 'netplay_setting', 'Settings'),
    @('online protocol', 'online_protocol', 'Online protocol'),
    @('online identity', 'online_identity', 'Online protocol', 'Saves / user data'),
    @('online service', 'online_service', 'Online protocol', 'Direct / Relay', 'Player Match', 'Multiple fights / matches', 'Online parity'),
    @('fight telemetry (reporter, guest reads on both images, queue, wire)', 'fight_telemetry', 'Fight telemetry', 'Base013', 'TU023', 'Online protocol', 'Direct / Relay', 'Multiple fights / matches', 'Hooks and patches', 'Online parity'),
    @('online rendezvous', 'online_rendezvous', 'Direct / Relay'),
    @('online connection test', 'online_connection', 'Online protocol', 'Direct / Relay'),
    @('friend code field', 'friend_field', 'Online protocol'),
    @('online setting', 'online_setting', 'Settings'),
    @('player match', 'player_match', 'Player Match', 'Base013', 'TU023'),
    @('guest ui', 'guest_ui', 'Player Match'),
    @('player match net', 'player_match_net', 'Player Match'),
    @('online menu', 'online_menu', 'Player Match'),
    @('player match lobby', 'player_match_lobby', 'Player Match', 'Frame Zero', 'Multiple fights / matches'),
    # Launcher, controls and display
    @('launcher settings', 'launcher_settings', 'Settings'),
    @('keyboard remapping and hotkeys', 'keyboard', 'Launcher controls', 'Local multiplayer', 'Overlays / hotkeys'),
    @('player names', 'player_name', 'Local multiplayer', 'Settings'),
    @('launcher quit', 'launcher_quit', 'Launcher controls'),
    @('launcher controller (issue #9: XInput on Windows, SDL on Linux)', 'launcher_gamepad', 'Launcher controls', 'Windows platform paths'),
    @('window placement', 'window_placement', 'Display / Graphics'),
    @('display scaling', 'dpi', 'Display / Graphics'),
    @('window host', 'window_host', 'Display / Graphics', 'Overlays / hotkeys'),
    @('graphics card', 'graphics_adapter', 'Display / Graphics', 'Settings'),
    @('D3D12 transfer short-circuit', 'd3d12_transfer_short_circuit', 'Display / Graphics'),
    @('D3D12 sampler memo', 'd3d12_sampler_memo', 'Display / Graphics'),
    @('shared memory fast path', 'shared_memory_fast_path', 'Display / Graphics'),
    @('GPU draw state memo', 'gpu_draw_state_memo', 'Display / Graphics'),
    @('D3D12 async submission', 'd3d12_async_submission', 'Display / Graphics'),
    @('GPU register wake', 'gpu_register_wake', 'Display / Graphics'),
    # Achievements, saves, updates and the runtime
    @('achievements', 'achievement', 'Achievements'),
    @('achievement metadata (real game files)', 'achievement_metadata', 'Achievements'),
    @('updater', 'update', 'Updates', 'Saves / user data'),
    @('log path', 'log_path', 'Saves / user data'),
    @('guest clock', 'guest_clock', 'Core runtime'),
    @('file dialog', 'file_dialog', 'Core runtime'),
    @('diagnostics', 'diagnostics', 'Core runtime'),
    @('crash report', 'crash_report', 'Core runtime'),
    @('active log', 'active_log', 'Core runtime'),
    @('log filter and session log', 'log_filter', 'Core runtime'),
    @('lifecycle', 'lifecycle', 'Core runtime'),
    @('physical heap stress', 'physical_heap_stress!', 'Core runtime'),
    @('title thread shutdown', 'title_thread_shutdown', 'Core runtime')
)

function Get-SuiteExe([string]$Spec) {
    $name, $mode = $Spec -split ' ', 2
    $exe = if ($name.EndsWith('!')) { "ssf2t_$($name.TrimEnd('!')).exe" } else { "ssf2t_${name}_tests.exe" }
    return @((Join-Path $BuildDir $exe), $mode)
}

$buildProblem = $null
if ($SkipRuntime) {
    $buildProblem = '-SkipRuntime'
} else {
    $SdkRoot = Join-Path $Root 'tools\rexglue\win-amd64'
    $problems = @(Test-PatchedRuntime $SdkRoot)
    if ($problems.Count) {
        $problems | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
        $buildProblem = 'tools\rexglue does not hold the patched runtime; run tools\build.ps1 first'
    } elseif (-not (Test-Path (Join-Path $BuildDir 'build.ninja'))) {
        $buildProblem = "$BuildDir is not configured; run tools\build.ps1 first"
    } elseif (-not (Select-String -Path (Join-Path $BuildDir 'CMakeCache.txt') -SimpleMatch "CMAKE_BUILD_TYPE:STRING=$Config" -Quiet)) {
        $buildProblem = "$BuildDir is not a $Config build (use -Config or -BuildDir)"
    }
}

if (-not $buildProblem) {
    $SavedEnv = [Environment]::GetEnvironmentVariables()
    try {
        $vsRoot = Get-VisualStudioRoot
        $vsDevCmd = Join-Path $vsRoot 'Common7\Tools\VsDevCmd.bat'
        $llvmBin = Join-Path $vsRoot 'VC\Tools\Llvm\x64\bin'
        $cmakeBin = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
        $ninjaBin = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
        foreach ($tool in @($vsDevCmd, (Join-Path $llvmBin 'clang++.exe'), (Join-Path $cmakeBin 'cmake.exe'), (Join-Path $ninjaBin 'ninja.exe'))) {
            if (-not (Test-Path $tool)) { throw "Required tool not found: $tool" }
        }
        $env:PATH = (($env:PATH -split ';') | Where-Object { $_ -and $_ -notmatch '\\msys64\\' }) -join ';'
        $env:PATH = "$env:PATH;$(Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer')"
        foreach ($line in (cmd.exe /c "`"$vsDevCmd`" -arch=x64 -host_arch=x64 -no_logo >nul && set")) {
            if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2] }
        }
        if (-not $env:VCToolsVersion) { throw 'VsDevCmd.bat did not set up the x64 environment' }
        $env:PATH = "$llvmBin;$cmakeBin;$ninjaBin;$env:PATH"
        Write-Host '== build the C++ suites'
        & (Join-Path $cmakeBin 'cmake.exe') --build $BuildDir --target ssf2t_runtime_tests
        if ($LASTEXITCODE) { $buildProblem = "building ssf2t_runtime_tests failed ($LASTEXITCODE)" }
    } catch {
        $buildProblem = "$_"
    } finally {
        foreach ($name in @([Environment]::GetEnvironmentVariables().Keys)) {
            if (-not $SavedEnv.Contains($name)) { Remove-Item "Env:$name" }
        }
        foreach ($name in $SavedEnv.Keys) { Set-Item "Env:$name" $SavedEnv[$name] }
    }
    if (-not $buildProblem) {
        $suffix = @{ RelWithDebInfo = 'rd'; Release = '' }[$Config]
        $dll = Join-Path $BuildDir "rexruntime$suffix.dll"
        if ((Get-FileSha256 $dll) -ne (Get-FileSha256 (Join-Path $SdkRoot "bin\rexruntime$suffix.dll"))) { $buildProblem = "$dll is not the patched runtime" }
    }
}

if ($buildProblem) {
    if ($buildProblem -ne '-SkipRuntime') { Add-Result 'build the C++ suites' @('Core runtime') 'FAIL' $buildProblem }
    foreach ($suite in $Suites) {
        Skip-Check $suite[0] $suite[2..($suite.Count - 1)] $buildProblem
        if ($suite[1] -like 'global_feature_parity *') { Add-TargetResults $suite[0] $NotRun @() $buildProblem }
    }
} else {
    foreach ($suite in $Suites) {
        $exe, $mode = Get-SuiteExe $suite[1]
        Invoke-Check $suite[0] $suite[2..($suite.Count - 1)] { if ($mode) { & $exe $mode } else { & $exe } }.GetNewClosure()
        if ($suite[1] -like 'global_feature_parity *') { Add-TargetResults $suite[0] $script:LastCode $script:LastOutput }
    }
    $clipboard = Join-Path $BuildDir 'ssf2t_clipboard_tests.exe'
    Invoke-Check 'clipboard (Win32)' @('Windows platform paths', 'Online protocol') { & $clipboard }.GetNewClosure()
    $helper = Join-Path $BuildDir 'ssf2t_update_helper_tests.exe'
    Invoke-Check 'updater helper' @('Updates', 'Windows platform paths', 'Saves / user data') { & $helper }.GetNewClosure()
    $releaseUpdaterDir = Join-Path $BuildDir 'trg-updater-clang64'
    $releaseUpdater = Join-Path $releaseUpdaterDir 'trg-updater.exe'
    $updaterSources = @(Get-ChildItem -LiteralPath (Join-Path $Root 'src\updater') -File | Where-Object Extension -in '.cpp', '.h') +
        @(Get-Item -LiteralPath (Join-Path $Root 'CMakeLists.txt'), (Join-Path $PSScriptRoot 'build-updater.ps1'))
    $newestSource = ($updaterSources | Measure-Object LastWriteTimeUtc -Maximum).Maximum
    $releaseUpdaterStale = -not (Test-Path -LiteralPath $releaseUpdater) -or (Get-Item -LiteralPath $releaseUpdater).LastWriteTimeUtc -lt $newestSource
    Invoke-Check 'release update helper build (MSYS2 CLANG64, rebuilt when missing or stale)' @('Updates', 'Packaging structure') {
        if ($releaseUpdaterStale) {
            & (Join-Path $PSScriptRoot 'build-updater.ps1') -OutDir $releaseUpdaterDir
        } else {
            Write-Host 'current'
            $global:LASTEXITCODE = 0
        }
    }.GetNewClosure()
    if (Test-Path -LiteralPath $releaseUpdater) {
        Invoke-Check 'updater helper (release CLANG64 build)' @('Updates', 'Packaging structure') { & $helper $releaseUpdater }.GetNewClosure()
    } else {
        Skip-Check 'updater helper (release CLANG64 build)' @('Updates') 'the CLANG64 helper was not built'
    }

    $integration = Join-Path $BuildDir 'ssf2t_online_integration_tests.exe'
    $what = 'online integration (C++ client against the local lobby)'
    $in = @('Online protocol', 'Direct / Relay', 'Player Match', 'Multiple fights / matches')
    if ($node) {
        Write-Host "== $what"
        $port = 18000 + (Get-Random -Maximum 2000)
        $out = Join-Path ([IO.Path]::GetTempPath()) "ssf2t-lobby-$port.log"
        $server = Start-Process -FilePath $node.Source -PassThru -WindowStyle Hidden -RedirectStandardOutput $out -RedirectStandardError "$out.err" `
            -ArgumentList @((Join-Path $Root 'server\ssf2t-lobby\local\server.js'), '--host', '127.0.0.1', '--port', "$port", '--db', ':memory:')
        try {
            $listening = $false
            for ($i = 0; $i -lt 100 -and -not $listening -and -not $server.HasExited; $i++) {
                try { ([Net.Sockets.TcpClient]::new('127.0.0.1', $port)).Close(); $listening = $true } catch { Start-Sleep -Milliseconds 100 }
            }
            if (-not $listening) {
                Add-Result $what $in 'FAIL' 'the local lobby did not start'
            } else {
                $env:SSF2T_LOBBY_TEST_URL = "ws://127.0.0.1:$port/"
                & $integration
                $code = $LASTEXITCODE
                if ($code) { Add-Result $what $in 'FAIL' "exit $code" } else { Add-Result $what $in 'PASS' }
            }
        } finally {
            Remove-Item Env:SSF2T_LOBBY_TEST_URL -ErrorAction SilentlyContinue
            if (-not $server.HasExited) { Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue }
            Remove-Item -LiteralPath $out, "$out.err" -ErrorAction SilentlyContinue
        }
    } else {
        Skip-Check $what $in 'node not found'
    }
}

# ---- Linux, inside WSL -----------------------------------------------------------------------------------------

$LinuxInfo = [ordered]@{}
$LinuxDistro = $null

function Get-WslDistros {
    $env:WSL_UTF8 = '1'
    $names = @(& wsl.exe --list --quiet 2>$null | ForEach-Object { ("$_" -replace "`0", '').Trim() } | Where-Object { $_ })
    return @($names | Where-Object { $_ -notmatch '^docker-desktop' } | Sort-Object { $_.ToLowerInvariant() })
}

function ConvertTo-WslPath([string]$Distro, [string]$Path) {
    $converted = & wsl.exe -d $Distro --exec wslpath -a $Path
    if ($LASTEXITCODE -or -not $converted) { throw "wslpath could not convert $Path in $Distro" }
    return "$converted".Trim()
}

# The first installed distro (SSF2T_WSL_DISTRO, or all of them in name order) that tools/test.sh accepts: Ubuntu 20.04,
# glibc 2.31 and the build tools.
function Find-LinuxDistro {
    if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
        return @{ Error = 'wsl.exe was not found; install WSL (wsl --install) and an Ubuntu 20.04 distro with tools/setup-linux-build-env.sh' }
    }
    $installed = Get-WslDistros
    if (-not $installed.Count) {
        return @{ Error = 'WSL has no distribution; install Ubuntu 20.04 (wsl --install -d Ubuntu-20.04) and run tools/setup-linux-build-env.sh in it' }
    }
    $candidates = if ($env:SSF2T_WSL_DISTRO) { @($env:SSF2T_WSL_DISTRO) } else { $installed }
    $rejected = [Collections.Generic.List[string]]::new()
    foreach ($name in $candidates) {
        if ($installed -notcontains $name) { $rejected.Add("${name}: not installed"); continue }
        try { $wslRoot = ConvertTo-WslPath $name $Root } catch { $rejected.Add("${name}: $_"); continue }
        $output = @(& wsl.exe -d $name --exec bash "$wslRoot/tools/test.sh" --check-environment 2>&1 | ForEach-Object { "$_".Trim() } | Where-Object { $_ })
        if ($LASTEXITCODE -eq 0) { return @{ Name = $name; Root = $wslRoot } }
        $rejected.Add("${name}: $($output -join ' ')")
    }
    return @{ Error = "no WSL distribution can run the Linux gate (set SSF2T_WSL_DISTRO to choose one). Checked: $($rejected -join ' | ')" }
}

function Write-LfFile([string]$Path, [string[]]$Lines) {
    [IO.File]::WriteAllText($Path, (($Lines -join "`n") + "`n"), [Text.UTF8Encoding]::new($false))
}

# Runs one tools/test.sh invocation in WSL, shows its output and folds its @@ lines into the results.
function Invoke-LinuxScript([string]$Distro, [string[]]$ScriptArgs) {
    $state = @{ Seen = 0; Build = ''; Work = '' }
    $savedEncoding = [Console]::OutputEncoding
    [Console]::OutputEncoding = [Text.Encoding]::UTF8
    try {
        & wsl.exe -d $Distro --exec bash @ScriptArgs 2>&1 | ForEach-Object {
            $line = "$_"
            if ($line -match '^@@AREA (.+)$') {
                if (-not $LinuxAreas.Contains($Matches[1])) { $LinuxAreas.Add($Matches[1]) }
            } elseif ($line -match '^@@RESULT (PASS|FAIL)\|(.*)\|([^|]*)$') {
                $state.Seen++
                Add-Result $Matches[2] @($Matches[3] -split ';' | Where-Object { $_ }) $Matches[1] '' 'Linux'
            } elseif ($line -match '^@@BUILD (.+)$') {
                $state.Build = $Matches[1].Trim()
            } elseif ($line -match '^@@WORK (.+)$') {
                $state.Work = $Matches[1].Trim()
            } elseif ($line -match '^@@ENV ([^=]+)=(.*)$') {
                $LinuxInfo[$Matches[1]] = $Matches[2]
            } else {
                Write-Host $line
            }
        }
        $state.Code = $LASTEXITCODE
    } finally {
        [Console]::OutputEncoding = $savedEncoding
    }
    return $state
}

function Invoke-LinuxGate {
    $found = Find-LinuxDistro
    if ($found.Error) {
        foreach ($area in 'Linux environment', 'Linux test.sh gate') { $LinuxAreas.Add($area) }
        Write-Host "PARITY FAIL: Linux validation environment is unavailable: $($found.Error)" -ForegroundColor Red
        Add-Result "Linux validation environment is unavailable: $($found.Error)" @('Linux environment', 'Linux test.sh gate') 'FAIL' '' 'Linux'
        return
    }
    $script:LinuxDistro = $found.Name
    $shScript = "$($found.Root)/tools/test.sh"

    Write-Host "== Linux work tree in WSL distro '$($found.Name)' (tools/test.sh --prepare)"
    $prepared = Invoke-LinuxScript $found.Name @($shScript, '--prepare')
    if ($prepared.Code -or -not $prepared.Build -or -not $prepared.Work) {
        foreach ($area in 'Linux environment', 'Linux test.sh gate') { if (-not $LinuxAreas.Contains($area)) { $LinuxAreas.Add($area) } }
        Add-Result "the Linux work tree could not be prepared in WSL '$($found.Name)' (exit $($prepared.Code))" @('Linux environment', 'Linux test.sh gate') 'FAIL' '' 'Linux'
        return
    }

    $stamp = [Guid]::NewGuid().ToString('N').Substring(0, 8)
    $windowsVectors = Join-Path ([IO.Path]::GetTempPath()) "ssf2t-wire-windows-$stamp.txt"
    $script:LinuxVectors = Join-Path ([IO.Path]::GetTempPath()) "ssf2t-wire-linux-$stamp.txt"
    $wireExe = Join-Path $BuildDir 'ssf2t_wire_parity_tests.exe'
    $gateArgs = @("$($prepared.Work)/tools/test.sh", '--build-dir', $prepared.Build, '--emit-vectors', (ConvertTo-WslPath $found.Name $script:LinuxVectors))
    if (Test-Path -LiteralPath $wireExe) {
        Write-LfFile $windowsVectors @(& $wireExe --print)
        $gateArgs += @('--peer-vectors', (ConvertTo-WslPath $found.Name $windowsVectors))
    }
    Write-Host "== Linux gate in WSL (tools/test.sh --build-dir $($prepared.Build))"
    try {
        $gate = Invoke-LinuxScript $found.Name $gateArgs
    } finally {
        Remove-Item -LiteralPath $windowsVectors -ErrorAction SilentlyContinue
    }
    if (-not $gate.Seen) {
        Add-Result "tools/test.sh --build-dir in WSL '$($found.Name)' produced no results (exit $($gate.Code))" @('Linux test.sh gate') 'FAIL' '' 'Linux'
    } elseif ($gate.Code -and -not (@($Results | Where-Object { $_.Platform -eq 'Linux' -and $_.Status -eq 'FAIL' }).Count)) {
        Add-Result "tools/test.sh --build-dir in WSL '$($found.Name)' exited $($gate.Code)" @('Linux test.sh gate') 'FAIL' '' 'Linux'
    }
    if ($LinuxInfo['build_dir'] -ne $prepared.Build) {
        Add-Result "tools/test.sh --build-dir did not report using $($prepared.Build)" @('Linux test.sh --build-dir') 'FAIL' '' 'Linux'
    }
}

if ($SkipLinux) {
    Write-Host '== Linux gate'
    Write-Host '   NOT RUN: -SkipLinux was given; this run is partial' -ForegroundColor Yellow
} else {
    Invoke-LinuxGate
    $wireExe = Join-Path $BuildDir 'ssf2t_wire_parity_tests.exe'
    $crossArea = @('Windows/Linux wire compatibility')
    if ($LinuxVectors -and (Test-Path -LiteralPath $LinuxVectors)) {
        if (Test-Path -LiteralPath $wireExe) {
            Invoke-Check 'Windows reads and reproduces the vectors the Linux build produced' $crossArea { & $wireExe --peer $LinuxVectors }
            Invoke-Check 'Windows and Linux produce byte-identical vectors' $crossArea {
                $windows = @(& $wireExe --print) -join "`n"
                $linux = ((Get-Content -LiteralPath $LinuxVectors) -join "`n")
                if ($windows -ne $linux) { Write-Host 'The vectors differ between Windows and Linux'; $global:LASTEXITCODE = 1 } else { Write-Host 'identical'; $global:LASTEXITCODE = 0 }
            }
        } else {
            Add-Result 'Windows reads the vectors the Linux build produced' $crossArea 'FAIL' 'ssf2t_wire_parity_tests.exe was not built'
        }
        if ($node) {
            Invoke-Check 'Worker reads the vectors the Linux build produced (Node on Windows)' ($crossArea + 'Direct / Relay') {
                $env:SSF2T_WIRE_VECTORS = $LinuxVectors
                Push-Location (Join-Path $Root 'server\ssf2t-lobby')
                try { & $node.Source --test 'test/wire.test.js' } finally { Pop-Location; Remove-Item Env:SSF2T_WIRE_VECTORS -ErrorAction SilentlyContinue }
            }
        }
    } else {
        Add-Result 'the Linux build produced no wire vectors for Windows to read' $crossArea 'FAIL'
    }
    if ($LinuxVectors) { Remove-Item -LiteralPath $LinuxVectors -ErrorAction SilentlyContinue }
}

# ---- Required Windows checks: a registered check that never reported cannot hide behind the totals -------------

$RequiredWindows = @(
    'codegen scanner self-test', 'codegen scanner verify (TU023 image)', 'guest symbol prefix (base013_/tu023_)',
    'release folder check cases', 'online instance script', 'online service (Worker, Node)', 'fight telemetry (Worker, Node)',
    'recent matches (Worker, Node)', 'dashboard (page and admin API, Node)', 'fight telemetry mutations (Worker and dashboard)',
    'fight telemetry mutations (client, C++ compiler)'
) + @($Suites | ForEach-Object { $_[0] }) + @(
    $Suites | Where-Object { $_[1] -like 'global_feature_parity *' } | ForEach-Object { $name = $_[0]; $TargetAreas.Values | ForEach-Object { "$name ($_)" } }
)
if (-not $buildProblem) {
    $RequiredWindows += @(
        'clipboard (Win32)', 'updater helper', 'release update helper build (MSYS2 CLANG64, rebuilt when missing or stale)',
        'updater helper (release CLANG64 build)', 'online integration (C++ client against the local lobby)'
    )
}
$executedWindows = @($Results | Where-Object Platform -eq 'Windows' | ForEach-Object { $_.What })
foreach ($required in $RequiredWindows) {
    if ($executedWindows -notcontains $required) {
        Add-Result "required suite '$required' did not execute" @('Core runtime') 'FAIL'
    }
}

# ---- Summary ---------------------------------------------------------------------------------------------------

function Write-AreaLine([string]$Label, [object[]]$Mine, [string]$Suffix = '', [string]$Pass = 'PASS') {
    $failed = @($Mine | Where-Object Status -eq 'FAIL').Count
    $passed = @($Mine | Where-Object Status -eq 'PASS').Count
    $skipped = @($Mine | Where-Object Status -eq 'NOT RUN').Count
    if ($failed) { $text = "FAIL ($failed of $($Mine.Count) checks)"; $color = 'Red' }
    elseif (-not $passed) { $text = 'NOT RUN'; $color = 'Yellow' }
    elseif ($skipped) { $text = "PASS ($passed checks, $skipped not run)"; $color = 'Yellow' }
    else { $text = "$Pass ($passed checks)$Suffix"; $color = 'Green' }
    Write-Host ('  {0,-46}{1}' -f $Label, $text) -ForegroundColor $color
}

function Get-AreaResults([string]$Area, [string]$Platform) {
    return @($Results | Where-Object { $_.Platform -eq $Platform -and $_.Areas -contains $Area })
}

$distroSuffix = if ($LinuxInfo['distro']) { ' (WSL)' } else { '' }

Write-Host ''
Write-Host 'Parity:'
Write-Host ''
Write-Host 'Windows, executed on Windows'
foreach ($area in $Areas) {
    $label = if ($AreaLabels.ContainsKey($area)) { $AreaLabels[$area] } else { $area }
    Write-AreaLine $label (Get-AreaResults $area 'Windows')
}
Write-Host ''
if ($LinuxAreas.Count) {
    $environment = @($LinuxInfo.Keys | Where-Object { $_ -in 'glibc', 'compiler', 'cmake', 'ninja', 'node', 'highest_glibc' } | ForEach-Object { "$_=$($LinuxInfo[$_])" })
    Write-Host "Linux, executed in WSL ($($LinuxInfo['distro'] ?? $LinuxDistro))"
    if ($environment.Count) { Write-Host "  $($environment -join '; ')" -ForegroundColor DarkGray }
    $linuxOrder = @($LinuxAreas | Where-Object { $_ -ne 'Linux test.sh gate' }) + @('Linux test.sh gate')
    foreach ($area in $linuxOrder) {
        $word = if ($area -in 'Linux clipboard runtime', 'Linux test.sh --build-dir') { 'EXECUTED PASS' } else { 'PASS' }
        Write-AreaLine $area (Get-AreaResults $area 'Linux') $distroSuffix $word
    }
} else {
    Write-Host 'Linux, executed in WSL'
    Write-AreaLine 'Linux gate' @([pscustomobject]@{ Status = 'NOT RUN' })
}
Write-Host ''
Write-Host 'Windows and Linux together'
foreach ($area in $CrossAreas) {
    Write-AreaLine $area @($Results | Where-Object { $_.Areas -contains $area })
}
$globalWindows = Get-AreaResults 'Global feature parity' 'Windows'
$globalLinux = Get-AreaResults 'Linux global feature parity' 'Linux'
Write-AreaLine 'Global feature parity (both platforms)' @($globalWindows + $globalLinux)
Write-Host ''
Write-Host 'MANUAL RUNTIME VALIDATION (not covered by this script; needs the real game, a window, audio, a pad or two machines):'
foreach ($line in @(
        'rendering, presentation and aspect ratios on screen (D3D12 and Vulkan)',
        'physical controllers and keyboard feel in the launcher and in game',
        'audio output and the desktop/window-manager integration of the Linux AppImage',
        'online play between real peers across NATs (direct UDP and relay)'
    )) { Write-Host "  - $line" }

$failures = @($Results | Where-Object Status -eq 'FAIL')
$notRun = @($Results | Where-Object Status -eq 'NOT RUN')
$windowsCount = @($Results | Where-Object Platform -eq 'Windows').Count
$linuxCount = @($Results | Where-Object Platform -eq 'Linux').Count
Write-Host ''
Write-Host ("{0} checks ({1} on Windows, {2} in WSL): {3} passed, {4} failed, {5} not run" -f $Results.Count, $windowsCount, $linuxCount,
    @($Results | Where-Object Status -eq 'PASS').Count, $failures.Count, $notRun.Count)
Write-Host ("Required parity suites NOT RUN: {0}" -f $notRun.Count)
if ($failures.Count) {
    foreach ($f in $failures) { Write-Host "  PARITY FAIL: $($f.What)" -ForegroundColor Red }
    exit 1
}
if ($Partial) {
    Write-Host 'PARTIAL RUN (-SkipRuntime or -SkipLinux): nothing failed, but this is not the parity gate.' -ForegroundColor Yellow
    exit 2
}
Write-Host 'All headless parity checks passed (Windows and Linux).'
