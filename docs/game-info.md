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

Extracted to `game/extracted/` (27 files, 383,111,392 bytes).

| File | Size | Notes |
|---|---:|---|
| `default.xex` | 3,932,160 | Only executable module |
| `flog_x360.fpg` | 378,257,408 | Single packed asset archive, magic `30GF`, format not yet identified |
| `ArcadeInfo.xml` | 12,746 | XBLA metadata |
| `Achievement_Icon_1..12.png` | 5–8 KB each | Achievement icons |
| `ArcadeTop.png`, `BillboardofDay.png`, `LiveBlade.png`, `MarketplaceMain.png`, `MarketplaceStoreFr.png`, `MPlaceBanner.png`, `Title_Image.png` | 7 KB–253 KB | Dashboard art |
| `ESRB_Rating.png`, `GRB_Rating.png`, `OFLC_Rating.png`, `PEGI_Rating.png`, `USK_Rating.png` | 5–8 KB | Rating marks |

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

Decrypted and decompressed image: `game/analysis/default.image.bin` (46,202,880 bytes, SHA-256 `59BB31AFE495DF559A3523251541BF7080D6377B4B5568FE2145F8E328F7768D`). It is a valid PE with 8 sections:

| Section | VA | Virtual size |
|---|---|---|
| `.rdata` | `0x82000400` | `0x1570AC` |
| `.pdata` | `0x82157600` | `0x15B90` |
| `.text` | `0x82170000` | `0x18AC244` |
| `.data` | `0x83A20000` | `0x11B2B98` |
| `.XBMOVIE` | `0x84BD2C00` | `0xC` |
| `.idata` | `0x84BE0000` | `0x3AA` |
| `.XBLD` | `0x84BF0000` | `0x90` |
| `.reloc` | `0x84BF0200` | `0x95B864` (extends past the mapped image; ReXGlue skips it) |

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

## Codegen notes

- `rexglue codegen` 0.10.0 needs three manual function starts in `config/overrides.toml`: `0x824521C0`, `0x839D3020`, `0x839DAFE8`. They are tail-branch targets with no `.pdata` start. Without them codegen fails with four unresolved calls.
- 55 functions exceed the default `max_file_size_bytes` split target (1 MiB). The largest is about 8.5 MB. These are long straight-line table setup sequences (`lis`/`addi`/`stw`) with few `blr`. This is a warning only; the code is still generated.
- Result: `generated/default/`, 725 files written, exit code 0.
