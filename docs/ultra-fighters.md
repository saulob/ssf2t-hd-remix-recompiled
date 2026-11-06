# Evil Ryu and Violent Ken

## Overview

The port implements Evil Ryu and Violent Ken as two additional playable fighters. They are off by default and are turned on with one launcher option, **Enable Evil Ryu and Violent Ken** (Gameplay page; setting `ssf2t_enable_ultra_fighters`). With the option on:

- Character Select gains one extra cell for each fighter, to the right of the original roster.
- Each fighter has its own moves, palettes, portraits, HUD name and icon, voices, winner screen, results row, Character Moves page and ending.
- Both can be used in Local Match, Arcade and, when both players and both builds agree, in an Online Match.

The source tree calls the pair the *Ultra fighters* (`ultra_*` files, the setting name). Neither fighter exists in the game's own data. Each one is a **carrier** fighter (Ryu or Ken) that the game already runs, plus port-side code that changes what that carrier looks like and does while the Ultra identity is selected. All of it is installed as hooks around guest functions, for both game images (Base 0.0.1.3 and Title Update 0.0.2.3).

Online negotiation is summarized here and specified in [online.md](online.md#secret-characters).

## Implementation status

| Area | Status |
|---|---|
| Character Select cells, navigation, cursor, names, large and mini portraits | Implemented |
| Evil Ryu: teleports, Shun Goku Satsu, Senpu Kyaku, Shakunetsu Hadoken, strong Tatsumaki, walk, frame timings, hit/hurt boxes | Implemented (mix of port overrides and adapted guest behaviour, see below) |
| Violent Ken: Rasetsukyaku, Shinbu Messatsu, Inazuma Kick, Shoryuken, Tatsumaki, walk, jump, removed Ken moves | Implemented; data from `violent_ken_gameplay.bin` |
| Palettes (palette RAM and HD textures), HUD name and icon, winner banner and quotes, results rows, Character Moves page, endings | Implemented; art and icons come from packages |
| Voices (move calls, KO, winner announcement) | Implemented; come from packages |
| Online negotiation, per-character availability, rollback of host-side state | Implemented |
| Teleport and rush "ghost" look | Approximated (alternate-frame body suppression) |
| Extra poses | Approximated (two poses for Evil Ryu, one for Violent Ken, drawn over the carrier's frame) |
| Classic style | Same cells as Remix in every Character Select (see below) |
| Runtime packages in the build, the Windows ZIP and the Linux AppImage | Implemented, see [Package lifecycle](#package-lifecycle) |

"Implemented" means present in source and covered by the tests listed in the [source map](#source-map); gameplay and presentation are verified by playing, not by the test suite.

## Character identities

The port keeps its own identity per side. The guest never learns about it: the guest fighter keeps its carrier's character id.

| | Evil Ryu | Violent Ken |
|---|---|---|
| Host identity (`FighterIdentity`) | `kEvilRyu` = 2 | `kViolentKen` = 4 |
| Guest carrier | Ryu, guest character 0 | Ken, guest character 4 |
| Action-control label (source comments for the move tables) | 18 | 19 |
| Secret-character key / bit in the setup word | `evil_ryu` / 2 | `violent_ken` / 4 |
| Character Select cell | top roster row of the extra column | bottom roster row of the extra column |
| Online identity | reported as character 17 (carrier stays 0) | reported as character 18 (carrier stays 4) |

Other `FighterIdentity` values: `kGuest` = 0 (use the normal guest selection), `kRyu` = 1, `kKen` = 3.

Several numbering schemes coexist; they are independent of one another:

| Scheme | Evil Ryu | Violent Ken | Where |
|---|---|---|---|
| Guest character id (carrier) | 0 | 4 | `fighter_identity.h` |
| Cell entry: live id the cursor leaves from | 9 | 8 | `ultra_navigation.h` |
| Cell cursor: unused live id that carries the cursor | 18 | 19 | `ultra_navigation.h` |
| Roster cell record id in `ultra_portraits.bin` / roster row | 0 / row 2 | 1 / row 3 | `ultra_portraits.h` |
| Large portrait id | 17 (the unused id after Akuma) | Ken's id 4, or spare id 1 | `ultra_select_presentation.h` |
| Character Moves page index | 17 | 18 | `character_moves.h` |
| Results screen: character value / display row | 17 / row 1 | 18 / row 3 | `evil_ryu_results.h` |
| Telemetry character | 17 | 18 | `online/fight_telemetry.h` |
| Rollback cell mask bits (P1, P2) | bit 0, bit 1 | bit 2, bit 3 | `ultra_navigation.cpp` |
| Host action ids | 0x5E (Shun Goku, subs 0-2), 0x40 sub 8 (Senpu), 0x4C subs 3-5, 0x50 sub 0 (replaced) | 0xE0 Inazuma, 0xE2 Rasetsu, 0xE4 Shinbu (past every guest action header) | `evil_ryu_actions.h`, `violent_ken_data.h` |
| Replacement sound ids | 0xE0-0xEA | 0xD0-0xDE | `evil_ryu_audio.h`, `violent_ken_audio.h` |
| HD palette slot | 10 + side | 10 + side (Ken's palette row) | `ultra_hd_palette_hook.cpp` |

The `nChr` name does not appear in the tracked source; the "17" and "18" above are the port's own numbers for the two fighters wherever they need a value past the guest's 0-16 range.

### When a side counts as Evil Ryu or Violent Ken

`UltraIdentityOnSide` (`ultra_fight.h`) is evaluated on every use rather than cached. A side is an Ultra fighter only when all of these hold:

1. `ssf2t_enable_ultra_fighters` is on.
2. The side's requested identity (set by the Character Select cell) is that fighter.
3. The guest fighter has started (its first byte is non-zero), its character is the carrier (Ryu for Evil Ryu, Ken for Violent Ken) and it is not the hidden Akuma.

Because the identity is derived from the fighter on each call, nothing about it needs to be rolled back separately from the requested identity and the cell mask.

## Carrier architecture

| Concern | How it is done |
|---|---|
| Guest carrier fighter | The guest runs a normal Ryu or Ken. Everything else is layered on top. |
| Host identity | `RequestedIdentity(side)` is set by the Character Select integration; `fighter_identity.*` resolves it against the fighter. |
| Selection identity | While an Ultra cell is selected, the guest's own selection is parked on the carrier's cell (live 0 or 4). The cell mask lives in `UltraNavigation`. |
| Fight setup | No guest data is edited permanently. Per fighter, lazily built copies are used: an action table copy, a box-pack copy and a walk-row copy, allocated on the guest heap and pointed to only for that fighter. |
| Action override | The guest's action lookup hook returns port-built records (Evil Ryu: compiled-in tables in `evil_ryu_actions.cpp`; Violent Ken: records from `violent_ken_gameplay.bin`). Each patch checks the original guest byte first; a mismatch skips that action and the guest's own data stays. |
| Palette handling | After the guest copies fighter colours into palette RAM, the palette hook replaces the carrier colour's words with the Ultra fighter's. HD textures use a spare palette sheet, see [Visual behavior](#visual-behavior). |
| Presentation override | The sprite draw hook swaps in package art for names and extra poses; the HUD hook swaps the name and the portrait icon; the portrait hook lends a portrait slot for the draw. |
| Host-only moves | Moves that have no guest handler (teleports, Rasetsukyaku, Shinbu Messatsu, Inazuma Kick, Senpu Kyaku) run from a per-side `HostMove` record that the special-tracker hooks advance each frame. |

Every hook starts with `if (!UltraFightersEnabled()) return original(...)`, so with the option off the game runs unmodified. `ultra_hook_table.h` lists the guest function each hook wraps, per image, so the parity suites can check each pair against the real images.

## Character Select integration

**Layout.** The cells sit one column to the right of the guest grid: column 8 in the arcade grid (2x8) and column 7 in the versus grid (3x6, used by Local Match and Online Match). Evil Ryu is on the top roster row, Violent Ken on the one below. The cells are drawn inside the guest's own roster pass (`roster_cells_hook.cpp`), so the cursors, markers and the full-screen fade land on top of them.

**Availability.**

| Mode | Evil Ryu cell | Violent Ken cell |
|---|---|---|
| Local, Arcade | `ssf2t_enable_ultra_fighters` **and** Evil Ryu's required packages valid | the same option **and** Violent Ken's required packages valid |
| Online Match (game mode 3) | negotiated and locked result for Evil Ryu | negotiated and locked result for Violent Ken |

A fighter whose required packages are not valid is never offered, locally or online. See [Runtime package requirements](#runtime-package-requirements).

**One availability rule.** `SecretCellsFor(game_mode)` (`ResolveSecretCells` in `character/secret_select.h`) answers which secret cells exist, and the roster, navigation, confirm and the Akuma unlock all ask it, so a drawn cell is always a usable cell. The HD style is deliberately not an input: Classic (style byte 0) and Remix offer the same cells in every mode, and the style only decides how a pick is committed.

| Game mode byte | Menu item | Grid | Availability |
|---|---|---|---|
| 0 | Arcade | 2x8 | Local options and packages |
| 1 | Local Versus | 3x6 | Local options and packages |
| 2 | Training | 3x6 | Local options and packages |
| 3 | Ranked, Player and Tournament Match | 3x6 | The negotiated set, per fighter |

In the developer Netplay Test the Ultra cells are not drawn: its two instances agree on a fight through frame zero guest state, which holds no Ultra identity. Akuma stays, because he is guest state.

Classic uses the same grids as Remix and the same action hooks, but its own Ryu and Ken action data, so an Ultra fighter is the port overrides applied to that data. An Ultra fighter has no old version. Outside Arcade a locked side is asked NORMAL / SUPER, so `AskLockedSide` hides that prompt from a side on an Ultra cell; in Arcade a press in the post-confirm window sets the flag (`+0x3B6`) instead, and `UltraDropsOldVersion` clears it. Neither touches a side off the cells. The GOUKI? prompt beside them never shows: the guest clears its option every frame.

Training selects both sides in turn from one controller, the guest copying the pad across. Player 1's cell stays through the dummy's turn because the select session has no gap, and a Training reset re-resolves the identity from the same request. A new Character Select clears it.

**Akuma in Classic.** The guest has no cursor route to him in Classic: its move tables have no Akuma entry and its commit (`sub_82240870` / TU `sub_822409A0`) ignores the live cell. The cell therefore feeds the guest's own secret, the same way in every mode. While hovering, `live` is 32 and the special byte set, as in Remix, with the selected-cell word written too because the Classic cursor step drops id 32 unless the hover already says Akuma. On confirm, `ConfirmSelect` gives the guest the Ryu slot and opens the secret's window (`+162` = 2, `+161` = 0). On the next tick the guest's own window call commits; `CommitAkuma` only supplies, for that one call, the three punches and the online session word (`0x83C24DB0` / TU `0x83C25128`) that online Classic otherwise refuses without, since the negotiated set already decided. The commit picks Classic Akuma from the style. The Start and three punches secret is untouched.

**Navigation** (`UltraNavigation::Resolve`, tried before the Akuma navigation):

| From | Input | Result |
|---|---|---|
| Original roster cell with live id 9 | Right | Enters Evil Ryu: the guest moves, then the host selects the cell and puts the guest cursor back on carrier Ryu |
| Original roster cell with live id 8 | Right | Enters Violent Ken (carrier Ken) |
| Evil Ryu | Left | Back to live id 9 |
| Evil Ryu | Down | Moves to Violent Ken if available |
| Violent Ken | Left | Back to live id 8 |
| Violent Ken | Up | Moves to Evil Ryu if available |
| Either cell | any other direction | Held: the guest move is skipped |

If the cell a side is on stops being available (for example the option or negotiation changes), the side drops back to the guest selection.

**Cursor.** The guest places a cursor from `live[side]`. While an Ultra cell is selected, the cursor object is updated with `live[side]` pointed at an unused live id (18 or 19) whose position-table entry is rewritten to the cell (X 336 arcade / 304 versus; Y 48 Evil Ryu / 16 Violent Ken), then the carrier id is put back.

**Confirmation.** Confirming locks the carrier, as for any fighter; the requested identity already says which Ultra fighter it is. A new interactive select (a gap of at least 3 s in select activity) resets the cell mask and the host move trackers.

**P1 and P2.** The mask, requested identity, presentation and portraits are all per side. Both sides can be the same Ultra fighter at once; HD palettes use a separate mirror entry for P2 in that case.

**Names.** The guest draws Ryu's name from the tile string 0x700..0x703 (HUD 0x408) and Ken's from 0x740..0x743 (HUD 0x40E). Which side a tile belongs to is decided from its screen position. For a side whose requested identity is the Ultra fighter, the sprite hook draws the package name art instead. If the guest has not drawn a name for that side within 250 ms, the roster name plate drawn by the roster pass is used (`violent_ken_assets.bin` / `evil_ryu_assets.bin`).

**Large portrait.** See [Portrait system](#portrait-system).

**Online restrictions.** Online, a cell is offered only for characters in the locked negotiated set. See [Online behavior](#online-behavior).

## Portrait system

**Mini portraits (roster cells).** `ultra_portraits.bin`, in the program's `assets/characters` folder, holds the two roster cell images. Header: magic `SSF2UPT1`, a little-endian count (at most 16), then per record a little-endian id, width and height followed by RGBA pixels. Records must be 128x128; the file is capped at 1 MiB. Id 0 is Evil Ryu, id 1 Violent Ken. At load the original roster's frame is copied over the outer 6 pixels of the art (`ApplyRosterFrame`, using the Akuma cell image read from `flog_x360.fpg`). The result is uploaded once as a guest texture and drawn as a quad in the roster pass. Name plates (`evil_ryu_assets.bin`, `violent_ken_assets.bin`, record id 2) are drawn the same way.

**Large portraits.** The guest draws a hovered fighter's large portrait through per-id tables. The port does not enlarge them; it **lends** entries for the length of one draw (`PortraitLoan`, `portrait_loan.h`):

- Evil Ryu uses the unused portrait id 17 (the one after Akuma). The loan writes the fighter's large texture into that id's large and "beaten" table entries and into its palette row, points the side's hover word at 17, runs the guest draw, then restores every word it changed.
- Violent Ken has no id of his own. He borrows Ken's id 4, or the spare id 1 when the other side is really showing Ken. The id is chosen by `ViolentPortraitId`.
- A side that is *not* an Ultra fighter but hovers id 17 has its portrait hidden for the pass, so it never shares the slot.
- The beaten/Continue screens read tables that stop at the last guest character, so `CarrierPortraitLoan` lends the carrier's own entries instead.
- If the large art cannot be used the loan is inactive, the side's hover word becomes "none" and no large portrait is drawn for that side. `evil_ryu_sprites.bin` and `violent_ken_sprites.bin` are required, so this does not happen on a valid install.

**Invariant.** The guest's large-portrait table is bounded: `kLargePortraitIds` = 18, valid ids are 0-17, and the mini-portrait tables follow it directly (id 18 would be Ryu's mini portrait). `PortraitLoan` refuses any id at or past that bound, because writing there would overwrite neighbouring guest tables. This is why Violent Ken reuses an existing id instead of taking a new one, and how the original Ryu and Ken table entries stay intact: every loan saves the original words and restores them before the draw call returns.

**Where the loans apply.** Presentation modes 2, 3, 5 and 9 (shared portraits) use the large-portrait loan; modes 6 and 7 (losing, Continue) use the carrier loan with the "beaten" art. If the package has no beaten face, the large portrait stands in; it never falls back to the carrier's art.

## Evil Ryu gameplay

All of it runs only on a side that is Evil Ryu. Classification: **Native** = the guest's code runs unchanged; **Override** = a hook replaces the behaviour; **Adapted** = guest code runs with port changes; **Approximated** = visual stand-in; **Limitation** = documented gap.

| Feature | Class | Implementation |
|---|---|---|
| Teleports (forward / back, punch / kick = 4 variants) | Override | Recognised by `TeleportRecognizer` from 10 three-step motion paths followed by all three punches or all three kicks (newly pressed or released), 15-tick step window. Runs as a `HostMove`: startup, dash, 5-tick tail, recovery. Punch teleports use velocity 0x8A0 / threshold 0x240, kick teleports 0x600 / 0x1B0, both with deceleration 0x24; back variants are negated. During the dash the fighter is untargetable and faces the opponent. |
| Teleport look | Approximated | Body drawn on alternate frames during the dash only; startup shows a back-turned pose. Startup has no hurtbox or pushbox, recovery is hittable. |
| Shun Goku Satsu | Adapted | Input LP, LP, Forward, LK, HP (or LP, LP, Forward+LK, HP) with full meter (0x30). The guest's own raging-demon handler runs the freeze, grab, blackout and damage; the port arms a 21-tick startup then a long dash that whiffs when speed falls to 0x400, and supplies the start and finish actions (0x5E subs 0-2, with a back-turned finish pose). The grab-range lookup is temporarily pointed at a different character's row while the move is active. |
| Guest 236236+P super | Override (refused) | `GateSuper` refuses the guest's super start for both Ultra fighters; meter and HUD are untouched. |
| Senpu Kyaku (Forward + MK hop) | Override | Starts only from a neutral stand or crouch frame; action 0x40 sub 8, attack box id 74, hop velocity 0x480 / 0x300 with a hold frame until landing, 240-tick timeout. |
| Forward + HP unique | Adapted | Redirected to the close or far heavy punch by distance to the opponent. |
| Shakunetsu Hadoken (63214 + P) | Adapted | The guest's half-circle tracker sees mirrored direction bits for Evil Ryu only, so the reverse motion is recognised. Action 0x4C subs 3-5 are replaced; recovery is set per strength (42 / 50 / 60 ticks). The 236+P Hadoken stays the guest's. |
| Tatsumaki | Adapted | Light keeps Ryu's actions. Medium and heavy use a 5-frame startup (action 0x50 sub 0); spin count follows strength (0, 1, 2); attack box id 75. |
| Shoryuken, plain Hadoken | Native | Guest actions; the box pack changes some attack class bytes. |
| Walk | Override | Per-fighter walk row: forward 0x398, back 0x268. |
| Jump | Native | No hook. |
| Frame timings | Adapted | Patches to idle, walk, pre-jump, far/round kick, crouch kick and air kick durations and some hurtboxes, each checked against the expected original byte. |
| Attack boxes, hurtboxes | Adapted | `BuildEvilPack` copies the fighter's box pack (0x1200 bytes), relocates the 76-record attack table after it, patches boxes (ids 2, 16, 26, 29, legs box 13, class bytes of several groups) and appends records 74 and 75. Patches check original bytes because the HD ruleset can renumber records; a mismatch is skipped. |
| Poses, win poses | Approximated | Two poses (back-turned, power stance) are drawn over the carrier's frame at port-defined placements; win-pose actions 0x38, 0x3A and 0x52 map onto them. |
| Meter | Native | Only the full-meter test for the super; consumption is the guest handler's. |

**Start gate.** A host move starts only when the fighter is not in states 12, 14 or 20, the side is human controlled (`+0x382`, named `kRuleset` in the code), the old-version flag is clear, the fighter is not busy or untargetable, the two fighter health fields `kHealth` and `kHealthMax` are equal and three game flags are clear. Supers need full meter. Teleports are additionally refused in guest state 10, and in state 4 at step 6, unless the fighter's hit-confirmed flag is set. Moves cannot start while airborne; ground height is learned from the last standing height.

## Violent Ken gameplay

| Feature | Class | Implementation |
|---|---|---|
| Rasetsukyaku | Override | Input 623 + kick (5 motion paths), heaviest newly pressed kick sets strength. Host action 0xE2, sub = strength. Dash velocity 0x680 / 0x800 / 0xA00, threshold 0x4A0 / 0x5F0 / 0x740, then recovery with the speed decaying by 0x24 per tick, 240-tick timeout. |
| Shinbu Messatsu | Override | Input 236236 + kick (4 paths), full meter. Starts through the guest's super freeze; then rush (velocity 0x600), rising rush (0xC00 / 0xA00, untargetable), landing, recovery. Host action 0xE4 subs 1-4. The rising rush happens whether or not the rush connected. |
| Inazuma Kick | Override | Forward + MK from a neutral stand or crouch frame; host action 0xE0; plays to its end flag or 240 ticks. |
| Shoryuken | Adapted | Guest actions 0x4E subs 0-2 with port horizontal velocities 0x400 / 0x900 / 0xC00. |
| Tatsumaki (ground) | Native | Guest. |
| Tatsumaki (air) | Adapted | Action 0x50 subs 5-7 with horizontal velocity 0x280 / 0x320 / 0x3C0 and zero vertical speed and gravity. |
| Walk | Override | Per-fighter row: forward 0x400, back 0x300. |
| Jump | Override | The jump-start hook (human and CPU share it) overwrites the stored velocities and accelerations from three rows (forward, back, neutral). |
| Removed Ken moves | Override | Move ids 6 and 8-12 end immediately; the three Ken kick-command trackers are skipped. |
| Super refusal | Override | Same `GateSuper` refusal as Evil Ryu. |
| CPU | Override | If the CPU arms a removed special, the selection is cleared and the CPU move is re-rolled (up to 4 tries). Nothing equivalent exists for Evil Ryu. |
| Poses, attacks, frames | Adapted | `violent_ken_gameplay.bin` supplies replacement actions (per frame: duration, event, source frame, hurtbox head/body/legs, attack, push, shape, weight, stance flag), patches and attack records. Stance-flagged frames draw the stance art over the carrier. |
| Palette | Adapted | Package palettes keyed by the Ken colour, see [Visual behavior](#visual-behavior). |
| Fight setup | Override | `UseViolentPack`, `UseViolentWalk` and the action table copy are installed for the side every frame before the guest tracker runs. |

`violent_ken_gameplay.bin` format: magic `SSF2VKG1`, three little-endian counts (actions, patches, attacks; limits 64 / 512 / 128), then actions (4-byte header plus 14-byte frames), patches (8 bytes: action, sub, frame, field, expected, value) and attacks (18 bytes: id, flag, 16-byte record). The file size must match exactly and is capped at 8 MiB. The attack `replace` flag is parsed but not read when applying: any record id below 128 is overwritten.

## Visual behavior

- **Palette RAM.** `evil_ryu_palette.bin` / `violent_ken_palette.bin` hold, for each of the 10 base colours, the carrier's 16 body words and the Ultra fighter's. After the guest fills palette RAM, the hook finds the carrier colour whose words 1-14 match (`MatchRyuColour`) and writes the Ultra fighter's words.
- **HD textures.** `evil_ryu_hd_palette.bin` / `violent_ken_hd_palette.bin` hold 11 palettes of 1024 bytes (10 colours plus a mirror entry used for P2 when both sides are the same Ultra fighter). A spare palette sheet is created with the guest's texture constructor, rewritten when the colour changes, and the fighter's colour byte is pointed at it for the draw.
- **Sprite substitution.** Names (HUD and Character Select tiles), the two Evil Ryu poses and Violent Ken's stance pose are indexed textures from the sprite packages. They are drawn by pointing the sprite at a port-owned record with its own placement, then restoring the sprite afterwards. Sprites have no palette of their own and are drawn through the fighter's palette, so they appear only once the HD palette is in place.
- **Fallback poses.** The sprite and HD palette packages are required, so a fighter that is offered has its poses. If a texture upload fails at run time the carrier's own frame is drawn.
- **Teleport and rush ghost.** Alternate-frame body suppression during the dash (Evil Ryu teleport, Violent Ken rush), no shader.
- **Portrait framing.** Large portraits are stored facing left because the guest mirrors them for P1; the HUD icon is 128x128 ARGB, facing left.
- **HUD.** `UpdateHud` replaces the six name widgets of the side's HUD with "Evil Ryu" or "Violent Ken" and points the portrait element at the icon texture. Both are undone when the side stops showing the Ultra fighter, so the other side's Ryu or Ken keeps the original.
- **Winner screen.** The banner uses a different text id for the winner so the guest font and layers are kept ("EVIL RYU WINS" / "VIOLENT KEN WINS", built from each language's own banner). The quote shown comes from per-fighter text tables (5 lines for Evil Ryu, 4 for Violent Ken) swapped into the carrier's quote row for that call; a slot takes the quote in turn, an approximation of the guest's weighted pick.
- **Results screen.** The results list grows from 17 to 19 rows; Evil Ryu and Violent Ken get their own win/loss records, kept on the host.
- **Character Moves page.** Pages 17 and 18 are added to the Character Moves screen (screen set, script and string-table lines are patched in memory). The page is also a precondition for the winner banner, quotes and endings.
- **Endings.** Each fighter has an ending group injected into its carrier's ending screen: four cuts, a line of text every 5 s, with the carrier's music. If the optional package, the injected group or the patched documents are missing, the carrier's own ending plays.

## Audio

- Packages: `evil_ryu_audio.bin` (6 clips: Hadoken, Shoryuken, Tatsumaki, KO, Shun Goku Satsu, announcer name) and `violent_ken_audio.bin` (8 clips: Hadoken, Shoryuken, Tatsumaki, throw, KO, Rasetsu, Shinbu, announcer name). Format: an 8-byte magic, a count, then per clip an id, a sample rate (at most 48000) and 16-bit PCM samples. A package missing any clip is rejected as a whole.
- **Detection.** Each package is read once, lazily. It is installed when the guest's sound loader hook runs (once at boot, after the guest's own load) and only if `ssf2t_enable_ultra_fighters` is on at that moment.
- **Installation.** `InstallSounds` fills 11 (Evil Ryu) and 15 (Violent Ken) guest sound slots with ids the guest leaves unused. Each takes its carrier sound's flag word, so the guest creates and cuts voices the same way. Every slot gets its own allocation because the guest frees what each slot points to. If any target id is not unused, that character's install is abandoned.
- **Playback.** The sound-request hook swaps the id of queued carrier voices (Shoryuken, Hadoken, Tatsumaki, KO, announcer name; Ken's throw) when the side is an Ultra fighter. Host moves mark the next Hadoken voice (`MarkRagingDemonVoice`, `MarkViolentMoveVoice`) so it plays the move's call. Marks are refused during replayed rollback frames, so a replay cannot queue a duplicate sound. The end-of-round KO and the Continue call use the P1 id for either side, so the loser side is recorded from the queue slot. The winner announcer's delay is extended to the length of the name clip (capped at 127 frames).
- **Fallback.** If a package is missing or invalid, or a target sound id is not free, that character's voices are not replaced and the carrier's voices play. Evil Ryu and Violent Ken are installed independently.

## Generated runtime packages

The program reads every package from `assets/characters` beside its executable (`usr/bin/assets/characters` inside the AppImage), and never from the game folder. `src/character/character_packages.h` is the one list: name, size, SHA-256, owning fighter and whether the package is required. CMake, the packaging scripts and the release checks parse that table, so the executable and its packages cannot drift apart unnoticed.

Sixteen packages:

| Package | Runtime purpose | Consumer | Need | When it is not valid |
|---|---|---|---|---|
| `ultra_portraits.bin` | Roster cell images, both fighters | `roster_cells_hook.cpp` | Required, both | Both fighters disabled |
| `evil_ryu_sprites.bin` | Large and beaten portraits, names, poses | `ultra_hd_palette_hook.cpp` | Required | Evil Ryu disabled |
| `evil_ryu_palette.bin` | Palette RAM words | `ultra_palette_hook.cpp` | Required | Evil Ryu disabled |
| `evil_ryu_hd_palette.bin` | HD texture palettes | `ultra_hd_palette_hook.cpp` | Required | Evil Ryu disabled |
| `evil_ryu_assets.bin` | Name plate fallback in Character Select | `roster_cells_hook.cpp` | Optional | The guest-drawn name tile art is used alone |
| `evil_ryu_hud_icon.bin` | HUD portrait icon | `ultra_hud_hook.cpp` | Optional | Carrier HUD icon, name still replaced |
| `evil_ryu_audio.bin` | Voices | `ultra_audio_hook.cpp` | Optional | Carrier voices |
| `evil_ryu_ending.bin` | Ending cuts | `ultra_ending_hook.cpp` | Optional | Carrier's ending |
| `violent_ken_gameplay.bin` | Actions, patches, attack records | `ultra_moves_hook.cpp` | Required | Violent Ken disabled |
| `violent_ken_sprites.bin` | Same as Evil Ryu | `ultra_hd_palette_hook.cpp` | Required | Violent Ken disabled |
| `violent_ken_palette.bin` | Same as Evil Ryu | `ultra_palette_hook.cpp` | Required | Violent Ken disabled |
| `violent_ken_hd_palette.bin` | Same as Evil Ryu | `ultra_hd_palette_hook.cpp` | Required | Violent Ken disabled |
| `violent_ken_assets.bin` | Same as Evil Ryu | `roster_cells_hook.cpp` | Optional | Same as Evil Ryu |
| `violent_ken_hud_icon.bin` | Same as Evil Ryu | `ultra_hud_hook.cpp` | Optional | Same as Evil Ryu |
| `violent_ken_audio.bin` | Voices | `ultra_audio_hook.cpp` | Optional | Carrier voices |
| `violent_ken_ending.bin` | Ending cuts | `ultra_ending_hook.cpp` | Optional | Carrier's ending |

"Optional" describes only how the running program degrades; a release must hold all sixteen. Producing tools, all in `tools/`: `extract-ultra-portraits.py`; `extract-evil-ryu-assets.py`, `-sprites.py`, `-palette.py`, `-hd-palette.py`, `-hud-icon.py`, `-audio.py`, `-ending.py`; `extract-violent-ken-gameplay.py`; `extract-violent-ken-presentation.py`; `extract-ultra-character-audio.py`. `flog_x360.fpg` is the game's own asset archive, not a package; the roster cells read their frame from it in the game folder.

## Runtime package requirements

At startup, before the launcher, the program checks each package's size and SHA-256 against the table (`ValidateCharacterPackages`) and fixes the result for that run:

- **Required package missing or different:** that fighter is disabled deliberately. No cell, no navigation, not advertised online, and the Online page shows it as disabled locally. With the option on, the launcher's Gameplay page says which fighter is unavailable and asks for the release to be reinstalled. This covers the selectable-but-invisible cell and missing gameplay tables: neither can reach the player. Evil Ryu's moves are compiled in, but without his art he is still not offered.
- **Optional package missing:** the fighter stays available with the intentional fallback in the table above, and the problem is logged.
- **Everything valid:** both fighters are offered when the option is on.

An optional package that is present but different from the table is reported the same way as a missing one and does not disable the fighter; each consumer's own parser still refuses a file it cannot read.

## Package lifecycle

- **Inputs.** A checkout keeps the packages in `assets/characters`, which git ignores: they are local build inputs and are never committed. The package tools write there by default. After regenerating a package on purpose, `tools/update-character-packages.ps1` rewrites the sizes and hashes in the table.
- **Local builds.** Every build runs `cmake/stage_character_packages.cmake`, which makes `<build>/assets/characters` hold exactly the valid packages the table names. A stale, unknown or leftover file does not stay, and a missing or different input is reported by the build (`character packages: n of 16 staged`). A checkout without the packages still builds, and the program then disables the fighters.
- **Windows release.** `tools/package-release.ps1` checks the inputs before it builds, then copies the listed packages to `assets\characters\` in the staging folder. `tools/check-release-folder.ps1` refuses a missing, stale or unknown package there, on the staging folder and on the extracted ZIP.
- **Linux AppImage.** `tools/package-linux.sh` makes the same input check, and `stage_appdir` copies the listed packages to `usr/bin/assets/characters`. The expected-contents list comes from the table, and each file is verified in the AppDir and in the extracted AppImage.
- **Reinstall Game.** It replaces the game folder and keeps only a supported `default.xexp`. The packages are outside that folder, so it does not touch them. The same holds for installing or removing the Title Update.
- **Launcher updates.** The packages are ordinary release files: a Windows update installs them with the rest of the release, and an AppImage update replaces the whole image.

## Online behavior

Details are in [online.md](online.md#secret-characters); the character-specific parts:

- **Capability.** Each client sends `supports` (what the build understands: all three secret characters) and `enabled`: the local option **and** that fighter's required packages valid on this machine. The two Ultra fighters are negotiated independently of each other and of Akuma, so a peer with a damaged install simply does not get that fighter.
- **Effective capability.** `EffectiveSecrets` is `supports` and `enabled` on both sides; the service reports it, and the client takes the intersection with its own result, so it can only shrink.
- **Locking.** The result is locked when the online Character Select starts (`LockOnlineSecrets`) and stays until the fight is left. During an online select, `ActiveOnlineSecrets` returns the locked set. It is added to the frame-0 setup words (bit 0 Akuma, bit 1 Evil Ryu, bit 2 Violent Ken) only when any bit is set, so clients that disagree refuse the fight and clients with nothing on send the words they always did.
- **Selection.** The selection is never sent: both consoles replay the same inputs through the game's Character Select. When the fighters are known, each console validates them (`ValidateSecretSelection`); an Evil Ryu or Violent Ken the match did not negotiate logs `online fighter reject` and leaves the match on both consoles.
- **Peers without the characters.** A client that does not support a character, or an older service, never gets it; matches between such clients run exactly as before. Disabled characters show on the Online page as `Disabled locally`, `Disabled by opponent`, `Unsupported by opponent` and so on.
- **Identity reporting.** Fight telemetry reports the fighters as 17 and 18 only when the match negotiated them.

## Rollback and state

Rolled-back and replayed frames must reproduce host-side state exactly, so everything the Ultra fighters keep outside the guest is in the rollback blob. `host_rollback.h` defines it (`kHostRollbackBytes`) and `host_rollback.cpp` installs it as the host-state codec; the blob is appended to every saved simulation state and a blob of the wrong size is rejected on load.

Layout, in order:

1. One byte: the `UltraNavigation` cell mask (bit 0 P1 Evil Ryu, bit 1 P2 Evil Ryu, bit 2 P1 Violent Ken, bit 3 P2 Violent Ken). Restoring it also restores the requested identity of each side.
2. Per side: the Akuma column memory (`int8`) and layout (`uint8`).
3. Both sides' `HostMove` records, saved as raw bytes (the type is statically asserted trivially copyable): move kind and phase, tail, variant, tick counter, dash state, the teleport / Shun Goku / Rasetsu / Shinbu recognisers, rise velocity, ground height.

Not in the blob, by design: action-table, box-pack and walk-row copies (rebuilt deterministically from the guest table), the results book (`HostResultsBook`, never saved), and audio marks (refused during replayed frames). The identity itself is derived from the fighter on each use, so there is no cached copy to go stale.

Host state is reset when a new interactive Character Select begins, when the online secrets are locked (`ResetOnlineSelectState`), and when a host move ends (which keeps only the learned ground height).

## Debug and diagnostics

Permanent messages useful for these characters:

- `Character packages: <n> of 16 valid, Evil Ryu ready|disabled, Violent Ken ready|disabled`: the result of the startup check.
- `Character package <file>: missing|different from this build`: each package that is not valid.
- `Roster cell <n> failed at '<step>'` and `Roster cell <n> failed at 'create texture'`: a roster cell or name plate image could not be used (`open file`, `decode`, `find entry`, ...). `<n>` is the `RosterArt` value (1 Evil Ryu cell, 2 Violent Ken cell, 3 and 4 the name plates).
- `online secret-cap local|peer|effective ...` and `online secret-cap locked ...`: the negotiated set.
- `online fighter side=<n> base=<name> variant=<EvilRyu|ViolentKen|Akuma|none>` and `online fighter reject ...`: the selection check at the start of an online fight.

The per-hook messages (select, palette, audio, ending, HUD, sprite) carry the temporary `DEBUG_SAULO` prefix and are not a supported feature; they are expected to be removed.

## Known limitations

- Teleport and rush ghost, extra poses and quote selection are approximations (above).
- Each fighter has only the art its package provides: Evil Ryu two poses, Violent Ken one stance.
- Patches whose expected guest byte does not match are skipped silently and leave the guest's action as it is.
- No classic-style variant: in Classic the Ultra fighter plays the port overrides, not a classic rewrite.
- The 236236+P super is refused for both fighters; Evil Ryu has no CPU handling for host moves.
- Host moves cannot start in the air or in the guest states the start gate refuses.
- Voices need the option on at boot and every target sound id free; any conflict drops that character's voices.
- Results and quote data are per session and per host; they are not rolled back or shared online.

## Source map

| File | Responsibility |
|---|---|
| `src/character/character_packages.*` | The package table, startup validation, per-fighter readiness |
| `src/character/ultra_fighters.*` | Setting, per-mode availability |
| `src/character/fighter_identity.*` | `FighterIdentity`, requested and resolved identity per side |
| `src/character/secret_select.h` | Local vs. negotiated availability, selection validation, reported identity |
| `src/character/ultra_navigation.*` | Cell navigation, mask, classic guard, select session |
| `src/character/akuma_navigation.*` | Select-screen move dispatch (Ultra cells first, then Akuma) |
| `src/character/ultra_cursor_hook.cpp`, `ultra_confirm_hook.cpp` | Cursor placement, confirm/classic observation |
| `src/character/roster_cells*.{h,cpp}`, `ultra_portraits.*` | Roster cells and name plates, `ultra_portraits.bin` |
| `src/character/ultra_select_presentation.h`, `portrait_loan.h` | Presentation rules, portrait ids and loans |
| `src/character/ultra_hd_palette_hook.cpp` | Sprite, name and portrait draws, HD palette, beaten portrait |
| `src/character/ultra_palette_hook.cpp`, `evil_ryu_palette.*` | Palette RAM words, palette package parsing |
| `src/character/evil_ryu_sprites.*`, `evil_ryu_assets.*` | Art packages, HUD icon, indexed-texture helpers |
| `src/character/ultra_hud_hook.cpp` | HUD name and portrait icon |
| `src/character/ultra_fight.h`, `ultra_gameplay*.{h,cpp}` | Per-fight identity test, super refusal |
| `src/character/ultra_moves_hook.cpp` | Special trackers, host moves, action lookup, jump, CPU hooks |
| `src/character/evil_ryu_moves.*`, `evil_ryu_actions.*` | Evil Ryu recognisers, dashes, action tables, box pack, walk row |
| `src/character/violent_ken_moves.*`, `violent_ken_actions.*`, `violent_ken_data.*` | Violent Ken recognisers, tables, gameplay and presentation package loaders |
| `src/character/ultra_audio_hook.cpp`, `evil_ryu_audio.*`, `violent_ken_audio.*` | Voice packages and sound replacement |
| `src/character/character_moves*.{h,cpp}` | Character Moves page, winner banner text, quote and ending text |
| `src/character/ultra_winner_hook.cpp`, `ultra_quote.h` | Winner banner and quote swap |
| `src/character/evil_ryu_results.*`, `ultra_results_hook.cpp` | Results rows |
| `src/character/ultra_ending*.{h,cpp}`, `evil_ryu_ending_text.h` | Ending screen patch and playback |
| `src/character/host_rollback.*` | Rollback blob for host-side state |
| `src/character/ultra_hook_table.h` | Guest function addresses per image |
| `src/online/secret_characters.*`, `secret_view.*`, `src/online_runtime.cpp`, `src/netplay_hook.cpp` | Negotiation, Online page rows, locked set, selection check, reporting |
| `src/launcher.cpp`, `src/settings.cpp` | Setting toggle and persistence |
| `src/feature_parity.h` | Feature registry (also lists the dormant sources below) |
| `cmake/stage_character_packages.cmake`, `tools/character-packages.ps1`, `tools/update-character-packages.ps1`, `tools/linux-policy.sh` | Staging and checking the packages |
| `tests/` | `character_packages_tests`, `ultra_*_tests`, `evil_ryu_*_tests`, `violent_ken_*_tests`, `roster_cells_tests`, `secret_select_tests`, `fighter_identity_tests`, `character_moves_tests` |

Four character sources are kept on disk but built by no target and are listed as dormant in `feature_parity.h`: `character_hook.cpp`, `character_select_debug.cpp`, `ultra_name_hook.cpp`, `ultra_presentation_hook.cpp`.
