<div align="center">

# Super Street Fighter II Turbo HD Remix - PC Port

### Super Street Fighter II Turbo HD Remix for Xbox Live Arcade, running natively on Windows, and experimentally on Linux and Steam Deck, through static recompilation, with the TRG launcher and modern PC options

[![Latest release](https://img.shields.io/github/v/release/saulob/ssf2t-hd-remix-recompiled?style=for-the-badge&label=release&color=FF8514&labelColor=07101f&include_prereleases)](https://github.com/saulob/ssf2t-hd-remix-recompiled/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/saulob/ssf2t-hd-remix-recompiled/total?style=for-the-badge&color=FF8514&labelColor=07101f)](https://github.com/saulob/ssf2t-hd-remix-recompiled/releases)
![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux%20experimental%20%7C%20Steam%20Deck%20experimental-00acc1?style=for-the-badge&labelColor=07101f)

<sub>The original Xbox 360 game code, statically recompiled to native PC code with the <a href="https://github.com/rexglue/rexglue-sdk">ReXGlue SDK</a>. <b>No game files included</b>: bring your own Super Street Fighter II Turbo HD Remix Xbox Live Arcade package and its Title Update. The port incorporates the official Xbox 360 Title Update #1 (game version 0.0.2.3).</sub>

</div>

<img width="1930" height="1149" alt="title screen" src="https://github.com/user-attachments/assets/d52ee275-da0b-4860-ae1d-24216df5b05f" />

## Features

- **Native static recompilation**: the game's own code, not an emulator. Windows, plus an experimental Linux AppImage.
- **TRG launcher** with a game package installer, display and graphics options, and an Achievements page.
- **Launcher updates**: the launcher checks GitHub releases once per start and installs a newer release only when you choose UPDATE NOW. Game files, saves, settings and logs are kept.
- **Display**: Windowed or Borderless Fullscreen (**F11**), window size, monitor and VSync. Per-monitor high-DPI support, and the game window opens where and how large you chose.
- **Presentation**: Letterbox keeps the picture's proportions, Stretch fills the window. Aspect ratios 16:9, 16:10, 21:9, 32:9, 4:3 and 5:4.
- **Fight Area**: Original keeps the fight's original framing, Fill 16:9 removes the black bars at the sides of the fight, Character Select and VS.
- **Render resolution and Internal render resolution**: Render resolution (1x to 4x) sets the target resolution of the picture. Internal render resolution (100% Native, 75% Performance, 50% Low-end) lowers the resolution the GPU draws at before the picture is scaled up, trading sharpness for performance. Both are separate from the Window size.
- **Graphics card selection**: choose which compatible GPU renders the game.
- **Anti-aliasing, multisampling and texture filtering** choices.
- **Language**: English, French, German, Italian or Spanish.
- **Local saves**: the game saves and loads as on the console.
- **Offline achievements**: the game's 12 achievements (200G) are kept on this PC, listed with **F7**, in the game's own Achievements menu item and in the launcher. There is no Xbox Live connection, and the four achievements that need online ranked matches cannot be earned.
- **Xbox controller support** with no setup, and keyboard play as an Xbox 360 controller.
- **In-game frame counter** (**F2**) and optional frame rate or render resolution in the window title.
- **Portable user data**: saves, settings and achievements are in a `user_data` folder beside the program.

The game runs at its original speed of 60 updates per second. Online play is not supported.

## What you need

- **Windows:** Windows 10 or 11, 64-bit, and a DirectX 12 graphics card, with the latest Microsoft Visual C++ 2015-2022 Redistributable (x64). Most PCs have it.
- **Linux / Steam Deck (experimental):** 64-bit x86 with glibc 2.31 or newer and a Vulkan driver. Steam Deck support is experimental and has not yet been validated on hardware. See [docs/linux.md](docs/linux.md).
- **Your own Super Street Fighter II Turbo HD Remix Xbox Live Arcade package and its official Title Update #1 package.** The port incorporates the Title Update: its code is recompiled from the updated game, version `0.0.2.3`. Both packages are needed, because the game's data comes from your files when it runs and only the updated data matches the code.

| | Game package | Title Update package |
| --- | --- | --- |
| Game | Super Street Fighter II Turbo HD Remix, Xbox Live Arcade | SuperStreetFighter2THD Title Update #1 |
| Title ID | `584108A4` | `584108A4` |
| Media ID | `37B7D7D3` | `37B7D7D3` |
| Content type | `000D0000` (Xbox Live Arcade), STFS `LIVE` | `000B0000` (Title Update), STFS `LIVE` |
| On an Xbox 360 drive | `Content\0000000000000000\584108A4\000D0000\` | `Content\0000000000000000\584108A4\000B0000\` |
| Version | `0.0.1.3` | updates `0.0.1.3` to `0.0.2.3` |
| File the port uses | `default.xex`: 3,932,160 bytes, CRC32 `B734F075`, SHA-256 `6E1C2F9ED7D1102A311A5F5626563527EDA5F85B2FBE8DB8405B866DED3F31EE` | `default.xexp`: 2,166,784 bytes, CRC32 `38DE2310`, SHA-256 `A2E8E66B482C5D3A0D3A5D3F67A3CECB7A12CE1F6189196694FD3E765910606C` |

The launcher tells the two packages apart by their content type, checks the Title ID, and checks `default.xex` and `default.xexp` after extracting them. Any other version is refused with the version that was found, because the port is built for this exact game code.

## Installing

1. Download **SSF2THDRemix-v0.2.1-windows-x64.zip** from the [latest release](https://github.com/saulob/ssf2t-hd-remix-recompiled/releases/latest) and extract it anywhere you can write to.
2. Run **ssf2t_hd_remix.exe**. The launcher opens.
3. On the **Play** page click **Install from package...** and pick your game package. It is one file with a long hexadecimal name and no extension. On an Xbox 360 drive it is in `Content\0000000000000000\584108A4\000D0000\`.
4. Click **Install from package...** again and pick the Title Update package (`Content\0000000000000000\584108A4\000B0000\` on an Xbox 360 drive). The order does not matter; the launcher recognises each package and copies only the update's `default.xexp`.
5. Wait until **Ready to play** appears. The game files (about 370 MB) are copied into the `game` folder next to the program.
6. Press **PLAY**.

**No game files are included** in the release. **Reinstall from package...** repairs the game files and keeps your settings and saves.

### Linux AppImage (experimental)

Download **SSF2THDRemix-v0.2.1-x86_64.AppImage**, make it executable (`chmod +x`) and run it from a folder you can write to. The launcher works as on Windows; `game`, `logs` and `user_data` are created next to the AppImage file. It has not yet been validated on real Linux or Steam Deck hardware. Details, requirements and the FUSE fallback are in [docs/linux.md](docs/linux.md).

### Windows SmartScreen

Windows may warn the first time you run the program, because the release is not digitally signed. Choose **More info**, then **Run anyway**.

## The launcher

Your choices are saved to `settings.toml` in the `user_data` folder next to the program. To turn the launcher off, set **Show this launcher** to **Off** on the Play page. Hold **Shift** while starting to bring it back.

- **Play**: package install and reinstall, status
- **Display**: Display mode, Window size, Monitor, Aspect Ratio, VSync, Presentation (Letterbox or Stretch) and Fight Area (Original or Fill 16:9)
- **Graphics**: Graphics card, Render resolution, Internal render resolution, Anti-aliasing, Multisampling, Texture filtering
- **Gameplay**: Language, frame rate and render resolution in the title, frame counter
- **Achievements**: unlocked achievements and gamerscore
- **Controls**: keyboard on or off, every key remappable, and **Keyboard defaults** to restore the standard layout
- **About**: versions, folders (game, saves and settings, logs), **Show settings file**, **Detailed logging**, **Open logs folder** and **Reset all settings**

During gameplay the mouse cursor follows the last input device: mouse activity shows it, keyboard or controller activity hides it, and mouse-driven overlays always show it.

## Screenshots

<img width="1935" height="1158" alt="gameplay" src="https://github.com/user-attachments/assets/a5295360-0c5a-40b2-9bc1-c45fcd6d2e30" />

## Reporting problems

If the game does not start or crashes, please open an [issue](https://github.com/saulob/ssf2t-hd-remix-recompiled/issues). Attach the newest `ssf2t_hd_remix_*.log` file from the `logs` folder next to the program, and say what you were doing when the problem happened. The launcher and the game write to the same log for one application session. After a crash on Windows the log ends with a `CRASH REPORT` section; no separate crash file or minidump is written. For a non-crash problem, turn Detailed logging on in the launcher's About page, reproduce it, and send the newest log.

## Building it yourself

The recompiled C++ is generated from your own game files, so the repository does not contain it. The TRG launcher is included in `third_party\trg-launcher`.

1. Install Visual Studio with the C++ and LLVM/Clang components.
2. Extract your game package and then your Title Update package into `game\extracted` with `tools\extract_stfs.ps1`, so that `game\extracted\default.xex` and `game\extracted\default.xexp` exist.
3. `.\tools\codegen.ps1` downloads the pinned ReXGlue SDK 0.10.0, checks `default.xex` and `default.xexp` and generates the C++ into `generated\`. ReXGlue applies the Title Update while it loads `default.xex`, so the code is generated from version 0.0.2.3. `python -I tools\xex_image.py` writes the same updated image to `game\analysis\default.image.bin` for the function scanner and the image tests.
4. `.\tools\build.ps1` builds `build\ssf2t_hd_remix.exe`. It first checks that ReXGlue's runtime and Xenos GPU plugin in `tools\rexglue` were built from this project's patches (`patches\rexglue-sdk`) and rebuilds them from the v0.10.0 source when they were not.
5. `.\tools\test.ps1` runs the headless tests.
6. `.\tools\package-release.ps1` (PowerShell 7, and MSYS2 CLANG64 for the update helper) makes the release ZIP in `dist\`. It refuses to package a runtime that does not match the patches and publishes nothing. See [docs/release.md](docs/release.md).

## Sponsoring

Super Street Fighter II Turbo HD Remix Recompiled is maintained and developed in my free time. If you enjoy the project or find it useful, consider supporting its continued development.

<p align="center">
<a href="https://github.com/sponsors/saulob" target="_blank"><img src="https://img.shields.io/badge/GitHub%20Sponsors-Sponsor-EA4AAA?style=for-the-badge&logo=githubsponsors&logoColor=white" alt="GitHub Sponsors"></a>
</p>

## Credits

- Super Street Fighter II Turbo HD Remix is a game by its original rights holders. This is an unofficial fan-made PC port. It is not affiliated with or endorsed by them, and it contains none of their files.
- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) for the recompiler and runtime (BSD 3-Clause)
- [TRG Launcher](https://github.com/TekRantGaming/trg-launcher), the launcher interface, pinned at v1.1.0 (revision b384a28), licensed under the MIT License
- The licenses of the libraries the release includes are in the `licenses` folder of the ZIP

## AI disclosure

AI tools were used to assist with code analysis, development, debugging and documentation. Changes were directed, tested and reviewed by the maintainer.

## License

Original code in this project is licensed under the GNU General Public License v2.0 only (GPL-2.0-only). See [LICENSE](LICENSE).

Third-party components retain their respective licenses and notices. See [licenses/ReXGlue-LICENSE.txt](packaging/licenses/ReXGlue-LICENSE.txt) and [licenses/THIRD-PARTY-LICENSES.txt](packaging/licenses/THIRD-PARTY-LICENSES.txt).

No game files are included. Super Street Fighter II Turbo HD Remix and its assets are not covered by this project's license.
