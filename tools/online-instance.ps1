<#
.SYNOPSIS
  Starts one isolated test instance of the game for online testing on a single PC: its own user folder (settings,
  online identity, friend code, saves, caches) and its own log file, on the production online service.
.DESCRIPTION
  Instance "a" uses build\online-instances\a\user_data and build\online-instances\a\logs, instance "b" the same with "b",
  and so on. The first start copies the display and graphics settings of the main user folder (-CopyFrom) and gives the
  instance its own online name (Test-a, Test-b, or -OnlineName), Online play Off and no Infinite Time; later starts keep
  the instance's own files and only restore a missing online name. The online identity is never copied, so every
  instance makes its own friend code on first use. Every start writes a new log named after the instance
  (ssf2t_hd_remix_a_001.log, _002, ...), always inside that instance's logs folder, and detailed logging is on. Fights
  use the fixed gameplay ports 7001 (the Player Match host) and 7002; the two instances on one PC find each other through the
  online service and need no addresses, ports or codes.
  Runs the launcher; Online play is switched on from the instance's own Online page. -PrepareOnly sets the folders up
  and prints the same summary without starting the game.
.EXAMPLE
  tools\online-instance.ps1 -Name a            # terminal 1
  tools\online-instance.ps1 -Name b            # terminal 2
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9-]{1,16}$')][string]$Name,
    [string]$OnlineName = '',
    [string]$CopyFrom = '',
    [string]$Executable = '',
    [string]$GameData = '',
    [string]$InstanceRoot = '',
    [switch]$PrepareOnly
)
$ErrorActionPreference = 'Stop'

$Root = Split-Path $PSScriptRoot -Parent
$BuildDir = Join-Path $Root 'build'
if (-not $Executable) { $Executable = Join-Path $BuildDir 'ssf2t_hd_remix.exe' }
if (-not $GameData) { $GameData = Join-Path $Root 'game\extracted' }
if (-not $CopyFrom) { $CopyFrom = Join-Path $BuildDir 'user_data' }
if (-not $InstanceRoot) { $InstanceRoot = Join-Path $BuildDir 'online-instances' }
$label = $Name.ToLowerInvariant()
if (-not $OnlineName) { $OnlineName = "Test-$label" }
$OnlineName = ($OnlineName -replace '[^A-Za-z0-9_-]', '')
if ($OnlineName.Length -gt 16) { $OnlineName = $OnlineName.Substring(0, 16) }
if (-not $OnlineName) { $OnlineName = 'Player' }

$user = Join-Path (Join-Path $InstanceRoot $label) 'user_data'
$logs = Join-Path (Join-Path $InstanceRoot $label) 'logs'
$settingsFile = Join-Path $user 'settings.toml'

# Window placement, the online choice, the cheat and the logging choice are the instance's own; everything else is copied once.
$OwnKeys = '^\s*(ssf2t|hcu)_(launcher_(x|y|width|height|maximized|fullscreen)|online_(enabled|name|ping_display)|infinite_time)\s*=|^\s*detailed_logging\s*='
$NameKey = 'ssf2t_online_name'

function Set-SettingLine([string[]]$Lines, [string]$Key, [string]$Value) {
    @($Lines | Where-Object { $_ -notmatch ('^\s*' + [regex]::Escape($Key) + '\s*=') }) + "$Key = $Value"
}

function Next-LogFile([string]$Folder, [string]$Label) {
    $prefix = "ssf2t_hd_remix_${Label}_"
    $last = 0
    foreach ($file in Get-ChildItem -LiteralPath $Folder -Filter "$prefix*.log" -ErrorAction SilentlyContinue) {
        if ($file.BaseName.Substring($prefix.Length) -match '^\d{1,9}$') { $last = [Math]::Max($last, [int]$Matches[0]) }
    }
    Join-Path $Folder ('{0}{1:000}.log' -f $prefix, ($last + 1))
}

$fresh = -not (Test-Path -LiteralPath $user)
if ($fresh) {
    New-Item -ItemType Directory -Force $user | Out-Null
    $lines = @('# Super Street Fighter II Turbo HD Remix: online test instance settings.')
    $source = Join-Path $CopyFrom 'settings.toml'
    if (Test-Path -LiteralPath $source) {
        $lines = @(Get-Content -LiteralPath $source | Where-Object { $_ -notmatch $OwnKeys })
    }
    $lines = Set-SettingLine $lines $NameKey ('"' + $OnlineName + '"')
    Set-Content -LiteralPath $settingsFile -Value $lines -Encoding utf8
} elseif (-not (Select-String -LiteralPath $settingsFile -Pattern ('^\s*' + $NameKey + '\s*=') -Quiet)) {
    Add-Content -LiteralPath $settingsFile -Value "$NameKey = `"$OnlineName`"" -Encoding utf8
}
New-Item -ItemType Directory -Force $logs | Out-Null
$logFile = Next-LogFile $logs $label

Write-Host "Online test instance $($label.ToUpperInvariant())$(if ($fresh) { ' (new)' })"
Write-Host "  instance:       $label"
Write-Host "  user folder:    $user"
Write-Host "  settings:       $settingsFile"
Write-Host "  identity:       $(Join-Path $user 'online_identity.ini') (made on first use; this instance has its own friend code)"
Write-Host "  logs folder:    $logs"
Write-Host "  log file:       $logFile"
Write-Host "  online service: $(if ($env:SSF2T_LOBBY) { "SSF2T_LOBBY override $env:SSF2T_LOBBY" } else { 'production' })"

if ($PrepareOnly) { return }

if (-not (Test-Path -LiteralPath $Executable)) { throw "Not built yet: $Executable" }
$gameArgs = @("--game_data_root=`"$GameData`"", "--user_data_root=`"$user`"", "--log_file=`"$logFile`"")
Start-Process -FilePath $Executable -ArgumentList $gameArgs -WorkingDirectory (Split-Path -Parent $Executable)
