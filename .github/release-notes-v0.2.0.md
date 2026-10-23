## Super Street Fighter II Turbo HD Remix PC Port v0.2.0

Local multiplayer, performance and quality-of-life update.

### Highlights
- **Improved local multiplayer**: controller/keyboard slot assignment for local multiplayer was corrected, so each local player's input reaches the right in-game slot.
- **Configurable player names**: set each local player's display name in the launcher's Gameplay page.
- **Infinite Time**: a new Gameplay option that removes the round timer.
- **Fill 16:9 Fight Area**: a new Display option that removes the black bars at the sides of the fight, filling a 16:9 window with the fight area.
- **Performance improvements**: a round of ReXGlue/Direct3D 12 performance and CPU-overhead work, including asynchronous D3D12 submission, reduced shared-memory and render-target overhead, less redundant draw work, lock-free guest clock reads, optimized GPU command processing, inline bit scans, rexgpu-xenos ThinLTO, an unchanged-constant-write skip, a texture resolve lookup optimization and a per-draw loop optimization.

### Additional improvements
- **Ctrl+S** saves launcher settings while typing in a text field.
- **Graphics card selection** now lists only the GPUs actually detected on this PC; stale or empty settings resolve to the first available one.
- Linux: file-picker fallback fixes for systems without a native file dialog.
- Launcher header artwork.
- The mouse cursor now follows the last input device: mouse activity shows it, keyboard or controller activity hides it, and mouse-driven overlays always show it.
- Diagnostics: a Detailed logging option and an "Open logs folder" shortcut on the About page; crashes on Windows now end the log with a `CRASH REPORT` section.
- The launcher checks for updates on start and can install a newer release in place, keeping game files, saves, settings and logs.
- Per-monitor DPI and windowing fixes.

### Install
1. Unzip **SSF2THDRemix-v0.2.0-windows-x64.zip** anywhere you can write to and run **ssf2t_hd_remix.exe**.
2. On the launcher's **Play** page click **Install from package...** and pick your own Super Street Fighter II Turbo HD Remix Xbox Live Arcade package.
3. Wait for **Ready to play**, then press **PLAY**.

**No game files are included.** You need your own package, this exact game version:

| | |
| --- | --- |
| Title ID | `584108A4` (Xbox Live Arcade, content type `000D0000`) |
| Game version | `default.xex` 0.0.1.3, 3,932,160 bytes, CRC32 `B734F075`, SHA-256 `6E1C2F9ED7D1102A311A5F5626563527EDA5F85B2FBE8DB8405B866DED3F31EE` |

The launcher checks the Title ID when you pick the package and checks `default.xex` after extracting it. It explains if your package is a different version.

### Linux AppImage (experimental)
Download **SSF2THDRemix-v0.2.0-x86_64.AppImage**, run `chmod +x` on it and start it from a folder you can write to. The launcher and the **Install from package...** flow are the same as on Windows; `game`, `logs` and `user_data` are kept next to the AppImage. It needs glibc 2.31 or newer and a Vulkan driver (see `docs/linux.md`). It has been built and checked by script only: it has not been tested on real Linux or Steam Deck hardware yet, so please report what you find.

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
The SHA-256 of each file is published with the release as `SSF2THDRemix-v0.2.0-windows-x64.sha256` and `SSF2THDRemix-v0.2.0-x86_64.sha256`.

| File | SHA-256 |
| --- | --- |
| `SSF2THDRemix-v0.2.0-windows-x64.zip` | `bc251514b7101e45a9f49400e1b4f029dc91764c3220c5a9b426cce1d57ad689` |
| `SSF2THDRemix-v0.2.0-x86_64.AppImage` | `7955653b47cb10fb70a6e634a5f6e80f40789d5f46bee027402baf2fa6f56487` |
