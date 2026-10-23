# Linux

The Linux build is a native x86_64 program, not a Windows build under Proton or Wine. It uses the same code and the same
launcher as the Windows build: ReXGlue SDK 0.10.0's Linux runtime and the Xenos GPU plugin's Vulkan backend, both built
with the project's patches. It ships as one AppImage.

**Experimental.** The AppImage is built and checked by the project's scripts, but this release has not yet been validated
on real Linux PCs or Steam Deck hardware. Reports are welcome.

Every launcher setting works on Linux, apart from **Graphics card**: that list selects a Direct3D 12 adapter, so it exists
on Windows only. On Linux the runtime prefers the dedicated GPU, and the usual selection variables (`DRI_PRIME`,
`MESA_VK_DEVICE_SELECT`, `VK_LOADER_DEVICE_SELECT`, `__NV_PRIME_RENDER_OFFLOAD`) take precedence when they are set.

## Requirements

- 64-bit x86 Linux with glibc 2.31 or newer, such as Ubuntu 20.04 or later (`ldd --version` shows it)
- A Vulkan driver (Mesa RADV on most AMD and Intel PCs)
- An X11 or Wayland desktop
- `zenity` or `kdialog` for the launcher's file picker, and `xdg-open` for its folder buttons
- Your own Super Street Fighter II Turbo HD Remix Xbox Live Arcade package, game version 0.0.1.3 (see the README)

The AppImage carries the C++ runtime of its build machine (GCC 13 or newer). It is used only when the system's own copy
is older than GCC 13.

## Installing

1. Download **SSF2THDRemix-v…-x86_64.AppImage** and put it in a folder you can write to.
2. Make it executable: right-click it > **Properties** > **Permissions** > **Is executable**, or
   `chmod +x SSF2THDRemix-v*-x86_64.AppImage`.
3. Run it. The launcher opens.
4. On the **Play** page click **Install from package...** and pick your Xbox Live Arcade package. The game files are
   copied into a `game` folder next to the AppImage.
5. Wait for **Ready to play**, then press **PLAY**.

Steam Deck is not a tested target for v0.2.0. Adding the AppImage as a non-Steam game should work, but nobody has tried it.

### If the AppImage does not start

An AppImage mounts itself with FUSE before any of its own code runs. Where that mount is not possible, the AppImage
unpacks itself into a private folder under `/tmp`, runs the game from there with the same arguments, returns the game's
exit status and removes the folder afterwards. A restart from the launcher starts the original AppImage again. Where FUSE
works, nothing is unpacked. `game`, `logs` and `user_data` stay next to the AppImage in both cases.

A line such as `Error: $FUSERMOUNT_PROG not set` can still appear in the terminal; it is expected and harmless. To force
the unpacking, run `APPIMAGE_EXTRACT_AND_RUN=1 ./SSF2THDRemix-v*-x86_64.AppImage` or pass `--appimage-extract-and-run`.
`VERBOSE=1` makes the AppImage print which path it took.

### If Vulkan is unavailable

When the Vulkan graphics system cannot start, the program opens a small launcher window drawn without the GPU. It shows the
game files and graphics status; **Play** explains that a working Vulkan driver is needed and points at the `logs` folder
next to the AppImage, and **Quit** closes it.

## Where things are

| What | Where |
| --- | --- |
| Game files | `game` next to the AppImage |
| Logs | `logs` next to the AppImage (`user_data/logs` if that folder is not writable) |
| Settings, saves, achievements | `user_data` next to the AppImage |

Nothing is written inside the AppImage, which is read-only. A build run outside an AppImage keeps `game`, `logs` and
`user_data` next to the program instead. `--game_data_root` and `--user_data_root` override the folders, as on Windows.

The program tells the desktop it is `io.github.saulob.ssf2t-hd-remix` (the Wayland app_id and the X11 window class). The
`.desktop` file in `packaging/linux` has the same name and sets it as `StartupWMClass`, and `tools/package-linux.sh` stops
if they differ. Restarting from the launcher starts the original AppImage file, never the mounted or unpacked copy.

## Known limits

- No testing on real hardware: rendering, audio, controllers and performance are unconfirmed on Linux.
- Holding **Shift** to bring the launcher back works only if the window gets keyboard focus while Shift is still held.
- The VSync setting maps to Vulkan's present mode: **Off** allows immediate presentation (tearing), **On** uses mailbox or FIFO.
- On a Wayland desktop with display scaling above 100%, window sizes can come out larger than chosen.

## Building the AppImage

Run on Linux or WSL, after `tools/codegen.ps1` has generated `generated/default` on Windows and with your own
`game/extracted/default.xex` in place (it is only read to check that the generated code is current):

```sh
tools/package-linux.sh            # incremental Release build, then package
tools/package-linux.sh --clean    # clean Release build first
tools/package-linux.sh --verify-only dist/SSF2THDRemix-v0.2.0-x86_64.AppImage
tools/test.sh                     # headless tests, after tools/build.sh
```

Releases are built on Ubuntu 20.04 (glibc 2.31); the script stops on a newer machine and if any packaged library needs
glibc above 2.31. `tools/setup-linux-build-env.sh` prepares a clean Ubuntu 20.04 machine as root (clang 20, the GCC 13
runtime, CMake 3.31.8, libdecor and PipeWire headers).

The script copies the sources to `~/ssf2t-linux-build` (`SSF2T_WORK` changes it), downloads the pinned ReXGlue SDK and
appimagetool with SHA-256 checks, builds the patched ReXGlue runtime and Vulkan GPU plugin when the patches changed
(`tools/build-patched-rexruntime.sh`, fingerprinted like the Windows runtime; stock or stale libraries are refused),
builds the `linux-amd64-release` preset, checks that the AppDir holds exactly the allow-listed files and no game files,
saves, logs, local paths (ASCII and UTF-16) or `DEBUG_SAULO`, checks RPATH, unresolved libraries and glibc symbols, builds
the AppImage, extracts it again and runs the same checks on the result. It writes
`dist/SSF2THDRemix-v<version>-x86_64.AppImage` and its `.sha256`, and publishes nothing.

The AppImage runtime is built from the pinned upstream AppImage/type2-runtime commit with the patch in
`patches/appimage-runtime` (`tools/build-appimage-runtime.sh`), so that an AppImage whose FUSE mount fails falls back to
extract-and-run instead of stopping.
