#include <cstdint>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include "character/akuma.h"
#include "character/akuma_navigation.h"
#include "character/ultra_fighters.h"
#include "character/ultra_navigation.h"
#include "guest_memory.h"
#include "netplay/fight_snapshot.h"
#include "netplay/guest_netplay.h"
#include "netplay_setting.h"
#include "player_match_guest.h"
#include "title_update_image.h"

namespace ssf2t::character {
namespace {

struct Base013Move {
  static constexpr uint32_t kPad = 0x83CD52B7;
  static constexpr uint32_t kLive = 0x83CA6C9C;
  static constexpr uint32_t kSpecial = 0x83C133A8;
  static constexpr uint32_t kMode = 0x83CA294F;
  static constexpr uint32_t kHdStyle = 0x83A3876D;
};

struct TitleUpdate023Move {
  static constexpr uint32_t kPad = 0x83CD57CF;
  static constexpr uint32_t kLive = 0x83CA71B4;
  static constexpr uint32_t kSpecial = 0x83C13428;
  static constexpr uint32_t kMode = 0x83CA2E67;
  static constexpr uint32_t kHdStyle = 0x83A387E5;
};

constexpr uint32_t kSideOffset = 0x381;
constexpr uint8_t kUpBit = 0x8;
constexpr uint8_t kDownBit = 0x4;
constexpr uint8_t kLeftBit = 0x2;
constexpr uint8_t kRightBit = 0x1;

AkumaNavigation g_navigation;

struct PendingMove {
  bool valid = false;
  int side = 0;
  uint8_t live = 0;
  NavDirection direction = NavDirection::kUp;
  SelectLayout layout = SelectLayout::kArcade;
};

template <typename G>
bool OnlineSelect(const uint8_t* base) {
  return *GuestHost(base, G::kGameMode) == kOnlineGameMode;
}

template <typename G, typename S>
PendingMove ReadMove(const PPCContext& ctx, const uint8_t* base) {
  PendingMove move;
  const uint8_t pad = *GuestHost(base, S::kPad);
  const bool up = (pad & kUpBit) != 0;
  const bool down = !up && (pad & kDownBit) != 0;
  if (!AkumaEnabled() || ctx.r3.u32 != 0 || (!up && !down)) return move;
  if (*GuestHost(base, S::kHdStyle) == 0 || OnlineSelect<G>(base)) return move;
  const uint32_t work = LoadBE32(GuestHost(base, G::kFightWork + 4), 0);
  if (work < 0x10000 || work >= 0xFFFF0000u) return move;
  move.side = *GuestHost(base, work + kSideOffset);
  if (move.side < 0 || move.side > 1) return move;
  move.live = *GuestHost(base, S::kLive + move.side);
  move.direction = up ? NavDirection::kUp : NavDirection::kDown;
  move.layout = SelectLayoutForMode(*GuestHost(base, S::kMode));
  move.valid = true;
  return move;
}

template <typename G, typename S>
void NavigateAkuma(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  const PendingMove move = ReadMove<G, S>(ctx, base);
  original(ctx, base);
  uint8_t next = 0;
  if (!move.valid || !g_navigation.Resolve(true, move.layout, move.side, move.live, move.direction, next)) return;
  *(base + S::kLive + move.side) = next;
  *(base + S::kSpecial + move.side) = next == kAkumaSelect ? 1 : 0;
}

bool ReadUltraDirection(uint8_t pad, PadDirection& direction) {
  if ((pad & kUpBit) != 0) direction = PadDirection::kUp;
  else if ((pad & kDownBit) != 0) direction = PadDirection::kDown;
  else if ((pad & kLeftBit) != 0) direction = PadDirection::kLeft;
  else if ((pad & kRightBit) != 0) direction = PadDirection::kRight;
  else return false;
  return true;
}

// Evil Ryu is a host identity the rollback peers do not exchange, so he stays out of every online Character Select.
template <typename G>
bool NetplaySelect(const uint8_t* base) {
  return *GuestHost(base, G::kGameMode) == kOnlineGameMode ||
         rex::cvar::GetFlagByName(kNetplayRoleSetting) != kNetplayRoleOff;
}

// Returns true when the Evil Ryu cell consumed the move and the guest handler must not run.
template <typename G, typename S>
bool NavigateUltra(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (!UltraFightersEnabled() || ctx.r3.u32 != 0 || NetplaySelect<G>(base)) return false;
  PadDirection direction;
  if (!ReadUltraDirection(*GuestHost(base, S::kPad), direction)) return false;
  if (*GuestHost(base, S::kHdStyle) == 0) return false;
  const uint32_t work = LoadBE32(GuestHost(base, G::kFightWork + 4), 0);
  if (work < 0x10000 || work >= 0xFFFF0000u) return false;
  const int side = *GuestHost(base, work + kSideOffset);
  if (side < 0 || side > 1) return false;

  const UltraMove move = GlobalUltraNavigation().Resolve(true, side, *GuestHost(base, S::kLive + side), direction);
  if (move.action == UltraMoveAction::kNone) return false;
  REXLOG_INFO("DEBUG_SAULO EVIL_RYU select side={} action={}", side + 1,
              move.action == UltraMoveAction::kEnter ? "enter" : move.action == UltraMoveAction::kLeave ? "leave" : "hold");
  if (move.action == UltraMoveAction::kEnter) original(ctx, base);
  *(base + S::kLive + side) = move.live;
  *(base + S::kSpecial + side) = 0;
  return true;
}

template <typename G, typename S>
void NavigateSelect(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (NavigateUltra<G, S>(ctx, base, original)) return;
  NavigateAkuma<G, S>(ctx, base, original);
}

}  // namespace
}  // namespace ssf2t::character

REX_EXTERN(__imp__base013_sub_82243D78);
REX_HOOK_RAW(base013_sub_82243D78) {
  ssf2t::character::NavigateSelect<ssf2t::netplay::Base013Netplay, ssf2t::character::Base013Move>(
      ctx, base, __imp__base013_sub_82243D78);
}

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_82243DE0);
REX_HOOK_RAW(tu023_sub_82243DE0) {
  ssf2t::character::NavigateSelect<ssf2t::netplay::TitleUpdate023Netplay, ssf2t::character::TitleUpdate023Move>(
      ctx, base, __imp__tu023_sub_82243DE0);
}
#endif
