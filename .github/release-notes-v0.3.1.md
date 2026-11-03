## Super Street Fighter II Turbo HD Remix PC Port v0.3.1

**v0.3.1 is a maintenance release.** It fixes the black side bars on the winner screen and improves the reliability of the online service. It adds no new gameplay feature, and the Windows and Linux builds change together.

No game files are included. You need your own Super Street Fighter II Turbo HD Remix Xbox Live Arcade package, and optionally the official Title Update #1 package.

## Fixes and Improvements

- **Winner screen with Fill**: with **Fight Area** set to **Fill 16:9**, the winner screen no longer shows black bars on the left and right. It now fills the window like the VS screen. Character Select, VS and the fights themselves are unchanged.
- **Fight statistics**: the host of an online match now reports each fight to the online service, anonymously: the two characters, who won, whether the match used a direct connection or the relay, the game build and the platform. No names, addresses, files or settings are sent. Local Match, Arcade and Training never report, and the program still never connects to the online service while Online play is off. A fight is counted once, and a reporting failure cannot affect the fight, the rollback or the connection.
- **Lobby hardening**: the online service now limits repeated sign-ins, match creation, joins, invites, resumes and invalid relay traffic per account and per address, so one misbehaving client cannot exhaust it for everyone else. Normal play is not affected. The service still accepts v0.3.0 clients; they simply add no fight statistics.

## Windows and Linux

The Linux AppImage has the same applicable feature set as Windows, with the same platform-specific differences as in v0.3.0. The project's automated release gate checks Windows and Linux parity together, for both supported game builds: Base013 (0.0.1.3) and TU023 (0.0.2.3). The gate is headless; the game was not played on every combination, and the Linux AppImage is still **experimental**, so please report what you find.

## Base Game and Title Update Support

The base game 0.0.1.3 is fully supported on its own. Title Update #1 (0.0.2.3) stays optional, and the fixes above apply to both.

## Updating from v0.3.0

The v0.3.0 launcher offers this update when it starts; choose **UPDATE NOW**. Game files, an installed Title Update, saves, settings, achievements and logs are kept, and nothing has to be reinstalled.

## Install

Install, Title Update and AppImage instructions are the same as in v0.3.0.

- **Windows**: unzip **SSF2THDRemix-v0.3.1-windows-x64.zip** anywhere you can write to and run **ssf2t_hd_remix.exe**. On the launcher's **Play** page click **Install Game** and pick your own package, optionally **Install Title Update**, then press **PLAY**.
- **Linux AppImage (experimental)**: download **SSF2THDRemix-v0.3.1-x86_64.AppImage**, run `chmod +x` on it and start it from a folder you can write to. `game`, `logs` and `user_data` are kept next to the AppImage. It needs glibc 2.31 or newer and a Vulkan driver (see `docs/linux.md`).

## Known limitations

- Some fixed arcade and interface artwork stays in English in Português Brasil.
- Relay fallback has not been played between real players; Direct UDP has.
- Friend matches only; no Ranked Match, public matchmaking or Tournament Match.
- Windows is the supported platform. The Linux AppImage is experimental.

## Reporting problems

If the game does not start, crashes or an online match fails, please open an issue and attach the newest `ssf2t_hd_remix_*.log` file from the `logs` folder next to the program, with a line about what you were doing.

## Requirements

Windows 10 or 11 (64-bit), a DirectX 12 graphics card, and the latest Microsoft Visual C++ 2015-2022 Redistributable (x64). Linux: x86_64 with Vulkan and glibc 2.31 or newer. Online play needs an internet connection.

## Checksums

The SHA-256 of each file is published with the release as `SSF2THDRemix-v0.3.1-windows-x64.sha256` and `SSF2THDRemix-v0.3.1-x86_64.sha256`, so you can verify your download.

| File | SHA-256 |
| --- | --- |
| `SSF2THDRemix-v0.3.1-windows-x64.zip` | `6520113ba494d76aae79a395d69f4d97442e59f4822e7083c068ca846f988b6c` |
| `SSF2THDRemix-v0.3.1-x86_64.AppImage` | `7e76791dfb946d711f83961fc6e9693ba0cbd3eb1eddf4fbafa2e00ee4f86132` |

## VirusTotal

Both final release artifacts were scanned with VirusTotal before publication and had 0 detections at release preparation time

| File | Result | VirusTotal |
| --- | --- | --- |
| SSF2THDRemix-v0.3.1-windows-x64.zip | 0 detections | [VirusTotal report](https://www.virustotal.com/gui/file/6520113ba494d76aae79a395d69f4d97442e59f4822e7083c068ca846f988b6c) |
| SSF2THDRemix-v0.3.1-x86_64.AppImage | 0 detections | [VirusTotal report](https://www.virustotal.com/gui/file/7e76791dfb946d711f83961fc6e9693ba0cbd3eb1eddf4fbafa2e00ee4f86132) |

VirusTotal is an additional check, not a guarantee of safety, so please also verify the SHA-256 of your download
