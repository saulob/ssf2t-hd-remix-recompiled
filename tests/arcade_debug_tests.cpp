#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "character/fighter_identity.h"
#include "character/ultra_arcade_debug.h"

// DEBUG_SAULO: the temporary F1 Arcade final-boss shortcut, see ultra_arcade_debug.h.

namespace {

using namespace ssf2t::character;

int failures = 0;

void Check(bool condition, const char* what) {
  if (condition) return;
  std::printf("FAIL %s\n", what);
  ++failures;
}

// ---------------------------------------------------------------------------------------------------------------------
// The key handler and the guest's frame hook.

ArcadeStatus Status(bool ticking, ArcadeMode mode, bool ready) {
  ArcadeStatus status;
  status.ticking = ticking;
  status.mode = mode;
  status.ready = ready;
  return status;
}

void TestGating() {
  Check(HostGate(Status(true, ArcadeMode::kArcade, true)) == ArcadeReject::kNone, "F1 is taken in a live Arcade run");
  Check(HostGate(Status(false, ArcadeMode::kArcade, true)) == ArcadeReject::kMenu,
        "F1 is ignored where no game is running (menus)");
  Check(HostGate(Status(false, ArcadeMode::kOnline, true)) == ArcadeReject::kMenu, "no game loop is a menu in any mode");
  Check(HostGate(Status(true, ArcadeMode::kArcade, false)) == ArcadeReject::kNotReady,
        "F1 is ignored before the Arcade run has chosen its ladder");
  Check(HostGate(Status(true, ArcadeMode::kLocalMatch, true)) == ArcadeReject::kLocalMatch, "F1 is ignored in Local Match");
  Check(HostGate(Status(true, ArcadeMode::kOnline, true)) == ArcadeReject::kOnline, "F1 is ignored online");
  Check(HostGate(Status(true, ArcadeMode::kOther, true)) == ArcadeReject::kNotArcade, "F1 is ignored in other modes");
  Check(HostGate(Status(true, ArcadeMode::kOnline, false)) == ArcadeReject::kOnline, "online wins over a missing run");

  Check(ArcadeModeOf(0, false) == ArcadeMode::kArcade, "game mode 0 is Arcade");
  Check(ArcadeModeOf(1, false) == ArcadeMode::kLocalMatch, "game mode 1 is Local Match");
  Check(ArcadeModeOf(3, false) == ArcadeMode::kOnline, "game mode 3 is online");
  Check(ArcadeModeOf(2, false) == ArcadeMode::kOther, "game mode 2 is none of them");
  Check(ArcadeModeOf(0, true) == ArcadeMode::kOnline, "an online session role is online even on the Arcade byte");
  Check(ArcadeModeOf(1, true) == ArcadeMode::kOnline, "and on the Local Match byte");

  Check(std::strcmp(ArcadeRejectName(ArcadeReject::kNotArcade), "not-arcade") == 0, "the not-arcade reason");
  Check(std::strcmp(ArcadeRejectName(ArcadeReject::kOnline), "online") == 0, "the online reason");
  Check(std::strcmp(ArcadeRejectName(ArcadeReject::kLocalMatch), "local-match") == 0, "the local-match reason");
  Check(std::strcmp(ArcadeRejectName(ArcadeReject::kMenu), "menu") == 0, "the menu reason");
  Check(std::strcmp(ArcadeRejectName(ArcadeReject::kNotReady), "not-ready") == 0, "the not-ready reason");
  Check(std::strcmp(ArcadeRejectName(ArcadeReject::kAlreadyFinalBoss), "already-final-boss") == 0,
        "the already-final-boss reason");
}

void TestEdge() {
  Check(IsFirstPress(false), "the first key-down is a press");
  Check(!IsFirstPress(true), "a held key repeats key-down events and is not a press");

  ArcadeBridge bridge;
  Check(!bridge.Take(0), "nothing is waiting at first");
  Check(bridge.Post(100), "a press is posted");
  Check(!bridge.Post(120), "a second press while one waits is not posted");
  Check(bridge.Take(150), "the guest thread takes the request");
  Check(!bridge.Take(160), "and only once");
  Check(bridge.Post(1000), "a new press is posted after the first was taken");
  Check(!bridge.Take(1000 + kRequestWindowMs), "a request nobody took in time is dropped");
  Check(!bridge.Take(1000 + kRequestWindowMs + 1), "and stays dropped");

  Check(!bridge.Status(0).ticking, "no frame has run yet");
  bridge.Publish(5000, ArcadeMode::kArcade, true);
  Check(bridge.Status(5000 + kTickWindowMs - 1).ticking, "a recent frame means a game is running");
  Check(!bridge.Status(5000 + kTickWindowMs).ticking, "an old frame means the loop stopped");
  Check(bridge.Status(5000).mode == ArcadeMode::kArcade && bridge.Status(5000).ready, "the guest's view is published");
  bridge.Reset();
  Check(!bridge.Status(5001).ticking && !bridge.Status(5001).ready, "reset forgets the game");
}

// ---------------------------------------------------------------------------------------------------------------------
// The request from F1 to the ending.

LadderState Ladder(uint16_t index, uint8_t bonus = 0, bool chosen = true) {
  LadderState ladder;
  ladder.index = index;
  ladder.bonus_stages_done = bonus;
  ladder.chosen = chosen;
  return ladder;
}

struct RecordingWriter {
  std::vector<std::pair<ArcadeWrite, uint32_t>> writes;
  void Write(ArcadeWrite what, uint32_t value) { writes.emplace_back(what, value); }
};

FinalBossRequest ReadyRun() {
  FinalBossRequest request;
  request.OnLadderChosen(ArcadeMode::kArcade);
  return request;
}

void TestRequest() {
  FinalBossRequest fresh;
  Check(fresh.Request(ArcadeMode::kArcade, Ladder(2)) == ArcadeReject::kNotReady, "no ladder yet, F1 is not ready");
  Check(fresh.phase() == FinalBossPhase::kIdle, "and nothing waits");

  FinalBossRequest request = ReadyRun();
  Check(request.Request(ArcadeMode::kLocalMatch, Ladder(2)) == ArcadeReject::kLocalMatch, "Local Match is refused");
  Check(request.Request(ArcadeMode::kOnline, Ladder(2)) == ArcadeReject::kOnline, "online is refused");
  Check(request.Request(ArcadeMode::kOther, Ladder(2)) == ArcadeReject::kNotArcade, "other modes are refused");
  Check(request.Request(ArcadeMode::kArcade, Ladder(2, 0, false)) == ArcadeReject::kNotReady,
        "an unconfirmed fighter has no ladder");
  Check(request.Request(ArcadeMode::kArcade, Ladder(kLadderFinalBossIndex)) == ArcadeReject::kAlreadyFinalBoss,
        "at the final boss there is nothing to request");
  Check(request.Request(ArcadeMode::kArcade, Ladder(kLadderFinalBossIndex + 1)) == ArcadeReject::kAlreadyFinalBoss,
        "past the final boss neither");
  Check(request.phase() == FinalBossPhase::kIdle, "refusals leave nothing waiting");

  Check(request.Request(ArcadeMode::kArcade, Ladder(0)) == ArcadeReject::kNone, "a request in a live run waits");
  Check(request.phase() == FinalBossPhase::kPending, "for the next opponent");
  Check(request.Request(ArcadeMode::kArcade, Ladder(0)) == ArcadeReject::kPending, "a second request is not queued");

  // The game picks the next opponent: the ladder moves and nothing else.
  const FinalBossStep step = request.OnNextOpponent(ArcadeMode::kArcade, Ladder(4, 1));
  Check(step.kind == FinalBossStep::Kind::kApply, "the request is applied at the next opponent");
  Check(step.plan.ladder_index == kLadderFinalBossIndex && step.plan.ladder_index == 11, "to the last ladder slot");
  Check(step.plan.bonus_stages_done == kBonusStagesTotal, "with no bonus stage left before the ending");
  Check(step.plan.boss_only == 1 && step.plan.akuma_only == 0, "and the game's Bison-only code, not its Akuma-only code");
  Check(request.phase() == FinalBossPhase::kApplied, "the run is now on its way to the final boss");
  Check(request.OnNextOpponent(ArcadeMode::kArcade, Ladder(11)).kind == FinalBossStep::Kind::kNone,
        "the next picks are the guest's own");
  Check(request.OnNextOpponent(ArcadeMode::kArcade, Ladder(12)).kind == FinalBossStep::Kind::kNone,
        "including the one after M. Bison");
  Check(request.Request(ArcadeMode::kArcade, Ladder(11)) == ArcadeReject::kAlreadyFinalBoss,
        "F1 again on the way to the final boss has nothing to do");
  Check(request.OnEnding(), "the guest's ending is reached through the final-boss route");
  Check(request.ReleaseFlag(), "the Bison-only flag it set is given back");
  Check(!request.ReleaseFlag(), "once");
  Check(request.phase() == FinalBossPhase::kIdle && !request.Ready(Ladder(12)), "and the run is over");
  Check(!request.OnEnding(), "the route is reported once");
}

void TestNormalRunIsUntouched() {
  FinalBossRequest request = ReadyRun();
  for (uint16_t index = 0; index <= 13; ++index) {
    const FinalBossStep step = request.OnNextOpponent(ArcadeMode::kArcade, Ladder(index, 0));
    Check(step.kind == FinalBossStep::Kind::kNone, "without F1 the next opponent is never touched");
  }
  Check(request.phase() == FinalBossPhase::kIdle, "and no state appears");
  Check(!request.OnEnding(), "a normal ending is not reported as a shortcut");
  Check(!request.ReleaseFlag(), "and no flag was ever set");
  request.OnFrame(ArcadeMode::kArcade);
  Check(request.phase() == FinalBossPhase::kIdle, "frames change nothing");
}

void TestDropsAndResets() {
  FinalBossRequest request = ReadyRun();
  request.Request(ArcadeMode::kArcade, Ladder(2));
  FinalBossStep step = request.OnNextOpponent(ArcadeMode::kLocalMatch, Ladder(2));
  Check(step.kind == FinalBossStep::Kind::kDrop && step.reason == ArcadeReject::kLocalMatch,
        "a request is dropped when the game turned out to be Local Match");
  Check(request.phase() == FinalBossPhase::kIdle, "and forgotten");

  request = ReadyRun();
  request.Request(ArcadeMode::kArcade, Ladder(10));
  step = request.OnNextOpponent(ArcadeMode::kArcade, Ladder(11));
  Check(step.kind == FinalBossStep::Kind::kApply && step.plan.boss_only == 1,
        "a request made on the way to the last slot still keeps the Akuma rules off for it");

  request = ReadyRun();
  request.Request(ArcadeMode::kArcade, Ladder(10));
  step = request.OnNextOpponent(ArcadeMode::kArcade, Ladder(12));
  Check(step.kind == FinalBossStep::Kind::kDrop && step.reason == ArcadeReject::kAlreadyFinalBoss,
        "a request that the ladder passed is dropped, not applied to the ending");

  request = ReadyRun();
  request.Request(ArcadeMode::kArcade, Ladder(2));
  request.OnFrame(ArcadeMode::kLocalMatch);
  Check(request.phase() == FinalBossPhase::kIdle && !request.Ready(Ladder(2)), "leaving Arcade ends the run");
  request.OnFrame(ArcadeMode::kArcade);
  Check(request.Request(ArcadeMode::kArcade, Ladder(2)) == ArcadeReject::kNotReady, "a new run must choose its ladder");

  request = ReadyRun();
  request.Request(ArcadeMode::kArcade, Ladder(2));
  request.OnNextOpponent(ArcadeMode::kArcade, Ladder(3));
  request.OnLadderChosen(ArcadeMode::kArcade);
  Check(request.phase() == FinalBossPhase::kIdle, "a new ladder forgets the old run's shortcut");
  Check(!request.OnEnding(), "and its ending is not reported");
  Check(request.ReleaseFlag(), "but the flag it left is still given back");

  request.OnLadderChosen(ArcadeMode::kLocalMatch);
  Check(!request.Ready(Ladder(0)), "a Local Match ladder is not an Arcade run");
}

// ---------------------------------------------------------------------------------------------------------------------
// Identity: the shortcut writes the ladder and the two start-code flags and nothing a fighter or the ending reads.

void TestIdentityPreserved() {
  RecordingWriter writer;
  ApplyFinalBossPlan(writer, *PlanFinalBoss(Ladder(3, 0)));
  Check(writer.writes.size() == kPlanWrites.size(), "the plan makes exactly its listed writes");
  for (size_t i = 0; i < kPlanWrites.size() && i < writer.writes.size(); ++i) {
    Check(writer.writes[i].first == kPlanWrites[i], "in the listed order");
  }
  Check(writer.writes[0] == std::make_pair(ArcadeWrite::kLadderIndex, uint32_t(11)), "the ladder index is slot 11");
  Check(writer.writes[1] == std::make_pair(ArcadeWrite::kBonusStagesDone, uint32_t(3)), "the bonus stages are done");
  Check(writer.writes[2] == std::make_pair(ArcadeWrite::kBossOnlyFlag, uint32_t(1)), "Bison only is set");
  Check(writer.writes[3] == std::make_pair(ArcadeWrite::kAkumaOnlyFlag, uint32_t(0)), "Akuma only is clear");

  Check(PlanFinalBoss(Ladder(0, 5))->bonus_stages_done == 5, "a count above the schedule is never lowered");
  Check(!PlanFinalBoss(Ladder(12)), "no plan once the ladder is past the final boss");

  // The names in the log are the player's pick, whatever the guest plays it on.
  constexpr const char* kExpected[17] = {"Ryu",     "EHonda", "Blanka", "Guile",   "Ken",    "ChunLi",
                                         "Zangief", "Dhalsim", "Bison", "Sagat",   "Balrog", "Vega",
                                         "Cammy",   "THawk",  "FeiLong", "DeeJay", "Akuma"};
  for (int guest = 0; guest < 16; ++guest) {
    const FighterIdentity identity = ResolveFighterIdentity(guest, false, true, FighterIdentity::kGuest).identity;
    Check(std::strcmp(ArcadeFighterName(guest, false, identity), kExpected[guest]) == 0, "every original fighter");
  }
  Check(std::strcmp(ArcadeFighterName(0, true, FighterIdentity::kGuest), "Akuma") == 0, "Akuma is the hidden flag");
  Check(std::strcmp(ArcadeFighterName(4, true, FighterIdentity::kGuest), "Akuma") == 0, "on any carrier");

  const FighterIdentity evil = ResolveFighterIdentity(kRyuGuestCharacter, false, true, FighterIdentity::kEvilRyu).identity;
  const FighterIdentity violent =
      ResolveFighterIdentity(kKenGuestCharacter, false, true, FighterIdentity::kViolentKen).identity;
  Check(evil == FighterIdentity::kEvilRyu && violent == FighterIdentity::kViolentKen, "the carriers resolve to the picks");
  Check(std::strcmp(ArcadeFighterName(kRyuGuestCharacter, false, evil), "EvilRyu") == 0, "Evil Ryu is not Ryu");
  Check(std::strcmp(ArcadeFighterName(kKenGuestCharacter, false, violent), "ViolentKen") == 0, "Violent Ken is not Ken");
  Check(std::strcmp(ArcadeFighterName(kRyuGuestCharacter, false, FighterIdentity::kRyu), "Ryu") == 0, "Ryu stays Ryu");
  Check(std::strcmp(ArcadeFighterName(99, false, FighterIdentity::kGuest), "unknown") == 0, "a bad id is unknown");

  // Running the whole request leaves the requested identities and the resolved ones as they were.
  SetRequestedIdentity(0, FighterIdentity::kEvilRyu);
  SetRequestedIdentity(1, FighterIdentity::kViolentKen);
  FinalBossRequest request = ReadyRun();
  request.Request(ArcadeMode::kArcade, Ladder(5));
  request.OnNextOpponent(ArcadeMode::kArcade, Ladder(6));
  request.OnEnding();
  Check(RequestedIdentity(0) == FighterIdentity::kEvilRyu && RequestedIdentity(1) == FighterIdentity::kViolentKen,
        "the picks survive the shortcut and its ending");
  ResetIdentities();
}

// ---------------------------------------------------------------------------------------------------------------------
// The guest's Akuma rule, as sub_8237FAF0 and the second half of sub_8237ED98 read it (the image checks below hold the
// order of the tests in both builds).

struct AkumaInputs {
  bool arcade = true;
  bool final_boss_slot = true;   // the opponent id is M. Bison's
  bool endless = false;
  uint16_t ladder_index = kLadderFinalBossIndex;
  bool boss_only = false;
  bool akuma_only = false;
  bool challenge = false;
  bool player_flag = false;
  bool pending_award = false;
  bool fast_run = false;
  bool high_score = false;
};

bool AkumaReplacesBison(const AkumaInputs& in) {
  if (!in.arcade || !in.final_boss_slot) return false;
  if (in.endless) return in.ladder_index >= 0x10;
  if (in.akuma_only) return true;
  if (in.boss_only) return false;
  if (in.challenge) return in.player_flag && in.pending_award;
  return in.fast_run || in.high_score;
}

struct FlagWriter {
  AkumaInputs* inputs;
  void Write(ArcadeWrite what, uint32_t value) {
    if (what == ArcadeWrite::kBossOnlyFlag) inputs->boss_only = value != 0;
    if (what == ArcadeWrite::kAkumaOnlyFlag) inputs->akuma_only = value != 0;
    if (what == ArcadeWrite::kLadderIndex) inputs->ladder_index = uint16_t(value);
  }
};

void TestAkumaSuppressedOnlyForTheShortcut() {
  int replaced = 0;
  for (unsigned bits = 0; bits < 128; ++bits) {
    AkumaInputs in;
    in.endless = bits & 1;
    in.akuma_only = bits & 2;
    in.challenge = bits & 4;
    in.player_flag = bits & 8;
    in.pending_award = bits & 16;
    in.fast_run = bits & 32;
    in.high_score = bits & 64;
    in.boss_only = false;
    const bool normal = AkumaReplacesBison(in);
    replaced += normal;

    AkumaInputs shortcut = in;
    FlagWriter writer{&shortcut};
    ApplyFinalBossPlan(writer, *PlanFinalBoss(Ladder(3)));
    Check(!AkumaReplacesBison(shortcut), "after F1 the final boss is the normal M. Bison, whatever the Akuma rules say");

    AkumaInputs untouched = in;
    Check(AkumaReplacesBison(untouched) == normal, "without F1 the rules decide as before");
  }
  Check(replaced > 0, "the Akuma rules do replace M. Bison in a normal run");

  AkumaInputs akuma_code;
  akuma_code.akuma_only = true;
  Check(AkumaReplacesBison(akuma_code), "the game's own Akuma-only code still fights Akuma");
  FlagWriter writer{&akuma_code};
  ApplyFinalBossPlan(writer, *PlanFinalBoss(Ladder(0)));
  Check(!AkumaReplacesBison(akuma_code), "and F1 asks for the normal M. Bison instead");

  AkumaInputs other_opponent;
  other_opponent.final_boss_slot = false;
  other_opponent.fast_run = true;
  Check(!AkumaReplacesBison(other_opponent), "no other opponent is ever replaced");
}

// ---------------------------------------------------------------------------------------------------------------------
// The ending stays the guest's: the hook source never opens an ending or a screen itself.

std::string ReadText(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void TestEndingIsTheGuests() {
  const std::string root = SSF2T_SOURCE_DIR;
  const std::string hook = ReadText(root + "/src/character/ultra_arcade_debug_hook.cpp");
  const std::string keys = ReadText(root + "/src/character/ultra_arcade_debug_keys.cpp");
  Check(!hook.empty() && !keys.empty(), "the hook and key sources are readable");
  for (const char* forbidden : {"PlayEnding", "PostGame", "ultra_ending", "UltraEnding", "LoadScreen", "FinishCinematic",
                                "823CBE80", "823CC460", "82239D98", "82239EC0", "GuestText", "SendEvent", "SetWinner",
                                "kEndingCharacter", "83BE5904", "83BE5984", "Screen"}) {
    Check(hook.find(forbidden) == std::string::npos, forbidden);
    Check(keys.find(forbidden) == std::string::npos, forbidden);
  }
  // The only guest functions it replaces, each of which runs the original.
  size_t hooks = 0;
  for (size_t at = hook.find("REX_HOOK_RAW("); at != std::string::npos; at = hook.find("REX_HOOK_RAW(", at + 1)) ++hooks;
  Check(hooks == 8, "four functions in each build");
  size_t originals = 0;
  for (size_t at = hook.find("original(ctx, base);"); at != std::string::npos; at = hook.find("original(ctx, base);", at + 1)) {
    ++originals;
  }
  Check(originals >= 4, "each hook runs the guest's own function");
  const size_t ending = hook.find("void EndingChosen(");
  Check(ending != std::string::npos &&
            hook.find("original(ctx, base);", ending) < hook.find("OnEnding()", ending),
        "the ending is chosen by the guest before it is looked at");
  const size_t next = hook.find("void NextOpponent(");
  Check(next != std::string::npos && hook.find("ApplyFinalBossPlan", next) < hook.find("original(ctx, base);", next),
        "the ladder is set before the guest picks the next opponent");
  Check(hook.find("ReleaseBossOnly<H>(base);", hook.find("void EndingChosen(")) != std::string::npos,
        "the flag is given back after the ending is chosen");
  Check(hook.find("DEBUG_SAULO ARCADE final-boss-request fighter=") != std::string::npos &&
            hook.find("DEBUG_SAULO ARCADE final-boss-transition opponent=bison") != std::string::npos &&
            hook.find("DEBUG_SAULO ARCADE ending-route fighter=") != std::string::npos &&
            hook.find("DEBUG_SAULO ARCADE final-boss-request rejected reason=") != std::string::npos,
        "the four log lines");
  Check(keys.find("kF1") != std::string::npos && keys.find("prev_state") != std::string::npos &&
            keys.find("HasFocus") != std::string::npos,
        "F1 is edge-triggered and only while the window has focus");
}

// ---------------------------------------------------------------------------------------------------------------------
// Both real images.

enum Image { kBase013, kTitleUpdate023 };

constexpr uint32_t kImageBase = 0x82000000;

struct BuildFacts {
  const char* file;
  uint32_t frame, ending, ladder_init, next_opponent;
  uint32_t boss_only_flag, akuma_only_flag;
  uint32_t ladder_rows, selector_rows;
  uint32_t post_fight_step, post_fight_next_call, post_fight_end_test, post_fight_end_call;
  uint32_t clear_handler, clear_advance;
  uint32_t ladder_shortcut_index, ladder_shortcut_opponent, ladder_end_store;
  uint32_t akuma_due_akuma_only, akuma_due_boss_only, akuma_choice_bison;
  uint32_t ending_slot;
  uint32_t vs_call_site;
};

// Each build's addresses, read from that build alone.
constexpr BuildFacts kBuilds[] = {
    {"game/analysis/default-0.0.1.3.image.bin", 0x8223DD58, 0x8223A5A0, 0x82380C98, 0x823804A0, 0x83CA2906, 0x83CA0BBF,
     0x820E0EE0, 0x820E1EE0, 0x8223C09C, 0x8223C0D4, 0x8223C0FC, 0x8223C15C, 0x82239D98, 0x82239DC4, 0x823812AC,
     0x823812BC, 0x82381258, 0x8237FBC4, 0x8237FBDC, 0x8237F3F8, 0x83A2D028, 0x82241F70},
    {"game/analysis/default.image.bin", 0x8223DE80, 0x8223A6C8, 0x82380D68, 0x82380570, 0x83CA2E1E, 0x83CA10D7,
     0x820E1080, 0x820E2080, 0x8223C1C4, 0x8223C1FC, 0x8223C224, 0x8223C284, 0x82239EC0, 0x82239EEC, 0x8238137C,
     0x8238138C, 0x82381328, 0x8237FC94, 0x8237FCAC, 0x8237F4C8, 0x83A2D028, 0x82241FD8},
};

uint32_t Word(const std::vector<uint8_t>& image, uint32_t address) {
  const size_t at = address - kImageBase;
  return uint32_t(image[at]) << 24 | uint32_t(image[at + 1]) << 16 | uint32_t(image[at + 2]) << 8 | image[at + 3];
}

bool Calls(const std::vector<uint8_t>& image, uint32_t site, uint32_t target) {
  const uint32_t branch = Word(image, site);
  const int32_t reach = int32_t((branch & 0x03FFFFFC) << 6) >> 6;
  return (branch & 0xFC000003) == 0x48000001 && site + uint32_t(reach) == target;
}

void TestImage(Image which) {
  const BuildFacts& build = kBuilds[which];
  std::ifstream in(std::string(SSF2T_SOURCE_DIR) + "/" + build.file, std::ios::binary);
  if (!in) {
    std::printf("note: %s not found, image checks skipped\n", build.file);
    return;
  }
  const std::vector<uint8_t> image((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

  // The ladder: every row is eight opponents, then Balrog, Vega, Sagat, M. Bison, then the end marker, and the row
  // is picked by the fighter's own id, so M. Bison is slot 11 for every fighter including Evil Ryu's and Violent
  // Ken's carriers.
  bool rows_ok = true;
  for (uint32_t row = 0; row < 128; ++row) {
    const uint32_t at = build.ladder_rows + row * 32;
    // A halfword slot, read through the big-endian word that holds it.
    const auto slot_value = [&](uint32_t slot) {
      const uint32_t word = Word(image, at + (slot / 2) * 4);
      return uint16_t((slot & 1) ? word & 0xFFFF : word >> 16);
    };
    rows_ok = rows_ok && slot_value(8) == 10 && slot_value(9) == 11 && slot_value(10) == 9 &&
              slot_value(kLadderFinalBossIndex) == kLadderBisonId && slot_value(12) == kLadderEndId &&
              slot_value(13) == kLadderEndId && slot_value(14) == kLadderEndId && slot_value(15) == kLadderEndId;
  }
  Check(rows_ok, "every ladder row ends Balrog, Vega, Sagat, M. Bison and the end marker");
  bool selector_ok = true;
  for (uint32_t row = 0; row < 16; ++row) {
    for (uint32_t i = 0; i < 16; ++i) {
      selector_ok = selector_ok && image[build.selector_rows - kImageBase + row * 16 + i] == (i & 7);
    }
  }
  Check(selector_ok, "the ladder variant is picked by the fighter id and a random 0 to 7");

  constexpr uint32_t kBonusTable = 0x83A35564;
  const uint8_t bonus[8] = {3, 0x1D, 6, 0x1E, 9, 0x1F, 0xFF, 0};
  for (uint32_t i = 0; i < 8; ++i) Check(image[kBonusTable - kImageBase + i] == bonus[i], "three bonus stages, then none");

  // The game's own steps the shortcut leans on.
  Check(Word(image, build.post_fight_step) == 0xB16A0482, "a won fight advances the ladder index (sth 0x482)");
  Check(Calls(image, build.post_fight_next_call, build.next_opponent), "and then asks for the next opponent");
  Check(Word(image, build.post_fight_end_test) == 0x396BFFE0, "the end marker 0x20 is tested on the opponent word");
  Check(Calls(image, build.post_fight_end_call, build.clear_handler), "and ends the run through the guest's clear step");
  Check(Word(image, build.clear_advance) == 0xB16A0004, "which advances the state to the ending");
  Check(Word(image, build.ending_slot) == build.ending, "the state after it is the ending choice");
  Check(Calls(image, build.vs_call_site, build.next_opponent), "the next-opponent screen asks the same function");
  Check(Word(image, build.ladder_shortcut_index - 4) == 0x3940000B && Word(image, build.ladder_shortcut_index) == 0xB14B0482,
        "the game's Bison-only start sets the ladder index to 11");
  Check(Word(image, build.ladder_shortcut_opponent - 4) == 0x39400008 && Word(image, build.ladder_shortcut_opponent) == 0xB14B0480,
        "and the opponent to M. Bison (8)");
  Check(Word(image, build.ladder_end_store - 4) == 0x39400020 && Word(image, build.ladder_end_store) == 0xB14B0000,
        "after the sixteen slots the guest stores the end marker");

  // Akuma: the Akuma-only flag answers yes first, then the Bison-only flag answers no; the other half of the choice
  // makes the normal Bison.
  const uint32_t high = 0x3D6083CA;
  Check(Word(image, build.akuma_due_akuma_only - 4) == high && Word(image, build.akuma_due_akuma_only) == (0x896B0000u | (build.akuma_only_flag & 0xFFFF)) &&
            Word(image, build.akuma_due_akuma_only + 8) == 0x419A000C && Word(image, build.akuma_due_akuma_only + 12) == 0x38600001,
        "the Akuma-only flag makes the Akuma test answer yes");
  Check(Word(image, build.akuma_due_boss_only - 4) == high && Word(image, build.akuma_due_boss_only) == (0x896B0000u | (build.boss_only_flag & 0xFFFF)) &&
            Word(image, build.akuma_due_boss_only + 8) == 0x419A000C && Word(image, build.akuma_due_boss_only + 12) == 0x38600000,
        "the Bison-only flag makes the Akuma test answer no");
  Check(build.akuma_due_akuma_only < build.akuma_due_boss_only, "and it is asked second");
  Check(Word(image, build.akuma_choice_bison) == 0x39400008 && Word(image, build.akuma_choice_bison + 4) == 0x994B039D,
        "the fight setup gives the opponent M. Bison's id when no Akuma rule applies");

  for (uint32_t address : {build.frame, build.ending, build.ladder_init, build.next_opponent}) {
    Check(Word(image, address) == 0x7D8802A6 && Word(image, address + 4) == 0x9181FFF8, "each hooked function starts a frame");
  }
  Check(Word(image, build.frame + 20) == 0xA16B0000, "the frame hook is the dispatch on the top game state word");
}

}  // namespace

int main() {
  TestGating();
  TestEdge();
  TestRequest();
  TestNormalRunIsUntouched();
  TestDropsAndResets();
  TestIdentityPreserved();
  TestAkumaSuppressedOnlyForTheShortcut();
  TestEndingIsTheGuests();
  TestImage(kBase013);
  TestImage(kTitleUpdate023);
  if (failures == 0) std::printf("arcade final-boss shortcut tests passed\n");
  return failures == 0 ? 0 : 1;
}
