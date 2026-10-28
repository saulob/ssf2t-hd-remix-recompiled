# Super Street Fighter II Turbo HD Remix (XBLA) package analysis

No game data is stored in this repository. The original package and extracted files live under the Git-ignored `game/` directory.

## Package

| Field | Value |
|---|---|
| Path | `game/Super Street Fighter II Turbo - HD Remix (World) (XBLA)/584108A4/000D0000/8C26905E15CE4933B0EFF627A9CD30AD1D4D1F9B58` |
| Size | 385,478,656 bytes |
| Format | Xbox 360 STFS, magic `LIVE` |
| Title ID | `0x584108A4` (from the STFS header) |
| Content type | `0x000D0000` (Arcade title) |
| Content ID | `8C26905E15CE4933B0EFF627A9CD30AD1D4D1F9B58` |
| Media ID | `0x37B7D7D3` (XEX execution info) |
| Version | `0.0.1.3` (XEX execution info) |
| Extraction | `tools/extract_stfs.ps1` (title ID checked; the STFS hash tree was not separately verified) |
| Required with | the Title Update below |

Extracted to `game/extracted/` (27 files, 383,111,392 bytes).

| File | Size | Notes |
|---|---:|---|
| `default.xex` | 3,932,160 | Only executable module |
| `flog_x360.fpg` | 378,257,408 | Single packed asset archive, magic `30GF`, format not yet identified |
| `ArcadeInfo.xml` | 12,746 | XBLA metadata |
| `Achievement_Icon_1..12.png` | 5–8 KB each | Achievement icons |
| `ArcadeTop.png`, `BillboardofDay.png`, `LiveBlade.png`, `MarketplaceMain.png`, `MarketplaceStoreFr.png`, `MPlaceBanner.png`, `Title_Image.png` | 7 KB–253 KB | Dashboard art |
| `ESRB_Rating.png`, `GRB_Rating.png`, `OFLC_Rating.png`, `PEGI_Rating.png`, `USK_Rating.png` | 5–8 KB | Rating marks |

## Title Update

The port is recompiled from the game with its official Title Update applied. The update was identified from its own
metadata, not from its file or folder name.

| Field | Value |
|---|---|
| Path | `game/Title Update (v2)/37B7D7D3/TU_1C42254_000010C000000.00000000000G3` (the folder name is not a version) |
| Size | 2,236,416 bytes, SHA-256 `997BDF307C847E3DD6EEA13DA915484900E017B46FA8697CA9504F2CA4E8D3BF` |
| Format | Xbox 360 STFS, magic `LIVE`, read-only layout; header SHA-1 and the whole hash tree (530 blocks) verified |
| Display name | `SuperStreetFighter2THD Title Update #1` |
| Title ID | `0x584108A4` |
| Content type | `0x000B0000` (Title Update) |
| Media ID | `0x37B7D7D3` |
| STFS version / base version | `0.0.1.3` / `0.0.1.3` (the version the update applies to) |
| Content | one file, `default.xexp`, 2,166,784 bytes, CRC32 `38DE2310`, SHA-256 `A2E8E66B482C5D3A0D3A5D3F67A3CECB7A12CE1F6189196694FD3E765910606C` |

`default.xexp` is an XEX2 delta patch: module flags `0x50` (module patch, delta patch), normal encryption, compression
type 3 (LZX delta, window `0x8000`), delta descriptor `0.0.1.3` to `0.0.2.3`, source digest = SHA-1 of the
`default.xex` RSA signature. It patches the XEX headers and the image. It is the only Title Update for this media ID
found locally; no newer update is known from the files here.

| Field | `default.xex` 0.0.1.3 | with Title Update, 0.0.2.3 |
|---|---|---|
| Build timestamp | 2008-11-06 08:05:21 UTC | 2009-02-20 07:35:39 UTC |
| Entry point | `0x839C45E8` | `0x839C4D48` |
| Image size | `0x2C10000` | `0x2C10000` |
| Imports | `xam.xex` 152, `xboxkrnl.exe` 247 records | same libraries and counts, thunks moved |
| Static libraries | XDK 2.0.7645.0 | unchanged |
| Achievements resource (XDBF) | `0x84BE0000`, `0x2DD07` bytes | unchanged |

