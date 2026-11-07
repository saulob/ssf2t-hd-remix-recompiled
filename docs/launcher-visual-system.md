# Launcher visual system

The launcher is the TRG Launcher (`third_party/trg-launcher`, v1.1.0, unmodified) built from a patched copy:
`patches/trg-launcher/*.patch` are applied in order by CMake at configure time. `0001`–`0007` are the port's own
changes (hyperlinks, download headers and allowed URLs, Linux file dialog, Ctrl+S in text inputs, the UI translator and
the Sponsor link). `0008`–`0015` carry the launcher's layout and visual system.

## Where the visual patches come from

`0008`–`0011` reproduce the final state of the TRG layout and polish patches of the sister port that shares this
launcher library, re-applied onto this port's patch stack and merged by hand where the translator and Sponsor patches
differ. They add `trg/layout.h` (every metric in logical pixels) and `trg/page_navigation.h`.

| This port | Concept | Origin |
|---|---|---|
| `0008-compact-layout` | type scale, control height, row padding, compact header and footer, sidebar icon slot (`Page::icon`), `Ui::RowLeft`/`RowControl`/`InfoGrid` | sister `0006` |
| `0009-page-frames-and-sidebar-footer` | one page per frame, sidebar and footer alignment, keycap shortcuts, outlined panels | sister `0007`, `0008` |
| `0010-visual-polish` | Sponsor card and button, panel borders, text hierarchy | sister `0009` |
| `0011-final-polish-and-row-rhythm` | stable scrollbar gutter, drawn row separators, row rhythm, sidebar proportions and adaptive sidebar rows | sister `0010`–`0015` |
| `0012-translate-remaining-text` | translates input hints, task labels and section titles | this port |
| `0013-achievement-card-type` | achievement cards use the type scale | this port |
| `0014-sponsor-before-footer-note` | in a narrow window the footer note gives way to the Sponsor button | this port |
| `0015-device-pixel-ratio` | `Launcher::SetPixelRatio`: icons, panel borders and separators land on whole device pixels | this port |

Not ported: the sidebar count badge (`Page::badge`), which this launcher does not use.

## Page icons

`assets/launcher/icons/*.svg` are compiled into the executable (`cmake/embed_files.cmake`) and rasterized with
nanosvg (`third_party/nanosvg`) at the exact pixel size the sidebar asks for: `kSidebarIcon * scale * framebuffer scale`,
rounded to whole pixels, so icons stay sharp at 100%, 150% and 225%. The program draws the launcher in logical units, so it
tells the launcher the display scale with `SetPixelRatio`; without it the icons are rasterized at logical size and enlarged. The image is white; the sidebar tints it (selected:
on-accent, hovered: text, idle: dim). `src/launcher_pages.h` maps each page to its file; `assets/launcher/icons/SOURCES.md`
records the sources and licences.

| Page | Icon |
|---|---|
| Play | play |
| Display | monitor |
| Graphics | sliders |
| Gameplay | gamepad |
| Online | globe |
| Achievements | trophy |
| Controls | keyboard |
| Bug Report | bug file |
| About | info |

## Pages

- Online: label-size tabs on a crisp row line; friend requests, the required Connection test and the Lobby list as
  cards; the Friend Code as an emphasized row with a full-width Copy; controls fill the right column; the Lobby players
  sit in one bordered list.
- Bug Report: the same page as the sister port's: the four numbered sections, the selected-log facts with View selected
  log, My reports with rows and an expanded card, the Remove confirmation dialog, and content kept clear of the
  scrollbar (`src/page_margin.h`, which reads the window from `GetCurrentContext()` because the runtime does not export
  `GImGui`).
- Play: the Title Update button stacks under the game button instead of halving a narrow column; the long package note
  moved under the row.
- Achievements: the summary and cards use the type scale.

## Tests

`ssf2t_launcher_visual_tests` checks the page order, that every page has an icon that rasterizes at 21, 32 and 47 px, the
sidebar fit for every window height, the layout constants, the footer shortcuts and that the launcher is wired to the
patched TRG. `ssf2t_localization_tests` checks that every label, help and button text a page hands to the launcher has a
pt-BR row and that sidebar names and footer shortcuts keep their shape in pt-BR. Nothing compares pixels.
