// Online Match lobby: the game's player table, packet and op names, the lobby error call sites per image, the log
// limits, the XSession leave completion and the hook wiring. Pure logic and source reads.
// Run: ssf2t_player_match_lobby_tests

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "guest_memory.h"
#include "player_match_guest.h"
#include "player_match_lobby.h"

using namespace ssf2t;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

std::string ReadSource(const char* name) {
  std::ifstream in(std::filesystem::path(SSF2T_SOURCE_DIR) / "src" / name, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool Has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

void TestPlayerTableLayout() {
  std::vector<uint8_t> table(1024, 0);
  StoreBE32(table.data(), 80, 2);
  StoreBE32(table.data(), 84, 0);
  StoreBE32(table.data(), 88, 1);
  StoreBE32(table.data(), 92, 0);
  StoreBE32(table.data(), 904, 2);
  const LobbyTable t = ReadLobbyTable(table.data());
  CHECK(t.public_slots == 2 && t.private_slots == 0 && t.public_used == 1 && t.private_used == 0 && t.players == 2);
  CHECK(kPlayerTableOffset + 904 == 19160);
  CHECK(kPlayerEntryBytes == 80);
  CHECK(Base013PlayerMatch::kNetManager == 0x847D4600 && TitleUpdate023PlayerMatch::kNetManager == 0x847D4B20);
}

void TestNames() {
  CHECK(std::string(LobbyPacketName(6)) == "player_set");
  CHECK(std::string(LobbyPacketName(7)) == "player_change");
  CHECK(std::string(LobbyPacketName(8)) == "player_list");
  CHECK(std::string(LobbyPacketName(0)) == "player_ready");
  CHECK(std::string(LobbyPacketName(14)) == "player_update");
  CHECK(std::string(LobbyPacketName(3)) == "game");
  CHECK(std::string(LobbyPacketName(200)) == "unknown");
  CHECK(std::string(OnlineOpName(2)) == "CREATE");
  CHECK(std::string(OnlineOpName(3)) == "ANSWER_JOINER_AND_PROPAGATE_HIS_JOIN");
  CHECK(std::string(OnlineOpName(5)) == "JOIN_REMOTE");
  CHECK(std::string(OnlineOpName(7)) == "VERIFY_FULL_CONNECTIVITY_ON_JOIN");
  CHECK(std::string(OnlineOpName(12)) == "LEAVE_LOCAL");
  CHECK(std::string(OnlineOpName(14)) == "DELETE");
  CHECK(std::string(OnlineOpName(36)) == "MIGRATE_HOST_TO_ME");
  CHECK(std::string(OnlineOpName(37)) == "unknown");
}

void TestLobbyErrorSitesPerImage() {
  std::set<uint32_t> base, update;
  for (const LobbyFatalSite& site : kLobbyFatalSites) {
    base.insert(site.base013);
    update.insert(site.tu023);
    CHECK(std::string(LobbyFatalReason(site.base013, false)) == site.reason);
    CHECK(std::string(LobbyFatalReason(site.tu023, true)) == site.reason);
    CHECK(site.base013 != site.tu023);
  }
  CHECK(base.size() == std::size(kLobbyFatalSites) && update.size() == std::size(kLobbyFatalSites));
  CHECK(std::string(LobbyFatalReason(0x82429610, true)) == "player_set_without_remote_players");
  CHECK(std::string(LobbyFatalReason(0x82428EB0, false)) == "player_set_without_remote_players");
  CHECK(std::string(LobbyFatalReason(0x82429610, false)) == "other");
  CHECK(std::string(LobbyFatalReason(0x82427B04, true)) == "leave_local_failed");
}

void TestRepeatFilterAndBudget() {
  RepeatFilter repeats;
  CHECK(repeats.Next("a") == 0);
  CHECK(repeats.Next("a") == -1);
  CHECK(repeats.Next("a") == -1);
  CHECK(repeats.Next("b") == 2);
  CHECK(repeats.Next("a") == 0);

  LineBudget budget(3);
  CHECK(budget.Allow(10.0) && budget.Allow(10.1) && budget.Allow(10.2));
  CHECK(!budget.Allow(10.3) && !budget.Allow(10.9));
  CHECK(budget.TakeDropped() == 2 && budget.TakeDropped() == 0);
  CHECK(budget.Allow(11.0));
}

void TestSearchLogsChangesAndOneSummaryPerSecond() {
  SearchLogGate gate;
  CHECK(gate.ShouldLog(0, 100.0) && gate.calls_since_log() == 1);
  int logged = 0;
  for (int i = 1; i < 30; ++i) logged += gate.ShouldLog(0, 100.0 + i * 0.016) ? 1 : 0;
  CHECK(logged == 0);
  CHECK(gate.ShouldLog(1, 100.5) && gate.calls_since_log() == 30);
  CHECK(!gate.ShouldLog(1, 100.6));
  CHECK(gate.ShouldLog(1, 101.6) && gate.calls_since_log() == 2);
}

void TestHexHead() {
  const uint8_t data[20] = {0x06, 0x02, 0xAB, 0xCD};
  CHECK(HexHead(data, 4) == "0602ABCD");
  CHECK(HexHead(data, 20).size() == 32);
}

void TestLeaveCompletesInsteadOfFailing() {
  CHECK(kXgiSessionLeave == 0x000B0013 && kErrorIoPending == 997);
  const std::string hook = ReadSource("player_match_hook.cpp");
  for (const char* wrapper : {"base013_sub_839C2790", "base013_sub_839C2838", "tu023_sub_839C2EF0", "tu023_sub_839C2F98"}) {
    CHECK(Has(hook, std::string("REX_HOOK_RAW(") + wrapper + ") {\n") || Has(hook, std::string("REX_HOOK_RAW(") + wrapper + ") {\r\n"));
  }
  CHECK(Has(hook, "SessionLeave(ctx, base, __imp__base013_sub_839C2790, \"XSessionLeaveLocal\", true)"));
  CHECK(Has(hook, "SessionLeave(ctx, base, __imp__base013_sub_839C2838, \"XSessionLeaveRemote\", false)"));
  CHECK(Has(hook, "SessionLeave(ctx, base, __imp__tu023_sub_839C2EF0, \"XSessionLeaveLocal\", true)"));
  CHECK(Has(hook, "SessionLeave(ctx, base, __imp__tu023_sub_839C2F98, \"XSessionLeaveRemote\", false)"));
  const size_t begin = hook.find("static void SessionLeave(");
  const std::string leave = hook.substr(begin, hook.find("static void SessionStart(", begin) - begin);
  CHECK(Has(leave, "REX_KERNEL_STATE()->CompleteOverlappedImmediate(overlapped, 0);"));
  CHECK(Has(leave, "ctx.r3.u64 = ssf2t::kErrorIoPending;"));
  // Offline the SDK path stays.
  CHECK(Has(leave, "if (!CurrentLive().available) {") && Has(leave, "original(ctx, base);"));
  CHECK(Has(hook, "if (live) ssf2t::ResetLobbyTelemetry();"));
  CHECK(Has(hook, "if (!gate.ShouldLog(written, NowSeconds())) return;"));
}

// Leaving the lobby the game's own way (local leave, delete) or losing a connected peer ends the online match, so no
// rollback session, LobbyMux route or hand-off is left for a peer that is gone. A remote player's leave alone does not,
// and neither does the online zone, which the game leaves for screens inside a running match.
void TestLeavingTheLobbyEndsTheMatch() {
  const std::string hook = ReadSource("player_match_hook.cpp");
  const size_t begin = hook.find("static void SessionLeave(");
  const std::string leave = hook.substr(begin, hook.find("static void SessionStart(", begin) - begin);
  CHECK(Has(leave, "if (local) EndOnlineMatch(\"lobby_left\");"));
  CHECK(Has(hook, "EndOnlineMatch(\"session_deleted\");"));
  CHECK(!Has(hook, "online_zone_left"));
  const size_t zone_begin = hook.find("static void SetInOnlineZone(");
  const std::string zone = hook.substr(zone_begin, hook.find("\n  }\n", zone_begin) - zone_begin);
  CHECK(!Has(zone, "LeaveMatch") && !Has(zone, "EndOnlineMatch") && !Has(zone, "EndMatchInput"));
  CHECK(Has(zone, "teardown=false") && Has(zone, "if (active_match) return;"));
  CHECK(Has(hook, "ssf2t::EndMatchInput(reason);"));
  CHECK(Has(hook, "ssf2t::Online().LeaveMatch();"));
  const std::string netplay = ReadSource("netplay_hook.cpp");
  const size_t start = netplay.find("bool EnsureStarted(");
  const std::string ensure = netplay.substr(start, netplay.find("void Fault(", start) - start);
  CHECK(Has(ensure, "if (peer_seen_) {") && Has(ensure, "ssf2t::Online().LeaveMatch();"));
  CHECK(Has(ensure, "ssf2t::EndMatchInput(\"peer_timeout\");"));
  // The fight reads the owner's pad, and online popups answer to it.
  CHECK(Has(netplay, "ssf2t::MatchFightUser(int32_t(ssf2t::LoadBE32(base, G::kActiveUser)))"));
  CHECK(Has(hook, "if (OnlineErrorPopup(code) && ssf2t::ValidUser(owner)) GiveMenusTo(base, owner);"));
  CHECK(Has(netplay, "if (state == np::SessionState::kConnected || state == np::SessionState::kRunning) peer_seen_ = true;"));
  // No fake host migration: XSessionMigrateHost still runs the SDK path, which the 1v1 lobby turns into Connection lost.
  CHECK(Has(hook, "SessionCall(ctx, base, __imp__base013_sub_839C2F68, \"XSessionMigrateHost\")"));
  CHECK(Has(hook, "SessionCall(ctx, base, __imp__tu023_sub_839C36C8, \"XSessionMigrateHost\")"));
}

void TestLobbyHooksCoverBothImages() {
  const std::string hook = ReadSource("player_match_lobby_hook.cpp");
  for (const char* fn : {"base013_sub_8242AD00", "base013_sub_82426B28", "base013_sub_82428C20", "base013_sub_824246D0",
                         "base013_sub_8241E8E8", "base013_sub_824282C8", "tu023_sub_8242B460", "tu023_sub_82427288",
                         "tu023_sub_82429380", "tu023_sub_82424E30", "tu023_sub_8241F0A8", "tu023_sub_82428A28"}) {
    CHECK(Has(hook, std::string("REX_HOOK_RAW(") + fn + ")") && Has(hook, std::string("REX_EXTERN(__imp__") + fn + ")"));
  }
  const size_t guard = hook.find("#if SSF2T_TITLE_UPDATE_IMAGE");
  CHECK(guard != std::string::npos && guard < hook.find("REX_HOOK_RAW(tu023_"));
  CHECK(Has(hook, "player_match leave requested reason="));
  CHECK(Has(hook, "player_match remote insert begin slot="));
  CHECK(Has(hook, "player_match GameLobby players before="));
  CHECK(Has(hook, "constexpr uint32_t kPacketsLogged = 16;"));
  CHECK(Base013PlayerMatch::kNetPrintf == 0x8242AD00 && TitleUpdate023PlayerMatch::kNetPrintf == 0x8242B460);
  CHECK(Base013PlayerMatch::kNetPrintBuffer == 0x847F88B8 && TitleUpdate023PlayerMatch::kNetPrintBuffer == 0x847F8DD8);
  CHECK(Base013PlayerMatch::kLobbyFatalError == 0x82426B28 && TitleUpdate023PlayerMatch::kLobbyFatalError == 0x82427288);
  CHECK(Base013PlayerMatch::kOpTimerExpired == 0x8241E8E8 && TitleUpdate023PlayerMatch::kOpTimerExpired == 0x8241F0A8);
  CHECK(Base013PlayerMatch::kWaitForPlayerListDone == 0x824282C8 &&
        TitleUpdate023PlayerMatch::kWaitForPlayerListDone == 0x82428A28);
  CHECK(kPlayerListReceivedOffset == 0x22DE9);
  // Only the lobby's wait is held to the establishment budget: every other op keeps the game's own timer.
  CHECK(Has(hook, "op != ssf2t::online::kWaitForPlayerListOp"));
  CHECK(std::string(OnlineOpName(6)) == "WAIT_FOR_PLAYER_LIST");
  const std::string cmake = ReadSource("../CMakeLists.txt");
  CHECK(Has(cmake, "src/player_match_lobby.cpp") && Has(cmake, "src/player_match_lobby_hook.cpp"));
}


void TestLocalUsersAreRefreshedBeforeTheGate() {
  const std::string hook = ReadSource("player_match_hook.cpp");
  const size_t begin = hook.find("static void RequestOnlineSignIn(");
  const std::string gate = hook.substr(begin, hook.find("static void ShowGameInviteUi(", begin) - begin);
  const size_t live = gate.find("if (gate == ssf2t::OnlineGate::kLive) {");
  const size_t refresh = gate.find("refresh_local_users(ctx, base);");
  const size_t original = gate.find("original(ctx, base);");
  CHECK(live != std::string::npos && live < refresh && refresh < original);
  CHECK(Has(gate, "ctx.r3.u64 = uint64_t(int64_t(argument));"));
  CHECK(Has(hook, "__imp__base013_sub_823EAEE8,\n                            __imp__base013_sub_8242B7E8);") ||
        Has(hook, "__imp__base013_sub_823EAEE8,\r\n                            __imp__base013_sub_8242B7E8);"));
  CHECK(Has(hook, "__imp__tu023_sub_8242BF48);"));
  CHECK(Has(hook, "player_match local users refreshed live_user="));
}

void TestInvitePlayerAndAcceptInviteHooks() {
  const std::string hook = ReadSource("player_match_hook.cpp");
  CHECK(Has(hook, "REX_HOOK_RAW(base013_sub_839C3D70) { Base::ShowGameInviteUi("));
  CHECK(Has(hook, "REX_HOOK_RAW(tu023_sub_839C44D0) { Update::ShowGameInviteUi("));
  CHECK(Has(hook, "REX_HOOK_RAW(base013_sub_839C2E38) { Base::InviteAcceptedInfo("));
  CHECK(Has(hook, "REX_HOOK_RAW(tu023_sub_839C3598) { Update::InviteAcceptedInfo("));
  const size_t begin = hook.find("static void ShowGameInviteUi(");
  const std::string show = hook.substr(begin, hook.find("static void InviteAcceptedInfo(", begin) - begin);
  // Online play never reaches the Guide stub; offline keeps the SDK call.
  CHECK(Has(show, "if (!CurrentLive().available) {") && Has(show, "player_match invite open"));
  CHECK(Has(show, "ctx.r3.u64 = 0;"));
  CHECK(Has(hook, "REX_KERNEL_STATE()->BroadcastNotification(kInviteAcceptedNotification, uint32_t(choice.user));"));
  CHECK(Has(hook, "choice = ChooseInviteOwner(accepted_by, g_owner.user(), g_activity, int32_t(g_live_user.load()));"));
  CHECK(Has(hook, "REX_HOOK_RAW(base013_sub_839C5180) { Base::PadPoll(") &&
        Has(hook, "REX_HOOK_RAW(tu023_sub_839C58E0) { Update::PadPoll("));
  CHECK(Has(hook, "ssf2t::WriteInviteInfo(GuestPtr(base, out), live.xuid, ssf2t::VirtualXuid(invite->player), invite->session);"));
  const std::string ui = ReadSource("invite_ui.cpp");
  CHECK(Has(ui, "Online().SendInvite(choice.player);"));
  CHECK(Has(ui, "const InputSource source = ActivationSource();"));
  CHECK(Has(ui, "if (const auto ad = Online().AcceptInvite(invite_.id)) DeliverAcceptedInvite(*ad, source);"));
  CHECK(Has(ui, "Online().DeclineInvite(invite_.id);"));
  CHECK(Has(ui, "online::InviteChoices(Online().snapshot().friends)"));
  CHECK(!Has(ui, "hallenge") && !Has(ReadSource("launcher.cpp"), "Challenge"));
  CHECK(!Has(ReadSource("online_page.cpp"), "hallenge"));
  CHECK(Has(ReadSource("../CMakeLists.txt"), "src/invite_ui.cpp"));
  CHECK(Has(ReadSource("ssf2t_hd_remix_app.h"), "CreateInviteUi(imgui_drawer(),"));
}

}  // namespace

// One accepted invite is one transition: the accept box's per-frame gate reaches the game once, the join and the
// decline consume the invite, and a second Accept of the same invite is refused.
void TestOneInviteIsOneTransition() {
  const std::string hook = ReadSource("player_match_hook.cpp");
  const size_t begin = hook.find("static void RequestOnlineSignIn(");
  const std::string gate = hook.substr(begin, hook.find("static void SkipConfirmRepeat(", begin) - begin);
  CHECK(Has(gate, "const bool confirm = caller == G::kInviteConfirmSignInReturn;"));
  CHECK(gate.find("if (confirm && SkipConfirmRepeat(ctx, base)) return;") < gate.find("original(ctx, base);"));
  CHECK(Has(gate, "if (argument == 1 && !confirm) {"));
  CHECK(Has(gate, "if (argument == 1 && !confirm && gate == ssf2t::OnlineGate::kLive"));
  CHECK(Has(hook, "if (gate == ssf2t::InviteLifecycle::Gate::kRepeat) {\n"
                  "      ctx.r3.u64 = g_confirm_result;\n      return true;"));
  CHECK(Has(hook, "if (!g_invite.Deliver(ad.session, Online().AcceptedInviteGeneration())) {"));
  CHECK(Has(hook, "player_match invite confirm gate generation="));
  CHECK(Has(hook, "g_invite.Consume()") && Has(hook, "g_invite.Cancel()"));
  for (const char* fn : {"base013_sub_823C7AB0) { Base::JoinLobbyFromInvite(",
                         "base013_sub_824206A8) { Base::DeclineAcceptedInvite(",
                         "tu023_sub_823C80A8) { Update::JoinLobbyFromInvite(",
                         "tu023_sub_82420E68) { Update::DeclineAcceptedInvite("}) {
    CHECK(Has(hook, std::string("REX_HOOK_RAW(") + fn));
  }
  CHECK(Has(hook, "g_in_online_zone = entering;"));
}

// A finished fight's rollback core never reaches the next fight, and a new match never reuses the old session.
void TestEachFightAndMatchStartsClean() {
  const std::string netplay = ReadSource("netplay_hook.cpp");
  const size_t pump = netplay.find("void PumpNetwork(uint64_t now_ms, const uint8_t* base) {");
  CHECK(pump != std::string::npos && netplay.find("EndFightOutsideFightLoop(base, now_ms);", pump) <
                                         netplay.find("session_->Poll(now_ms);", pump));
  const size_t end = netplay.find("void EndFightOutsideFightLoop(");
  const std::string left = netplay.substr(end, netplay.find("void Fault(", end) - end);
  CHECK(Has(left, "if (!session_->local_ready() || LocalFightActive(base)) {"));
  CHECK(Has(left, "ResetFight();\n    session_->SetLocalReady(false, now_ms);"));
  CHECK(Has(netplay, "const bool fight_active = LocalFightActive(base);"));
  CHECK(Has(netplay, "if (session_ && online_driven_ && plan.online && plan.generation != generation_) {\n"
                     "      Shutdown(now_ms, \"new_match\");"));
  CHECK(Has(netplay, "Shutdown(now_ms, \"match_ended\");"));
  CHECK(Has(netplay, "DEBUG_SAULO netplay match cleanup generation={} reason={}"));
  CHECK(Has(netplay, "generation_ = plan.generation;"));
  const size_t shutdown = netplay.find("void Shutdown(uint64_t now_ms, const char* reason) {");
  const std::string reset = netplay.substr(shutdown, netplay.find("void LogTransitions(", shutdown) - shutdown);
  for (const char* field : {"ResetFight();", "session_.reset();", "mux_.reset();", "transport_.reset();",
                            "PublishLobby(nullptr);", "generation_ = 0;", "peer_seen_ = false;",
                            "logged_local_ready_ = false;", "logged_remote_ready_ = false;", "takeover_logged_ = false;"}) {
    CHECK(Has(reset, field));
  }
  CHECK(Has(ReadSource("window_host.cpp"), "ssf2t::PumpNetplayNetwork(base);"));
}

// Both peers' frame 0 is the same state: native steps only up to the game's own NetSync start point,
// then the fight holds without stepping until the barrier passes, and the BeginFight frame steps nothing natively.
void TestGuestSearchWaitsForTheServiceAnswer() {
  const std::string hook = ReadSource("player_match_hook.cpp");
  const size_t begin = hook.find("static void SessionSearchEx(");
  const std::string search = hook.substr(begin, hook.find("static void SessionCall(", begin) - begin);
  const size_t ask = search.find("search = ssf2t::Online().AskPlayerMatchSearch();");
  const size_t defer = search.find("CompleteSearchWhenAnswered(base, search.epoch,");
  const size_t original = search.find("original(ctx, base);");
  CHECK(ask != std::string::npos && ask < defer && defer < original);
  CHECK(Has(search, "if (search.pending && header && size_ptr && overlapped && max_results &&"));
  CHECK(Has(search, "ctx.r3.u64 = ssf2t::kErrorIoPending;"));
  CHECK(!Has(search, "SetPlayerMatchSearch(true)"));
  const size_t wait = hook.find("static void CompleteSearchWhenAnswered(");
  const std::string deferred = hook.substr(wait, begin - wait);
  CHECK(Has(deferred, "while (ssf2t::Online().PlayerMatchSearchPending(epoch) && std::chrono::steady_clock::now() < limit)"));
  CHECK(Has(deferred, "ssf2t::Online().PlayerMatchResults(epoch));"));
  CHECK(Has(deferred, "REX_KERNEL_STATE()->CompleteOverlappedDeferred("));
  CHECK(Has(hook, "constexpr std::chrono::seconds kSearchWaitLimit(7);"));
}

void TestInviteTransitionIsTracked() {
  const std::string hook = ReadSource("player_match_hook.cpp");
  CHECK(Has(hook, "invite state accepted generation={} session={}"));
  CHECK(Has(hook, "invite transition teardown begin generation={} cause={}"));
  CHECK(Has(hook, "invite transition teardown complete generation={}"));
  CHECK(Has(hook, "invite transition joining generation={} session={}"));
  CHECK(Has(hook, "invite transition failed generation={} reason=join_refused"));
  CHECK(Has(hook, "NoteInviteTeardown(\"lobby_leave\");") && Has(hook, "NoteInviteTeardown(\"session_deleted\");"));
  CHECK(Has(hook, "if (entering) NoteInviteTeardownComplete();") && Has(hook, "NoteInviteTeardown(\"zone_left\");"));
  const size_t join = hook.find("static void JoinLobbyFromInvite(");
  const std::string join_body = hook.substr(join, hook.find("static void DeclineAcceptedInvite(", join) - join);
  CHECK(join_body.find("NoteInviteTeardownComplete();") < join_body.find("InviteConsumed("));
  CHECK(Has(hook, "declined ? g_invite.Cancel() : g_invite.Consume()"));
  const size_t zone = hook.find("static void SetInOnlineZone(");
  const std::string zone_body = hook.substr(zone, hook.find("static void CreateMatchDefaults(", zone) - zone);
  CHECK(!Has(zone_body, "DropAcceptedInvite") && !Has(zone_body, "AcceptInvite"));
}

void TestFrameZeroIsTheSameOnBothPeers() {
  const std::string netplay = ReadSource("netplay_hook.cpp");
  const size_t begin = netplay.find("bool BeginWhenBothReady(");
  const std::string start = netplay.substr(begin, netplay.find("bool LocalFightActive(", begin) - begin);
  CHECK(Has(start, "if (online_driven_ && phase_ == FightPhase::kIdle) return false;"));
  CHECK(Has(start, "return state == np::SessionState::kWaiting || state == np::SessionState::kConnecting ||"));
  CHECK(start.rfind("return true;") > start.find("session_->BeginFight(*live_sim_, now_ms)"));
  CHECK(Has(start, "np::DescribeStateHashes(view)"));
  CHECK(Has(netplay, "if (session_->state() != np::SessionState::kRunning) return BeginWhenBothReady(ctx, base, calls, now_ms);"));
  CHECK(Has(netplay, "FightRuntime<G>::Instance().FightInitReached();"));
  CHECK(Has(netplay, "  void ResetFight() {\n    live_sim_.reset();\n    EnterPhase(FightPhase::kIdle);\n"));
  for (const char* reset : {"    if (!fight_active) {\n      ResetFight();\n      return false;",
                            "    ResetFight();\n    session_->SetLocalReady(false",
                            "    frames_outside_fight_ = 0;\n    ResetFight();"}) {
    CHECK(Has(netplay, reset));
  }
  CHECK(Has(netplay, "takeover stopped reason=fight_ended checksums={} mismatches={}"));
}

// Real test 84b7d2f1: "netplay join failed reason=bad_peer_address" with no logged value. The raw address is now
// logged before rejection, and a match stuck on it for too long is left instead of blocking Invite Player forever.
void TestBadPeerAddressIsDiagnosedAndEventuallyLeft() {
  const std::string netplay = ReadSource("netplay_hook.cpp");
  CHECK(Has(netplay, "DEBUG_SAULO netplay join failed reason=bad_peer_address raw_address={}"));
  CHECK(Has(netplay, "const bool usable = np::UsableAddress(np::ParsePeerAddress(assignment->peer_ip, address));"));
  CHECK(Has(netplay, "plan.raw_peer_address = assignment->peer_ip;"));
  const size_t begin = netplay.find("if (plan.bad_address) {");
  const std::string give_up = netplay.substr(begin, netplay.find("Fault(plan, now_ms);\n      return false;", begin) - begin);
  CHECK(Has(give_up, "if (now_ms - bad_address_since_ms_ >= kBadAddressGiveUpMs) {"));
  CHECK(Has(give_up, "ssf2t::Online().LeaveMatch();"));
  CHECK(Has(netplay, "uint64_t bad_address_since_ms_ = 0;") && Has(netplay, "uint32_t bad_address_generation_ = 0;"));
  const size_t shutdown = netplay.find("void Shutdown(uint64_t now_ms, const char* reason) {");
  CHECK(Has(netplay.substr(shutdown, netplay.find("void LogTransitions(", shutdown) - shutdown),
            "bad_address_since_ms_ = 0;"));

  // One socket, bound right after Play with Online on (so Windows Firewall prompts then), carries every online match:
  // its STUN mapping is what the peer was told, so the fight borrows it and never binds its own or closes it.
  CHECK(Has(netplay, "np::OnlineGameSocket& shared = ssf2t::GameSocket();"));
  CHECK(Has(netplay, "if (plan.online || (shared.is_open() && shared.bound_port() == local_port)) {"));
  CHECK(Has(netplay, "transport_ = &shared;"));
  CHECK(Has(netplay, "np::Transport* transport_ = nullptr;"));
  CHECK(!Has(netplay, "TakeOnlineGameSocket"));
  CHECK(Has(netplay.substr(shutdown, netplay.find("void LogTransitions(", shutdown) - shutdown),
            "transport_ = nullptr;\n    own_transport_.reset();"));
  const std::string runtime = ReadSource("online_runtime.cpp");
  CHECK(Has(runtime, "if (settings.enabled) EnsureOnlineGameSocket();"));
  CHECK(Has(runtime, "g_game_socket.Open({kNetplayHostPort, kNetplayJoinPort})"));
  CHECK(Has(runtime, "DEBUG_SAULO udp socket bound local=0.0.0.0:{}"));
  CHECK(Has(runtime, "config.game_socket = &g_game_socket;"));
  CHECK(runtime.find("netplay::OnlineGameSocket g_game_socket;") < runtime.find("std::unique_ptr<online::OnlineService> g_service;"));
}

int main() {
  TestPlayerTableLayout();
  TestNames();
  TestLobbyErrorSitesPerImage();
  TestRepeatFilterAndBudget();
  TestSearchLogsChangesAndOneSummaryPerSecond();
  TestHexHead();
  TestLeaveCompletesInsteadOfFailing();
  TestLeavingTheLobbyEndsTheMatch();
  TestLobbyHooksCoverBothImages();
  TestLocalUsersAreRefreshedBeforeTheGate();
  TestInvitePlayerAndAcceptInviteHooks();
  TestOneInviteIsOneTransition();
  TestEachFightAndMatchStartsClean();
  TestBadPeerAddressIsDiagnosedAndEventuallyLeft();
  TestGuestSearchWaitsForTheServiceAnswer();
  TestInviteTransitionIsTracked();
  TestFrameZeroIsTheSameOnBothPeers();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
