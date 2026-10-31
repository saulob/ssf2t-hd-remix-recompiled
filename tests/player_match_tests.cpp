#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "guest_memory.h"
#include "input_owner.h"
#include "invite_lifecycle.h"
#include "player_match.h"
#include "player_match_guest.h"
#include "player_match_net.h"

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(expr)                                                     \
  do {                                                                  \
    ++g_checks;                                                         \
    if (!(expr)) {                                                      \
      ++g_failures;                                                     \
      std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #expr);     \
    }                                                                   \
  } while (0)

using namespace ssf2t;

online::PlayerMatchAd Ad(const std::string& session, const std::string& player) {
  online::PlayerMatchAd ad;
  ad.session = session;
  ad.player = player;
  ad.guest = online::kGuestTu023;
  ad.build = 1;
  return ad;
}

void TestLiveNeedsOnlinePlayAndASignedInService() {
  CHECK(!LiveAvailable(false, false));
  CHECK(!LiveAvailable(false, true));
  CHECK(!LiveAvailable(true, false));
  CHECK(LiveAvailable(true, true));
}

void TestVirtualXuidIsStablePerFriendCode() {
  const uint64_t a = VirtualXuid("ABCD-2345");
  CHECK(a == VirtualXuid("ABCD-2345"));
  CHECK(a != VirtualXuid("ABCD-2346"));
  CHECK((a >> 48) == 0x0009);
  CHECK((a & 0x0000FFFFFFFFFFFFull) != 0);
  CHECK(a != 0xB13EBABEBABEBABEull);
  std::set<uint64_t> seen;
  const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  for (int i = 0; i < 2000; ++i) {
    std::string code = "ABCD-";
    for (int d = 0, n = i; d < 4; ++d, n /= 32) code += alphabet[n % 32];
    seen.insert(VirtualXuid(code));
  }
  CHECK(seen.size() == 2000);
}

void TestOnlineMatchGate() {
  // Authenticated: the guest's own gate runs, and now sees the LIVE profile.
  CHECK(DecideOnlineGate(1, true, true) == OnlineGate::kLive);
  // Online play on, service not signed in: the port's ONLINE UNAVAILABLE popup.
  CHECK(DecideOnlineGate(1, true, false) == OnlineGate::kServiceUnavailable);
  // Online play off: the original gate and its CONNECTION REQUIRED popup.
  CHECK(DecideOnlineGate(1, false, false) == OnlineGate::kOriginal);
  CHECK(DecideOnlineGate(1, false, true) == OnlineGate::kOriginal);
  // Leaderboards keep the original gate.
  CHECK(DecideOnlineGate(0, true, false) == OnlineGate::kOriginal);
  CHECK(DecideOnlineGate(0, true, true) == OnlineGate::kOriginal);
}

void TestSignInReturnConvention() {
  // RequestOnlineSignIn returns 1 only past every check; each refusal raises its popup and returns 0.
  CHECK(SignInAllowed(1));
  CHECK(!SignInAllowed(0));
  CHECK(kConnectionRequiredError == 14 && kPopupErrorCode == kConnectionRequiredError);
}

void TestLiveProfileFollowsTheActiveUser() {
  for (uint32_t user = 0; user < 4; ++user) CHECK(LiveUserIndex(user) == user);
  CHECK(LiveUserIndex(0xFFFFFFFFu) == 0);
  CHECK(LiveUserIndex(4) == 0);
  CHECK(LocalUser(0) && LocalUser(3) && !LocalUser(4) && !LocalUser(0xFF));
}

PadReading Press(uint16_t buttons) {
  PadReading reading;
  reading.buttons = buttons;
  return reading;
}

