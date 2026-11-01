## Super Street Fighter II Turbo HD Remix PC Port v0.3.0

**v0.3.0 is the Online Play release.** It adds native online multiplayer with rollback netplay, direct peer-to-peer UDP connections and an automatic relay fallback, together with a Brazilian Portuguese localization, the same features on the base game and with Title Update #1, the same applicable features on Windows and Linux, and a fix for the Windows launcher idle hang reported in [#9](https://github.com/saulob/ssf2t-hd-remix-recompiled/issues/9).

No game files are included. You need your own Super Street Fighter II Turbo HD Remix Xbox Live Arcade package, and optionally the official Title Update #1 package.

## Online Play

Play Online Match against your friends over the internet, from the game's own **Multiplayer** menu.

- **Online Match**: **Find Match** joins a friend's match, **Create Match** hosts one, and the game's own **Invite Player** invites an online friend. A match is always one against one.
- **Friend codes and friend requests**: share your Friend Code, add a friend's code, and they accept. **Presence** shows which friends are online, hosting or in a game, with their platform and version.
- **Online name**: choose the name other players see. Local player names stay separate and are used only in Local Match.
- **No IP addresses**: the online service handles identity, friends, presence, finding matches, signalling and the relay fallback. You never type an address or forward a port by hand.
- **Rollback netplay**: fights are synchronized with rollback, so online play stays responsive. The two players agree on the fight's starting state before every fight (Frame Zero), and you can play several fights in a row in one session, going back to the menus and starting a new match without restarting.
- **Connection test**: checks the online service and how other players will reach you (STUN, router and relay availability) and reports Ready, Limited or Failed.
- **Ping display**: **F3** shows the round trip to the other player next to the frame counter.
- **Version compatibility**: both players must run the same game build, Base013 with Base013 or TU023 with TU023. A mismatch is shown instead of a failed match.

Online play is **Off by default** and the program never connects to the online service while it is off. Infinite Time is disabled while Online play is on, because it changes the simulation both players run.

### Setting it up
1. Launcher, **Online** page: turn **Online play** On and choose an **Online name**.
2. Run the **Connection test**. With Online play on, **PLAY** is enabled once one test has finished, whatever its result.
3. Share your **Friend Code**, paste a friend's code and **Send request**, and have them **Accept**.
4. Both press **PLAY**, then choose **Multiplayer > Online Match** in the game.
5. One player chooses **Create Match** (and can **Invite Player**), the other chooses **Find Match** or accepts the invite.

On Windows, allow network access if Windows Defender Firewall asks.

### Direct UDP first, Relay as fallback
- **Direct UDP** is always tried first. The two PCs exchange public endpoints through the online service, then use STUN and UDP hole punching to connect directly. In a direct match the fight itself never passes through the service.
- **Relay** is the automatic fallback. When a direct connection cannot be established, for example behind a strict or carrier-grade NAT, the match is carried through the online service's relay instead. There is no setting for it, and not every match uses it.
- The host decides, both players always use the same transport, and it never changes during a match. The Connection test shows whether the relay is available.

### What has been tested
- Real internet play between two players on different connections used **Direct UDP** successfully: Online Match, Invite, several fights in one session, rollback synchronization and matching Frame Zero state, with no gameplay desync observed.
- The relay fallback is implemented, selected automatically and covered by the automated tests. It has not yet been played between real players over the internet.
- Not every router and NAT combination is guaranteed to work. If a connection fails or falls back unexpectedly, run the Connection test and send the log (see Reporting problems).

### Online scope
Friend matches only: there is no public matchmaking, Ranked Match, Tournament Match or Quarter Match. Because Ranked Match is not offered, the four achievements that need online ranked matches still cannot be earned.

## Português Brasil

**Português Brasil is a new language added by this PC port.** The original game never had a Brazilian Portuguese option, and this is not an official localization.

- The **launcher**, the **Online page**, overlays, dialogs and system messages
- The **game menus**, options, help and **How to Play** text, character-specific text and **move descriptions**
- The **online menus** and messages: Online Match, Find Match, Create Match, Invite Player and network errors
- Arcade **victory quotes** where the game shows them
- **Achievement** names, descriptions and the unlock toast

The text uses natural Brazilian Portuguese and keeps Street Fighter terminology where fighting-game players use it: character names, canonical move names such as Hadoken, Shoryuken and Sonic Boom, and terms such as Round, KO, Super and Combo. Choose it in the launcher's **Gameplay** page under **Language**.

## Base Game and Title Update Support

**Title Update #1 is optional again.** Both game versions are built into the same program and have the same features:

| | Base013 | TU023 |
| --- | --- | --- |
| Game version | 0.0.1.3 | 0.0.2.3 |
| What you install | Game package | Game package and Title Update #1 package |
| `default.xex` / `default.xexp` | `default.xex` CRC32 `B734F075` | plus `default.xexp` CRC32 `38DE2310` |

- **PLAY** is enabled as soon as the game package is installed. Install the Title Update later, or remove it, whenever you like.
- **Remove Title Update** deletes only `default.xexp`. Game files, settings, saves and achievements are kept, and the game keeps working as 0.0.1.3.
- Online play, the Connection test, Ping, pt-BR, achievements, Infinite Time, keyboard support, Fight Area and the other options work on both builds. Online matches pair the same build only.

