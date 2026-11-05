#include <cstdint>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include "character/fighter_identity.h"
#include "character/ultra_arcade_debug.h"
#include "character/ultra_fighters.h"
#include "character/ultra_hook_table.h"
#include "guest_memory.h"
#include "netplay/fight_snapshot.h"
#include "netplay/guest_netplay.h"
#include "netplay_setting.h"
#include "title_update_image.h"

// DEBUG_SAULO: temporary F1 Arcade final-boss shortcut, see ultra_arcade_debug.h. The guest keeps its own flow: the next
// opponent, the fight, the winner and the ending are all its code; the hooks only read its state and set the ladder
// where the game's own "Bison only" start code sets it.

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_82380570);
REX_EXTERN(__imp__tu023_sub_82380D68);
REX_EXTERN(__imp__tu023_sub_8223DE80);
REX_EXTERN(__imp__tu023_sub_8223A6C8);
#endif
REX_EXTERN(__imp__base013_sub_823804A0);
REX_EXTERN(__imp__base013_sub_82380C98);
REX_EXTERN(__imp__base013_sub_8223DD58);
REX_EXTERN(__imp__base013_sub_8223A5A0);

namespace ssf2t::character {
namespace {

struct Base013Arcade {
  using Netplay = netplay::Base013Netplay;
  using Hooks = Base013UltraHooks;
};

#if SSF2T_TITLE_UPDATE_IMAGE
struct TitleUpdate023Arcade {
  using Netplay = netplay::TitleUpdate023Netplay;
  using Hooks = TitleUpdate023UltraHooks;
};
#endif

// Fields of the guest's game state (the object the fight work pointer names), the same in both images.
constexpr uint32_t kLadderIndexField = 0x482;
constexpr uint32_t kChosenField = 0x492;
constexpr uint32_t kBonusStagesDoneField = 0x4B4;
constexpr uint32_t kHumanFlags[2] = {0x382, 0x3AE};
constexpr uint32_t kFightWorkMin = 0x10000;

template <typename H>
uint32_t GameState(const uint8_t* base) {
  const uint32_t work = LoadBE32(GuestHost(base, H::Netplay::kFightWork), 0);
  return work >= kFightWorkMin && work < 0xFFFF0000u ? work : 0;
}

template <typename H>
ArcadeMode CurrentMode(const uint8_t* base) {
  const bool online_session = rex::cvar::GetFlagByName(kNetplayRoleSetting) != kNetplayRoleOff;
  return ArcadeModeOf(*GuestHost(base, H::Netplay::kGameMode), online_session);
}

template <typename H>
LadderState ReadLadder(const uint8_t* base, uint32_t state) {
  LadderState ladder;
  ladder.index = LoadBE16(GuestHost(base, state + kLadderIndexField), 0);
  ladder.bonus_stages_done = *GuestHost(base, state + kBonusStagesDoneField);
  ladder.chosen = *GuestHost(base, state + kChosenField) != 0;
  return ladder;
}

// The player's side: the one with a controller. Two players on the same Arcade run read as Player 1.
template <typename H>
const char* PlayerName(const uint8_t* base) {
  const uint32_t block = LoadBE32(GuestHost(base, H::Netplay::kFighterBlockPointer), 0);
  if (block < kFightWorkMin || block >= 0xFFFF0000u) return "unknown";
  int side = 0;
  const uint32_t first = block + netplay::kFighterOffsets[0];
  if (*GuestHost(base, first + kHumanFlags[0]) == 0 && *GuestHost(base, first + kHumanFlags[1]) == 0) side = 1;
  const uint32_t fighter = block + netplay::kFighterOffsets[side];
  const int guest = int(int8_t(*GuestHost(base, fighter + netplay::kCharacterOffset)));
  const bool hidden = *GuestHost(base, fighter + netplay::kHiddenCharacterOffset) != 0;
  const FighterIdentity identity =
      ResolveFighterIdentity(guest, hidden, UltraFightersEnabled(), RequestedIdentity(side)).identity;
  return ArcadeFighterName(guest, hidden, identity);
}

template <typename H>
struct GuestWriter {
  uint8_t* base;
  uint32_t state;

