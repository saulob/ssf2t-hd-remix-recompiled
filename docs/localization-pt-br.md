# Português Brasil (pt-BR) localization

The launcher language list holds English, French, German, Italian, Spanish and **Português Brasil**. The game has no
Portuguese slot, so pt-BR runs on the English slot (`XC_LANGUAGE` 1) and the port layer replaces the text.

## Architecture

| Text | Where it lives | How pt-BR is applied |
| --- | --- | --- |
| Game string table (menus, options, move lists, how to play, endings, win quotes, help text, error boxes) | `TEXT/STR_<lang>.TXT` inside `flog_x360.fpg`, 1989 line-aligned entries per language | `src/localization/game_text_hook.cpp` replaces the English table when `ssf2t_language` is `pt-BR`; Base013 and TU023 each hook their own file reader |
| Launcher, online page, overlays, dialogs | string literals in `src/` | `ssf2t::Tr()` (`src/localization/ui_text.h`) looks the English text up in the pt-BR table; a missing entry returns the English text |
| Achievement names and descriptions | XDBF in the XEX, one set per `XC_LANGUAGE` | pt-BR rows in the UI table, applied where the launcher and overlay draw them |
| Text drawn into textures | `SG28XT03` textures in `flog_x360.fpg` | `src/localization/baked_texture.*`: the texture parse (Base `0x823FFDC8`, TU `0x82400478`) swaps in a redrawn texture when it is the stock one (name and CRC); the table is empty for now |

Source of truth for pt-BR text: `src/localization/pt_br_game_text.inc` (game table) and `src/localization/pt_br_ui_text.inc`
(port UI). Each game row is `line<TAB>id<TAB>crc32 of the English line<TAB>text`. A `=` text keeps the English line on purpose.
The CRC pins a row to its English source: a row whose CRC no longer matches is skipped (English) and logged.

Escapes in the text column: `\xNN` for the button glyph codes U+0080..U+009F, `\\` for a backslash, `\t` for a tab.
Markup (`[break]`, `[color=...]`, `$gamertag`, `%d`) is copied unchanged.

## Rules

* English is the meaning; Spanish is context only. A questionable Spanish line never overrides English.
* Brazilian Portuguese vocabulary, natural game-UI register, no literal word order.
* The game fonts hold every accented Latin-1 letter (`À Á Â Ã` included) but no em dash (U+2014): use a hyphen or a comma. `localization_tests game` checks every character of the table against the six text fonts.
* Keep a title or label about as long as the English one (at most +30% for labels, +40% for sentences).
* Error title: at most 127 bytes; error message: at most 511 bytes (guest buffers).
* Placeholders, markup and glyph codes: the same multiset as the English line.

## Glossary

| English | pt-BR |
| --- | --- |
| Player | Jogador |
| Match | Partida |
| Online Match | Partida Online |
| Local Match | Partida Local |
| Find Match | Buscar Partida |
| Create Match | Criar Partida |
| Invite / Invite Player | Convidar / Convidar Jogador |
| Game Lobby / Lobby | Lobby |
| Achievements | Conquistas |
| Settings | Configurações |
| Display | Tela |
| Graphics | Gráficos |
| Gameplay | Jogabilidade |
| Language | Idioma |
| Controller / Controls | Controle / Controles |
| Keyboard | Teclado |
| Save / Cancel / Back / Continue | Salvar / Cancelar / Voltar / Continuar |
| Round / Fight / Winner | Round / Luta / Vencedor |
| Connection / Connection Test | Conexão / Teste de Conexão |
| Friend / Friend code / Friend request | Amigo / Código de amigo / Pedido de amizade |
| Accept / Decline | Aceitar / Recusar |
| Offline / Online | Offline / Online |
| Ready | Pronto |
| Searching / Connecting | Procurando / Conectando |
| Error / Retry | Erro / Tentar novamente |
| Single Player / Multiplayer | Um Jogador / Multijogador |
| Training Mode | Modo Treino |
| Tournament / Ranked Match | Torneio / Partida Ranqueada |
| Easy / Medium / Hard / Expert | Fácil / Médio / Difícil / Especialista |
| Punch / Kick | Soco / Chute |
| Jab / Strong / Fierce | Fraco / Médio / Forte |
| Super Combo / Super | Super Combo / Super |
| Pause / Quit | Pausa / Sair |
| Help & Options | Ajuda e Opções |
| Direct / Relay | Direto / Relay |

## Terms kept as in the original

Character names. Canonical move names: Hadoken, Shoryuken, Tatsumaki Senpu-kyaku, Sonic Boom, Flash Kick,
Spinning Bird Kick, Yoga Fire, Yoga Flame, Tiger Shot, Tiger Uppercut, Hundred Hand Slap, Rolling Attack and the rest of
the `CMD_*` names. Technical terms: UDP, STUN, NAT, Relay, FPS, VSync, MSAA, Xbox LIVE, gamertag. Fighting-game terms
used as such in Brazil: Round, KO, Perfect, Super, Combo.

## Runtime literals of the guest images

`localization_tests runtime` scans both guest images (text regions only) and requires every English-looking literal to be in
`tests/runtime_literals.inc` as translated, canonical or unreachable, with a reason; an unlisted literal fails the gate. The
unreachable runs rest on reference counts of the arcade code that would show them (`kFacts` in the test): a count that
changes means new code reaches that text and the audit must be redone.

* **Translated:** the Training prompt `Press START to activate` (hook on the UI text helper), the leaderboard
  `Error reading entries.`, the arcade win quotes drawn by Original graphics in Base013 (ASCII, original line lengths,
  patched in the writable pool after the init, since the image is read-only), the English achievement text.
* **Canonical:** titles, character names, initials and the other stock languages' blocks of the Xbox Live Arcade resource.
* **Unreachable:** the arcade debug, test and front-end menus (gated by a stage-flow byte that is never 4), the arcade ending
  and tournament-help pools (task types no spawn path creates), `THANK YOU FOR YOUR PLAYING` (task type 151 is never created),
  and identifiers, file names and diagnostics that no draw path reaches.

Arcade task type 0x20 (handler `821DC5E0` / TU `821DCC50`) and the request-record helper `823B4408` / TU `823B4438` are code
with no text path: their call closure holds no string address and no text renderer, and `kCodeFacts` pins their instructions.
The win quotes are patched in both images; the arcade bitmap that draws them is presented in Base013 with Original graphics
only, because the Title Update presents it only while the fight-active byte is set (`kCodeFacts` pins the guard).

## Not translated

Text baked into arcade textures stays English on purpose, including the in-fight `PRESS START` sprite: `CONTINUE`, `GAME OVER`,
`CHARACTER SELECT`, `STAGE SELECT`, `STAGE`, `1P`/`2P`, country captions and character names. `localization_tests game` lists
every texture of `textures/replacements.tsv` and fails on one that is neither redrawn nor exempt with a reason. The
achievement unlock toast is the port's own (`src/achievement_toast.cpp`) and is translated. Log files, crash reports and
diagnostics stay English.

## Next version backlog

- Create a proper pt-BR `PRESS START` artwork replacement.
- Translate the `CHARACTER SELECT` artwork.
- Review `CONTINUE`, `GAME OVER`, `STAGE SELECT` and `STAGE` artwork for pt-BR.
- Translate the 12 achievement names and descriptions shown by the game's achievement screen.
- Review the remaining fixed English UI textures.
- Perform a final visual clipping review.
