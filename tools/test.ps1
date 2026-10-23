<#
.SYNOPSIS
  Headless checks: the codegen scanner self-test, the release folder check cases and the patched ReXGlue runtime
  tests. Nothing opens a window, plays audio or needs the game files.

.DESCRIPTION
  The updater tests (tests\update_tests.cpp, tests\update_helper_tests.cpp) run the real install transaction and the
  real trg-updater.exe against a marker program; they never start the game or the launcher.
  The runtime tests (tests\physical_heap_stress.cpp, tests\title_thread_shutdown_tests.cpp) are built into the
  configured build folder and run against the patched runtime in tools\rexglue, so run tools\build.ps1 once first.
  The scanner --verify step runs only when generated\default and game\analysis\default.image.bin exist.

.EXAMPLE
  .\tools\test.ps1
  .\tools\test.ps1 -SkipRuntime     # scanner and release check only
#>
param(
    [ValidateSet('RelWithDebInfo', 'Release')]
    [string]$Config = 'RelWithDebInfo',
    [string]$BuildDir,  # relative to the repository root; default: build
    [switch]$SkipRuntime
)
$ErrorActionPreference = 'Stop'

$Root = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'rexruntime-patch.ps1')
$BuildDir = if (-not $BuildDir) { Join-Path $Root 'build' } elseif ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $Root $BuildDir }

function Invoke-Step([string]$What, [scriptblock]$Command) {
    Write-Host "== $What"
    & $Command
    if ($LASTEXITCODE) { throw "$What failed ($LASTEXITCODE)" }
}

$python = (Get-Command python -ErrorAction SilentlyContinue)
if ($python) {
    $scanner = Join-Path $PSScriptRoot 'find-missing-functions.py'
    Invoke-Step 'scanner self-test' { & $python.Source -I $scanner --self-test }
    $image = Join-Path $Root 'game\analysis\default.image.bin'
    if ((Test-Path $image) -and (Test-Path (Join-Path $Root 'generated\default\sources.cmake'))) {
        Invoke-Step 'scanner verify' { & $python.Source -I $scanner --verify }
    } else {
        Write-Host '== scanner verify skipped (needs generated\default and game\analysis\default.image.bin)'
    }
} else {
    Write-Host '== scanner checks skipped (python not found)'
}

Write-Host '== release folder check'
& (Join-Path $PSScriptRoot 'test-release-check.ps1')
if ($LASTEXITCODE) { throw "Release folder check tests failed ($LASTEXITCODE)" }

if ($SkipRuntime) { return }

$SdkRoot = Join-Path $Root 'tools\rexglue\win-amd64'
$problems = @(Test-PatchedRuntime $SdkRoot)
if ($problems.Count) {
    $problems | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    throw 'tools\rexglue does not hold the patched runtime; run tools\build.ps1 first'
}
if (-not (Test-Path (Join-Path $BuildDir 'build.ninja'))) { throw "$BuildDir is not configured; run tools\build.ps1 first" }
if (-not (Select-String -Path (Join-Path $BuildDir 'CMakeCache.txt') -SimpleMatch "CMAKE_BUILD_TYPE:STRING=$Config" -Quiet)) {
    throw "$BuildDir is not a $Config build (use -Config or -BuildDir)"
}

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
    Invoke-Step 'build runtime tests' { & (Join-Path $cmakeBin 'cmake.exe') --build $BuildDir --target ssf2t_runtime_tests }
} finally {
    foreach ($name in @([Environment]::GetEnvironmentVariables().Keys)) {
        if (-not $SavedEnv.Contains($name)) { Remove-Item "Env:$name" }
    }
    foreach ($name in $SavedEnv.Keys) { Set-Item "Env:$name" $SavedEnv[$name] }
}

