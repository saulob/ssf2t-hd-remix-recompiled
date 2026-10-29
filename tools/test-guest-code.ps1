<#
.SYNOPSIS
  Headless checks of the guest symbol prefix step and the generated-image currency stamp. Needs no game file,
  generated code or SDK.
#>
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'guest-code.ps1')

$failures = 0
function Assert-That([bool]$Condition, [string]$What) {
    if (-not $Condition) { $script:failures++; Write-Host "  FAIL $What" -ForegroundColor Red }
}

$work = Join-Path ([IO.Path]::GetTempPath()) ('ssf2t_guest_code_' + [Guid]::NewGuid().ToString('N'))
try {
    $raw = Join-Path $work 'raw'
    New-Item -ItemType Directory -Force $raw | Out-Null
    [IO.File]::WriteAllText((Join-Path $raw 'ssf2t_hd_remix_pch.h'), @'
#define DEFINE_REX_FUNC(name) extern "C" void __imp__##name(int)
#define DECLARE_REX_FUNC(name) extern "C" void name(int)
extern const rex::PPCImageInfo PPCImageConfig;
extern PPCFuncMapping PPCFuncMappings[];
'@)
    [IO.File]::WriteAllText((Join-Path $raw 'ssf2t_hd_remix_funcs.0.h'), @'
DECLARE_REX_FUNC(sub_82170000);
DECLARE_REX_FUNC(__savegprlr_14);
DECLARE_REX_FUNC(__restvmx_127);
DECLARE_REX_FUNC(xstart);
DECLARE_REX_FUNC(__imp__KeLockL2);
DECLARE_REX_FUNC(__imp____C_specific_handler);
'@)
    [IO.File]::WriteAllText((Join-Path $raw 'ssf2t_hd_remix_recomp.0.cpp'), @'
DEFINE_REX_FUNC(sub_82170000) {
	sub_82170128(ctx, base);
	__imp__KeLockL2(ctx, base);
	__imp____C_specific_handler(ctx, base);
	__savegprlr_14(ctx, base);
	REXLOG_WARN("SEH exception caught in sub_82170000");
	ctx.r3.u32 = 0x82170000;
	goto loc_82170018;
}
DEFINE_REX_FUNC(xstart) {
	__imp__sub_82170000(ctx, base);
}
'@)
    [IO.File]::WriteAllText((Join-Path $raw 'ssf2t_hd_remix_init.cpp'), @'
const rex::PPCImageInfo PPCImageConfig = { .func_mappings = PPCFuncMappings };
PPCFuncMapping PPCFuncMappings[] = { { 0x82170000, sub_82170000 }, { 0x839C4D48, xstart }, { 0, nullptr } };
'@)
    [IO.File]::WriteAllText((Join-Path $raw 'ssf2t_hd_remix_register.cpp'), "void ssf2t_hd_remix_RegisterFunctions(int);`n")
    [IO.File]::WriteAllText((Join-Path $raw 'sources.cmake'), "set(GENERATED_SOURCES `${CMAKE_CURRENT_LIST_DIR}/ssf2t_hd_remix_recomp.0.cpp)`n")
    [IO.File]::WriteAllText((Join-Path $raw 'codegen.d'), 'S:/abs/path')

    $images = @{}
    foreach ($item in $GuestVariants) {
        $out = Join-Path $work $item.Name
        Add-GuestSymbolPrefix $raw $out $item.Prefix | Out-Null
        $images[$item.Name] = Get-ChildItem $out -File | ForEach-Object { $_.Name + "`n" + [IO.File]::ReadAllText($_.FullName) } | Out-String
        $p = $item.Prefix
        $code = [IO.File]::ReadAllText((Join-Path $out 'ssf2t_hd_remix_recomp.0.cpp'))
        $decl = [IO.File]::ReadAllText((Join-Path $out 'ssf2t_hd_remix_funcs.0.h'))
        $init = [IO.File]::ReadAllText((Join-Path $out 'ssf2t_hd_remix_init.cpp'))

        Assert-That $code.Contains("DEFINE_REX_FUNC(${p}sub_82170000)") "$($item.Name): function definition is prefixed"
        Assert-That $code.Contains("${p}sub_82170128(ctx, base);") "$($item.Name): call is prefixed"
        Assert-That $code.Contains("__imp__${p}sub_82170000(ctx, base);") "$($item.Name): original-entry call is prefixed after __imp__"
        Assert-That $code.Contains("DEFINE_REX_FUNC(${p}xstart)") "$($item.Name): entry point is prefixed"
        Assert-That $code.Contains("${p}__savegprlr_14(ctx, base);") "$($item.Name): save/restore helper is prefixed"
        Assert-That $code.Contains('ctx.r3.u32 = 0x82170000;') "$($item.Name): guest addresses are untouched"
        Assert-That $code.Contains('goto loc_82170018;') "$($item.Name): local labels are untouched"
        Assert-That $code.Contains('__imp__KeLockL2(ctx, base);') "$($item.Name): kernel imports are untouched"
        Assert-That $code.Contains('__imp____C_specific_handler(ctx, base);') "$($item.Name): imports with a leading underscore are untouched"
        Assert-That $decl.Contains("DECLARE_REX_FUNC(${p}__restvmx_127);") "$($item.Name): declarations are prefixed"
        Assert-That $decl.Contains('DECLARE_REX_FUNC(__imp__KeLockL2);') "$($item.Name): import declarations are untouched"
        Assert-That $init.Contains("const rex::PPCImageInfo ${p}PPCImageConfig") "$($item.Name): image config is prefixed"
        Assert-That $init.Contains("${p}PPCFuncMappings[] = {") "$($item.Name): function table is prefixed"
        Assert-That ([IO.File]::ReadAllText((Join-Path $out 'ssf2t_hd_remix_register.cpp')).Contains("${p}ssf2t_hd_remix_RegisterFunctions")) "$($item.Name): register function is prefixed"
        Assert-That (Test-Path (Join-Path $out 'sources.cmake')) "$($item.Name): sources.cmake is copied"
        Assert-That (-not (Test-Path (Join-Path $out 'codegen.d'))) "$($item.Name): absolute-path build records are not copied"
    }
    Assert-That ($images['base'] -ne $images['tu']) 'the two images differ'

    $leaked = Join-Path $work 'leaked'
    New-Item -ItemType Directory -Force $leaked | Out-Null
    [IO.File]::WriteAllText((Join-Path $leaked 'x.cpp'), "DEFINE_REX_FUNC(other_name) {`n}`n")
    $threw = $false
    try { Add-GuestSymbolPrefix $leaked (Join-Path $work 'leaked_out') 'base013_' | Out-Null } catch { $threw = $true }
    Assert-That $threw 'a definition that keeps its unprefixed name is rejected'
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}

# The manifest handed to ReXGlue names the staged default.xex absolutely, wherever the script is started from.
foreach ($item in $GuestVariants) {
    $paths = Get-GuestPaths $item
    $expected = [IO.Path]::GetFullPath((Join-Path $GuestRoot "out\codegen\$($item.Name)\input\default.xex"))
    Assert-That ((Join-Path $paths.Input 'default.xex') -eq $expected) "$($item.Name): staging path is under the repository root"
    $createdInput = -not (Test-Path (Join-Path $paths.Input 'default.xex'))
    $staged = Join-Path $paths.Input 'default.xex'
    try {
        if ($createdInput) { New-Item -ItemType Directory -Force $paths.Input | Out-Null; [IO.File]::WriteAllText($staged, 'x') }
        Push-Location ([IO.Path]::GetTempPath())
        try {
            Write-GuestManifest $item
            $manifest = [IO.File]::ReadAllText($paths.Manifest)
            Assert-That (-not $manifest.Contains('@ROOT@')) "$($item.Name): the manifest has no unresolved placeholder"
            $null = $manifest -match '(?m)^file_path\s*=\s*"([^"]+)"'
            Assert-That ([IO.Path]::GetFullPath($Matches[1]) -eq $expected) "$($item.Name): the manifest entrypoint is the staged default.xex"
            foreach ($key in 'game_root', 'out_directory_path') {
                $null = $manifest -match "(?m)^$key\s*=\s*`"([^`"]+)`""
                Assert-That ([IO.Path]::IsPathRooted($Matches[1])) "$($item.Name): $key is absolute"
            }
            foreach ($include in ([regex]::Matches(($manifest -split "`n" | Where-Object { $_ -like 'includes*' }), '"([^"]+)"') | ForEach-Object { $_.Groups[1].Value })) {
                Assert-That (Test-Path -LiteralPath $include) "$($item.Name): include $include exists"
            }
            $threw = $false
            try { Assert-GuestStaging $item } catch { $threw = $true }
            Assert-That (-not $threw) "$($item.Name): matching staging passes the check"

            [IO.File]::WriteAllText($paths.Manifest, $manifest.Replace('/input/default.xex', '/elsewhere/default.xex'))
            $threw = $false
            try { Assert-GuestStaging $item } catch { $threw = $true }
            Assert-That $threw "$($item.Name): a manifest that names another file is rejected before ReXGlue runs"
        } finally { Pop-Location }
    } finally {
        if ($createdInput) { Remove-Item -LiteralPath $staged -Force -ErrorAction SilentlyContinue }
        Remove-Item -LiteralPath $paths.Manifest -Force -ErrorAction SilentlyContinue
    }
}
Assert-That ((Get-ConfigSha256 (Get-GuestPaths (Get-GuestVariant 'base')).ManifestTemplate) -ne (Get-ConfigSha256 (Get-GuestPaths (Get-GuestVariant 'tu')).ManifestTemplate)) 'the manifest templates differ per image'
# The stamp depends on config alone and differs per image; the Title Update hash is only in the update image's stamp.
$base = Get-GuestVariant 'base'
$update = Get-GuestVariant 'tu'
$baseStamp = Get-GuestStamp $base
$updateStamp = Get-GuestStamp $update
Assert-That ($baseStamp -ne $updateStamp) 'stamps differ per image'
Assert-That ($baseStamp.Contains("xexp=none`n")) 'the base stamp has no Title Update'
Assert-That ($updateStamp.Contains('xexp=' + $GuestXexpSha256.ToLowerInvariant())) 'the update stamp names the supported Title Update'
Assert-That ($baseStamp.Contains('prefix=base013_') -and $updateStamp.Contains('prefix=tu023_')) 'stamps name the symbol prefix'
Assert-That ((Get-GuestStamp $base) -eq $baseStamp) 'the stamp is deterministic'

# The Title Update source for codegen comes from package metadata, without touching the installed game files.
$decoyRoot = Join-Path ([IO.Path]::GetTempPath()) ('ssf2t_tu_source_' + [Guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Force (Join-Path $decoyRoot 'Title Update (v2)\37B7D7D3') | Out-Null
    [IO.File]::WriteAllBytes((Join-Path $decoyRoot 'Title Update (v2)\37B7D7D3\TU_1C42254_000010C000000.00000000000G3'), (New-Object byte[] 0x2000))
    [IO.File]::WriteAllText((Join-Path $decoyRoot 'default.xexp'), 'not a title update')
    $extracted = Join-Path $decoyRoot 'extracted'
    New-Item -ItemType Directory -Force $extracted | Out-Null
    Assert-That ($null -eq (Find-TitleUpdateSource $extracted $decoyRoot (Join-Path $decoyRoot 'tmp'))) 'a file with the right name but no package metadata is not a Title Update source'
    Assert-That ($null -eq (Find-TitleUpdateSource $extracted (Join-Path $decoyRoot 'missing') (Join-Path $decoyRoot 'tmp'))) 'a missing discovery root is not an error'

    $realRoot = Join-Path $GuestRoot 'game'
    $realExtracted = Join-Path $realRoot 'extracted'
    $installedBefore = $false
    $tmp = Join-Path $decoyRoot 'real'
    $source = Find-TitleUpdateSource $extracted $realRoot $tmp
    if ($source) {
        Assert-That ((Get-FileHash $source -Algorithm SHA256).Hash -eq $GuestXexpSha256) 'the discovered default.xexp is the supported one'
        if (-not $installedBefore) {
            Assert-That ($source.StartsWith($tmp)) 'with no installed default.xexp the source is extracted to temporary staging'
            Assert-That (-not (Test-Path (Join-Path $extracted 'default.xexp'))) 'the extraction folder passed in is left untouched'
        }
    } else { Write-Host '  (no local Title Update package: package discovery skipped)' }
} finally { Remove-Item -LiteralPath $decoyRoot -Recurse -Force -ErrorAction SilentlyContinue }
# A generated image is complete only when every file it needs exists, whatever the stamp says.
function New-FakeImage([string]$Dir, [int]$Parts) {
    New-Item -ItemType Directory -Force $Dir | Out-Null
    foreach ($n in 'pch.h', 'init.h', 'funcs.h') { [IO.File]::WriteAllText((Join-Path $Dir "ssf2t_hd_remix_$n"), '') }
    foreach ($n in 'init.cpp', 'register.cpp') { [IO.File]::WriteAllText((Join-Path $Dir "ssf2t_hd_remix_$n"), '') }
    $list = ''
    for ($i = 0; $i -lt $Parts; $i++) {
        [IO.File]::WriteAllText((Join-Path $Dir "ssf2t_hd_remix_recomp.$i.cpp"), "DEFINE_REX_FUNC(sub_8217000$i) {`n}`n")
        [IO.File]::WriteAllText((Join-Path $Dir "ssf2t_hd_remix_funcs.$i.h"), '')
        $list += "    `${CMAKE_CURRENT_LIST_DIR}/ssf2t_hd_remix_recomp.$i.cpp`n"
    }
    [IO.File]::WriteAllText((Join-Path $Dir 'sources.cmake'), "set(GENERATED_SOURCES`n    `${CMAKE_CURRENT_LIST_DIR}/ssf2t_hd_remix_init.cpp`n    `${CMAKE_CURRENT_LIST_DIR}/ssf2t_hd_remix_register.cpp`n$list)`n")
}
$fakeRoot = Join-Path ([IO.Path]::GetTempPath()) ('ssf2t_guest_tree_' + [Guid]::NewGuid().ToString('N'))
try {
    foreach ($item in $GuestVariants) {
        $dir = Join-Path $fakeRoot $item.Name
        New-FakeImage $dir 3
        Assert-That (@(Get-MissingGuestFiles $dir).Count -eq 0) "$($item.Name): a complete tree has nothing missing"
        Remove-Item (Join-Path $dir 'ssf2t_hd_remix_register.cpp')
        Assert-That ((Get-MissingGuestFiles $dir) -contains 'ssf2t_hd_remix_register.cpp') "$($item.Name): a missing register.cpp is reported"
        New-FakeImage $dir 3
        Remove-Item (Join-Path $dir 'ssf2t_hd_remix_recomp.2.cpp')
        Assert-That ((Get-MissingGuestFiles $dir) -contains 'ssf2t_hd_remix_recomp.2.cpp') "$($item.Name): a missing listed source is reported"
        New-FakeImage $dir 3
        Remove-Item (Join-Path $dir 'ssf2t_hd_remix_funcs.1.h')
        Assert-That ((Get-MissingGuestFiles $dir) -contains 'ssf2t_hd_remix_funcs.1.h') "$($item.Name): a missing declaration header is reported"
        Remove-Item (Join-Path $dir 'ssf2t_hd_remix_pch.h')
        Assert-That ((Get-MissingGuestFiles $dir) -contains 'ssf2t_hd_remix_pch.h') "$($item.Name): a missing pch.h is reported"

        $raw = Join-Path $fakeRoot "$($item.Name)_raw"
        $out = Join-Path $fakeRoot "$($item.Name)_out"
        New-FakeImage $raw 3
        Publish-GuestImage $item $raw $out | Out-Null
        Assert-That ((Test-Path (Join-Path $out $GuestStampName)) -and @(Get-MissingGuestFiles $out).Count -eq 0) "$($item.Name): a complete image is published with its stamp"
        Assert-That ((Get-ChildItem $out -Filter '*recomp.0.cpp').Count -eq 1 -and ([IO.File]::ReadAllText((Join-Path $out 'ssf2t_hd_remix_recomp.0.cpp')).Contains("DEFINE_REX_FUNC($($item.Prefix)sub_82170000)"))) "$($item.Name): the published sources are prefixed"
        $before = (Get-ChildItem $out -File | Measure-Object).Count

        Remove-Item (Join-Path $raw 'ssf2t_hd_remix_register.cpp')
        $threw = $false
        try { Publish-GuestImage $item $raw $out | Out-Null } catch { $threw = $true }
        Assert-That $threw "$($item.Name): an incomplete raw tree is not published"
        Assert-That ((Get-ChildItem $out -File | Measure-Object).Count -eq $before -and (Test-Path (Join-Path $out $GuestStampName))) "$($item.Name): the previous complete image is kept"
        Assert-That (-not (Test-Path "$out.new")) "$($item.Name): no half-written tree is left behind"

        $fresh = Join-Path $fakeRoot "$($item.Name)_fresh"
        New-FakeImage $raw 3
        [IO.File]::WriteAllText((Join-Path $raw 'ssf2t_hd_remix_recomp.1.cpp'), "DEFINE_REX_FUNC(unprefixable_name) {`n}`n")
        $threw = $false
        try { Publish-GuestImage $item $raw $fresh | Out-Null } catch { $threw = $true }
        Assert-That ($threw -and -not (Test-Path $fresh) -and -not (Test-Path "$fresh.new")) "$($item.Name): a failed transformation leaves no tree and no stamp"
    }
} finally { Remove-Item -LiteralPath $fakeRoot -Recurse -Force -ErrorAction SilentlyContinue }
if ($failures) { Write-Host "$failures failure(s)" -ForegroundColor Red; exit 1 }
Write-Host 'guest code checks passed'
