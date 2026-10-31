#pragma once

#include <cstdint>

namespace ssf2t {

// Where Player Match lives in each recompiled image. Natives are the script functions the menus' FSM XML calls by
// name; the X* entries are the title's own wrappers around the Xam/XNet imports and XGI session messages.
struct Base013PlayerMatch {
  static constexpr const char* kName = "base013";
  static constexpr uint32_t kRequestOnlineSignIn = 0x823C6DF8;
  static constexpr uint32_t kSetInOnlineZone = 0x823C6AD8;
  static constexpr uint32_t kCreateMatchSetDefaults = 0x823C8958;
  static constexpr uint32_t kCreateMatchIncNumPlayers = 0x823CCA10;
  static constexpr uint32_t kCreateMatchDecNumPlayers = 0x823CCB00;
  static constexpr uint32_t kCreateLobby = 0x823BD588;
  static constexpr uint32_t kSetError = 0x823EAEE8;
  static constexpr uint32_t kXmlDocumentParse = 0x82416E80;
  static constexpr uint32_t kHelpTextLoad = 0x823D4150;
  static constexpr uint32_t kGetScreenMainText = 0x823D44F8;

  static constexpr uint32_t kUserIndex = 0x83A38770;
  static constexpr uint32_t kUiInputPointer = 0x8480BD30;
  static constexpr uint32_t kCreateMatchSettings = 0x83A3862C;
  static constexpr uint32_t kGameMode = 0x83CA294F;
  static constexpr uint32_t kErrorTitle = 0x83E37ED0;
  static constexpr uint32_t kErrorMessage = 0x83E37CD0;
  static constexpr uint32_t kErrorLevel = 0x83E37F74;
  static constexpr uint32_t kHelpTextTable = 0x83C28140;

  static constexpr uint32_t kSigninState = 0x839C34D0;
  static constexpr uint32_t kCheckPrivilege = 0x839C34E0;
  static constexpr uint32_t kSigninInfo = 0x839C3D98;
  static constexpr uint32_t kGetXuid = 0x839C38A0;
  static constexpr uint32_t kTitleXnAddr = 0x839C1E18;

  static constexpr uint32_t kSessionCreate = 0x839C23E0;
  static constexpr uint32_t kSessionDelete = 0x839C2590;
  static constexpr uint32_t kSessionJoinLocal = 0x839C2640;
  static constexpr uint32_t kSessionJoinRemote = 0x839C26E8;
  static constexpr uint32_t kSessionLeaveLocal = 0x839C2790;
  static constexpr uint32_t kSessionLeaveRemote = 0x839C2838;
  static constexpr uint32_t kSessionArbitrationRegister = 0x839C28E0;
  static constexpr uint32_t kSessionStart = 0x839C29B8;
  static constexpr uint32_t kSessionEnd = 0x839C2A50;
  static constexpr uint32_t kSessionSearchEx = 0x839C2B00;
  static constexpr uint32_t kSessionWriteStats = 0x839C2BE8;
  static constexpr uint32_t kSessionMigrateHost = 0x839C2F68;

  static constexpr uint32_t kXnAddrToInAddr = 0x839C1D48;
  static constexpr uint32_t kInAddrToXnAddr = 0x839C1D60;
  static constexpr uint32_t kConnectStatus = 0x839C1D78;
  static constexpr uint32_t kQosListen = 0x839C1D88;
  static constexpr uint32_t kQosLookup = 0x839C1DA8;
  static constexpr uint32_t kSocket = 0x839C1EE8;
  static constexpr uint32_t kCloseSocket = 0x839C1F00;
  static constexpr uint32_t kIoctl = 0x839C1F10;
  static constexpr uint32_t kBind = 0x839C1F28;
  static constexpr uint32_t kConnect = 0x839C1F40;
  static constexpr uint32_t kRecvFrom = 0x839C1F80;
  static constexpr uint32_t kSendTo = 0x839C1FB8;
  static constexpr uint32_t kLastError = 0x839C1FE0;
  static constexpr uint32_t kGetName = 0x839C34C8;

