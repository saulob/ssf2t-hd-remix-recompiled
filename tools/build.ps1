<#
.SYNOPSIS
  Builds build\ssf2t_hd_remix.exe with Visual Studio's clang++, lld-link, CMake and Ninja.
  Only -Run launches the game.

.DESCRIPTION
  First makes sure the patched ReXGlue runtime and GPU plugin in tools\rexglue match the baseline and
  patches\rexglue-sdk, rebuilding them with build-patched-rexruntime.ps1 when they do not. After the build it
  makes sure the DLLs next to the executable are the ones in tools\rexglue, and it never launches without that.

.EXAMPLE
  .\tools\build.ps1              # incremental build only
  .\tools\build.ps1 -Clean       # clean configure and build
  .\tools\build.ps1 -Run         # build, then launch the game
  .\tools\build.ps1 -Clean -Run
  .\tools\build.ps1 -Config Release -BuildDir build-release   # separate tree
#>
param(
    [switch]$Clean,
    [switch]$Run,
    [switch]$NoRun,  # overrides -Run; building only is the default
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release')]
    [string]$Config = 'RelWithDebInfo',
    [string]$BuildDir,  # relative to the repository root; default: build
    [int]$Jobs = 0
)
$ErrorActionPreference = 'Stop'

$Root = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'rexruntime-patch.ps1')
$SdkRoot = Join-Path $Root 'tools\rexglue\win-amd64'
$BuildDir = if (-not $BuildDir) { Join-Path $Root 'build' } elseif ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $Root $BuildDir }
$GameData = Join-Path $Root 'game\extracted'
$UserData = Join-Path $BuildDir 'user_data'
$Exe = Join-Path $BuildDir 'ssf2t_hd_remix.exe'
$XexSha256 = '6E1C2F9ED7D1102A311A5F5626563527EDA5F85B2FBE8DB8405B866DED3F31EE'

$VsRoot = Get-VisualStudioRoot
$VsDevCmd = Join-Path $VsRoot 'Common7\Tools\VsDevCmd.bat'
$LlvmBin = Join-Path $VsRoot 'VC\Tools\Llvm\x64\bin'
$ClangXX = Join-Path $LlvmBin 'clang++.exe'
$LldLink = Join-Path $LlvmBin 'lld-link.exe'
$CMake = Join-Path $VsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$Ninja = Join-Path $VsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'

foreach ($tool in @($VsDevCmd, $ClangXX, $LldLink, $CMake, $Ninja)) {
    if (-not (Test-Path $tool)) { throw "Required tool not found: $tool (install Visual Studio with the LLVM and CMake components)" }
}

$Xex = Join-Path $Root 'game\extracted\default.xex'
if (-not (Test-Path $Xex)) { throw "Missing $Xex (extract the XBLA package into game\extracted first)" }
if ((Get-FileHash $Xex -Algorithm SHA256).Hash -ne $XexSha256) { throw "Unsupported $Xex (expected SHA-256 $XexSha256)" }
if (-not (Test-Path (Join-Path $SdkRoot 'lib\cmake\rexglue'))) { throw 'Missing tools\rexglue (run tools\codegen.ps1 first)' }
if (-not (Test-Path (Join-Path $Root 'generated\default\sources.cmake'))) { throw 'Missing generated\default (run tools\codegen.ps1 first)' }

function Assert-RuntimeReady {
    $problems = @(Test-StagedRuntime $SdkRoot $BuildDir $Config)
    if ($Config -ne 'Debug') { $problems += @(Test-PatchedRuntime $SdkRoot) }
    if (-not $problems.Count) { return }
    Write-Host ''
    Write-Host 'RUNTIME CHECK FAILED' -ForegroundColor Red
    foreach ($problem in $problems) { Write-Host "  $problem" -ForegroundColor Red }
    throw 'The files next to ssf2t_hd_remix.exe are not the current ReXGlue runtime and GPU plugin from tools\rexglue'
}

if ($Config -eq 'Debug') {
    Write-Host 'ReXGlue patches: not applied to the Debug runtime (stock rexruntimed.dll and rexgpu-xenosd.dll)'
} else {
    Update-PatchedRuntime $SdkRoot $Jobs
}