void TestInputActivityCreditsTheUserThatPressed() {
  InputActivity activity;
  CHECK(activity.last_user() == -1 && activity.last_pad_user() == -1);
  activity.Observe(1, PadReading{});
  CHECK(activity.last_user() == -1);
  activity.Observe(1, Press(0x1000));
  CHECK(activity.last_user() == 1 && activity.last_pad_user() == 1);
  activity.Observe(0, Press(0x0010));
  CHECK(activity.last_user() == 0 && activity.last_pad_user() == 1);
  // Holding the same buttons is not new input; a stick push is.
  activity.Observe(1, Press(0x1000));
  CHECK(activity.last_user() == 0);
  PadReading stick;
  stick.thumbs[0] = 20000;
  activity.Observe(2, stick);
  CHECK(activity.last_user() == 2 && activity.last_pad_user() == 2);
  PadReading drift;
  drift.thumbs[1] = 3000;
  drift.left_trigger = 10;
  CHECK(!drift.Meaningful());
  activity.Observe(5, Press(1));
  CHECK(activity.last_user() == 2);
}

void TestMenuEntryOwnsTheMatch() {
  InputActivity activity;
  const OwnerChoice pad = ChooseMenuOwner(1, activity);
  CHECK(pad.user == 1 && std::string(pad.source) == "menu");
  CHECK(ChooseMenuOwner(0, activity).user == 0);
  activity.Observe(1, Press(0x1000));
  const OwnerChoice unknown = ChooseMenuOwner(-1, activity);
  CHECK(unknown.user == 1 && std::string(unknown.source) == "menu-last-input");
  CHECK(ChooseMenuOwner(-1, InputActivity{}).user == kKeyboardUser);
}

void TestInviteAcceptPicksTheAcceptingDevice() {
  InputActivity pad_user;
  pad_user.Observe(1, Press(0x1000));
  const OwnerChoice controller = ChooseInviteOwner(InputSource::kController, -1, pad_user, 0);
  CHECK(controller.user == 1 && std::string(controller.source) == "invite-controller");
  const OwnerChoice keyboard = ChooseInviteOwner(InputSource::kKeyboard, 1, pad_user, 1);
  CHECK(keyboard.user == 0 && std::string(keyboard.source) == "invite-keyboard");
  // A mouse click keeps the controller that already owns the menus.
  const OwnerChoice mouse = ChooseInviteOwner(InputSource::kMouse, 1, InputActivity{}, 0);
  CHECK(mouse.user == 1 && std::string(mouse.source) == "invite-mouse-fallback");
  CHECK(ChooseInviteOwner(InputSource::kMouse, -1, pad_user, 0).user == 1);
  // Nothing known about the pad: a controller accept is still a pad user, never the keyboard's 0.
  InputActivity keyboard_only;
  keyboard_only.Observe(0, Press(0x0010));
  const OwnerChoice unknown = ChooseInviteOwner(InputSource::kController, -1, keyboard_only, 0);
  CHECK(unknown.user == kFirstPadUser && std::string(unknown.source) == "invite-controller-first-pad");
  CHECK(ChooseInviteOwner(InputSource::kController, 2, InputActivity{}, 0).user == 2);
  CHECK(ChooseInviteOwner(InputSource::kMouse, -1, keyboard_only, 1).user == 0);
  CHECK(ChooseInviteOwner(InputSource::kMouse, -1, InputActivity{}, 3).user == 3);
  CHECK(ChooseInviteOwner(InputSource::kMouse, -1, InputActivity{}, -1).user == 0);
}

// Every screen of a running match keeps its owner; only a real end releases it, and the released owner still answers
// the popup that follows.
void TestOwnerLastsForTheWholeMatch() {
  MatchInputOwner owner;
  CHECK(owner.user() == -1 && !owner.active());
  CHECK(owner.FightUser(2) == 2 && owner.FightUser(-1) == kKeyboardUser);
  owner.Set({1, "menu"});
  const MatchEvent screens[] = {
      MatchEvent::kOnlineZoneEntered, MatchEvent::kLobby,          MatchEvent::kCharacterSelect,
      MatchEvent::kFight,             MatchEvent::kPause,          MatchEvent::kAchievements,
      MatchEvent::kOnlineZoneLeft,    MatchEvent::kOnlineZoneEntered, MatchEvent::kResults,
      MatchEvent::kErrorPopup,        MatchEvent::kRemotePlayerLeft};
  for (const MatchEvent event : screens) {
    CHECK(!EndsOnlineMatch(event));
    if (EndsOnlineMatch(event)) owner.End();
    CHECK(owner.active() && owner.user() == 1);
    // The game may move its active user (pause menu, another pad on a screen); the fight keeps the owner's pad.
    CHECK(owner.FightUser(0) == 1);
  }
  for (const MatchEvent end : {MatchEvent::kLocalLeave, MatchEvent::kSessionDeleted, MatchEvent::kPeerTimedOut}) {
    CHECK(EndsOnlineMatch(end));
  }
  owner.End();
  CHECK(!owner.active() && owner.user() == 1);
  CHECK(owner.FightUser(0) == 0);
  owner.Set({0, "invite-keyboard"});
  CHECK(owner.active() && owner.user() == 0);
}

