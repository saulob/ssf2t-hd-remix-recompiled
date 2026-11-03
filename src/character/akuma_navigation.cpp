#include <cstdint>

#include <rex/hook.h>

#include "character/akuma.h"
#include "character/akuma_navigation.h"
#include "guest_memory.h"
#include "netplay/fight_snapshot.h"
#include "netplay/guest_netplay.h"
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

AkumaNavigation g_navigation;

struct PendingMove {
  bool valid = false;
  int side = 0;
  uint8_t live = 0;
  NavDirection direction = NavDirection::kUp;
};

template <typename G, typename S>
PendingMove ReadMove(const PPCContext& ctx, const uint8_t* base) {
  PendingMove move;
  const uint8_t pad = *GuestHost(base, S::kPad);
  const bool up = (pad & kUpBit) != 0;
  const bool down = !up && (pad & kDownBit) != 0;
  if (!AkumaEnabled() || ctx.r3.u32 != 0 || (!up && !down)) return move;
  if (*GuestHost(base, S::kHdStyle) == 0 || *GuestHost(base, S::kMode) != 0) return move;
  const uint32_t work = LoadBE32(GuestHost(base, G::kFightWork + 4), 0);
  if (work < 0x10000 || work >= 0xFFFF0000u) return move;
  move.side = *GuestHost(base, work + kSideOffset);
  if (move.side < 0 || move.side > 1) return move;
  move.live = *GuestHost(base, S::kLive + move.side);
  move.direction = up ? NavDirection::kUp : NavDirection::kDown;
  move.valid = true;
  return move;
}

template <typename G, typename S>
void NavigateAkuma(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  const PendingMove move = ReadMove<G, S>(ctx, base);
  original(ctx, base);
  uint8_t next = 0;
  if (!move.valid || !g_navigation.Resolve(true, move.side, move.live, move.direction, next)) return;
  *(base + S::kLive + move.side) = next;
  *(base + S::kSpecial + move.side) = next == kAkumaSelect ? 1 : 0;
}

}  // namespace
}  // namespace ssf2t::character

REX_EXTERN(__imp__base013_sub_82243D78);
REX_HOOK_RAW(base013_sub_82243D78) {
  ssf2t::character::NavigateAkuma<ssf2t::netplay::Base013Netplay, ssf2t::character::Base013Move>(
      ctx, base, __imp__base013_sub_82243D78);
}

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_82243DE0);
REX_HOOK_RAW(tu023_sub_82243DE0) {
  ssf2t::character::NavigateAkuma<ssf2t::netplay::TitleUpdate023Netplay, ssf2t::character::TitleUpdate023Move>(
      ctx, base, __imp__tu023_sub_82243DE0);
}
#endif