  void Write(ArcadeWrite what, uint32_t value) {
    switch (what) {
      case ArcadeWrite::kLadderIndex: GuestStoreBE16(base, state + kLadderIndexField, uint16_t(value)); break;
      case ArcadeWrite::kBonusStagesDone: GuestStore8(base, state + kBonusStagesDoneField, uint8_t(value)); break;
      case ArcadeWrite::kBossOnlyFlag: GuestStore8(base, H::Hooks::kArcadeBossOnlyFlag, uint8_t(value)); break;
      case ArcadeWrite::kAkumaOnlyFlag: GuestStore8(base, H::Hooks::kArcadeAkumaOnlyFlag, uint8_t(value)); break;
    }
  }
};

void LogRejected(ArcadeReject reason) {
  REXLOG_INFO("DEBUG_SAULO ARCADE final-boss-request rejected reason={}", ArcadeRejectName(reason));
}

// The Bison-only flag is the game's own start-code state; the shortcut gives it back when its run is over.
template <typename H>
void ReleaseBossOnly(uint8_t* base) {
  if (GlobalFinalBossRequest().ReleaseFlag()) GuestStore8(base, H::Hooks::kArcadeBossOnlyFlag, 0);
}

// Every sim frame: tells the key handler what the game is doing and turns its press into a waiting request.
template <typename H>
void Frame(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  FinalBossRequest& request = GlobalFinalBossRequest();
  const ArcadeMode mode = CurrentMode<H>(base);
  request.OnFrame(mode);
  if (mode != ArcadeMode::kArcade) ReleaseBossOnly<H>(base);
  const uint32_t state = GameState<H>(base);
  const LadderState ladder = state ? ReadLadder<H>(base, state) : LadderState{};
  const int64_t now = ArcadeNowMs();
  GlobalArcadeBridge().Publish(now, mode, state != 0 && request.Ready(ladder));
  if (state && GlobalArcadeBridge().Take(now)) {
    const ArcadeReject reason = request.Request(mode, ladder);
    if (reason == ArcadeReject::kNone) {
      REXLOG_INFO("DEBUG_SAULO ARCADE final-boss-request fighter={}", PlayerName<H>(base));
    } else {
      LogRejected(reason);
    }
  }
  original(ctx, base);
}

// The game is about to choose the next opponent, after a fight or when it shows the next one.
template <typename H>
void NextOpponent(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  FinalBossRequest& request = GlobalFinalBossRequest();
  const uint32_t state = GameState<H>(base);
  if (state && request.phase() == FinalBossPhase::kPending) {
    const FinalBossStep step = request.OnNextOpponent(CurrentMode<H>(base), ReadLadder<H>(base, state));
    if (step.kind == FinalBossStep::Kind::kApply) {
      GuestWriter<H> writer{base, state};
      ApplyFinalBossPlan(writer, step.plan);
      REXLOG_INFO("DEBUG_SAULO ARCADE final-boss-transition opponent=bison");
    } else if (step.kind == FinalBossStep::Kind::kDrop) {
      LogRejected(step.reason);
    }
  }
  original(ctx, base);
}

// The player confirmed a fighter and the guest built this run's ladder.
template <typename H>
void LadderChosen(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  original(ctx, base);
  ReleaseBossOnly<H>(base);
  GlobalFinalBossRequest().OnLadderChosen(CurrentMode<H>(base));
}

// The guest chose the ending for the winner; the ending itself plays later through the guest's own cinematic.
template <typename H>
void EndingChosen(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  original(ctx, base);
  if (GlobalFinalBossRequest().OnEnding()) {
    REXLOG_INFO("DEBUG_SAULO ARCADE ending-route fighter={}", PlayerName<H>(base));
  }
  ReleaseBossOnly<H>(base);
}

}  // namespace
}  // namespace ssf2t::character

REX_HOOK_RAW(base013_sub_8223DD58) {
  ssf2t::character::Frame<ssf2t::character::Base013Arcade>(ctx, base, __imp__base013_sub_8223DD58);
}

REX_HOOK_RAW(base013_sub_823804A0) {
  ssf2t::character::NextOpponent<ssf2t::character::Base013Arcade>(ctx, base, __imp__base013_sub_823804A0);
}

REX_HOOK_RAW(base013_sub_82380C98) {
  ssf2t::character::LadderChosen<ssf2t::character::Base013Arcade>(ctx, base, __imp__base013_sub_82380C98);
}

REX_HOOK_RAW(base013_sub_8223A5A0) {
  ssf2t::character::EndingChosen<ssf2t::character::Base013Arcade>(ctx, base, __imp__base013_sub_8223A5A0);
}

#if SSF2T_TITLE_UPDATE_IMAGE
REX_HOOK_RAW(tu023_sub_8223DE80) {
  ssf2t::character::Frame<ssf2t::character::TitleUpdate023Arcade>(ctx, base, __imp__tu023_sub_8223DE80);
}

REX_HOOK_RAW(tu023_sub_82380570) {
  ssf2t::character::NextOpponent<ssf2t::character::TitleUpdate023Arcade>(ctx, base, __imp__tu023_sub_82380570);
}

REX_HOOK_RAW(tu023_sub_82380D68) {
  ssf2t::character::LadderChosen<ssf2t::character::TitleUpdate023Arcade>(ctx, base, __imp__tu023_sub_82380D68);
}

REX_HOOK_RAW(tu023_sub_8223A6C8) {
  ssf2t::character::EndingChosen<ssf2t::character::TitleUpdate023Arcade>(ctx, base, __imp__tu023_sub_8223A6C8);
}
#endif