SHA-256 values for both files are in the README.

## Windows and Linux

The Linux AppImage has the same applicable feature set as Windows: online play, rollback netplay, direct UDP and relay, the Connection test, pt-BR, the Title Update choice and the launcher. The only platform-specific differences are the **Graphics card** selector, which is Windows-only, and the Windows Firewall hint.

Windows and Linux, Base013 and TU023 are all checked by the project's automated parity gate. The Linux AppImage is still **experimental**: it has not been tested on real Linux or Steam Deck hardware, so please report what you find.

## Compatibility and Fixes

- **Windows launcher idle hang ([#9](https://github.com/saulob/ssf2t-hd-remix-recompiled/issues/9))**: fixed the controller polling path introduced in v0.2.0 that could leave the launcher unresponsive or crash after sitting idle in the background on some systems. The Windows launcher no longer opens a second SDL3 gamepad connection; its controller navigation reads Xbox/XInput controllers directly again. The game's own controller support is separate and unchanged.
- **Title Update optional**: v0.2.2 required Title Update #1 to play; v0.3.0 plays the base game and the updated game.
- **Launcher controller navigation on Windows** works with XInput-compatible controllers. Controllers in the game itself work as before.

## Other Changes

- **GitHub Sponsors** link on the launcher's About page. Support is optional.
- Online logging records connection, friend and match events (transport choice, Direct or Relay, failures) to help diagnose problems, without storing your private identity token.

## Updating from v0.2.2

The v0.2.2 launcher offers this update when it starts; choose **UPDATE NOW**. Game files, an installed Title Update, saves, settings, achievements and logs are kept, and nothing has to be reinstalled. If you use Title Update #1 you keep playing 0.0.2.3; use **Remove Title Update** if you want the base game 0.0.1.3. Online play starts Off; turn it on in the **Online** page and run the Connection test.

## Install

1. Unzip **SSF2THDRemix-v0.3.0-windows-x64.zip** anywhere you can write to and run **ssf2t_hd_remix.exe**.
2. On the launcher's **Play** page click **Install Game** and pick your own Xbox Live Arcade package. Wait for **Ready to play**.
3. Optional: click **Install Title Update** and pick the Title Update #1 package to play 0.0.2.3.
4. Press **PLAY**.

| | Game package | Title Update package (optional) |
| --- | --- | --- |
| Title ID | `584108A4` | `584108A4` |
| Media ID | `37B7D7D3` | `37B7D7D3` |
| Content type | `000D0000` (Xbox Live Arcade) | `000B0000` (Title Update) |
| Version | 0.0.1.3 | updates 0.0.1.3 to 0.0.2.3 |
| File the port uses | `default.xex`, 3,932,160 bytes, CRC32 `B734F075`, SHA-256 `6E1C2F9ED7D1102A311A5F5626563527EDA5F85B2FBE8DB8405B866DED3F31EE` | `default.xexp`, 2,166,784 bytes, CRC32 `38DE2310`, SHA-256 `A2E8E66B482C5D3A0D3A5D3F67A3CECB7A12CE1F6189196694FD3E765910606C` |

## Linux AppImage (experimental)

Download **SSF2THDRemix-v0.3.0-x86_64.AppImage**, run `chmod +x` on it and start it from a folder you can write to. The launcher and the install buttons work as on Windows; `game`, `logs` and `user_data` are kept next to the AppImage. It needs glibc 2.31 or newer and a Vulkan driver (see `docs/linux.md`).

## Known limitations

- Some fixed arcade and interface artwork stays in English in Português Brasil, including the `PRESS START` image, `CHARACTER SELECT`, `CONTINUE`, `GAME OVER` and `STAGE SELECT` labels, and the achievement names on the game's own Achievements screen. The text localization itself covers every supported runtime text path.
- Relay fallback has not been played between real players; Direct UDP has.
- Friend matches only; no Ranked Match, public matchmaking or Tournament Match, and the four ranked-match achievements cannot be earned.
- Windows is the supported platform. The Linux AppImage is experimental and untested on real hardware.
- The game runs at its original speed of 60 updates per second; there is no frame rate selector.
- The program is not code-signed, so Windows SmartScreen may show a warning the first time.

## Reporting problems

If the game does not start, crashes or an online match fails, please open an issue and attach the newest `ssf2t_hd_remix_*.log` file from the `logs` folder next to the program, with a line about what you were doing. For an online problem, run the **Connection test** first and say whether the match used Direct or Relay. For a problem that does not crash, turn **Detailed logging** on in the launcher's About page, reproduce it and send the newest log.

## Requirements

Windows 10 or 11 (64-bit), a DirectX 12 graphics card, and the latest Microsoft Visual C++ 2015-2022 Redistributable (x64). Online play needs an internet connection.

## Checksums

The SHA-256 of each file is published with the release as `SSF2THDRemix-v0.3.0-windows-x64.sha256` and `SSF2THDRemix-v0.3.0-x86_64.sha256`, so you can verify your download.

| File | SHA-256 |
| --- | --- |
| `SSF2THDRemix-v0.3.0-windows-x64.zip` | `PENDING` |
| `SSF2THDRemix-v0.3.0-x86_64.AppImage` | `PENDING` |
