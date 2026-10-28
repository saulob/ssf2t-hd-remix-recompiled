<#
.SYNOPSIS
  Downloads the pinned ReXGlue SDK, checks game\extracted\default.xex and the Title Update
  game\extracted\default.xexp, and generates the recompiled C++ sources into generated\default.
  ReXGlue applies default.xexp while it loads default.xex, so the code is generated from version 0.0.2.3.
#>
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

$sdkVersion = '0.10.0'
$sdkZipSha256 = 'AD4485DA853FBE932E9F5DBB643F9B5FE85DC73775EBBAD4DF6879FE6DF0C11B'
$xexSha256 = '6E1C2F9ED7D1102A311A5F5626563527EDA5F85B2FBE8DB8405B866DED3F31EE'
$xexpSha256 = 'A2E8E66B482C5D3A0D3A5D3F67A3CECB7A12CE1F6189196694FD3E765910606C'

$sdkDir = Join-Path $root 'tools\rexglue'
$rexglue = Join-Path $sdkDir 'win-amd64\bin\rexglue.exe'
$xex = Join-Path $root 'game\extracted\default.xex'
$xexp = Join-Path $root 'game\extracted\default.xexp'
$log = Join-Path $root 'generated\codegen.log'
$manifest = 'ssf2t_hd_remix_manifest.toml'

if (-not (Test-Path $rexglue)) {
    $zip = Join-Path $sdkDir "rexglue-sdk-$sdkVersion-win-amd64.zip"
    if (-not (Test-Path $zip)) {
        New-Item -ItemType Directory -Force $sdkDir | Out-Null
        $url = "https://github.com/rexglue/rexglue-sdk/releases/download/v$sdkVersion/rexglue-sdk-$sdkVersion-win-amd64.zip"
        Write-Host "Downloading ReXGlue SDK $sdkVersion..."
        Invoke-WebRequest -Uri $url -OutFile $zip
    }
    if ((Get-FileHash $zip -Algorithm SHA256).Hash -ne $sdkZipSha256) { throw "$zip is not the official ReXGlue $sdkVersion release" }
    Expand-Archive -Path $zip -DestinationPath $sdkDir -Force
}
$installed = "$(& $rexglue --version)".Trim()
if ($installed -ne $sdkVersion) { throw "tools\rexglue has ReXGlue $installed; expected $sdkVersion (delete tools\rexglue to re-download)" }

if (-not (Test-Path $xex)) { throw "Missing $xex (extract the XBLA package into game\extracted first)" }
$hash = (Get-FileHash $xex -Algorithm SHA256).Hash
if ($hash -ne $xexSha256) { throw "Unsupported default.xex (SHA-256 $hash). Expected $xexSha256 (Super Street Fighter II Turbo HD Remix XBLA 0.0.1.3)." }
if (-not (Test-Path $xexp)) { throw "Missing $xexp (extract the Title Update package into game\extracted with tools\extract_stfs.ps1)" }
$hash = (Get-FileHash $xexp -Algorithm SHA256).Hash
if ($hash -ne $xexpSha256) { throw "Unsupported default.xexp (SHA-256 $hash). Expected $xexpSha256 (Title Update #1, version 0.0.2.3)." }
Write-Host 'default.xex and its Title Update are the supported versions.'

New-Item -ItemType Directory -Force (Split-Path $log) | Out-Null
Push-Location $root
try {
    # rexglue logs to stderr; in Windows PowerShell 5.1 that would trip 'Stop', so judge by exit code.
    $ErrorActionPreference = 'Continue'
    $timer = [Diagnostics.Stopwatch]::StartNew()
    & $rexglue codegen $manifest 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $log
    $exitCode = $LASTEXITCODE
    Write-Host ("codegen exit code {0} after {1:N1} s (log: {2})" -f $exitCode, $timer.Elapsed.TotalSeconds, $log)
    if ($exitCode -ne 0) { throw "codegen failed ($exitCode)" }
} finally { Pop-Location; $ErrorActionPreference = 'Stop' }
