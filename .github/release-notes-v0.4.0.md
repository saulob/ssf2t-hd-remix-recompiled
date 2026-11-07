## Super Street Fighter II Turbo HD Remix PC Port v0.4.0

**v0.4.0 is the Characters and Community release.** It adds Evil Ryu and Violent Ken, a CPU opponent in Local Match, a public online Lobby, a redesigned Online page with Friends, an in-launcher Bug Report system and a modern launcher look. Windows and Linux change together, on both supported game builds.

No game files are included. You need your own Super Street Fighter II Turbo HD Remix Xbox Live Arcade package, and optionally the official Title Update #1 package.

## Highlights

- **Evil Ryu and Violent Ken** join the roster as optional fighters.
- **CPU opponent in Local Match**, with 8 difficulty levels ([#17](https://github.com/saulob/ssf2t-hd-remix-recompiled/issues/17)).
- **Public Lobby** and a redesigned **Online** page with **Settings**, **Friends** and **Lobby** tabs.
- **Bug Report** from the launcher, with a privacy preview, **My reports** and replies from the developer.
- **Modern launcher design** with page icons, sharper rendering on high-DPI screens and a refreshed Português Brasil translation.
- **Akuma**: an **Enable Akuma** option and experimental **Akuma bug fixes** ([#13](https://github.com/saulob/ssf2t-hd-remix-recompiled/issues/13)).
- **Lower CPU usage** while the game waits for the GPU ([#16](https://github.com/saulob/ssf2t-hd-remix-recompiled/issues/16)).

## Characters and gameplay

- **Evil Ryu and Violent Ken** ([#14](https://github.com/saulob/ssf2t-hd-remix-recompiled/issues/14)): turn on **Enable Evil Ryu and Violent Ken** in the launcher's **Gameplay** page (Off by default). Both get their own Character Select cells, moves, palettes, portraits, HUD, voices, winner quotes, Results rows, endings and **Character Moves** pages. Their runtime assets ship with the release.
- **CPU opponent in Local Match**: in the Local Match lobby press **X** to add a CPU opponent to the empty side and cycle its difficulty from 1 to 8, and **Y** to remove it. The select screen, HUD and Results show **CPU** for that side.
- **Enable Akuma** (Off by default) makes Akuma selectable on Character Select without the secret input.
- **Akuma bug fixes** (experimental, Off by default) corrects three of the HD Remix Akuma bugs documented by designer David Sirlin: the Raging Demon escape after air-fireball blockstun, the invulnerable and unthrowable red fireball startup, and the zero-frame Dragon Punch. The oversized air-fireball hit box is **not** changed yet. Classic Akuma and online matches stay original. Feedback is welcome on [#13](https://github.com/saulob/ssf2t-hd-remix-recompiled/issues/13).
- **Secret fighters stay selected** when a second player joins during Character Select, and are available in every select mode where they are enabled.
- **Skip intro** (Gameplay page, Off by default) starts directly at the title screen.
- The unused **Leaderboards** entries were removed from the main and pause menus.

## Online

- **Online page tabs**: **Settings** (Online name, Friend code, Connection test, Ping display, secret characters), **Friends** (friend requests and friends) and **Lobby**.
- **Public Lobby**: turn on **Show me in Lobby** (Off by default) to be listed for other players of this game with your Online name, status, platform and version compatibility, and send friend requests from the list. Matches are still played between friends; nothing else about you is shown.
- **Secret characters online**: Akuma, Evil Ryu and Violent Ken are available in an online match only when both players have them enabled. The choice is agreed before the fight, so both games stay in sync.
- Online play, rollback netplay, direct UDP and the relay fallback work as in v0.3.x, and v0.3.x players can still be friends with v0.4.0 players. Both players must run the same game build (Base013 with Base013, TU023 with TU023) to play a match.

## Launcher

- **Bug Report** page: describe the problem, choose one of the newest logs and see exactly what will be sent. Names, folders, network addresses and online identifiers are removed first. After sending you get a ticket; **My reports** shows its status and any reply, and the sidebar points out a new reply.
- A refreshed visual design: page icons in the sidebar, sharper icons on high-DPI displays, consistent panels, spacing and typography, and a clearer footer and Sponsor link.
- Português Brasil covers the new pages and options, including **Relatar bug**.

## Fixes

- Reduced CPU usage while waiting for the GPU: the render thread now sleeps until the GPU finishes a frame instead of spinning a CPU core ([#16](https://github.com/saulob/ssf2t-hd-remix-recompiled/issues/16)). Please report how it behaves on your PC.
- The ReXGlue developer console no longer opens from the keyboard during play.

## Windows and Linux

The Linux AppImage has the same applicable feature set as Windows, with the same platform-specific differences as before. The project's automated release gate checks Windows and Linux parity together, for both supported game builds: Base013 (0.0.1.3) and TU023 (0.0.2.3). The gate is headless; the game was not played on every combination, and the Linux AppImage is still **experimental**, so please report what you find.

## Base Game and Title Update Support

The base game 0.0.1.3 is fully supported on its own. Title Update #1 (0.0.2.3) stays optional, and every feature above works on both.

## Updating from v0.3.1

The v0.3.1 launcher offers this update when it starts; choose **UPDATE NOW**. Game files, an installed Title Update, saves, settings, achievements and logs are kept, and nothing has to be reinstalled. All new options start Off.

## Install

Install, Title Update and AppImage instructions are the same as in v0.3.0.

- **Windows**: unzip **SSF2THDRemix-v0.4.0-windows-x64.zip** anywhere you can write to and run **ssf2t_hd_remix.exe**. On the launcher's **Play** page click **Install Game** and pick your own package, optionally **Install Title Update**, then press **PLAY**.
- **Linux AppImage (experimental)**: download **SSF2THDRemix-v0.4.0-x86_64.AppImage**, run `chmod +x` on it and start it from a folder you can write to. `game`, `logs` and `user_data` are kept next to the AppImage. It needs glibc 2.31 or newer and a Vulkan driver (see `docs/linux.md`).

## Known limitations

- The Akuma air-fireball hit box is not corrected yet, and the Akuma bug fixes still need validation by experienced players.
- Evil Ryu and Violent Ken use the art their packages provide; some poses and effects are approximations.
- Some fixed arcade and interface artwork stays in English in Português Brasil.
- Relay fallback has not been played between real players; Direct UDP has.
- Friend matches only; no Ranked Match or Tournament Match. The Lobby helps you find friends; it does not start matches.
- Windows is the supported platform. The Linux AppImage is experimental.

## Reporting problems

Use the launcher's **Bug Report** page, or open an issue and attach the newest `ssf2t_hd_remix_*.log` file from the `logs` folder next to the program, with a line about what you were doing.

## Requirements

Windows 10 or 11 (64-bit), a DirectX 12 graphics card, and the latest Microsoft Visual C++ 2015-2022 Redistributable (x64). Linux: x86_64 with Vulkan and glibc 2.31 or newer. Online play and Bug Report need an internet connection.

## Checksums

The SHA-256 of each file is published with the release as `SSF2THDRemix-v0.4.0-windows-x64.sha256` and `SSF2THDRemix-v0.4.0-x86_64.sha256`, so you can verify your download.

| File | SHA-256 |
| --- | --- |
| `SSF2THDRemix-v0.4.0-windows-x64.zip` | `6591054b96cdf2338d3a3119930ac4f3b4002a55d47d88f596345170d43de3ed` |
| `SSF2THDRemix-v0.4.0-x86_64.AppImage` | `a09706f5f832aae91ff27ce984284189507639646888db3ae7c4329a70c805d8` |
