#include <array>
#include <chrono>
#include <cstdint>

#include <rex/hook.h>
#include <rex/logging.h>

#include "character/evil_ryu_moves.h"
#include "character/secret_select.h"
#include "character/select_activity.h"
#include "character/ultra_fighters.h"
#include "character/ultra_navigation.h"
#include "character/versus_cpu.h"
#include "guest_memory.h"
#include "netplay/fight_snapshot.h"
#include "netplay/guest_netplay.h"
#include "title_update_image.h"

namespace ssf2t::character {
namespace {

struct Base013Confirm {
  static constexpr uint32_t kHdStyle = 0x83A3876D;
  static constexpr uint32_t kOldVersionPrompt = 0x83CA6C76;
  static constexpr uint32_t kGameMode = 0x83CA294F;
};

struct TitleUpdate023Confirm {
  static constexpr uint32_t kHdStyle = 0x83A387E5;
  static constexpr uint32_t kOldVersionPrompt = 0x83CA718E;
  static constexpr uint32_t kGameMode = 0x83CA2E67;
};

constexpr uint32_t kWorkSideOffset = 897;
constexpr uint32_t kWorkLockedOffset = 899;
constexpr uint32_t kWorkClassicOffset = 950;
constexpr uint32_t kFighterSelectingOffset = 898;
constexpr uint32_t kFighterPlayedOffset = 942;

template <typename G>
SelectEntry ReadSelectEntry(const uint8_t* base) {
  const uint8_t game_mode = *GuestHost(base, G::kGameMode);
  const uint8_t joined = CharacterSelectJoinedSides();
  const uint32_t block = LoadBE32(GuestHost(base, G::kFighterBlockPointer), 0);
  SelectEntry entry;
  if (block < 0x10000 || block >= 0xFFFF0000u) return entry;
  const SecretCells cells = SecretCellsFor(game_mode);
  entry.arcade = game_mode == kArcadeGameMode;
  entry.evil_ryu_available = cells.evil_ryu;
  entry.violent_ken_available = cells.violent_ken;
  for (int side = 0; side < 2; ++side) {
    const uint32_t fighter = block + netplay::kFighterOffsets[side];
    SelectEntrySide& out = entry.sides[size_t(side)];
    out.joined = (joined & (1u << side)) != 0;
    out.played = *GuestHost(base, fighter + kFighterSelectingOffset) != 0 ||
                 *GuestHost(base, fighter + kFighterPlayedOffset) != 0;
    out.guest_character = int(int8_t(*GuestHost(base, fighter + netplay::kCharacterOffset)));
    out.hidden = *GuestHost(base, fighter + netplay::kHiddenCharacterOffset) != 0;
  }
  return entry;
}

template <typename G>
void TouchInteractiveSelect(const uint8_t* base) {
  const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();
  if (!GlobalSelectSession().Touch(now)) return;
  const SelectEntry entry = ReadSelectEntry<G>(base);
  const uint8_t kept = UltraMaskKeptByDefender(GlobalUltraNavigation().Mask(), entry);
  GlobalUltraNavigation().Restore(kept);
  GlobalHostMoves().Reset();
  if (kept == 0) return;
  for (int side = 0; side < 2; ++side) {
    if (!GlobalUltraNavigation().OnUltraCell(side)) continue;
    REXLOG_INFO("DEBUG_SAULO ULTRA reenter-select side={} guest={} restored={} reason=local_join", side + 1,
                entry.sides[size_t(side)].guest_character, IdentityName(RequestedIdentity(side)));
  }
}

// Both guest routes to the classic flag run inside the hooked function, so this sees them on the frame they happen.
void GuardClassic(uint8_t* base, uint32_t work, int side, uint8_t game_mode, uint8_t hd_style) {
  if (!UltraDropsOldVersion(GlobalUltraNavigation().OnUltraCell(side), *GuestHost(base, work + kWorkClassicOffset))) {
    return;
  }
  REXLOG_INFO("DEBUG_SAULO ULTRA classic guard side={} mode={} style={}", side + 1, GameModeName(game_mode),
              HdStyleName(hd_style));
  GuestStore8(base, work + kWorkClassicOffset, 0);
}

template <typename G, typename S>
void ObserveLock(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  const bool online = OnlineSelectMode(*GuestHost(base, G::kGameMode));
  if (UltraFightersEnabled() && !online) TouchInteractiveSelect<G>(base);
  original(ctx, base);
  if (!UltraFightersEnabled()) return;
  const uint32_t work = LoadBE32(GuestHost(base, G::kFightWork + 4), 0);
  if (work < 0x10000 || work >= 0xFFFF0000u) return;
  const int side = *GuestHost(base, work + kWorkSideOffset);
  if (side < 0 || side > 1) return;
  GuardClassic(base, work, side, *GuestHost(base, G::kGameMode), *GuestHost(base, S::kHdStyle));
  static std::array<bool, 2> locked{};
  const bool now = *GuestHost(base, work + kWorkLockedOffset) != 0;
  if (now == locked[side]) return;
  locked[side] = now;
  if (!GlobalUltraNavigation().OnUltraCell(side)) return;
  REXLOG_INFO("DEBUG_SAULO ULTRA {} side={} identity={}", now ? "confirm" : "unconfirm", side + 1,
              IdentityName(RequestedIdentity(side)));
}

// Outside Arcade a locked side answers NORMAL / SUPER, and NORMAL is the old version. The option byte is 0 in Remix.
// The guest's own CPU options refuse the old version, so a CPU side is not asked.
template <typename S>
void AskLockedSide(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  const int side = int(ctx.r3.u32);
  const bool skip = GlobalUltraNavigation().OnUltraCell(side) || GlobalVersusCpu().SideIn(base[S::kGameMode]) == side;
  const uint8_t option = *GuestHost(base, S::kOldVersionPrompt);
  if (skip) *(base + S::kOldVersionPrompt) = 0;
  original(ctx, base);
  if (skip) *(base + S::kOldVersionPrompt) = option;
}

}  // namespace
}  // namespace ssf2t::character

REX_EXTERN(__imp__base013_sub_82246820);
REX_HOOK_RAW(base013_sub_82246820) {
  ssf2t::character::AskLockedSide<ssf2t::character::Base013Confirm>(ctx, base, __imp__base013_sub_82246820);
}

REX_EXTERN(__imp__base013_sub_82246D38);
REX_HOOK_RAW(base013_sub_82246D38) {
  ssf2t::character::ObserveLock<ssf2t::netplay::Base013Netplay, ssf2t::character::Base013Confirm>(
      ctx, base, __imp__base013_sub_82246D38);
}

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_82246868);
REX_HOOK_RAW(tu023_sub_82246868) {
  ssf2t::character::AskLockedSide<ssf2t::character::TitleUpdate023Confirm>(ctx, base, __imp__tu023_sub_82246868);
}

REX_EXTERN(__imp__tu023_sub_82246D80);
REX_HOOK_RAW(tu023_sub_82246D80) {
  ssf2t::character::ObserveLock<ssf2t::netplay::TitleUpdate023Netplay, ssf2t::character::TitleUpdate023Confirm>(
      ctx, base, __imp__tu023_sub_82246D80);
}
#endif
