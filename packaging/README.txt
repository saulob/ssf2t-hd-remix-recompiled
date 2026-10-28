Super Street Fighter II Turbo HD Remix - PC port
Version @VERSION@

Super Street Fighter II Turbo HD Remix for Xbox Live Arcade, statically
recompiled to run natively on Windows with the ReXGlue SDK. This is a native
recompilation, not an emulator.

Source code and issue tracker:
https://github.com/saulob/ssf2t-hd-remix-recompiled


NO GAME FILES ARE INCLUDED
--------------------------
You need your own copy of the game: the Super Street Fighter II Turbo HD Remix
Xbox Live Arcade package and its official Title Update #1. The port is built
from the updated game, so it needs both:

  Title ID:      584108A4
  Media ID:      37B7D7D3
  Game package:  game version 0.0.1.3 (Xbox Live Arcade, default.xex)
  Title Update:  Title Update #1, updates the game to 0.0.2.3 (default.xexp)

Any other version is refused by the launcher, which names the version it found.


REQUIREMENTS
------------
- Windows 10 or 11, 64-bit
- A DirectX 12 capable graphics card
- The latest Microsoft Visual C++ Redistributable 2015-2022 (x64). Most PCs
  have it. If Windows reports a missing VCRUNTIME140.dll or MSVCP140.dll,
  install it from https://aka.ms/vs/17/release/vc_redist.x64.exe
- Your own game package and its Title Update package, plus about 400 MB of
  free disk space next to the program for the game files


INSTALL
-------
1. Extract the ZIP anywhere you can write to (not "Program Files").
2. Run ssf2t_hd_remix.exe. The launcher opens.
3. On the Play page, click "Install Game".
4. Select your Super Street Fighter II Turbo HD Remix Xbox Live Arcade
   package. It is one file with a long hexadecimal name and no extension. On an
   Xbox 360 hard drive it is in Content\0000000000000000\584108A4\000D0000\
5. Wait while the launcher checks the package and copies the game files into
   the "game" folder next to the program.
6. Click "Install Title Update" and select the Title Update #1 package. On an
   Xbox 360 hard drive it is in
   Content\0000000000000000\584108A4\000B0000\
   The two packages are installed separately, in either order. Installing one
   does not install the other, and each button refuses the other package. The
   launcher takes only the update's default.xexp from it.
7. When the Play page says "Ready to play", press PLAY. PLAY stays disabled
   until the game package and Title Update #1 are both installed.

To repair the game files, use "Reinstall Game" on the Play page. Your settings
and saves are kept, and so is an installed Title Update. "Remove Title Update"
deletes only default.xexp from the "game" folder; the game cannot be played
without it.


UPDATES
-------
The launcher checks the project's GitHub releases once per start. When a newer
release exists, an update banner appears in the header; nothing is installed
until you choose UPDATE NOW and then RESTART TO UPDATE. The update replaces
only the port's own program files. The "game" folder, "user_data", saves,
settings and logs are kept.


WINDOWS SMARTSCREEN
-------------------
The first time you run ssf2t_hd_remix.exe, Windows SmartScreen may show a
warning, because the program is not digitally signed. Choose "More info", then
"Run anyway".


PLAYING
-------
- Xbox controllers work without setup. The keyboard works too, as an Xbox 360
  controller; the Controls page lists the keys.
- F11 in the game switches between Windowed and Borderless Fullscreen. F11 in
  the launcher switches only the launcher.
- F7 opens the achievements list. The game's own Achievements menu item opens
  it too. Achievements are kept on this PC only; the four that need online
  ranked matches cannot be earned.
- F2 shows or hides a frame counter in the corner of the window.
- Saves work as on the console: the game saves to its own storage and loads it
  again at the next start. There is no Xbox Live connection and no online play.
- To open the launcher again after turning it off, hold Shift while starting.

Launcher options:
- Display: Windowed or Borderless Fullscreen, window size, monitor, aspect
  ratio, VSync and Presentation (Letterbox keeps the picture's proportions,
  Stretch fills the window).
- Graphics: graphics card, render resolution, internal render resolution,
  anti-aliasing, multisampling and texture filtering.
- Gameplay: language (English, French, German, Italian or Spanish), and what
  the window title and the corner counter show.
- Achievements: unlocked achievements and gamerscore.

The game always runs at its original speed of 60 updates per second.


WHERE THINGS ARE
----------------
- Game files:     the "game" folder next to ssf2t_hd_remix.exe
- Logs:           the "logs" folder next to ssf2t_hd_remix.exe
- Settings, saves, achievements and launcher settings:
                  the "user_data" folder next to ssf2t_hd_remix.exe
                  (created the first time it is needed)

Copy the whole program folder to another PC to take your saves, achievements
and settings with it. To update by hand, extract the new version over the old folder,
or move "user_data" (and "game") into the new one: deleting the old folder
deletes "user_data" with it.

If the game does not start or crashes, please attach the newest file from the
"logs" folder to a report at
https://github.com/saulob/ssf2t-hd-remix-recompiled/issues
The launcher and the game write to the same log for one session, and after a
crash on Windows the log ends with a CRASH REPORT section. For other problems,
turn on Detailed logging on the About page, reproduce the problem and attach
the newest log. "Open logs folder" on the same page opens the folder.


LICENSE
-------
The original project code of this port is licensed under the GNU General
Public License v2.0 only (GPL-2.0-only). The full text is in LICENSE.

Third-party components keep their own licenses. Their license files and
notices are in the "licenses" folder: ReXGlue-LICENSE.txt and
THIRD-PARTY-LICENSES.txt.

The launcher is TRG Launcher 1.1.0 (https://github.com/TekRantGaming/trg-launcher),
licensed under the MIT License (see licenses\THIRD-PARTY-LICENSES.txt).

Game files are not included and are not covered by this project's license.
This port is not affiliated with or endorsed by the makers or publishers of
Super Street Fighter II Turbo HD Remix.
