#include <array>
#include <chrono>
#include <cstdint>

#include <rex/hook.h>
#include <rex/logging.h>

#include "character/evil_ryu_moves.h"
#include "character/ultra_fighters.h"
#include "character/ultra_navigation.h"
#include "guest_memory.h"
#include "netplay/guest_netplay.h"
#include "title_update_image.h"

namespace ssf2t::character {
namespace {

constexpr uint32_t kWorkSideOffset = 897;
constexpr uint32_t kWorkLockedOffset = 899;

void TouchInteractiveSelect() {
  const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();
  if (!GlobalSelectSession().Touch(now)) return;
  GlobalUltraNavigation().Reset();
  GlobalHostMoves().Reset();
}

template <typename G>
void ObserveLock(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (UltraFightersEnabled()) TouchInteractiveSelect();
  original(ctx, base);
  if (!UltraFightersEnabled()) return;
  const uint32_t work = LoadBE32(GuestHost(base, G::kFightWork + 4), 0);
  if (work < 0x10000 || work >= 0xFFFF0000u) return;
  const int side = *GuestHost(base, work + kWorkSideOffset);
  if (side < 0 || side > 1) return;
  static std::array<bool, 2> locked{};
  const bool now = *GuestHost(base, work + kWorkLockedOffset) != 0;
  if (now == locked[side]) return;
  locked[side] = now;
  if (!GlobalUltraNavigation().OnEvilRyu(side)) return;
  if (now) {
    REXLOG_INFO("DEBUG_SAULO EVIL_RYU confirm side={} carrier=Ryu", side + 1);
  } else {
    REXLOG_INFO("DEBUG_SAULO EVIL_RYU unconfirm side={}", side + 1);
  }
}

}  // namespace
}  // namespace ssf2t::character

REX_EXTERN(__imp__base013_sub_82246D38);
REX_HOOK_RAW(base013_sub_82246D38) {
  ssf2t::character::ObserveLock<ssf2t::netplay::Base013Netplay>(ctx, base, __imp__base013_sub_82246D38);
}

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_82246D80);
REX_HOOK_RAW(tu023_sub_82246D80) {
  ssf2t::character::ObserveLock<ssf2t::netplay::TitleUpdate023Netplay>(ctx, base, __imp__tu023_sub_82246D80);
}
#endif
