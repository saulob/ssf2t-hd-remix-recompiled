# Saves, profile and achievements

Everything is offline and local. Nothing here signs in to Xbox Live or opens a network connection.

## Where player data lives

All writable player data is under the user data root: `<port folder>/user_data` in a release, `build\user_data` for development
(`tools\build.ps1` passes it as `--user_data_root`). Nothing is written to `game\extracted`, beside `default.xex`, or into the
original package.

| What | Path under the user data root | Written by |
|---|---|---|
| Launcher choices | `settings.toml` | the launcher |
| Game saves | `<xuid>/584108A4/00000001/<save name>/` and `<xuid>/584108A4/Headers/` | ReXGlue's content manager, for the game's own save calls |
| Achievement unlocks | `achievements/584108A4.toml` | ReXGlue's achievement store |
| Shader cache, logs | `cache/`, `logs/` | ReXGlue and the port |

Saves and achievements are separate systems with separate files.

## Save and profile mechanism

Found by reading the recompiled code, not by running the game:

- **Saves** are XamContent saved-game packages (content type 1). The game picks a storage device with `XamShowDeviceSelectorUI`,
  checks it with `XamContentGetDeviceState`, lists existing saves with `XamContentCreateEnumerator`, and reads or writes each
  file through a mounted root name (`XamContentCreate` with OPEN_EXISTING to load, CREATE_ALWAYS to save, then `XamContentClose`).
  A save that is not in the list is treated as absent and the game keeps its defaults.
- **Profile settings** are only read: voice options, difficulty and the gamer picture key. ReXGlue supplies all of them. The
  game imports no `XamUserWriteProfileSettings` and reads no title-specific setting.
- ReXGlue's content manager implements all of the above against a local profile and one dummy hard drive, under the user data root.
  No project code is involved, and no save fix was needed. The SDK was not changed.

Clean start: with an empty user data root the content list is empty, the game creates its own defaults and writes them through
the same calls. Nothing is pre-created or pre-unlocked. Restarting loads what the game wrote.

## Achievements

- 12 achievements, ids 1 to 12, 200 gamerscore. Names, descriptions, locked descriptions, gamerscore and icons come from the XDBF
  inside the player's own `default.xex`; the language follows the launcher's Language setting. Nothing is copied into the port.
- Achievements 3 to 6 need online ranked matches and cannot be earned offline.
- The game unlocks through `XUserWriteAchievements`, which ReXGlue already routes to its store, saves, and announces with its
  own toast. The port uses that unchanged. There is no achievement id 0, so no alias is needed.
- **Launcher**: the Achievements page sits below Gameplay. It shows unlocked count, gamerscore, a progress bar and the achievement
  cards, from the same store the game uses. It is read-only.
- **In game**: F7 opens and closes the Achievements overlay; the game's own Achievements menu item opens it too. Xbox B, Escape
  and F7 close it. While it is open the game reads neither the keyboard nor the controller, and keeps reading neutral input until
  the closing press is released, so B never also backs out of the game's menu.
- Host keys in use: F2 frame counter, F3 debug overlay, F4 settings, Backtick console, F7 achievements, F11 window mode.

## Tests

    tools\test.ps1

`ssf2t_achievement_tests` covers the list and summary logic, the close gate, the input rules, F7 toggling, the menu hook target and
the launcher and app wiring. `ssf2t_achievement_metadata_tests` reads the real game metadata and an unlock file in a temporary
folder; it skips without `game\extracted`.
