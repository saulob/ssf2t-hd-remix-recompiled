## Super Street Fighter II Turbo HD Remix PC Port v0.2.1

Release packaging and compatibility update. Some antivirus engines reported heuristic detections for v0.2.0 files; v0.2.1 changes how the release is built and packaged to reduce these false-positive detections. Gameplay, the launcher and the in-place updater behave exactly as in v0.2.0.

### Changes
- **Windows update helper**: `trg-updater.exe` is now built with the same conventional MSYS2 CLANG64 build model already used by the Dragon's Crown Pro PC port's update helper. It is built from the same source, remains a static, self-contained program that uses only Windows system libraries, and its debug symbols are kept in a separate file outside the release ZIP.
- **Windows game executable**: built with its normal Visual Studio clang Release configuration, as before. Its machine code is unchanged from v0.2.0; only the version information differs.
- **Windows ZIP layout**: the ZIP now extracts to a single `SSF2THDRemix-v0.2.1-windows-x64` folder.
- **Linux AppImage**: the program's debug symbols are now kept in a separate `.debug` file outside the AppImage, the conventional model already used by the Dragon's Crown Pro PC port. The bundled program code is unchanged.
- No gameplay, updater protocol or save behavior was changed by these packaging improvements.

### Antivirus checks
Both release artifacts were checked with VirusTotal before publication, with 0 detections at release preparation time:
- Windows ZIP: [VirusTotal report](https://www.virustotal.com/gui/file/348cb82a160fe615939a940850e1be98d1805e5e1e4307abef3e4508d15b0718)
- Linux AppImage: [VirusTotal report](https://www.virustotal.com/gui/file/ed8a75add5cf981020be981dcf849a6f7edf5a9fc632bebc6838b251c761dab7)

VirusTotal results can change as antivirus engines update, and a scan result is not a guarantee. Compare your download with the SHA-256 values below; if an engine flags a file, please open an issue with the engine name and detection.

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