$suffix = @{ RelWithDebInfo = 'rd'; Release = '' }[$Config]
$dll = Join-Path $BuildDir "rexruntime$suffix.dll"
if ((Get-FileSha256 $dll) -ne (Get-FileSha256 (Join-Path $SdkRoot "bin\rexruntime$suffix.dll"))) { throw "$dll is not the patched runtime" }
Invoke-Step 'window placement' { & (Join-Path $BuildDir 'ssf2t_window_placement_tests.exe') }
Invoke-Step 'menu text' { & (Join-Path $BuildDir 'ssf2t_menu_text_tests.exe') }
Invoke-Step 'launcher settings' { & (Join-Path $BuildDir 'ssf2t_launcher_settings_tests.exe') }
Invoke-Step 'fight area' { & (Join-Path $BuildDir 'ssf2t_fight_area_tests.exe') }
Invoke-Step 'infinite time' { & (Join-Path $BuildDir 'ssf2t_infinite_time_tests.exe') }
Invoke-Step 'keyboard remapping' { & (Join-Path $BuildDir 'ssf2t_keyboard_tests.exe') }
Invoke-Step 'player names' { & (Join-Path $BuildDir 'ssf2t_player_name_tests.exe') }
Invoke-Step 'launcher quit' { & (Join-Path $BuildDir 'ssf2t_launcher_quit_tests.exe') }
Invoke-Step 'display scaling' { & (Join-Path $BuildDir 'ssf2t_dpi_tests.exe') }
Invoke-Step 'window host' { & (Join-Path $BuildDir 'ssf2t_window_host_tests.exe') }
Invoke-Step 'game files' { & (Join-Path $BuildDir 'ssf2t_game_files_tests.exe') }
Invoke-Step 'achievements' { & (Join-Path $BuildDir 'ssf2t_achievement_tests.exe') }
Invoke-Step 'achievement metadata' { & (Join-Path $BuildDir 'ssf2t_achievement_metadata_tests.exe') }
Invoke-Step 'updater' { & (Join-Path $BuildDir 'ssf2t_update_tests.exe') }
Invoke-Step 'updater helper' { & (Join-Path $BuildDir 'ssf2t_update_helper_tests.exe') }
Invoke-Step 'guest clock' { & (Join-Path $BuildDir 'ssf2t_guest_clock_tests.exe') }
Invoke-Step 'D3D12 transfer short-circuit' { & (Join-Path $BuildDir 'ssf2t_d3d12_transfer_short_circuit_tests.exe') }
Invoke-Step 'D3D12 sampler memo' { & (Join-Path $BuildDir 'ssf2t_d3d12_sampler_memo_tests.exe') }
Invoke-Step 'shared memory fast path' { & (Join-Path $BuildDir 'ssf2t_shared_memory_fast_path_tests.exe') }
Invoke-Step 'GPU draw state memo' { & (Join-Path $BuildDir 'ssf2t_gpu_draw_state_memo_tests.exe') }
Invoke-Step 'D3D12 async submission' { & (Join-Path $BuildDir 'ssf2t_d3d12_async_submission_tests.exe') }
Invoke-Step 'GPU register wake' { & (Join-Path $BuildDir 'ssf2t_gpu_register_wake_tests.exe') }
Invoke-Step 'log path' { & (Join-Path $BuildDir 'ssf2t_log_path_tests.exe') }
Invoke-Step 'file dialog' { & (Join-Path $BuildDir 'ssf2t_file_dialog_tests.exe') }
Invoke-Step 'diagnostics' { & (Join-Path $BuildDir 'ssf2t_diagnostics_tests.exe') }
Invoke-Step 'crash report' { & (Join-Path $BuildDir 'ssf2t_crash_report_tests.exe') }
Invoke-Step 'active log' { & (Join-Path $BuildDir 'ssf2t_active_log_tests.exe') }
Invoke-Step 'log filter and session log' { & (Join-Path $BuildDir 'ssf2t_log_filter_tests.exe') }
Invoke-Step 'lifecycle' { & (Join-Path $BuildDir 'ssf2t_lifecycle_tests.exe') }
Invoke-Step 'relaunch' { & (Join-Path $BuildDir 'ssf2t_relaunch_tests.exe') }
Invoke-Step 'physical heap stress' { & (Join-Path $BuildDir 'ssf2t_physical_heap_stress.exe') }
Invoke-Step 'title thread shutdown' { & (Join-Path $BuildDir 'ssf2t_title_thread_shutdown_tests.exe') }
Write-Host 'All headless checks passed.'
