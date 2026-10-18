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
    'renderdoc'          = @{ Path = 'renderdoc.dll' }
    'vulkan layer'       = @{ Path = 'VkLayer_test.json' }
    'unexpected file'    = @{ Path = 'notes.txt' }
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
        foreach ($leaf in 'ssf2t_hd_remix.exe', 'rexruntime.dll', 'rexgpu-xenos.dll', 'ssf2t_hd_remix.toml', 'README.txt', 'LICENSE') {
            Set-Content -LiteralPath (Join-Path $stage $leaf) -Value 'placeholder'
        }
        $extra = $Cases[$case]
        if ($extra) {
            $target = Join-Path $stage $extra.Path
            if ($extra.Path.EndsWith('\')) {
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