// SetControllerMask(user + 1) and the menus' press loop both use bit user + 1.
void TestPopupAnswersTheOwner() {
  CHECK(ControllerMaskFor(0) == 0x2);
  CHECK(ControllerMaskFor(1) == 0x4);
  CHECK(ControllerMaskFor(3) == 0x10);
  for (int32_t user = 0; user < kLocalUserCount; ++user) CHECK((ControllerMaskFor(user) & (1u << (user + 1))) != 0);
  CHECK(kNetErrorCode == 1 && kPlayerLeftErrorCode == 2 && kNoConnectionErrorCode == 3);
  CHECK(kUiControllerMaskOffset == 8);
}

void TestOnlineUnavailableTextFitsTheGamePopup() {
  CHECK(std::string(kOnlineUnavailableTitle) == "ONLINE UNAVAILABLE");
  CHECK(std::string(kOnlineUnavailableMessage) == "Unable to connect to the online service.");
  CHECK(sizeof(kOnlineUnavailableTitle) < kErrorTitleBytes);
  CHECK(sizeof(kOnlineUnavailableMessage) < kErrorMessageBytes);
  for (const char* banned : {"LIVE", "Xbox", "sign", "CONNECTION REQUIRED"}) {
    CHECK(std::strstr(kOnlineUnavailableTitle, banned) == nullptr);
    CHECK(std::strstr(kOnlineUnavailableMessage, banned) == nullptr);
  }
}

void TestOnlineMatchIsOneAgainstOne() { CHECK(kPlayerMatchSlots == 2); }

void TestRulesComeFromTheGameCreateSettings() {
  uint8_t block[kCreateMatchSettingsBytes] = {};
  const uint32_t words[10] = {1, 2, 3, 6, 0, 1, 1, 0, 1, 0};
  for (uint32_t i = 0; i < 10; ++i) StoreBE32(block, i * 4, words[i]);
  const CreateMatchSettings settings = ReadCreateMatchSettings(block);
  CHECK(settings.rule == 1 && settings.rounds == 2 && settings.speed == 3 && settings.players == 6);
  CHECK(settings.timer == 1 && settings.akuma == 1 && settings.tournament == 0);
  CHECK(LoadBE32(block, kCreateMatchPlayersOffset) == 6);
  const online::PlayerMatchRules rules = PlayerMatchRulesFrom(settings);
  const online::PlayerMatchRules want = {{"rule", 1}, {"rounds", 2}, {"speed", 3}, {"timer", 1}, {"akuma", 1},
                                         {"handicap", 0}};
  CHECK(rules == want);
}