ReXGlue applies `default.xexp` itself whenever it loads `default.xex` from the same folder, in codegen and in the
runtime. `tools/xex_image.py` rebuilds the same image without ReXGlue and checks every page against the update's page
hash chain.

## default.xex

| Field | Value |
|---|---|
| SHA-256 | `6E1C2F9ED7D1102A311A5F5626563527EDA5F85B2FBE8DB8405B866DED3F31EE` |
| Format | XEX2, module flags `0x00000001` (title) |
| Encryption | Normal (retail AES key) |
| Compression | Normal (LZX, window `0x8000`), 63 compressed blocks |
| Image base | `0x82000000`, image size `0x2C10000` |
| Entry point | `0x839C45E8` |
| Architecture | PowerPC (Xenon) PE |

Decrypted and decompressed images, both 46,202,880 bytes (`python -I tools/xex_image.py`):

- `game/analysis/default.image.bin`, with the Title Update (0.0.2.3), SHA-256 `5304D4D651E5337B293A55E32A60D9504D5E9B9CFA20A7E80115D67260C1053E`. This is the image the port is recompiled from.
- `default.xex` alone (0.0.1.3, `--base-only`), SHA-256 `59BB31AFE495DF559A3523251541BF7080D6377B4B5568FE2145F8E328F7768D`.

Each is a valid PE with 8 sections:

| Section | VA | Virtual size 0.0.1.3 | Virtual size 0.0.2.3 |
|---|---|---|---|
| `.rdata` | `0x82000400` | `0x1570AC` | `0x15710C` |
| `.pdata` | `0x82157600` | `0x15B90` | `0x15B98` |
| `.text` | `0x82170000` | `0x18AC244` | `0x18ACA2C` |
| `.data` | `0x83A20000` | `0x11B2B98` | `0x11B30B8` |
| `.XBMOVIE` | `0x84BD2C00` / `0x84BD3200` | `0xC` | `0xC` |
| `.idata` | `0x84BE0000` | `0x3AA` | `0x3AA` |
| `.XBLD` | `0x84BF0000` | `0x90` | `0x90` |
| `.reloc` | `0x84BF0200` | `0x95B864` | `0x95B97C` (extends past the mapped image; ReXGlue skips it) |

The update is a relink, not an in-place patch: functions and data moved by different amounts all through `.text` and
`.data`, so every address the port uses was re-derived for 0.0.2.3 (see below).

### Imports

194 imported functions: 129 from `xboxkrnl.exe` and 76 from `xam.xex`, plus variable imports. Import areas:

