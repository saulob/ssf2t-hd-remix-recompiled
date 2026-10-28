## Super Street Fighter II Turbo HD Remix PC Port v0.2.2

Adds official Xbox 360 Title Update #1 support. The port is now recompiled from the updated game, version 0.0.2.3, so it needs the Xbox Live Arcade game package and its Title Update #1. Neither is included.

### Changes
- **Title Update #1 support**: the code is recompiled from the game with its official Title Update applied (game version 0.0.2.3). The runtime checks that the updated executable is the one loaded and stops with a message otherwise.
- **Separate installers on the Play page**: **Install Game** (or **Reinstall Game**) and **Install Title Update** (or **Remove Title Update**). The packages can be installed in either order, and each button refuses the other kind of package before anything is written. From the Title Update the launcher takes only `default.xexp`.
- **Title Update removal**: **Remove Title Update** deletes only `default.xexp`. The game files, saves, settings and achievements are not touched, and the game cannot be played until the update is installed again.
- **Reinstall Game keeps an installed Title Update.**
- **Revised package validation**: packages are told apart by content type and checked by Title ID; `default.xex` and `default.xexp` are checked by size and CRC32 after extraction. A wrong version is refused with the version that was found. **PLAY** is enabled only when the game files and Title Update #1 are both valid.
- **Fill 16:9 now also covers Character Select and VS**: the black bars at the sides are removed there as well as in fights. Original keeps the game's own framing.
- **Controller input is ignored while another program is in front** (Windows): a pad used in another program no longer drives the launcher or the achievements overlay.
- The Infinite Time option, the Achievements overlay and menu item and the Exit Game menu text were re-targeted to the updated executable.
- The launcher's license notice for the TRG Launcher (MIT) is now included in `THIRD-PARTY-LICENSES.txt`.

### Updating from v0.2.1
The v0.2.1 launcher offers this update when it starts; choose **UPDATE NOW** to install it in place. Game files, saves, settings and logs are kept. Your installed game is version 0.0.1.3, so after updating the Play page asks for **Title Update #1**: click **Install Title Update** and pick that package.

### Install
1. Unzip **SSF2THDRemix-v0.2.2-windows-x64.zip** anywhere you can write to and run **ssf2t_hd_remix.exe**.
2. On the launcher's **Play** page click **Install Game** and pick your own Super Street Fighter II Turbo HD Remix Xbox Live Arcade package.
3. Click **Install Title Update** and pick the Title Update #1 package.
4. Wait for **Ready to play**, then press **PLAY**.

**No game files and no Title Update files are included.** You need your own packages:

| | Game package | Title Update package |
| --- | --- | --- |
| Title ID | `584108A4` | `584108A4` |
| Media ID | `37B7D7D3` | `37B7D7D3` |
| Content type | `000D0000` (Xbox Live Arcade) | `000B0000` (Title Update) |
| Version | 0.0.1.3 | updates 0.0.1.3 to 0.0.2.3 |
| File the port uses | `default.xex`, 3,932,160 bytes, CRC32 `B734F075`, SHA-256 `6E1C2F9ED7D1102A311A5F5626563527EDA5F85B2FBE8DB8405B866DED3F31EE` | `default.xexp`, 2,166,784 bytes, CRC32 `38DE2310`, SHA-256 `A2E8E66B482C5D3A0D3A5D3F67A3CECB7A12CE1F6189196694FD3E765910606C` |

### Linux AppImage (experimental)
Download **SSF2THDRemix-v0.2.2-x86_64.AppImage**, run `chmod +x` on it and start it from a folder you can write to. The launcher and the **Install Game** and **Install Title Update** buttons work as on Windows; `game`, `logs` and `user_data` are kept next to the AppImage. It needs glibc 2.31 or newer and a Vulkan driver (see `docs/linux.md`). It has been built and checked by script only: it has not been tested on real Linux or Steam Deck hardware yet, so please report what you find.

### Known limitations
- Windows is the supported platform. The Linux AppImage is experimental and untested on real hardware; the **Graphics card** selector is Windows-only.
- Offline only: there is no Xbox Live connection or online play, so the four achievements that need online ranked matches cannot be earned.
- The game runs at its original speed of 60 updates per second; there is no frame rate selector.
- Only game version 0.0.2.3 (game 0.0.1.3 with Title Update #1) is supported.
- The program is not code-signed, so Windows SmartScreen may show a warning the first time.

### Reporting problems
If the game does not start or crashes, please open an issue and attach the newest `ssf2t_hd_remix_*.log` file from the `logs` folder next to the program, with a line about what you were doing.

### Requirements
Windows 10 or 11 (64-bit), a DirectX 12 graphics card, and the latest Microsoft Visual C++ 2015-2022 Redistributable (x64).

### Checksums
The SHA-256 of each file is published with the release as `SSF2THDRemix-v0.2.2-windows-x64.sha256` and `SSF2THDRemix-v0.2.2-x86_64.sha256`, so you can verify your download.

| File | SHA-256 |
| --- | --- |
| `SSF2THDRemix-v0.2.2-windows-x64.zip` | `e23ad87fb57ec27edd79b42597a080d2bf78d2630542b3350c52ba0c940df8c2` |
| `SSF2THDRemix-v0.2.2-x86_64.AppImage` | `d83ff83095ce58600cee1ca4c5274b0ac80e943cb2e59490f95f7285907f4616` |