void TestAcceptedInviteInfoNamesTheHostSession() {
  const uint64_t me = VirtualXuid("BBBB-3333");
  const uint64_t host = VirtualXuid("AAAA-2222");
  std::vector<uint8_t> info(kInviteInfoBytes, 0xCC);
  WriteInviteInfo(info.data(), me, host, "00112233445566aa");
  CHECK(LoadBE64(info.data(), 0) == me && LoadBE64(info.data(), 8) == host);
  CHECK(LoadBE32(info.data(), 16) == kSsf2tTitleId);
  CHECK(SessionIdText(info.data() + 20) == "00112233445566aa");
  CHECK(XnAddrXuid(info.data() + 28) == host);
  CHECK(LoadBE32(info.data(), 28) == SyntheticAddress(host));
  CHECK(LoadBE32(info.data(), 80) == 1);
  CHECK(kInviteAcceptedNotification == 0x02000002);
  // The same session and host address a search result for that session carries.
  std::vector<uint8_t> search(kSearchHeaderBytes + kSearchBytesPerResult, 0);
  online::PlayerMatchAd ad = Ad("00112233445566aa", "AAAA-2222");
  WriteSearchResults(search.data(), 0x40001000, uint32_t(search.size()), 1, {ad});
  CHECK(std::memcmp(search.data() + kSearchHeaderBytes, info.data() + 20, kSessionInfoBytes) == 0);
}

using Gate = InviteLifecycle::Gate;

// One accepted invite reaches the game once: one delivery, one confirmation gate however long the box stays open (the
// real run polled it 267 times), and one consumption by the join.
void TestOneInviteIsOneTransition() {
  InviteLifecycle invite;
  CHECK(invite.ConfirmGate() == Gate::kNoInvite && !invite.Consume());
  CHECK(invite.Deliver("s1") && invite.generation() == 1 && invite.pending());
  CHECK(!invite.Deliver("s1") && invite.generation() == 1);
  CHECK(invite.ConfirmGate() == Gate::kFirst);
  for (int frame = 0; frame < 267; ++frame) CHECK(invite.ConfirmGate() == Gate::kRepeat);
  CHECK(invite.repeats() == 267);
  CHECK(invite.Consume() && !invite.pending());
  CHECK(!invite.Consume());
  CHECK(invite.ConfirmGate() == Gate::kNoInvite);
}

// After a match ends a new invite, even from the same host session, is a new transition; a different invite replaces
// one the game has not joined yet; a declined one frees the next.
void TestLaterInvitesStartAgain() {
  InviteLifecycle invite;
  CHECK(invite.Deliver("s1") && invite.ConfirmGate() == Gate::kFirst && invite.Consume());
  CHECK(invite.Deliver("s1") && invite.generation() == 2);
  CHECK(invite.ConfirmGate() == Gate::kFirst && invite.repeats() == 0);
  CHECK(invite.Deliver("s2") && invite.generation() == 3 && invite.session() == "s2");
  CHECK(invite.ConfirmGate() == Gate::kFirst);
  CHECK(invite.Consume());
  CHECK(invite.Deliver("s2") && invite.generation() == 4 && invite.ConfirmGate() == Gate::kFirst);
}

// The accepted invite's phases: teardown begins and completes once, the join is requested by the game and started by
// the port once, and a decline never leaves a join to start.
void TestAcceptedInvitePhases() {
  using Phase = InviteLifecycle::Phase;
  InviteLifecycle invite;
  CHECK(!invite.BeginTeardown() && !invite.CompleteTeardown() && !invite.BeginJoining("s1"));
  CHECK(invite.Deliver("s1", 7) && invite.generation() == 7 && invite.phase() == Phase::kAccepted);
  CHECK(!invite.Deliver("s1", 8) && invite.generation() == 7);
  CHECK(!invite.CompleteTeardown() && !invite.BeginJoining("s1"));
  CHECK(invite.BeginTeardown() && !invite.BeginTeardown() && invite.phase() == Phase::kTeardown);
  CHECK(invite.pending());
  CHECK(invite.CompleteTeardown() && !invite.CompleteTeardown() && invite.phase() == Phase::kReady);
  CHECK(invite.pending() && !invite.BeginJoining("s1"));
  CHECK(invite.Consume() && invite.phase() == Phase::kJoinRequested);
  CHECK(!invite.BeginJoining("other") && invite.BeginJoining("s1") && !invite.BeginJoining("s1"));
  CHECK(invite.phase() == Phase::kJoining && !invite.BeginTeardown());

  CHECK(invite.Deliver("s2", 8) && invite.phase() == Phase::kAccepted);
  CHECK(invite.Cancel() && !invite.pending() && invite.phase() == Phase::kNone && !invite.BeginJoining("s2"));
  CHECK(!invite.Cancel());

  CHECK(invite.Deliver("s3", 9) && invite.Consume() && invite.BeginJoining("s3"));
  CHECK(invite.Deliver("s4") && invite.generation() == 10);
}

