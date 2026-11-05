#include <array>
#include <chrono>
#include <cstdint>

#include <rex/hook.h>
#include <rex/logging.h>

#include "character/evil_ryu_moves.h"
#include "character/secret_select.h"
#include "character/ultra_fighters.h"
#include "character/ultra_navigation.h"
#include "guest_memory.h"
#include "netplay/guest_netplay.h"
#include "title_update_image.h"

namespace ssf2t::character {
namespace {

struct Base013Confirm {
  static constexpr uint32_t kHdStyle = 0x83A3876D;
};

struct TitleUpdate023Confirm {
  static constexpr uint32_t kHdStyle = 0x83A387E5;
};

constexpr uint32_t kWorkSideOffset = 897;
constexpr uint32_t kWorkLockedOffset = 899;
constexpr uint32_t kWorkClassicOffset = 950;

void TouchInteractiveSelect() {
  const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();
  if (!GlobalSelectSession().Touch(now)) return;
  GlobalUltraNavigation().Reset();
  GlobalHostMoves().Reset();
}

// Both guest routes to the classic flag run inside the hooked function, so this sees them on the frame they happen.
void GuardClassic(uint8_t* base, uint32_t work, int side, uint8_t hd_style) {
  const UltraClassicGuard guard =
      GuardUltraClassic(hd_style, GlobalUltraNavigation().OnUltraCell(side), *GuestHost(base, work + kWorkClassicOffset));
  if (guard == UltraClassicGuard::kNone) return;
  REXLOG_INFO("DEBUG_SAULO ULTRA classic guard side={} action={}", side + 1, int(guard));
  if (guard == UltraClassicGuard::kLeaveCell) GlobalUltraNavigation().Leave(side);
  if (guard == UltraClassicGuard::kClearClassic) GuestStore8(base, work + kWorkClassicOffset, 0);
}

template <typename G, typename S>
void ObserveLock(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  const bool online = OnlineSelectMode(*GuestHost(base, G::kGameMode));
  if (UltraFightersEnabled() && !online) TouchInteractiveSelect();
  original(ctx, base);
  if (!UltraFightersEnabled()) return;
  const uint32_t work = LoadBE32(GuestHost(base, G::kFightWork + 4), 0);
  if (work < 0x10000 || work >= 0xFFFF0000u) return;
  const int side = *GuestHost(base, work + kWorkSideOffset);
  if (side < 0 || side > 1) return;
  GuardClassic(base, work, side, *GuestHost(base, S::kHdStyle));
  static std::array<bool, 2> locked{};
  const bool now = *GuestHost(base, work + kWorkLockedOffset) != 0;
  if (now == locked[side]) return;
  locked[side] = now;
  if (!GlobalUltraNavigation().OnUltraCell(side)) return;
  REXLOG_INFO("DEBUG_SAULO ULTRA {} side={} identity={}", now ? "confirm" : "unconfirm", side + 1,
              IdentityName(RequestedIdentity(side)));
}

}  // namespace
}  // namespace ssf2t::character

REX_EXTERN(__imp__base013_sub_82246D38);
REX_HOOK_RAW(base013_sub_82246D38) {
  ssf2t::character::ObserveLock<ssf2t::netplay::Base013Netplay, ssf2t::character::Base013Confirm>(
      ctx, base, __imp__base013_sub_82246D38);
}

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_82246D80);
REX_HOOK_RAW(tu023_sub_82246D80) {
  ssf2t::character::ObserveLock<ssf2t::netplay::TitleUpdate023Netplay, ssf2t::character::TitleUpdate023Confirm>(
      ctx, base, __imp__tu023_sub_82246D80);
}
#endif
