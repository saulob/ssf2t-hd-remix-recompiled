## Super Street Fighter II Turbo HD Remix PC Port v0.1.0

Play the Xbox 360 edition of Super Street Fighter II Turbo HD Remix natively on Windows, with the TRG launcher and modern PC options. This is the first playable release.

### Install
1. Unzip **SSF2THDRemix-v0.1.0-windows-x64.zip** anywhere you can write to and run **ssf2t_hd_remix.exe**.
2. On the launcher's **Play** page click **Install from package...** and pick your own Super Street Fighter II Turbo HD Remix Xbox Live Arcade package.
3. Wait for **Ready to play**, then press **PLAY**.

**No game files are included.** You need your own package, this exact game version:

| | |
| --- | --- |
| Title ID | `584108A4` (Xbox Live Arcade, content type `000D0000`) |
| Game version | `default.xex` 0.0.1.3, 3,932,160 bytes, CRC32 `B734F075`, SHA-256 `6E1C2F9ED7D1102A311A5F5626563527EDA5F85B2FBE8DB8405B866DED3F31EE` |

The launcher checks the Title ID when you pick the package and checks `default.xex` after extracting it. It explains if your package is a different version.

### What's in it
- Native Windows static recompilation with ReXGlue SDK 0.10.0 and a patched Release runtime
- The TRG launcher, with Play, Display, Graphics, Gameplay, Achievements, Controls and About pages
- Package installer with Title ID and game version checks, and **Reinstall from package...**
- Windowed or Borderless Fullscreen, switched while playing with F11; window size, monitor and VSync choices
- Per-monitor high-DPI support, with the launcher and the game window placed and sized correctly at 100% to 225% scaling and above
- Presentation: Letterbox keeps the picture's proportions, Stretch fills the window
- Aspect ratios 16:9, 16:10, 21:9, 32:9, 4:3 and 5:4, render resolution up to 4x, internal render resolution, FXAA, multisampling and texture filtering choices
- Graphics card selection
- Game language: English, French, German, Italian or Spanish
- Xbox controller support with no setup, and keyboard play
- Local saves: the game saves and loads as on the console
- Offline achievements: unlocks are kept on your PC, listed in the launcher and in the game with F7
- Portable player data in a `user_data` folder next to the program
- Clean exit when the window is closed and when the game ends normally

### Known limitations
- Windows only. There is no Linux build yet.
- Offline only: there is no Xbox Live connection or online play, so the four achievements that need online ranked matches cannot be earned.
- The game runs at its original speed of 60 updates per second; there is no frame rate selector.
- Only game version 0.0.1.3 is supported.
- The program is not code-signed, so Windows SmartScreen may show a warning the first time.

### Reporting problems
If the game does not start or crashes, please open an issue and attach the newest `ssf2t_hd_remix_*.log` file from the `logs` folder next to the program, with a line about what you were doing.

### Requirements
Windows 10 or 11 (64-bit), a DirectX 12 graphics card, and the latest Microsoft Visual C++ 2015-2022 Redistributable (x64).

### Checksums
The ZIP's SHA-256 is published with the release as `SSF2THDRemix-v0.1.0-windows-x64.sha256`.

| File | SHA-256 |
| --- | --- |
| `SSF2THDRemix-v0.1.0-windows-x64.zip` | `9a17a6b2f04629c8a9d23e69d5fdf56aecc9b63e88b66099a1e152b89715128c` |
