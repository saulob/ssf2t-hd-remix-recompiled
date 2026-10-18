# Import status

194 imports from `xboxkrnl.exe` and `xam.xex`, checked against the ReXGlue 0.10.0 SDK (`REX_EXPORT`, `REX_HOOK_RAW`, `REX_EXPORT_STUB`).

| Category | Count | Meaning |
|---|---:|---|
| SDK | 150 | Implemented by ReXGlue |
| SDK-STUB | 40 | Registered by ReXGlue as a stub or no-op |
| Project hook | 4 | Stub or unverified in the SDK; Hard Corps and Geometry Wars replace the game-side call path |
| Unresolved | 0 | Every import was found in the SDK |

## Imports needing project attention

- `XGetVideoMode`: Geometry Wars forces the non-HD video mode in `aspect_render.cpp`. Not yet ported; decide after the first log.
- `XamShowAchievementsUI`, `XamShowMarketplaceUI`, `XamUserCreateStatsEnumerator`: SDK stubs. Hard Corps hooks the game's own wrapper functions to show its overlays and serve stats. SSF2T's wrapper addresses are unknown yet, so these stay SDK stubs for the first boot.
- `XamContentInstall`, `XamShowGameInviteUI`, `XamShowGamerCardUIForXUID`, `XamShowPlayerReviewUI`, `XamShowPlayersUI`, `XamVoiceSubmitPacket`, `NetDll_XNetGetConnectStatus`, `NetDll_XNetQosLookup`: SDK stubs; these are online or UI calls and are not needed for offline startup.
- `VdInitializeEngines`, `VdSetSystemCommandBufferGpuIdentifierAddress`, `VdShutdownEngines`: SDK stubs in the graphics path. Watch the first log for their effect on Xenos init; the Geometry Wars and Hard Corps Xenos patches do not change them.
- `XAudioGetVoiceCategoryVolume`: returns a fixed 1.0.

Online and voice imports (`NetDll_*`, `XamVoice*`, `XamShow*UI`) are not required for offline boot.
