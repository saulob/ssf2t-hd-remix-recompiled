# Navigation icons

The sidebar icons of the launcher, one per page. Each SVG is compiled into the executable and drawn in the sidebar's
colours at the exact pixel size the sidebar asks for. `src/launcher_pages.h` maps every page to its file.

**Keep this file with the icons.** `display.svg` and `bug-report.svg` require attribution.

## Attribution required

| File | Title | Author | Collection | Source | License |
|---|---|---|---|---|---|
| `display.svg` | Display Television Tv Screen Gadget | nickylimyeanfen | Inc Device Icons | https://www.svgrepo.com/svg/381247/display-television-tv-screen-gadget | Creative Commons Attribution |
| `bug-report.svg` | Bug File | Vectopus | Atlas Variety Line Icons | https://www.svgrepo.com/svg/456346/bug-file | MIT License |

`display.svg` is not CC0 and not public domain. License: https://creativecommons.org/licenses/by/

`bug-report.svg` is under the MIT License; its notice is in `packaging/licenses/THIRD-PARTY-LICENSES.txt`.

Both files were renamed from `display-television-tv-screen-gadget-svgrepo-com.svg` and `bug-file-svgrepo-com.svg`. Their
contents are unchanged; the launcher draws them scaled and in the sidebar's colours.

## CC0

CC0 asks for no attribution; the sources are kept for provenance.

| File | Page | Title | Source | License |
|---|---|---|---|---|
| `play.svg` | Play | Play | https://www.svgrepo.com/svg/522228/play | CC0 |
| `graphics.svg` | Graphics | Controls Alt | https://www.svgrepo.com/svg/348105/controls-alt | CC0 |
| `gameplay.svg` | Gameplay | Gamepad | https://www.svgrepo.com/svg/523439/gamepad | CC0 |
| `online.svg` | Online | Globe | https://www.svgrepo.com/svg/522410/globe | CC0 |
| `controls.svg` | Controls | Keyboard | https://www.svgrepo.com/svg/524665/keyboard | CC0 |
| `trophies.svg` | Achievements | Achievements | https://www.svgrepo.com/svg/304482/achievements | CC0 |
| `about.svg` | About | About | https://www.svgrepo.com/svg/486929/about | CC0 |

## Changes from the downloaded files

All files have LF line endings and their canonical names. `online.svg` also lost its white background rectangle and the
drop-shadow filter on its lower line, because neither can be tinted. No path was changed. Each file keeps the colours it
was published with: the launcher uses only its shape.