$SavedEnv = [Environment]::GetEnvironmentVariables()
try {
    # Keep MSYS2's gcc, cmake and ninja out of the build.
    $env:PATH = (($env:PATH -split ';') | Where-Object { $_ -and $_ -notmatch '\\msys64\\' -and $_ -notmatch '^C:\\msys64' }) -join ';'

    Write-Host ''
    Write-Host '===================================================='
    Write-Host " Super Street Fighter II Turbo HD Remix - $Config build"
    Write-Host '===================================================='
    $env:PATH = "$env:PATH;$(Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer')"
    $vsEnv = cmd.exe /c "`"$VsDevCmd`" -arch=x64 -host_arch=x64 -no_logo >nul && set"
    foreach ($line in $vsEnv) {
        if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2] }
    }
    if (-not $env:VCToolsVersion) { throw 'VsDevCmd.bat did not set up the x64 environment' }

    $env:PATH = "$LlvmBin;$(Split-Path $CMake);$(Split-Path $Ninja);$env:PATH"
    $env:CC = Join-Path $LlvmBin 'clang.exe'
    $env:CXX = $ClangXX

    Write-Host "Clang C++: $(& $ClangXX --version | Select-Object -First 1)"
    Write-Host "CMake:     $(& $CMake --version | Select-Object -First 1)"
    Write-Host "Ninja:     $(& $Ninja --version)"
    Write-Host "MSVC:      $env:VCToolsVersion"
    Write-Host ''

    if ($Clean -and (Test-Path $BuildDir)) {
        Write-Host 'Cleaning build (keeping build\user_data)...'
        Get-ChildItem $BuildDir -Force | Where-Object { $_.Name -ne 'user_data' } | Remove-Item -Recurse -Force
    }

    $cache = Join-Path $BuildDir 'CMakeCache.txt'
    $configured = (Test-Path (Join-Path $BuildDir 'build.ninja')) -and (Test-Path $cache) -and
        (Select-String -Path $cache -SimpleMatch "CMAKE_BUILD_TYPE:STRING=$Config" -Quiet) -and
        (Select-String -Path $cache -SimpleMatch "CMAKE_CXX_COMPILER:FILEPATH=$($ClangXX -replace '\\', '/')" -Quiet)
    if (-not $configured) {
        Write-Host 'Configuring...'
        & $CMake -S $Root -B $BuildDir -G Ninja "-DCMAKE_BUILD_TYPE=$Config" `
            "-DCMAKE_CXX_COMPILER:FILEPATH=$ClangXX" "-DCMAKE_LINKER:FILEPATH=$LldLink" "-DCMAKE_MAKE_PROGRAM:FILEPATH=$Ninja"
        if ($LASTEXITCODE) { throw "CMake configure failed ($LASTEXITCODE)" }
    }

    Write-Host 'Building...'
    $parallel = if ($Jobs -gt 0) { @('--parallel', $Jobs) } else { @() }
    & $CMake --build $BuildDir @parallel
    if ($LASTEXITCODE) { throw "Build failed ($LASTEXITCODE)" }
} finally {
    foreach ($name in @([Environment]::GetEnvironmentVariables().Keys)) {
        if (-not $SavedEnv.Contains($name)) { Remove-Item "Env:$name" }
    }
    foreach ($name in $SavedEnv.Keys) { Set-Item "Env:$name" $SavedEnv[$name] }
}

Sync-StagedRuntime $SdkRoot $BuildDir $Config
$suffix = @{ Debug = 'd'; RelWithDebInfo = 'rd'; Release = '' }[$Config]
$required = @('ssf2t_hd_remix.exe', 'trg-updater.exe', 'ssf2t_hd_remix.toml') + (Get-RuntimeDllNames $Config)
if ($Config -ne 'Release') { $required += "TracyClient$suffix.dll" }
foreach ($file in $required) {
    $path = Join-Path $BuildDir $file
    if (-not (Test-Path $path)) { throw "Build output is missing $path" }
}
Assert-RuntimeReady

Write-Host ''
Write-Host '===================================================='
Write-Host ' BUILD SUCCESSFUL'
Write-Host '===================================================='
Write-Host "  $Exe"
Write-Host "  runtime: $((Get-RuntimeDllNames $Config) -join ', ') match tools\rexglue"
Write-Host ''

if (-not $Run -or $NoRun) { return }

$running = Get-Process -Name 'ssf2t_hd_remix' -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $Exe }
if ($running) {
    Write-Warning "ssf2t_hd_remix.exe is already running (PID $($running.Id -join ', ')); not launching another copy."
    return
}

Assert-RuntimeReady
$gameArgs = @("--game_data_root=`"$GameData`"", "--user_data_root=`"$UserData`"")
Write-Host 'Launching:'
Write-Host "  $Exe $($gameArgs -join ' ')"
Write-Host "  working directory: $BuildDir"
Write-Host "  logs: $(Join-Path $BuildDir 'logs')"
Start-Process -FilePath $Exe -ArgumentList $gameArgs -WorkingDirectory $BuildDir