void TestSessionIds() {
  const std::vector<uint8_t> bytes = SessionIdBytes("0123456789abcdef");
  CHECK((bytes == std::vector<uint8_t>{0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF}));
  CHECK(SessionIdText(bytes.data()) == "0123456789abcdef");
  CHECK(SessionIdBytes("xyz") == std::vector<uint8_t>(8, 0));
  CHECK(SessionIdBytes("0123456789abcdeg") == std::vector<uint8_t>(8, 0));
}

void TestSearchResultsLayout() {
  constexpr uint32_t kAddress = 0x40001000;
  std::vector<uint8_t> buffer(kSearchHeaderBytes + 3 * kSearchBytesPerResult, 0xCC);
  const std::vector<online::PlayerMatchAd> ads = {Ad("00112233445566aa", "AAAA-2222"),
                                                  Ad("00112233445566bb", "BBBB-3333")};
  CHECK(WriteSearchResults(buffer.data(), kAddress, uint32_t(buffer.size()), 3, ads) == 2);
  CHECK(LoadBE32(buffer.data(), 0) == 2);
  CHECK(LoadBE32(buffer.data(), 4) == kAddress + kSearchHeaderBytes);
  const uint8_t* second = buffer.data() + kSearchHeaderBytes + kSearchResultBytes;
  CHECK(SessionIdText(second) == "00112233445566bb");
  CHECK(XnAddrXuid(second + 8) == VirtualXuid("BBBB-3333"));
  CHECK(LoadBE32(second, 8) == SyntheticAddress(VirtualXuid("BBBB-3333")));
  CHECK(LoadBE32(second, kSessionInfoBytes) == 1);
  CHECK(LoadBE32(second, kSessionInfoBytes + 4) == 0);
  CHECK(LoadBE32(second, kSessionInfoBytes + 8) == 1);
  CHECK(LoadBE32(second, kSessionInfoBytes + 16) == 0 && LoadBE32(second, kSessionInfoBytes + 24) == 0);

  std::vector<uint8_t> one(kSearchHeaderBytes + kSearchBytesPerResult, 0);
  CHECK(WriteSearchResults(one.data(), kAddress, uint32_t(one.size()), 1, ads) == 1);
  std::vector<uint8_t> tiny(4, 0xCC);
  CHECK(WriteSearchResults(tiny.data(), kAddress, 4, 1, ads) == 0);
  CHECK(tiny[0] == 0xCC);
  std::vector<uint8_t> none(kSearchHeaderBytes, 0xCC);
  CHECK(WriteSearchResults(none.data(), kAddress, kSearchHeaderBytes, 4, {}) == 0);
  CHECK(LoadBE32(none.data(), 0) == 0 && LoadBE32(none.data(), 4) == 0);
}

template <typename G>
std::set<uint32_t> CodeAddresses() {
  return {G::kRequestOnlineSignIn, G::kSetInOnlineZone, G::kCreateMatchSetDefaults, G::kCreateMatchIncNumPlayers,
          G::kCreateMatchDecNumPlayers, G::kSetError, G::kXmlDocumentParse, G::kHelpTextLoad, G::kGetScreenMainText,
          G::kSigninState, G::kCheckPrivilege, G::kSigninInfo, G::kGetXuid, G::kTitleXnAddr, G::kSessionCreate,
          G::kSessionDelete, G::kSessionJoinLocal, G::kSessionJoinRemote, G::kSessionLeaveLocal,
          G::kSessionLeaveRemote, G::kSessionArbitrationRegister, G::kSessionStart, G::kSessionEnd,
          G::kSessionSearchEx, G::kSessionWriteStats, G::kSessionMigrateHost, G::kRefreshLocalUsers,
          G::kShowGameInviteUi, G::kInviteAcceptedInfo, G::kInputGetState, G::kJoinLobbyFromInvite,
          G::kDeclineAcceptedInvite};
}

