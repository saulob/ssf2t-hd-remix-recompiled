// The product's feature set, one entry per user-visible feature, for ssf2t_global_feature_parity_tests. Every supported
// game build and platform must expose every feature here unless kExceptions names the reason it cannot. The test checks
// each entry against production source and the real game images, and checks that production holds nothing this list
// leaves out. Nothing in the program reads it.
#pragma once

namespace ssf2t::parity {

struct GuestBuild {
  const char* id;            // symbol prefix of the recompiled functions
  const char* label;
  const char* traits;        // prefix of the per-build traits structs
  const char* codegen_dir;   // folder under config/guest and generated
  const char* analysis_image;  // the build's decrypted image
};

inline constexpr GuestBuild kGuestBuilds[] = {
    {"base013", "Base013", "Base013", "base", "game/analysis/default-0.0.1.3.image.bin"},
    {"tu023", "TU023", "TitleUpdate023", "tu", "game/analysis/default.image.bin"},
};

inline constexpr const char* kPlatforms[] = {"windows", "linux"};

// Comma-separated lists; "" for none.
//   guest        Struct::kMember, the per-build address tables (Struct is the traits name without its build prefix)
//   hooked       the members of `guest` the program hooks; each build must hook every one of them
//   settings     keys the launcher saves; a k...Setting constant stands for its value
//   internal     settings with no launcher row
//   windows_settings / linux_settings   the backend's own setting for the same choice
//   hotkeys      Fn=file that listens for it, or Fn=sdk when the runtime binds it
//   sources      files every platform builds; windows_sources / linux_sources only that platform builds
//   windows_functions / linux_functions   file:Function that platform must implement for real (not a stub)
//   tests        test sources that prove it; each must be built and run by tools/test.ps1 and tools/test.sh
//   rows         the launcher rows that show and change its settings
struct Feature {
  const char* name;
  const char* guest;
  const char* hooked;
  const char* settings;
  const char* internal;
  const char* windows_settings;
  const char* linux_settings;
  const char* hotkeys;
  const char* sources;
  const char* windows_sources;
  const char* linux_sources;
  const char* windows_functions;
  const char* linux_functions;
  const char* tests;
  const char* rows = "";  // launcher row labels, as string literals or label constants
};

inline constexpr Feature kFeatures[] = {
    {"Akuma",
     "Move::kHdStyle,Move::kLive,Move::kMode,Move::kPad,Move::kSpecial,UltraHooks::kUnlockAkuma1,"
     "UltraHooks::kDrawRoster1,"
     "UltraHooks::kNavigateSelect1",
     "UltraHooks::kUnlockAkuma1,UltraHooks::kNavigateSelect1,UltraHooks::kDrawRoster1",
     "kEnableAkumaSetting", "", "", "", "",
     "src/character/akuma.cpp,src/character/akuma_navigation.cpp,src/character/select_activity.cpp,"
     "src/character/akuma_portrait.cpp,src/character/akuma_select.cpp",
     "", "", "", "", "tests/secret_select_tests.cpp", "kEnableAkumaLabel"},
    {"Ultra fighters (Evil Ryu and Violent Ken)",
     "Audio::kSoundFlags,Audio::kSoundQueue,Audio::kSoundQueueHead,Audio::kSoundQueueTail,"
     "Audio::kSoundSlots,Confirm::kHdStyle,Cursor::kHdPositions,Cursor::kLive,Cursor::kVersusPositions,"
     "Ending::kEndingCharacter,Hd::kBeatenSide,Hd::kColourP1,Hd::kColourP2,Hd::kContinuePortraits,"
     "Hd::kLoserPortraitSlot,Hd::kLosingPortraits,Hd::kMysteryLargePortrait,Hd::kPortraitPaletteRows,"
     "Hd::kRyuPaletteSlot10,Hud::kFightHudActive,Hud::kWidgets,Moves::kActionTables,"
     "Palette::kPaletteRamPointer,Quote::kTaunts,Results::kLocalRecords,Results::kMatchWinner,"
     "Roster::kLayout,Roster::kLive,Roster::kMode,Select::kMode,UltraHooks::kPlayerCharacterIndex1,"
     "UltraHooks::kAnnounceWinnerName1,UltraHooks::kLoadSounds1,UltraHooks::kRequestSound1,"
     "UltraHooks::kRoundEndScream1,UltraHooks::kObserveLock1,UltraHooks::kPlaceCursor1,"
     "UltraHooks::kPlayEnding1,UltraHooks::kGateSuper1,UltraHooks::kDrawPortraits1,"
     "UltraHooks::kDrawSprite1,UltraHooks::kUpdateHud1,UltraHooks::kEmitSprites1,"
     "UltraHooks::kEmitSprites2,UltraHooks::kEmitSprites3,UltraHooks::kEmitSprites4,"
     "UltraHooks::kEmitSprites5,UltraHooks::kHeavyPunch1,UltraHooks::kTrackHalfCircle1,UltraHooks::kLoadGrabRange1,"
     "UltraHooks::kLookupAction1,UltraHooks::kRunKenSpecialClass1,UltraHooks::kRunSpecialClass1,"
     "UltraHooks::kSelectCpuMove1,UltraHooks::kStartJump1,UltraHooks::kStartJump2,"
     "UltraHooks::kTrackKenKickCommand1,UltraHooks::kTrackKenKickCommand2,"
     "UltraHooks::kTrackKenKickCommand3,UltraHooks::kTrackKenSpecials1,UltraHooks::kTrackSpecials1,"
     "UltraHooks::kApplyEvilRyuPalette1,UltraHooks::kPlayerResults1,"
     "UltraHooks::kPlayerResultsCharacter1,UltraHooks::kRecordMatch1,UltraHooks::kResetPlayerResults1,"
     "UltraHooks::kShowQuote1,UltraHooks::kShowWinText1",
     "UltraHooks::kPlayerCharacterIndex1,UltraHooks::kAnnounceWinnerName1,UltraHooks::kLoadSounds1,"
     "UltraHooks::kRequestSound1,UltraHooks::kRoundEndScream1,UltraHooks::kObserveLock1,"
     "UltraHooks::kPlaceCursor1,UltraHooks::kPlayEnding1,UltraHooks::kGateSuper1,"
     "UltraHooks::kDrawPortraits1,UltraHooks::kDrawSprite1,UltraHooks::kUpdateHud1,"
     "UltraHooks::kEmitSprites1,UltraHooks::kEmitSprites2,UltraHooks::kEmitSprites3,"
     "UltraHooks::kEmitSprites4,UltraHooks::kEmitSprites5,UltraHooks::kHeavyPunch1,UltraHooks::kTrackHalfCircle1,"
     "UltraHooks::kLoadGrabRange1,UltraHooks::kLookupAction1,UltraHooks::kRunKenSpecialClass1,"
     "UltraHooks::kRunSpecialClass1,UltraHooks::kSelectCpuMove1,UltraHooks::kStartJump1,"
     "UltraHooks::kStartJump2,UltraHooks::kTrackKenKickCommand1,UltraHooks::kTrackKenKickCommand2,"
     "UltraHooks::kTrackKenKickCommand3,UltraHooks::kTrackKenSpecials1,UltraHooks::kTrackSpecials1,"
     "UltraHooks::kApplyEvilRyuPalette1,UltraHooks::kPlayerResults1,"
     "UltraHooks::kPlayerResultsCharacter1,UltraHooks::kRecordMatch1,UltraHooks::kResetPlayerResults1,"
     "UltraHooks::kShowQuote1,UltraHooks::kShowWinText1",
     "kEnableUltraFightersSetting", "", "", "", "",
     "src/character/character_moves.cpp,src/character/character_moves_hook.cpp,"
     "src/character/evil_ryu_actions.cpp,src/character/evil_ryu_assets.cpp,"
     "src/character/evil_ryu_audio.cpp,src/character/evil_ryu_moves.cpp,"
     "src/character/evil_ryu_palette.cpp,src/character/evil_ryu_results.cpp,"
     "src/character/evil_ryu_sprites.cpp,src/character/fighter_identity.cpp,"
     "src/character/host_rollback.cpp,src/character/ultra_audio_hook.cpp,"
     "src/character/ultra_confirm_hook.cpp,src/character/ultra_cursor_hook.cpp,"
     "src/character/ultra_ending.cpp,src/character/ultra_ending_hook.cpp,"
     "src/character/ultra_fighters.cpp,src/character/ultra_gameplay_hook.cpp,"
     "src/character/ultra_hd_palette_hook.cpp,src/character/ultra_hud_hook.cpp,"
     "src/character/ultra_moves_hook.cpp,src/character/ultra_navigation.cpp,"
     "src/character/ultra_palette_hook.cpp,src/character/roster_cells_hook.cpp,"
     "src/character/ultra_portraits.cpp,src/character/ultra_results_hook.cpp,"
     "src/character/ultra_winner_hook.cpp,src/character/violent_ken_actions.cpp,"
     "src/character/violent_ken_audio.cpp,src/character/violent_ken_data.cpp,"
     "src/character/violent_ken_moves.cpp",
     "", "", "", "", "tests/violent_ken_tests.cpp,tests/violent_ken_audio_tests.cpp,tests/secret_select_tests.cpp,"
     "tests/ultra_ending_tests.cpp,tests/evil_ryu_moves_tests.cpp,tests/debug_hotkeys_removed_tests.cpp",
     "kEnableUltraFightersLabel"},
    {"Game files and Title Update", "", "", "", "", "", "", "",
     "src/game_files.cpp,src/stfs.cpp,src/guest_image.cpp,src/title_update_image.h", "", "", "", "",
     "tests/game_files_tests.cpp,tests/game_files_base_only_tests.cpp,tests/guest_build_tests.cpp"},
    {"Achievements", "Achievements::kShowAchievementsWrapper", "Achievements::kShowAchievementsWrapper", "", "", "", "",
     "F7=sdk", "src/achievement_toast.cpp,src/achievement_ui_hook.cpp,src/achievements_overlay.cpp,src/launcher_achievements.cpp", "", "", "", "",
     "tests/achievement_tests.cpp,tests/achievement_metadata_tests.cpp"},
    {"Game message boxes", "MessageBox::kShowMessageBoxWrapper", "MessageBox::kShowMessageBoxWrapper", "", "", "", "",
     "", "src/message_box_hook.cpp,src/message_box_ui.cpp,src/modal_dialog.cpp", "", "", "", "",
     "tests/guest_ui_tests.cpp"},
    {"Infinite Time", "Timer::kTimerTick", "Timer::kTimerTick", "kInfiniteTimeSetting", "", "", "",
     "F10=src/infinite_time_ui.cpp", "src/infinite_time.cpp,src/infinite_time_ui.cpp", "", "", "", "",
     "tests/infinite_time_tests.cpp", "kInfiniteTimeLabel"},
    {"Menu text (Exit Game, Online Match, Find Match)", "MenuText::kStringTableSplitter",
     "MenuText::kStringTableSplitter", "", "", "", "", "", "src/menu_text_hook.cpp", "", "", "", "",
     "tests/menu_text_tests.cpp"},
    {"Fight Area Fill 16:9",
     "Fight::kFightMaskDrawer,Fight::kWidescreenDisplayFlag,Fight::kWidescreenMode,Fight::kFightActiveFlag,"
     "Fight::kFightCameraX,Fight::kSceneDraw,Fight::kProjectionSetup,Fight::kOrtho,Fight::kFightRender,"
     "Fight::kGameRam,Fight::kMainModeDispatch,Fight::kGameModeDispatch,Fight::kMatchStepDispatch",
     "Fight::kFightMaskDrawer,Fight::kSceneDraw,Fight::kProjectionSetup,Fight::kOrtho,Fight::kFightRender",
     "kFightAreaSetting", "", "", "", "", "src/fight_area_hook.cpp", "", "", "", "", "tests/fight_area_tests.cpp", "kFightAreaLabel"},
    {"Character Select, VS and side panel fill",
     "Fight::kHdBackgroundDraw,Fight::kQuadDraw,Fight::kCharacterSelectBackgroundDraw,Fight::kScrollingBackgroundDraw,"
     "Fight::kCharacterSelectQuadCall,Fight::kScrollingQuadCall,Fight::kCharacterSelectQuadReturn,"
     "Fight::kScrollingQuadReturn,Fight::kSidePanelDraw,Fight::kLeftSidePanelQuadCall,Fight::kRightSidePanelQuadCall,"
     "Fight::kLeftSidePanelQuadReturn,Fight::kRightSidePanelQuadReturn,Fight::kVertexReserve,Fight::kVertexCommit,"
     "Fight::kCharacterSelectTexture,Fight::kDevicePointer,Fight::kTextureHeader,Fight::kBindSampler0,"
     "Fight::kBindTexture,Fight::kXdkSetTexture",
     "Fight::kQuadDraw", "", "", "", "", "", "src/fight_area.h", "", "", "", "", "tests/fight_area_tests.cpp"},
    {"Aspect ratio and presentation", "", "", "ssf2t_aspect_ratio,kLetterboxSetting", "", "", "", "",
     "src/aspect_ratio.h,src/aspect_render.cpp,src/presentation_setting.h", "", "", "", "",
     "tests/window_placement_tests.cpp", "Aspect Ratio,kPresentationLabel"},
    {"Display mode, window and VSync", "", "", "fullscreen,window_width,window_height,monitor,kTearingSetting", "", "",
     "", "F11=src/window_host.cpp", "src/window_host.cpp,src/window_placement.h,src/dpi.h", "", "", "", "",
     "tests/window_host_tests.cpp,tests/dpi_tests.cpp,tests/window_placement_tests.cpp", "Display mode,Window size,Monitor,VSync"},
    {"Render resolution, anti-aliasing, multisampling and texture filtering", "", "",
     "resolution_scale,ssf2t_internal_render_scale,swap_post_effect,ssf2t_msaa_compatibility,anisotropic_override", "",
     "", "", "", "src/internal_render_scale.h,src/multisampling.h", "", "", "", "",
     "tests/launcher_settings_tests.cpp", "Render resolution,Internal render resolution,Anti-aliasing,Multisampling,Texture filtering"},
    {"Graphics card", "", "", "ssf2t_graphics_adapter", "", "d3d12_adapter", "vulkan_device", "",
     "src/graphics_adapters.h,src/graphics_adapters.cpp", "", "", "src/graphics_adapters.cpp:ListGraphicsAdapters",
     "src/graphics_adapters.cpp:ListGraphicsAdapters", "tests/graphics_adapter_tests.cpp", "Graphics card"},
    {"Language", "", "", "kLanguageSetting", "", "", "", "", "src/language_setting.h", "", "", "", "",
     "tests/launcher_settings_tests.cpp", "kLanguageLabel"},
    {"Language: Português Brasil (game text, launcher and overlays)", "GameText::kTextTableRead,GameText::kTextTableReadReturn,GameText::kTextureParse,GameText::kPromptText,GameText::kLeaderboardError,GameText::kArcadeQuoteInit,GameText::kArcadeQuotePool",
     "GameText::kTextTableRead,GameText::kTextureParse,GameText::kPromptText,GameText::kLeaderboardError,GameText::kArcadeQuoteInit", "", "", "", "", "",
     "src/localization/arcade_quotes.cpp,src/localization/arcade_quotes.h,src/localization/arcade_quotes_patch.cpp,src/localization/baked_texture.cpp,src/localization/baked_texture.h,src/localization/pt_br_baked_textures.cpp,"
     "src/localization/game_text.cpp,src/localization/game_text.h,src/localization/game_text_guest.h,"
     "src/localization/game_text_hook.cpp,src/localization/pt_br_game_text.cpp,src/localization/pt_br_ui_text.cpp,"
     "src/localization/ui_text.cpp,src/localization/ui_text.h,src/localization/active_language.h",
     "", "", "", "", "tests/localization_tests.cpp"},
    {"Title bar and frame counter", "", "", "ssf2t_show_fps,ssf2t_show_render_resolution,ssf2t_show_frame_counter", "",
     "", "", "F2=src/frame_counter.cpp", "src/frame_counter.cpp,src/window_title.h", "", "", "", "",
     "tests/window_host_tests.cpp", "Frame rate in title,Render resolution in title,Frame counter"},
    {"Launcher", "", "", "",
     "ssf2t_launcher,ssf2t_skip_launcher,ssf2t_launcher_x,ssf2t_launcher_y,ssf2t_launcher_width,ssf2t_launcher_height,"
     "ssf2t_launcher_maximized,ssf2t_launcher_fullscreen",
     "", "", "", "src/launcher.cpp,src/launcher.h,src/launcher_quit.h", "", "", "", "",
     "tests/launcher_quit_tests.cpp,tests/launcher_settings_tests.cpp"},
    {"Launcher controller navigation", "", "", "", "", "", "", "", "src/imgui_gamepad.cpp,src/imgui_gamepad.h", "", "",
     "src/imgui_gamepad.cpp:ImGuiGamepad::Read", "src/imgui_gamepad.cpp:ImGuiGamepad::Read",
     "tests/launcher_gamepad_tests.cpp"},
    {"Keyboard controls", "", "", "mnk_mode", "", "", "", "", "src/keybinds.h,src/cursor_input.h,src/input_summary.cpp", "", "", "", "",
     "tests/keyboard_tests.cpp", "Keyboard,Keyboard controls"},
    {"Local multiplayer", "", "", "ssf2t_player_1_name,ssf2t_player_2_name", "", "", "", "",
     "src/local_player_name.h,src/player_name_setting.h,src/match_input.h,src/input_owner.h,src/pad_slot_scan.h", "", "",
     "", "", "tests/player_name_tests.cpp,tests/keyboard_tests.cpp", "kPlayerNamesTitle"},
    {"Save data and user data", "", "", "", "", "", "", "", "src/user_data.cpp,src/log_path.cpp,src/port_folder.cpp", "", "", "", "",
     "tests/user_data_tests.cpp,tests/log_path_tests.cpp,tests/linux_appimage_tests.cpp"},
    {"Logging and crash reports", "", "", "detailed_logging", "", "", "", "",
     "src/log_filter.cpp,src/session_log.cpp,src/active_log.cpp,src/diagnostics.cpp,src/crash_report.cpp", "", "",
     "src/crash_report.cpp:HandleUnhandledException", "src/crash_report.cpp:InstallPosixHandlers",
     "tests/crash_report_tests.cpp,tests/log_filter_tests.cpp,tests/diagnostics_tests.cpp,tests/active_log_tests.cpp", "Detailed logging"},
    {"Relaunch", "", "", "", "", "", "", "", "src/relaunch_command.h", "", "", "", "", "tests/relaunch_tests.cpp"},
    {"Self-updater", "", "", "", "", "", "", "",
     "src/update_config.cpp,src/updater/update_controller.cpp,src/updater/update_platform.cpp,"
     "src/updater/update_job.cpp,src/updater/update_paths.cpp,src/updater/update_zip.cpp,src/updater/update_ui.cpp,"
     "src/updater/github_release.cpp,src/updater/json.cpp,src/updater/sha256.cpp,src/updater/version.cpp,"
     "src/updater/third_party/miniz/miniz.c",
     "src/updater/helper_main.cpp,src/updater/install_transaction.cpp", "", "src/updater/update_platform.cpp:LaunchUpdateHelper",
     "src/updater/update_platform.cpp:ApplyAppImageUpdate", "tests/update_tests.cpp,tests/linux_appimage_tests.cpp"},
    {"Online service, identity and friends", "", "", "kOnlineEnabledSetting,kOnlineNameSetting", "", "", "", "",
     "src/online/online_service.cpp,src/online/identity.cpp,src/online/lobby_link.cpp,src/online/friend_view.cpp,"
     "src/online/instance.cpp,src/online/protocol.cpp,src/online_page.cpp,src/online_runtime.cpp,"
     "src/friend_code_field.cpp,src/online/websocket.cpp,src/online/websocket_frame.cpp,"
     "src/online/secret_characters.cpp,src/online/secret_view.cpp",
     "src/online/websocket_win.cpp,src/clipboard_win32.cpp", "src/online/websocket_posix.cpp,src/clipboard_sdl.cpp",
     "src/online/websocket_win.cpp:connect,src/online/websocket_win.cpp:send_text,src/online/websocket_win.cpp:receive,"
     "src/online/websocket_win.cpp:http_get,src/clipboard_win32.cpp:ReadClipboardText,"
     "src/clipboard_win32.cpp:WriteClipboardText",
     "src/online/websocket_posix.cpp:connect,src/online/websocket_posix.cpp:send_text,"
     "src/online/websocket_posix.cpp:receive,src/online/websocket_posix.cpp:http_get,"
     "src/clipboard_sdl.cpp:ReadClipboardText,src/clipboard_sdl.cpp:WriteClipboardText",
     "tests/online_service_tests.cpp,tests/online_protocol_tests.cpp,tests/online_identity_tests.cpp,"
     "tests/online_setting_tests.cpp,tests/friend_field_tests.cpp,tests/wire_parity_tests.cpp,"
     "tests/online_websocket_tls_tests.cpp,tests/clipboard_sdl_tests.cpp", "kOnlineNameLabel,kOnlineEnabledLabel,Your friend code,Add a friend"},
    {"Ping display", "", "", "kOnlinePingSetting", "", "", "", "F3=src/ping_display.cpp", "src/ping_display.cpp", "", "",
     "", "", "tests/online_setting_tests.cpp", "kOnlinePingLabel"},
    {"Connection Test", "", "", "", "kConnectionTestSetting", "", "", "",
     "src/online/connection_test.cpp,src/online/stun.cpp,src/online/upnp.cpp,src/online/local_addresses.cpp,"
     "src/online/net_addr.cpp,src/connection_test_gate.h",
     "", "", "", "", "tests/online_connection_tests.cpp"},
    {"Direct UDP, STUN and hole punching", "", "", "", "", "", "", "",
     "src/online/rendezvous.cpp,src/online/establishment.h,src/netplay/udp_transport.cpp", "", "", "", "",
     "tests/online_rendezvous_tests.cpp,tests/netplay_online_game_socket_tests.cpp"},
    {"Relay fallback", "", "", "", "", "", "", "",
     "src/netplay/relay_frame.cpp,src/netplay/online_game_socket.cpp,src/netplay/lobby_mux.cpp", "", "", "", "",
     "tests/netplay_online_game_socket_tests.cpp,tests/wire_parity_tests.cpp"},
    {"Online sign-in and profile",
     "PlayerMatch::kRequestOnlineSignIn,PlayerMatch::kSetInOnlineZone,PlayerMatch::kUserIndex,"
     "PlayerMatch::kSigninState,PlayerMatch::kCheckPrivilege,PlayerMatch::kSigninInfo,PlayerMatch::kGetXuid,"
     "PlayerMatch::kTitleXnAddr,PlayerMatch::kRefreshLocalUsers,PlayerMatch::kGameMode,PlayerMatch::kInputGetState",
     "PlayerMatch::kRequestOnlineSignIn,PlayerMatch::kSetInOnlineZone,PlayerMatch::kSigninState,"
     "PlayerMatch::kCheckPrivilege,PlayerMatch::kSigninInfo,PlayerMatch::kGetXuid,PlayerMatch::kTitleXnAddr,"
     "PlayerMatch::kInputGetState",
     "", "", "", "", "", "src/player_match_hook.cpp,src/player_match.cpp", "", "", "", "",
     "tests/player_match_tests.cpp"},
    {"Online error popups", "PlayerMatch::kSetError,PlayerMatch::kErrorTitle,PlayerMatch::kErrorMessage,"
                            "PlayerMatch::kErrorLevel",
     "PlayerMatch::kSetError", "", "", "", "", "", "src/player_match.h", "", "", "", "",
     "tests/player_match_tests.cpp"},
    {"Create Match",
     "PlayerMatch::kCreateMatchSetDefaults,PlayerMatch::kCreateMatchIncNumPlayers,PlayerMatch::kCreateMatchDecNumPlayers,"
     "PlayerMatch::kCreateLobby,PlayerMatch::kCreateMatchSettings",
     "PlayerMatch::kCreateMatchSetDefaults,PlayerMatch::kCreateMatchIncNumPlayers,PlayerMatch::kCreateMatchDecNumPlayers,"
     "PlayerMatch::kCreateLobby",
     "", "", "", "", "", "src/player_match_guest.h", "", "", "", "", "tests/player_match_tests.cpp"},
    {"Find Match", "PlayerMatch::kSessionSearchEx,PlayerMatch::kHelpTextTable", "PlayerMatch::kSessionSearchEx", "", "",
     "", "", "", "src/menu_text.h", "", "", "", "", "tests/menu_text_tests.cpp,tests/online_menu_tests.cpp"},
    {"Invite",
     "PlayerMatch::kShowGameInviteUi,PlayerMatch::kInviteAcceptedInfo,PlayerMatch::kJoinLobbyFromInvite,"
     "PlayerMatch::kDeclineAcceptedInvite,PlayerMatch::kInviteConfirmSignInReturn",
     "PlayerMatch::kShowGameInviteUi,PlayerMatch::kInviteAcceptedInfo,PlayerMatch::kJoinLobbyFromInvite,"
     "PlayerMatch::kDeclineAcceptedInvite",
     "", "", "", "", "", "src/invite_ui.cpp,src/player_match_invite.h,src/invite_lifecycle.h", "", "", "", "",
     "tests/player_match_lobby_tests.cpp"},
    {"Online menus (Find Match and Create Match screens)",
     "PlayerMatch::kXmlDocumentParse,PlayerMatch::kHelpTextLoad,PlayerMatch::kGetScreenMainText",
     "PlayerMatch::kXmlDocumentParse,PlayerMatch::kHelpTextLoad,PlayerMatch::kGetScreenMainText", "", "", "", "", "",
     "src/online_menu_hook.cpp,src/online_menu.cpp", "", "", "", "", "tests/online_menu_tests.cpp"},
    {"Player Match sessions",
     "PlayerMatch::kSessionCreate,PlayerMatch::kSessionDelete,PlayerMatch::kSessionJoinLocal,"
     "PlayerMatch::kSessionJoinRemote,PlayerMatch::kSessionLeaveLocal,PlayerMatch::kSessionLeaveRemote,"
     "PlayerMatch::kSessionArbitrationRegister,PlayerMatch::kSessionStart,PlayerMatch::kSessionEnd,"
     "PlayerMatch::kSessionWriteStats,PlayerMatch::kSessionMigrateHost",
     "PlayerMatch::kSessionCreate,PlayerMatch::kSessionDelete,PlayerMatch::kSessionJoinLocal,"
     "PlayerMatch::kSessionJoinRemote,PlayerMatch::kSessionLeaveLocal,PlayerMatch::kSessionLeaveRemote,"
     "PlayerMatch::kSessionArbitrationRegister,PlayerMatch::kSessionStart,PlayerMatch::kSessionEnd,"
     "PlayerMatch::kSessionWriteStats,PlayerMatch::kSessionMigrateHost",
     "", "", "", "", "", "src/player_match_hook.cpp", "", "", "", "", "tests/player_match_tests.cpp"},
    {"Player Match networking (XNet, QoS, VDP)",
     "PlayerMatch::kXnAddrToInAddr,PlayerMatch::kInAddrToXnAddr,PlayerMatch::kConnectStatus,PlayerMatch::kQosListen,"
     "PlayerMatch::kQosLookup,PlayerMatch::kSocket,PlayerMatch::kCloseSocket,PlayerMatch::kIoctl,PlayerMatch::kBind,"
     "PlayerMatch::kConnect,PlayerMatch::kRecvFrom,PlayerMatch::kSendTo,PlayerMatch::kLastError,PlayerMatch::kGetName",
     "PlayerMatch::kXnAddrToInAddr,PlayerMatch::kInAddrToXnAddr,PlayerMatch::kConnectStatus,PlayerMatch::kQosListen,"
     "PlayerMatch::kQosLookup,PlayerMatch::kSocket,PlayerMatch::kCloseSocket,PlayerMatch::kIoctl,PlayerMatch::kBind,"
     "PlayerMatch::kConnect,PlayerMatch::kRecvFrom,PlayerMatch::kSendTo,PlayerMatch::kLastError,PlayerMatch::kGetName",
     "", "", "", "", "", "src/player_match_net_hook.cpp,src/player_match_net.cpp", "", "", "", "",
     "tests/player_match_net_tests.cpp"},
    {"Player Match lobby",
     "PlayerMatch::kInLobby,PlayerMatch::kNumPlayersInLobby,PlayerMatch::kGetIsReady,PlayerMatch::kSetGameStarted,"
     "PlayerMatch::kNetSyncStart,PlayerMatch::kNetPrintf,PlayerMatch::kLobbyFatalError,PlayerMatch::kLobbyPacket,"
     "PlayerMatch::kPlayerTableInsert,PlayerMatch::kOpTimerExpired,PlayerMatch::kWaitForPlayerListDone,"
     "PlayerMatch::kNetPrintBuffer,PlayerMatch::kNetManager,PlayerMatch::kUiInputPointer",
     "PlayerMatch::kInLobby,PlayerMatch::kNumPlayersInLobby,PlayerMatch::kGetIsReady,PlayerMatch::kSetGameStarted,"
     "PlayerMatch::kNetSyncStart,PlayerMatch::kNetPrintf,PlayerMatch::kLobbyFatalError,PlayerMatch::kLobbyPacket,"
     "PlayerMatch::kPlayerTableInsert,PlayerMatch::kOpTimerExpired,PlayerMatch::kWaitForPlayerListDone",
     "", "", "", "", "", "src/player_match_lobby_hook.cpp,src/player_match_lobby.cpp", "", "", "", "",
     "tests/player_match_lobby_tests.cpp"},
    {"Rollback: snapshot state",
     "Netplay::kArenaPointer,Netplay::kSnapshotPointer,Netplay::kGlobalsSnapshot,Netplay::kStateFunction,"
     "Netplay::kDrawListLow,Netplay::kDrawListHigh",
     "", "", "", "", "", "", "src/netplay/rollback_core.cpp,src/netplay/guest_simulation.h", "", "", "", "",
     "tests/netplay_rollback_tests.cpp,tests/netplay_state_tests.cpp"},
    {"Rollback: input",
     "Netplay::kInputTable,Netplay::kControllerInputTable,Netplay::kInputRing,Netplay::kInputDecode,"
     "Netplay::kInputHistoryStep,Netplay::kActiveUser,Netplay::kUiInputPointer",
     "", "", "", "", "", "", "src/netplay/input_model.h,src/netplay/input_route.h", "", "", "", "",
     "tests/netplay_input_tests.cpp"},
    {"Rollback: random numbers and fighter order",
     "Netplay::kRngState,Netplay::kRandStep,Netplay::kSideOrder,Netplay::kSideSetup", "Netplay::kRandStep,Netplay::kSideSetup",
     "", "", "", "", "", "src/netplay/determinism.cpp", "", "", "", "", "tests/netplay_guest_tests.cpp"},
    {"Rollback: fight loop and pacing",
     "Netplay::kCallback,Netplay::kSimStep,Netplay::kEffectsGate,Netplay::kFightLoop,Netplay::kFrameCounter,"
     "Netplay::kPaceCounter,Netplay::kNetActive,Netplay::kNetActiveQuery,Netplay::kFrameTailNetCheck,"
     "Netplay::kFramePump",
     "Netplay::kSimStep,Netplay::kEffectsGate,Netplay::kNetActiveQuery,Netplay::kFramePump",
     "kNetplayDeterminismSetting,kNetplayRoleSetting,kNetplayPeerAddressSetting", "", "", "", "",
     "src/netplay_hook.cpp,src/netplay/session.cpp,src/netplay/packet.cpp,src/netplay/guest_netplay.h", "", "", "", "",
     "tests/netplay_session_tests.cpp,tests/netplay_packet_tests.cpp,tests/netplay_setting_tests.cpp", "kNetplayDeterminismLabel,kNetplayRoleLabel,kNetplayPeerAddressLabel"},
    {"Frame Zero",
     "Netplay::kFightWork,Netplay::kOnlineStage,Netplay::kButtonConfigFlag,Netplay::kButtonConfigA,"
     "Netplay::kButtonConfigB,Netplay::kGameMode",
     "", "", "", "", "", "", "src/netplay/frame_zero.cpp", "", "", "", "", "tests/netplay_session_tests.cpp"},
    {"Online fight telemetry",
     "Netplay::kFighterBlockPointer,Netplay::kRoundWinPlayer1,Netplay::kRoundWinPlayer2,Netplay::kFightCharacterInit",
     "", "", "", "", "", "",
     "src/online/fight_telemetry.cpp,src/online/fight_telemetry.h,src/netplay/fight_snapshot.h", "", "", "", "",
     "tests/fight_telemetry_tests.cpp"},
    {"Multiple fights, multiple matches and host/join reversal", "", "", "", "", "", "", "",
     "src/netplay/session.cpp,src/netplay_runtime.h,src/online/establishment.h", "", "", "", "",
     "tests/netplay_session_tests.cpp,tests/online_service_tests.cpp"},
    {"Release packaging", "", "", "", "", "", "", "", "tools/check-release-folder.ps1,tools/package-linux.sh",
     "tools/package-release.ps1,packaging/README.txt", "packaging/linux/AppRun", "", "",
     "tests/platform_parity_tests.cpp"},
};

// Features a platform cannot offer. Each needs a technical reason and the source that shows it.
struct Exception {
  const char* feature;
  const char* target;
  const char* reason;
  const char* source;
};

inline constexpr Exception kExceptions[] = {
    {"Connection Test", "linux",
     "The note about Windows Firewall asking to allow network access has no counterpart: Linux does not prompt per program.",
     "src/connection_test_gate.h"},
};

// Sources left on disk that no target builds. The gates skip them; none may gain a build target unreviewed.
struct DormantSource {
  const char* file;
  const char* reason;
};

inline constexpr DormantSource kDormantSources[] = {
    {"src/character/character_hook.cpp", "replaced by ultra_fight.h; delete at final review"},
    {"src/character/character_select_debug.cpp", "DEBUG_SAULO select tracer, never in CMake; delete at final review"},
    {"src/character/ultra_name_hook.cpp", "replaced by the name tile swap; delete at final review"},
    {"src/character/ultra_presentation_hook.cpp", "replaced by the portrait loans; delete at final review"},
};

// Source files with platform or build conditionals that belong to no feature's list above: what the conditional
// selects and why it changes no feature.
struct GateReview {
  const char* file;
  const char* reason;
};

inline constexpr GateReview kGateReviews[] = {
    {"src/platform.cpp", "per-platform paths, fonts, architecture and backend names; both platforms implement each"},
    {"src/settings.cpp", "applies launcher choices to the backend cvars of each platform; every choice has both"},
    {"src/ssf2t_hd_remix_app.h", "picks the guest image from the installed game files and tunes Windows scheduling"},
    {"src/system_info.cpp", "operating system, CPU and display descriptions for diagnostics, one per platform"},
    {"src/guest_functions.cpp", "resolves a crash address to a recompiled function on each platform"},
    {"src/guest_memory.h", "the physical heap offset of each host"},
    {"src/connection_test_gate.h", "the Windows Firewall note listed in kExceptions"},
    {"src/netplay/udp_transport.cpp", "socket calls of each platform behind one transport"},
    {"src/online/net_addr.cpp", "socket address conversion of each platform"},
    {"src/online/net_addr.h", "socket types of each platform"},
    {"src/online/local_addresses.cpp", "network interface listing of each platform"},
    {"src/online/stun.cpp", "socket calls of each platform"},
    {"src/online/upnp.cpp", "socket calls of each platform"},
    {"src/online/random.cpp", "operating system random bytes of each platform"},
    {"src/online/connection_test.cpp", "socket length type of each platform"},
    {"src/session_log.cpp", "log file handoff between runs on each platform"},
    {"src/updater/install_transaction.cpp", "file replacement primitives of each platform"},
    {"src/updater/update_paths.cpp", "path rules of each platform"},
    {"src/updater/update_zip.cpp", "path handling of each platform"},
    {"src/imgui_gamepad.h", "the controller backend of each platform, see Launcher controller navigation"},
    {"src/online_runtime.cpp", "platform name sent in presence, and the guest build name"},
    {"src/launcher.cpp", "the guest build label and the Shift key at start"},
    {"src/game_files.h", "the guest build enum the installed game files select"},
    {"src/netplay/packet.h", "the build id both peers send in the netplay handshake"},
    {"src/netplay/session.h", "the build id both peers send in the netplay handshake"},
    {"src/settings.h", "declares the platform's backend setting function"},
};

// Source files that serve every feature and belong to none of them.
inline constexpr GateReview kSupportFiles[] = {
    {"src/main.cpp", "program entry"},
};

// Source files whose text may say a feature is unavailable: each ties the message to the state of the player's
// machine or the online service, never to the platform or the game version.
inline constexpr GateReview kAvailabilityMessages[] = {
    {"src/game_files.cpp", "the installed game files are missing or of another version"},
    {"src/launcher.cpp", "the installed game files are missing or of another version"},
    {"src/crash_report.cpp", "the stack of a crash could not be captured"},
    {"src/netplay/determinism.cpp", "a state dump has the wrong size"},
    {"src/online/websocket_posix.cpp", "the system has no OpenSSL library, which Windows has in WinHTTP"},
    {"src/player_match.h", "the online service cannot be reached"},
    {"src/player_match_hook.cpp", "the online service cannot be reached"},
    {"src/settings.cpp", "no graphics card was found, or the game files are missing or of another version"},
    {"src/ssf2t_hd_remix_app.h", "no graphics is available, or the game files are missing or of another version"},
    {"src/updater/update_job.cpp", "the update job file is from another program version"},
    {"src/user_data.cpp", "the portable user data folder cannot be written"},
    {"src/online_page.cpp", "the online service cannot be reached"},
    {"src/character/ultra_audio_hook.cpp", "the local Ultra fighter audio package is missing"},
    {"src/character/ultra_ending_hook.cpp", "the local Ultra fighter ending package is missing"},
    {"src/character/ultra_hd_palette_hook.cpp", "the local Ultra fighter art package is missing"},
    {"src/character/ultra_hud_hook.cpp", "the local Ultra fighter HUD icon package is missing"},
    {"src/online/connection_test.cpp", "the router offers no automatic port setup"},
    {"src/online/online_service.cpp", "the online service lacks a capability"},
    {"src/online/secret_view.cpp", "the online service or the opponent lacks a capability"},
    {"src/online/protocol.cpp", "the online service refuses relay"},
    {"src/update_config.cpp", "the program was not started from the release package that can update itself"},
    {"src/updater/update_controller.cpp", "the update helper of the release package is missing"},
    {"src/updater/update_platform.cpp", "each package kind installs with its own mechanism"},
    {"src/updater/update_ui.cpp", "an update could not be installed"},
    {"src/updater/update_zip.cpp", "the update archive is unusable"},
    {"src/stfs.cpp", "the Xbox package is of a layout the installer cannot read"},
};

}  // namespace ssf2t::parity