  static constexpr uint32_t kInLobby = 0x823C2200;
  static constexpr uint32_t kNumPlayersInLobby = 0x823BDA08;
  static constexpr uint32_t kGetIsReady = 0x823C7BB0;
  static constexpr uint32_t kSetGameStarted = 0x823C7C28;
  static constexpr uint32_t kNetSyncStart = 0x8241DEC8;
  static constexpr uint32_t kNetPrintf = 0x8242AD00;
  static constexpr uint32_t kRefreshLocalUsers = 0x8242B7E8;
  static constexpr uint32_t kShowGameInviteUi = 0x839C3D70;
  static constexpr uint32_t kInviteAcceptedInfo = 0x839C2E38;
  static constexpr uint32_t kInputGetState = 0x839C5180;
  static constexpr uint32_t kJoinLobbyFromInvite = 0x823C7AB0;
  static constexpr uint32_t kDeclineAcceptedInvite = 0x824206A8;
  static constexpr uint32_t kInviteConfirmSignInReturn = 0x823EC49C;
  static constexpr uint32_t kLobbyFatalError = 0x82426B28;
  static constexpr uint32_t kLobbyPacket = 0x82428C20;
  static constexpr uint32_t kPlayerTableInsert = 0x824246D0;
  static constexpr uint32_t kOpTimerExpired = 0x8241E8E8;
  static constexpr uint32_t kWaitForPlayerListDone = 0x824282C8;
  static constexpr uint32_t kNetPrintBuffer = 0x847F88B8;
  static constexpr uint32_t kNetManager = 0x847D4600;
};

struct TitleUpdate023PlayerMatch {
  static constexpr const char* kName = "tu023";
  static constexpr uint32_t kRequestOnlineSignIn = 0x823C73F0;
  static constexpr uint32_t kSetInOnlineZone = 0x823C7100;
  static constexpr uint32_t kCreateMatchSetDefaults = 0x823C8F50;
  static constexpr uint32_t kCreateMatchIncNumPlayers = 0x823CCFF0;
  static constexpr uint32_t kCreateMatchDecNumPlayers = 0x823CD0E0;
  static constexpr uint32_t kCreateLobby = 0x823BD540;
  static constexpr uint32_t kSetError = 0x823EB5D8;
  static constexpr uint32_t kXmlDocumentParse = 0x82417548;
  static constexpr uint32_t kHelpTextLoad = 0x823D4820;
  static constexpr uint32_t kGetScreenMainText = 0x823D4BC8;

  static constexpr uint32_t kUserIndex = 0x83A387E8;
  static constexpr uint32_t kUiInputPointer = 0x8480C250;
  static constexpr uint32_t kCreateMatchSettings = 0x83A386A4;
  static constexpr uint32_t kGameMode = 0x83CA2E67;
  static constexpr uint32_t kErrorTitle = 0x83E38468;
  static constexpr uint32_t kErrorMessage = 0x83E38268;
  static constexpr uint32_t kErrorLevel = 0x83E3848C;
  static constexpr uint32_t kHelpTextTable = 0x83C284B8;

  static constexpr uint32_t kSigninState = 0x839C3C30;
  static constexpr uint32_t kCheckPrivilege = 0x839C3C40;
  static constexpr uint32_t kSigninInfo = 0x839C44F8;
  static constexpr uint32_t kGetXuid = 0x839C4000;
  static constexpr uint32_t kTitleXnAddr = 0x839C2578;

  static constexpr uint32_t kSessionCreate = 0x839C2B40;
  static constexpr uint32_t kSessionDelete = 0x839C2CF0;
  static constexpr uint32_t kSessionJoinLocal = 0x839C2DA0;
  static constexpr uint32_t kSessionJoinRemote = 0x839C2E48;
  static constexpr uint32_t kSessionLeaveLocal = 0x839C2EF0;
  static constexpr uint32_t kSessionLeaveRemote = 0x839C2F98;
  static constexpr uint32_t kSessionArbitrationRegister = 0x839C3040;
  static constexpr uint32_t kSessionStart = 0x839C3118;
  static constexpr uint32_t kSessionEnd = 0x839C31B0;
  static constexpr uint32_t kSessionSearchEx = 0x839C3260;
  static constexpr uint32_t kSessionWriteStats = 0x839C3348;
  static constexpr uint32_t kSessionMigrateHost = 0x839C36C8;

