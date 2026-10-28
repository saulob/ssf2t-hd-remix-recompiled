# Release

The project version is the `project(ssf2t_hd_remix VERSION x.y.z)` line in `CMakeLists.txt`. The executable's version resource, the launcher's About page, `tools\package-release.ps1` and the artifact names all read it.

## Windows package

    pwsh -NoProfile -ExecutionPolicy Bypass -File tools\package-release.ps1            # Release build, then package
    pwsh -NoProfile -ExecutionPolicy Bypass -File tools\package-release.ps1 -Clean     # clean Release build first
    pwsh -NoProfile -ExecutionPolicy Bypass -File tools\package-release.ps1 -VerifyOnly dist\<name>.zip

It builds `build-release\` (Release), requires `tools\rexglue` to hold the Release `rexruntime.dll` and `rexgpu-xenos.dll` built from `patches\rexglue-sdk` (`rexruntime-patch.txt` fingerprint; stock or stale files are refused), stages the allowlisted files into `dist\SSF2THDRemix-v<version>-windows-x64\`, runs `tools\check-release-folder.ps1` on it, writes the ZIP and `.sha256` next to it, then extracts the ZIP and checks it again.

The ZIP holds one folder, `SSF2THDRemix-v<version>-windows-x64`, with `ssf2t_hd_remix.exe`, `rexruntime.dll`, `rexgpu-xenos.dll`, `ssf2t_hd_remix.toml`, `README.txt`, `LICENSE` and `licenses\ReXGlue-LICENSE.txt` and `licenses\THIRD-PARTY-LICENSES.txt`. Nothing else: no game files, `default.xex` or `default.xexp` in any casing or any other `*.xex` or `*.xexp`, `user_data`, logs, dumps, symbols, RenderDoc or Vulkan layers, developer paths or `DEBUG_SAULO` text.

`trg-updater.exe` in the ZIP is not the CMake build: `tools\build-updater.ps1` compiles the same `src\updater` sources with MSYS2 CLANG64 (`C:\msys64`, package `mingw-w64-clang-x86_64-toolchain`), with the toolchain and flags of the Dragon's Crown Pro PC update helper: static libc++, only Windows system DLLs and the Windows UCRT. Its debug information goes to `dist\SSF2THDRemix-v<version>-windows-x64-symbols\trg-updater.debug`, outside the ZIP; the executable keeps a `.gnu_debuglink` to it. `tools\test.ps1 -Config Release -BuildDir build-release` runs the helper tests against it as well.

`tools\test-release-check.ps1` (part of `tools\test.ps1`) runs the scanner against staged folders that each add one forbidden item.

## Release notes

`.github\release-notes-v<version>.md`. The checksum is filled from the generated ZIP.

## Linux AppImage (experimental)

    tools/package-linux.sh            # on Ubuntu 20.04 or WSL; incremental Release build, then package
    tools/package-linux.sh --verify-only dist/SSF2THDRemix-v<version>-x86_64.AppImage

It builds the patched ReXGlue Linux runtime when the patches changed, builds the `linux-amd64-release` preset, stages an allow-listed AppDir, checks RPATH, unresolved libraries, the glibc 2.31 baseline, local paths and forbidden files, builds the AppImage with the pinned runtime (`patches/appimage-runtime`), extracts it again and checks it, and writes `dist/SSF2THDRemix-v<version>-x86_64.AppImage` and `.sha256`. The program's debug information goes to `dist/SSF2THDRemix-v<version>-x86_64-symbols/`, outside the AppImage. See [linux.md](linux.md).

`tools\stamp-release-hashes.ps1` fills the Checksums table of the release notes with both artifacts' hashes.
