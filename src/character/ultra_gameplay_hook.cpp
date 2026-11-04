#include <cstdint>

#include <rex/hook.h>
#include <rex/logging.h>

#include "character/ultra_fight.h"
#include "character/ultra_gameplay.h"
#include "guest_memory.h"
#include "netplay/fight_snapshot.h"
#include "netplay/guest_netplay.h"
#include "title_update_image.h"

namespace ssf2t::character {
namespace {

constexpr uint32_t kActiveFighterBelowWork = 4;
constexpr uint32_t kGateAddressBelowWork = 0x3C;
constexpr uint32_t kGateStrengthBelowWork = 0x14;
constexpr uint32_t kGateResultBelowWork = 0x34;

// The super start gate is shared by all fighters; refusing it leaves the move, the meter and the HUD untouched.
template <typename G>
void GateSuper(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (!UltraFightersEnabled()) return original(ctx, base);
  const uint32_t work = G::kFightWork;
  const uint32_t active = LoadBE32(GuestHost(base, work - kActiveFighterBelowWork), 0);
  const int side = EvilRyuSide<G>(base, active);
  if (side < 0 || CanStartShinkuHadoken(FighterIdentity::kEvilRyu)) return original(ctx, base);
  StoreBE32(base, work - kGateAddressBelowWork, 0);
  StoreBE32(base, work - kGateStrengthBelowWork, 1);
  StoreBE32(base, work - kGateResultBelowWork, 1);
  static bool logged[2];
  if (!logged[side]) {
    logged[side] = true;
    REXLOG_INFO("DEBUG_SAULO EVIL_RYU super-gate side={} refused", side + 1);
  }
}

}  // namespace
}  // namespace ssf2t::character

REX_EXTERN(__imp__base013_sub_8238D3A8);
REX_HOOK_RAW(base013_sub_8238D3A8) {
  ssf2t::character::GateSuper<ssf2t::netplay::Base013Netplay>(ctx, base, __imp__base013_sub_8238D3A8);
}

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_8238D398);
REX_HOOK_RAW(tu023_sub_8238D398) {
  ssf2t::character::GateSuper<ssf2t::netplay::TitleUpdate023Netplay>(ctx, base, __imp__tu023_sub_8238D398);
}
#endif