- Kernel and threads: `Ex*`, `Ke*`, `Nt*`, `Rtl*`, `Kf*`, `Hal*`, TLS (`KeTls*`). Notable: `ExCreateThread`, `KeWaitForMultipleObjects`, `KeSetAffinityThread`.
- Memory: `MmAllocatePhysicalMemoryEx`, `NtAllocateVirtualMemory`, `NtQueryVirtualMemory`.
- Files: `NtCreateFile`, `NtOpenFile`, `NtReadFile`, `NtReadFileScatter`, `NtWriteFile`, `NtQueryDirectoryFile`, `NtSetInformationFile`, `NtFlushBuffersFile`, `ObCreateSymbolicLink`.
- Graphics (Xenos): `Vd*` (`VdInitializeEngines`, `VdInitializeRingBuffer`, `VdSetDisplayMode`, `VdSwap`, `VdGetCurrentDisplayInformation`, `VdSetGraphicsInterruptCallback`).
- Audio: `XAudioSubmitRenderDriverFrame`, `XAudioRegisterRenderDriverClient`, `XAudioGetVoiceCategoryVolume`, `XMACreateContext`, `XMAReleaseContext`. Audio uses the XMA decoder and the low-level render-driver path.
- Input: `XamInputGetState`, `XamInputSetState`, `XamInputGetCapabilities`, `XamInputGetKeystrokeEx`.
- Users and content: `XamUserGetSigninState`, `XamUserGetSigninInfo`, `XamUserGetXUID`, `XamUserGetName`, `XamUserReadProfileSettings`, `XamContentCreateEx`, `XamContentGetLicenseMask`, `XamContentCreateEnumerator`, `XamContentInstall`.
- Achievements and stats: `XamUserCreateAchievementEnumerator`, `XamUserCreateStatsEnumerator`, `XamShowAchievementsUI`.
- Live UI: `XamShowMessageBoxUI(Ex)`, `XamShowSigninUI`, `XamShowMarketplaceUI`, `XamShowGamerCardUIForXUID`, `XamShowPlayersUI`, `XamShowGameInviteUI`, `XamShowPlayerReviewUI`, `XamShowDeviceSelectorUI`, `XamShowDirtyDiscErrorUI`.
- Loader: `XamLoaderLaunchTitle`, `XamLoaderTerminateTitle`, `XexLoadImage`, `XexUnloadImage`, `XexGetProcedureAddress`, `XexGetModuleHandle`, `XexCheckExecutablePrivilege`, `XamGetExecutionId`.
- Networking (online, not needed for offline boot): `NetDll_WSAStartup`, `NetDll_socket`, `NetDll_send`, `NetDll_sendto`, `NetDll_recv`, `NetDll_recvfrom`, `NetDll_XNet*`, `NetDll_XNetQos*`.
- Voice (online): `XamVoiceCreate`, `XamVoiceSubmitPacket`, `XamVoiceClose`, `XamVoiceHeadsetPresent`.
- Other: `XGetLanguage`, `XGetGameRegion`, `XGetVideoMode`, `XGetAVPack`, `XNotifyGetNext`, `XMsgStartIORequest`, `XMsgInProcessCall`, `DbgPrint`, `sprintf`, `_vsnprintf`, `__C_specific_handler`.

The full import list came from the codegen debug log and is not committed.

## What the Title Update changes

Compared function by function (11,122 `.pdata` functions in 0.0.1.3, 11,123 in 0.0.2.3) with branch targets and
address immediates normalised: 11,001 functions are unchanged apart from moving, 78 differ, and about 40 small
functions were only reordered or folded by the linker. The changed code is in these areas:

- Online play (most of the changes): the lobby and session state machine, the lobby and leaderboard event handler,
  stats enumeration, gamer pictures, network helpers, and the per-frame and in-match network synchronisation.
- In-game HUD: new UI elements `InGame` and `gamertag3` to `gamertag6`, `gamertag4HUD` and `gamertag6HUD`, and a
  restructured UI table in `.data`.
- Match flow and mode flags: character select and match flow for one game mode, and two checks of the session mode
  flags; the music loader chooses original or remixed music from one flag bit instead of a mask.
- Rendering: the shaders were rebuilt in the release configuration, and the scene draw checks one more flag.

No change was found in the fighter engine code, and the game data in `flog_x360.fpg` is not part of the update. The
port runs offline, so the online changes are present but unused.

## Codegen notes

- `rexglue codegen` 0.10.0 loads `default.xex` with `default.xexp` applied. The manifest keeps the update's SHA-256,
  because codegen does not track `default.xexp` as an input.
- Manual function starts in `config/overrides.toml`: `0x82452D00`, a tail-branch target with no `.pdata` start, and
  five this-adjust thunks at `0x839DE2E0`..`0x839DE300` that only vtables reach. The scanner rates the thunks below
  HIGH because they follow a padding word; 0.0.1.3 registered all of them.
- 55 functions exceed the default `max_file_size_bytes` split target (1 MiB). The largest is about 8.5 MB. These are long straight-line table setup sequences (`lis`/`addi`/`stw`) with few `blr`. This is a warning only; the code is still generated.
- Result: `generated/default/`, 725 files, exit code 0.
