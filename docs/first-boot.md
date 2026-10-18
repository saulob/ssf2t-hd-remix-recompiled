# First boot

## Status

| # | Milestone | Status |
|---|---|---|
| 1 | Package identified and documented | Done: [game-info.md](game-info.md) |
| 2 | ReXGlue codegen completes | Done (exit 0) |
| 3 | Native build succeeds, no warnings | Done: `build\ssf2t_hd_remix.exe` |
| 4 | Guest entry reached | Reached in the first manual test |
| 5 | Runtime init past the first unregistered call | Pending the second manual test |

## Build and codegen

    powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\codegen.ps1

`build.ps1` only builds. `build.ps1 -Run` builds, checks the runtime, then launches `build\ssf2t_hd_remix.exe` with `game\extracted` and `build\user_data`. `-NoRun` overrides `-Run`.

## Function discovery

ReXGlue's codegen only registers functions it finds through `.pdata` and direct branches. Functions reached only through `.rdata`/`.data` pointers (vtables, callbacks) are missed. `tools\find-missing-functions.py` finds them:

    python -I tools\find-missing-functions.py            # report only: build\missing-functions-report.txt
    python -I tools\find-missing-functions.py --apply-high   # rewrite config\discovered-functions.toml
    python -I tools\find-missing-functions.py --verify       # generated output matches the discovered entries and a re-scan
    python -I tools\find-missing-functions.py --self-test    # decoder, lis/addi tracking and call-closed rule
    python -I tools\find-missing-functions.py --runtime-address 0xADDR   # explain one address; edits nothing

It scans the decrypted image (`game\analysis\default.image.bin`) for code addresses referenced by `.rdata`/`.data` pointers, `lis`/`addi` pairs, vtables and jump tables. Each candidate is walked as a control-flow graph and rejected when it falls into another function, enters a label, lies inside a `.pdata` extent or is only referenced from exception metadata. Only candidates with a clean walk and no conflicts are HIGH.

- Config layering: `config\overrides.toml` holds manual starts that the scanner cannot derive. `config\discovered-functions.toml` holds scanner output, including manual starts the scanner confirms with the same end.
- The generated config is applied to codegen as `includes` in `ssf2t_hd_remix_manifest.toml`.

## Current discovery state

- Unique code targets: 7,646. Unregistered: 452.
- HIGH: 17 new, plus 2 manual starts confirmed (19 in `discovered-functions.toml`).
- LOW: 386 (most rejected because the address is inside a `.pdata` extent or enters another function). EH: 66. Jump-table references: 37 rejected.
- Manual override left: `0x824521C0` (a tail-branch target with no usable reference; the scanner cannot confirm it).
- Oversized functions (55): each is a `.pdata`-covered compiler function of 0.3 to 8.6 MB. Their interior addresses are not function starts, so they are not split. The warnings are expected.

## Assets

The package's file table lists 27 files. The complete content lives in `game:\flog_x360.fpg`, which the executable opens by name. The runtime's loose-path probes (`game:\particles\…`, `game:\textures\…`, `game:\fonts\…`) fail as expected. The archive's internal index format is not yet identified, so the asset archive reader is the next open item.

## Second manual test (you run this)

    .\build\ssf2t_hd_remix.exe --game_data_root="S:\VisualStudio\repos\ssf2t-hd-remix-recompiled\game\extracted" --user_data_root="S:\VisualStudio\repos\ssf2t-hd-remix-recompiled\build\user_data" --log_file="S:\VisualStudio\repos\ssf2t-hd-remix-recompiled\build\logs\ssf2t_second_boot.log"

1. The launcher opens. Press Play.
2. Wait about 30 seconds, then close the window.
3. Send `build\logs\ssf2t_second_boot.log`.

Expect the 0x839CDE90 call to proceed. The next failure (if any) is either a new unregistered address or an asset error.

## Open items

- FPG asset archive reader: identify the `flog_x360.fpg` index and map `game:\…` paths to it.
- Guest resolution is assumed 1280x720; confirm from `VdSetDisplayMode`.
- Not ported: cheats, skip intro, Live services. Saves, profile and achievements: [save-and-achievements.md](save-and-achievements.md).
- `third_party\trg-launcher` is a copy, not a git submodule.