  static constexpr uint32_t kXnAddrToInAddr = 0x839C24A8;
  static constexpr uint32_t kInAddrToXnAddr = 0x839C24C0;
  static constexpr uint32_t kConnectStatus = 0x839C24D8;
  static constexpr uint32_t kQosListen = 0x839C24E8;
  static constexpr uint32_t kQosLookup = 0x839C2508;
  static constexpr uint32_t kSocket = 0x839C2648;
  static constexpr uint32_t kCloseSocket = 0x839C2660;
  static constexpr uint32_t kIoctl = 0x839C2670;
  static constexpr uint32_t kBind = 0x839C2688;
  static constexpr uint32_t kConnect = 0x839C26A0;
  static constexpr uint32_t kRecvFrom = 0x839C26E0;
  static constexpr uint32_t kSendTo = 0x839C2718;
  static constexpr uint32_t kLastError = 0x839C2740;
  static constexpr uint32_t kGetName = 0x839C3C28;

  static constexpr uint32_t kInLobby = 0x823C27A8;
  static constexpr uint32_t kNumPlayersInLobby = 0x823BD9C0;
  static constexpr uint32_t kGetIsReady = 0x823C81A8;
  static constexpr uint32_t kSetGameStarted = 0x823C8220;
  static constexpr uint32_t kNetSyncStart = 0x8241E618;
  static constexpr uint32_t kNetPrintf = 0x8242B460;
  static constexpr uint32_t kRefreshLocalUsers = 0x8242BF48;
  static constexpr uint32_t kShowGameInviteUi = 0x839C44D0;
  static constexpr uint32_t kInviteAcceptedInfo = 0x839C3598;
  static constexpr uint32_t kInputGetState = 0x839C58E0;
  static constexpr uint32_t kJoinLobbyFromInvite = 0x823C80A8;
  static constexpr uint32_t kDeclineAcceptedInvite = 0x82420E68;
  static constexpr uint32_t kInviteConfirmSignInReturn = 0x823ECB8C;
  static constexpr uint32_t kLobbyFatalError = 0x82427288;
  static constexpr uint32_t kLobbyPacket = 0x82429380;
  static constexpr uint32_t kPlayerTableInsert = 0x82424E30;
  static constexpr uint32_t kOpTimerExpired = 0x8241F0A8;
  static constexpr uint32_t kWaitForPlayerListDone = 0x82428A28;
  static constexpr uint32_t kNetPrintBuffer = 0x847F8DD8;
  static constexpr uint32_t kNetManager = 0x847D4B20;
};

// Net manager byte set once the host's player list arrived; WAIT_FOR_PLAYER_LIST ends when it is set or its timer runs out.
inline constexpr uint32_t kPlayerListReceivedOffset = 0x22DE9;

// Net manager word that holds an accepted invite until JoinLobbyFromInvite or the decline clears it.
inline constexpr uint32_t kAcceptedInviteOffset = 0x27F8;

// The menus' input object (pointer at G::kUiInputPointer) answers only the users in its controller mask.
inline constexpr uint32_t kUiControllerMaskOffset = 8;

// Sizes of the game's error popup buffers (title, message).
inline constexpr uint32_t kErrorTitleBytes = 128;
inline constexpr uint32_t kErrorMessageBytes = 512;
// SetError(code, level): level 100 shows the popup and HandleError only clears it; code 14 has no other side effect.
// A level not above the pending one is ignored.
inline constexpr uint32_t kPopupErrorCode = 14;
inline constexpr uint32_t kPopupErrorLevel = 100;
inline constexpr uint8_t kOnlineGameMode = 3;

inline constexpr uint32_t kVdpProtocol = 254;
inline constexpr uint32_t kWouldBlock = 10035;
// XNetGetConnectStatus: XNET_CONNECT_STATUS_CONNECTED and XNET_CONNECT_STATUS_LOST.
inline constexpr uint32_t kXnetConnected = 2;
inline constexpr uint32_t kXnetLost = 3;

// XSessionCreate flag: this console hosts the session.
inline constexpr uint32_t kSessionCreateHost = 0x1;

}  // namespace ssf2t
