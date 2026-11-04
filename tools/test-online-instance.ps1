<#
.SYNOPSIS
  Checks tools\online-instance.ps1 -PrepareOnly in scratch folders: every instance gets its own user folder, identity
  path, logs, settings and online name, nothing identifying is copied, log files never collide, and nothing is started.
#>
$ErrorActionPreference = 'Stop'
$Script = Join-Path $PSScriptRoot 'online-instance.ps1'
$Work = Join-Path ([IO.Path]::GetTempPath()) ('ssf2t-online-instance-' + [guid]::NewGuid().ToString('N'))
$failures = 0
$checks = 0
function Check([bool]$ok, [string]$what) {
    $script:checks++
    if (-not $ok) { $script:failures++; Write-Host "  FAIL $what" -ForegroundColor Red }
}
function LineValue([string]$text, [string]$label) {
    $line = ($text -split "`r?`n" | Where-Object { $_ -match ('^\s*' + [regex]::Escape($label) + ':') } | Select-Object -First 1)
    if ($line) { ($line -replace ('^\s*' + [regex]::Escape($label) + ':\s*'), '').Trim() } else { '' }
}

try {
    $main = Join-Path $Work 'main'
    $instances = Join-Path $Work 'instances'
    New-Item -ItemType Directory -Force $main | Out-Null
    Set-Content (Join-Path $main 'settings.toml') @(
        '# main settings', 'resolution_scale = 2', 'ssf2t_aspect_ratio = "16:9"', 'ssf2t_launcher_x = 114',
        'ssf2t_launcher_maximized = true', 'ssf2t_online_enabled = true', 'ssf2t_online_name = "Main"',
        'ssf2t_infinite_time = true', 'hcu_launcher_width = 2332', 'detailed_logging = false')
    Set-Content (Join-Path $main 'online_identity.ini') 'token=secret'
    Set-Content (Join-Path $main 'online_instance.lock') ''

    $a = & $Script -Name a -CopyFrom $main -InstanceRoot $instances -PrepareOnly 6>&1 | Out-String -Width 4096
    $b = & $Script -Name b -CopyFrom $main -InstanceRoot $instances -PrepareOnly 6>&1 | Out-String -Width 4096

    $userA = Join-Path $instances 'a\user_data'
    $userB = Join-Path $instances 'b\user_data'
    $logsA = Join-Path $instances 'a\logs'
    $logsB = Join-Path $instances 'b\logs'
    Check (Test-Path $userA) 'instance a user folder exists'
    Check (Test-Path $userB) 'instance b user folder exists'
    Check ($userA -ne $userB) 'user folders differ'
    Check (Test-Path $logsA) 'instance a logs exist'
    Check (Test-Path $logsB) 'instance b logs exist'
    Check (-not (Test-Path (Join-Path $userA 'online_identity.ini'))) 'identity is not copied to a'
    Check (-not (Test-Path (Join-Path $userB 'online_identity.ini'))) 'identity is not copied to b'
    Check (-not (Test-Path (Join-Path $userB 'online_instance.lock'))) 'lock is not copied to b'
    Check ((LineValue $a 'identity') -ne (LineValue $b 'identity')) 'identity files differ between a and b'
    Check ((LineValue $a 'identity') -like "$userA*") 'identity a is inside user folder a'
    Check ((LineValue $b 'identity') -like "$userB*") 'identity b is inside user folder b'

    $settingsA = Get-Content (Join-Path $userA 'settings.toml')
    $settingsB = Get-Content (Join-Path $userB 'settings.toml')
    Check ($settingsA -contains 'resolution_scale = 2') 'display settings are copied'
    Check ($settingsA -contains 'ssf2t_aspect_ratio = "16:9"') 'aspect ratio is copied'
    Check (-not ($settingsA -match 'launcher_(x|maximized|width)')) 'window placement is not copied'
    Check (-not ($settingsA -match 'online_enabled')) 'Online play stays at its default Off'
    Check (-not ($settingsA -match 'infinite_time')) 'Infinite Time is not copied'
    Check (-not ($settingsA -match 'detailed_logging')) 'detailed logging keeps its default On, so logs are kept'
    Check ($settingsA -contains 'ssf2t_online_name = "Test-a"') 'instance a online name'
    Check ($settingsB -contains 'ssf2t_online_name = "Test-b"') 'instance b online name'
    Check ((LineValue $a 'settings') -eq (Join-Path $userA 'settings.toml')) 'summary a shows its settings file'
    Check ((LineValue $b 'settings') -eq (Join-Path $userB 'settings.toml')) 'summary b shows its settings file'

    $fileA = LineValue $a 'log file'
    $fileB = LineValue $b 'log file'
    Check ($fileA -eq (Join-Path $logsA 'ssf2t_hd_remix_a_001.log')) 'log a is named after the instance'
    Check ($fileB -eq (Join-Path $logsB 'ssf2t_hd_remix_b_001.log')) 'log b is named after the instance'
    Check ((Split-Path $fileA -Leaf) -ne (Split-Path $fileB -Leaf)) 'log file names differ between a and b'
    Check ((Split-Path $fileA -Parent) -eq (LineValue $a 'logs folder')) 'log a is inside the logs folder it prints'
    Check (-not ($fileA -match 'ssf2t_hd_remix\.log$') -and -not ($fileB -match 'ssf2t_hd_remix\.log$')) 'no shared generic log name'
    Check (-not ($fileA -match '\(\d+\)') -and -not ($fileB -match '\(\d+\)')) 'no "(1)" style names'

    Set-Content $fileA 'previous run'
    Set-Content (Join-Path $logsA 'ssf2t_hd_remix_a_007.log') 'previous run'
    Add-Content (Join-Path $userA 'settings.toml') 'ssf2t_online_enabled = true'
    $again = & $Script -Name a -CopyFrom $main -InstanceRoot $instances -PrepareOnly 6>&1 | Out-String -Width 4096
    Check ((Get-Content (Join-Path $userA 'settings.toml')) -contains 'ssf2t_online_enabled = true') 'a later start keeps the instance settings'
    Check ((LineValue $again 'log file') -eq (Join-Path $logsA 'ssf2t_hd_remix_a_008.log')) 'a later start writes a new numbered log'
    Check ((Get-Content $fileA) -eq 'previous run') 'an earlier log is left alone'

    $lines = Get-Content (Join-Path $userA 'settings.toml') | Where-Object { $_ -notmatch 'ssf2t_online_name' }
    Set-Content (Join-Path $userA 'settings.toml') $lines
    & $Script -Name a -CopyFrom $main -InstanceRoot $instances -PrepareOnly 6>&1 | Out-Null
    Check ((Get-Content (Join-Path $userA 'settings.toml')) -contains 'ssf2t_online_name = "Test-a"') 'a missing online name is restored, not random'
    Set-Content (Join-Path $userA 'settings.toml') ((Get-Content (Join-Path $userA 'settings.toml')) + 'ssf2t_online_name = "Saulo"')
    $lines = Get-Content (Join-Path $userA 'settings.toml') | Where-Object { $_ -notmatch 'Test-a' }
    Set-Content (Join-Path $userA 'settings.toml') $lines
    & $Script -Name a -CopyFrom $main -InstanceRoot $instances -PrepareOnly 6>&1 | Out-Null
    Check ((Get-Content (Join-Path $userA 'settings.toml')) -contains 'ssf2t_online_name = "Saulo"') 'a chosen online name is kept'

    foreach ($text in $a, $b) {
        Check ($text -match 'instance:') 'summary names the instance'
        Check ($text -match 'user folder:') 'summary shows the user folder'
        Check ($text -match 'logs folder:') 'summary shows the logs folder'
        Check ($text -match 'log file:') 'summary shows the exact log file'
        Check ($text -match 'online service: production') 'summary shows the production service'
    }
    Check ($a -match [regex]::Escape($userA)) 'summary a shows its folder'
    Check ($b -match [regex]::Escape($userB)) 'summary b shows its folder'

    $named = & $Script -Name c -OnlineName 'SauloC' -CopyFrom $main -InstanceRoot $instances -PrepareOnly 6>&1 | Out-String -Width 4096
    Check ((Get-Content (Join-Path $instances 'c\user_data\settings.toml')) -contains 'ssf2t_online_name = "SauloC"') '-OnlineName is used'

    # The secret-character matrix: each instance gets its own options, and none are copied from the main folder.
    Add-Content (Join-Path $main 'settings.toml') @('ssf2t_enable_akuma = true', 'ssf2t_enable_ultra_fighters = true')
    $plain = & $Script -Name d -CopyFrom $main -InstanceRoot $instances -PrepareOnly 6>&1 | Out-String -Width 4096
    $settingsD = Get-Content (Join-Path $instances 'd\user_data\settings.toml')
    Check (-not ($settingsD -match 'enable_akuma|enable_ultra_fighters')) 'secret options are not copied from the main folder'
    Check ((LineValue $plain 'secret options') -eq 'ssf2t_enable_akuma=false ssf2t_enable_ultra_fighters=false') 'summary shows both options Off'
    $on = & $Script -Name e -Akuma On -EvilRyu Off -CopyFrom $main -InstanceRoot $instances -PrepareOnly 6>&1 | Out-String -Width 4096
    $settingsE = Get-Content (Join-Path $instances 'e\user_data\settings.toml')
    Check ($settingsE -contains 'ssf2t_enable_akuma = true') '-Akuma On is written'
    Check ($settingsE -contains 'ssf2t_enable_ultra_fighters = false') '-EvilRyu Off is written'
    Check ((LineValue $on 'secret options') -eq 'ssf2t_enable_akuma=true ssf2t_enable_ultra_fighters=false') 'summary shows the chosen options'
    $kept = & $Script -Name e -EvilRyu On -CopyFrom $main -InstanceRoot $instances -PrepareOnly 6>&1 | Out-String -Width 4096
    $settingsE2 = Get-Content (Join-Path $instances 'e\user_data\settings.toml')
    Check (($settingsE2 -contains 'ssf2t_enable_akuma = true') -and ($settingsE2 -contains 'ssf2t_enable_ultra_fighters = true')) 'a later start changes only the option it names'
    Check (@($settingsE2 | Where-Object { $_ -match '^ssf2t_enable_akuma' }).Count -eq 1) 'an option is written once'
    $badChoice = $false
    try { & $Script -Name f -Akuma Maybe -InstanceRoot $instances -PrepareOnly 6>&1 | Out-Null } catch { $badChoice = $true }
    Check $badChoice 'an option other than On or Off is refused'

    $rejected = $false
    try { & $Script -Name 'bad name!' -InstanceRoot $instances -PrepareOnly 6>&1 | Out-Null } catch { $rejected = $true }
    Check $rejected 'an invalid instance name is refused'
    Check (-not (Get-Process -Name 'ssf2t_hd_remix' -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$Work*" })) 'nothing was started'
} finally {
    Remove-Item -LiteralPath $Work -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "online instance script: $checks checks, $failures failures"
if ($failures) { exit 1 }
