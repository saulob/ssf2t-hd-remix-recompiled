## Super Street Fighter II Turbo HD Remix PC Port v0.2.1

Release packaging update. Gameplay, the launcher and the in-place updater behave exactly as in v0.2.0.

### Changes
- **Windows release packaging**: improved release packaging and compatibility, intended to reduce the false-positive antivirus detections reported for the v0.2.0 update helper (`trg-updater.exe`). The helper now uses the same conventional MSYS2 CLANG64 build model as the Dragon's Crown Pro PC port's update helper, and its debug symbols are kept in a separate file outside the release ZIP. It remains a static, self-contained program that uses only Windows system libraries; its source code is unchanged. The game executable is built exactly as before.
- **Windows ZIP layout**: the ZIP now extracts to a single `SSF2THDRemix-v0.2.1-windows-x64` folder.
- **Linux AppImage packaging**: the program's debug symbols are now separated into a `.debug` file that is kept out of the AppImage, the same conventional model already used by the Dragon's Crown Pro PC port. The bundled program code is unchanged.
- No gameplay or updater behavior was changed by these packaging changes.

### Updating from v0.2.0
The v0.2.0 launcher offers this update when it starts; choose **UPDATE NOW** to install it in place. Game files, saves, settings and logs are kept.

### Install
1. Unzip **SSF2THDRemix-v0.2.1-windows-x64.zip** anywhere you can write to and run **ssf2t_hd_remix.exe**.
2. On the launcher's **Play** page click **Install from package...** and pick your own Super Street Fighter II Turbo HD Remix Xbox Live Arcade package.
3. Wait for **Ready to play**, then press **PLAY**.

**No game files are included.** You need your own package, this exact game version:

| | |
| --- | --- |
| Title ID | `584108A4` (Xbox Live Arcade, content type `000D0000`) |
| Game version | `default.xex` 0.0.1.3, 3,932,160 bytes, CRC32 `B734F075`, SHA-256 `6E1C2F9ED7D1102A311A5F5626563527EDA5F85B2FBE8DB8405B866DED3F31EE` |

The launcher checks the Title ID when you pick the package and checks `default.xex` after extracting it. It explains if your package is a different version.

### Linux AppImage (experimental)
Download **SSF2THDRemix-v0.2.1-x86_64.AppImage**, run `chmod +x` on it and start it from a folder you can write to. The launcher and the **Install from package...** flow are the same as on Windows; `game`, `logs` and `user_data` are kept next to the AppImage. It needs glibc 2.31 or newer and a Vulkan driver (see `docs/linux.md`). It has been built and checked by script only: it has not been tested on real Linux or Steam Deck hardware yet, so please report what you find.

### Known limitations
- Windows is the supported platform. The Linux AppImage is experimental and untested on real hardware; the **Graphics card** selector is Windows-only.
- Offline only: there is no Xbox Live connection or online play, so the four achievements that need online ranked matches cannot be earned.
- The game runs at its original speed of 60 updates per second; there is no frame rate selector.
- Only game version 0.0.1.3 is supported.
- The program is not code-signed, so Windows SmartScreen may show a warning the first time.

### Reporting problems
If the game does not start or crashes, please open an issue and attach the newest `ssf2t_hd_remix_*.log` file from the `logs` folder next to the program, with a line about what you were doing.

### Requirements
Windows 10 or 11 (64-bit), a DirectX 12 graphics card, and the latest Microsoft Visual C++ 2015-2022 Redistributable (x64).

### Checksums
The SHA-256 of each file is published with the release as `SSF2THDRemix-v0.2.1-windows-x64.sha256` and `SSF2THDRemix-v0.2.1-x86_64.sha256`, so you can verify your download.

| File | SHA-256 |
| --- | --- |
| `SSF2THDRemix-v0.2.1-windows-x64.zip` | `348cb82a160fe615939a940850e1be98d1805e5e1e4307abef3e4508d15b0718` |
| `SSF2THDRemix-v0.2.1-x86_64.AppImage` | `ed8a75add5cf981020be981dcf849a6f7edf5a9fc632bebc6838b251c761dab7` |