void TestEachImageHasItsOwnAddresses() {
  const std::set<uint32_t> base = CodeAddresses<Base013PlayerMatch>();
  const std::set<uint32_t> update = CodeAddresses<TitleUpdate023PlayerMatch>();
  CHECK(base.size() == 32 && update.size() == 32);
  CHECK(Base013PlayerMatch::kJoinLobbyFromInvite == 0x823C7AB0 &&
        TitleUpdate023PlayerMatch::kJoinLobbyFromInvite == 0x823C80A8);
  CHECK(Base013PlayerMatch::kDeclineAcceptedInvite == 0x824206A8 &&
        TitleUpdate023PlayerMatch::kDeclineAcceptedInvite == 0x82420E68);
  CHECK(Base013PlayerMatch::kInviteConfirmSignInReturn == 0x823EC49C &&
        TitleUpdate023PlayerMatch::kInviteConfirmSignInReturn == 0x823ECB8C);
  CHECK(kAcceptedInviteOffset == 0x27F8);
  CHECK(Base013PlayerMatch::kCreateLobby == 0x823BD588 && TitleUpdate023PlayerMatch::kCreateLobby == 0x823BD540);
  CHECK(kCreateMatchPrivateSlotsOffset == 16 && kCreateMatchPlayersOffset == 12);
  CHECK(Base013PlayerMatch::kInputGetState == 0x839C5180 && TitleUpdate023PlayerMatch::kInputGetState == 0x839C58E0);
  CHECK(Base013PlayerMatch::kUiInputPointer == 0x8480BD30 && TitleUpdate023PlayerMatch::kUiInputPointer == 0x8480C250);
  CHECK(Base013PlayerMatch::kRefreshLocalUsers == 0x8242B7E8 && TitleUpdate023PlayerMatch::kRefreshLocalUsers == 0x8242BF48);
  CHECK(Base013PlayerMatch::kShowGameInviteUi == 0x839C3D70 && TitleUpdate023PlayerMatch::kShowGameInviteUi == 0x839C44D0);
  CHECK(Base013PlayerMatch::kInviteAcceptedInfo == 0x839C2E38 && TitleUpdate023PlayerMatch::kInviteAcceptedInfo == 0x839C3598);
  for (const uint32_t address : base) CHECK(!update.count(address));
  for (const uint32_t address : update) CHECK(address >= 0x82000000 && address < 0x84000000 && address % 4 == 0);
  CHECK(TitleUpdate023PlayerMatch::kGameMode - Base013PlayerMatch::kGameMode == 0x518);
  CHECK(TitleUpdate023PlayerMatch::kErrorTitle - TitleUpdate023PlayerMatch::kErrorMessage == kErrorMessageBytes);
  CHECK(Base013PlayerMatch::kErrorTitle - Base013PlayerMatch::kErrorMessage == kErrorMessageBytes);
}

}  // namespace

int main() {
  TestLiveNeedsOnlinePlayAndASignedInService();
  TestVirtualXuidIsStablePerFriendCode();
  TestOnlineMatchGate();
  TestSignInReturnConvention();
  TestLiveProfileFollowsTheActiveUser();
  TestInputActivityCreditsTheUserThatPressed();
  TestMenuEntryOwnsTheMatch();
  TestInviteAcceptPicksTheAcceptingDevice();
  TestOwnerLastsForTheWholeMatch();
  TestPopupAnswersTheOwner();
  TestOnlineUnavailableTextFitsTheGamePopup();
  TestOnlineMatchIsOneAgainstOne();
  TestRulesComeFromTheGameCreateSettings();
  TestOneInviteIsOneTransition();
  TestAcceptedInvitePhases();
  TestLaterInvitesStartAgain();
  TestSessionIds();
  TestAcceptedInviteInfoNamesTheHostSession();
  TestSearchResultsLayout();
  TestEachImageHasItsOwnAddresses();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
