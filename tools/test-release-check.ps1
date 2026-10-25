<#
.SYNOPSIS
  Checks that check-release-folder.ps1 accepts a fresh extraction and refuses player data, logs, game files,
  debug markers and developer paths. Scratch folders only; nothing is built, launched or published.
#>
$ErrorActionPreference = 'Stop'
$ReleaseCheck = Join-Path $PSScriptRoot 'check-release-folder.ps1'
$Root = Split-Path $PSScriptRoot -Parent
$StageRoot = Join-Path ([IO.Path]::GetTempPath()) 'ssf2t_release_check'

# Each case adds one item to a clean extraction: a relative path, or path -> content.
$Cases = [ordered]@{
    'fresh extraction'   = $null
    'empty user_data'    = @{ Path = 'user_data\' }
    'settings'           = @{ Path = 'user_data\settings.toml' }
    'title save folder'  = @{ Path = '584108A4\content' }
    'cache'              = @{ Path = 'cache\shaders\a.bin' }
    'migration leftover' = @{ Path = 'user_data.migrating\' }
    'log'                = @{ Path = 'logs\ssf2t_hd_remix_001.log' }
    'game file'          = @{ Path = 'game\default.xex' }
    'game file mixed'    = @{ Path = 'game\Default.xex' }
    'game file upper'    = @{ Path = 'DEFAULT.XEX' }
    'asset archive'      = @{ Path = 'flog_x360.fpg' }
    'pdb'                = @{ Path = 'ssf2t_hd_remix.pdb' }
    'updater pdb'        = @{ Path = 'trg-updater.pdb' }
    'debug file'         = @{ Path = 'trg-updater.debug' }
    'renderdoc'          = @{ Path = 'renderdoc.dll' }
    'vulkan layer'       = @{ Path = 'VkLayer_test.json' }
    'unexpected file'    = @{ Path = 'notes.txt' }
    'nested xex'         = @{ Path = 'licenses\Default.Xex' }
    'any xex'            = @{ Path = 'other.xex' }
    'crash dump'         = @{ Path = 'crash.dmp' }
    'backup'             = @{ Path = 'ssf2t_hd_remix.toml.bak' }
    'temp file'          = @{ Path = 'x.tmp' }
    'save file'          = @{ Path = 'slot.sav' }
    'user profile data'  = @{ Path = 'achievements\584108A4.toml' }
    'missing license'    = @{ Path = 'licenses\ReXGlue-LICENSE.txt'; Delete = $true }
    'missing readme'     = @{ Path = 'README.txt'; Delete = $true }
    'missing updater'    = @{ Path = 'trg-updater.exe'; Delete = $true }
    'updater workspace'  = @{ Path = 'trg-update-1234\job.txt' }
    'updater log'        = @{ Path = 'trg-updater.log' }
    'save data'          = @{ Path = 'savegames.dat' }
    'user profile path'  = @{ Path = 'rexruntime.dll'; Bytes = [Text.Encoding]::ASCII.GetBytes('x C:\Users\someone\source\a.cpp') }
    'user path 16'       = @{ Path = 'rexgpu-xenos.dll'; Bytes = [Text.Encoding]::Unicode.GetBytes('x D:\Users\someone\a.cpp') }
    'repos path'         = @{ Path = 'ssf2t_hd_remix.exe'; Bytes = [Text.Encoding]::ASCII.GetBytes('x:/VisualStudio/repos/y/a.cpp') }
    'xex magic'          = @{ Path = 'rexruntime.dll'; Bytes = [Text.Encoding]::ASCII.GetBytes('XEX2....') }
    'package magic'      = @{ Path = 'rexruntime.dll'; Bytes = [Text.Encoding]::ASCII.GetBytes('LIVE....') }
    'debug marker'       = @{ Path = 'ssf2t_hd_remix.toml'; Bytes = [Text.Encoding]::ASCII.GetBytes('DEBUG_SAULO: x') }
    'repository path'    = @{ Path = 'rexruntime.dll'; Bytes = [Text.Encoding]::ASCII.GetBytes("path $Root x") }
    'repository path 16' = @{ Path = 'rexgpu-xenos.dll'; Bytes = [Text.Encoding]::Unicode.GetBytes("path $Root x") }
}
$failures = 0
try {
    foreach ($case in $Cases.Keys) {
        $stage = Join-Path $StageRoot ($case -replace '\W', '_')
        New-Item -ItemType Directory -Force $stage | Out-Null
        foreach ($leaf in 'ssf2t_hd_remix.exe', 'trg-updater.exe', 'rexruntime.dll', 'rexgpu-xenos.dll', 'ssf2t_hd_remix.toml', 'README.txt', 'LICENSE') {
            Set-Content -LiteralPath (Join-Path $stage $leaf) -Value 'placeholder'
        }
        New-Item -ItemType Directory -Force (Join-Path $stage 'assets\launcher') | Out-Null
        Set-Content -LiteralPath (Join-Path $stage 'assets\launcher\header-art.png') -Value 'placeholder'
        New-Item -ItemType Directory -Force (Join-Path $stage 'licenses') | Out-Null
        foreach ($leaf in 'ReXGlue-LICENSE.txt', 'THIRD-PARTY-LICENSES.txt') {
            Set-Content -LiteralPath (Join-Path $stage "licenses\$leaf") -Value 'placeholder'
        }
        $extra = $Cases[$case]
        if ($extra) {
            $target = Join-Path $stage $extra.Path
            if ($extra.Delete) {
                Remove-Item -LiteralPath $target
            } elseif ($extra.Path.EndsWith('\')) {
                New-Item -ItemType Directory -Force $target | Out-Null
            } else {
                New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
                if ($extra.Bytes) { [IO.File]::WriteAllBytes($target, $extra.Bytes) } else { Set-Content -LiteralPath $target -Value 'placeholder' }
            }
        }
        $global:LASTEXITCODE = 0
        & $ReleaseCheck -Folder $stage *> $null
        $accepted = $LASTEXITCODE -eq 0
        if ($accepted -ne ($null -eq $extra)) {
            Write-Host "  FAIL $case"
            $failures++
        }
    }
} finally {
    Remove-Item -LiteralPath $StageRoot -Recurse -Force -ErrorAction SilentlyContinue
}
Write-Host "$($Cases.Count) release check cases, $failures failures"
if ($failures) { throw 'Release check tests failed' }
$global:LASTEXITCODE = 0
