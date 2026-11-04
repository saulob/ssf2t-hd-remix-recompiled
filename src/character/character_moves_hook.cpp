#include <array>
#include <cstdint>

#include <rex/hook.h>
#include <rex/logging.h>

#include "character/character_moves.h"
#include "character/fighter_identity.h"
#include "character/ultra_fight.h"
#include "netplay/guest_netplay.h"
#include "title_update_image.h"

namespace ssf2t::character {
namespace {

// The script native behind GetPlayerCharacterIndex(side). Evil Ryu fights as Ryu, so the screen reports Ryu's page
// unless the fighter on that side is Evil Ryu.
template <typename G>
void PlayerCharacterIndex(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  const int side = int(ctx.r3.u32);
  original(ctx, base);
  if (!CharacterMovesPageReady() || !UltraFightersEnabled()) return;
  if (ctx.r3.u32 != uint32_t(kRyuGuestCharacter) || !EvilRyuOnSide<G>(base, side)) return;
  ctx.r3.u64 = uint64_t(kEvilRyuMoveListIndex);
  static std::array<bool, 2> logged{};
  if (!logged[side]) {
    logged[side] = true;
    REXLOG_INFO("DEBUG_SAULO EVIL_RYU character-moves side={} page=evil-ryu", side + 1);
  }
}

}  // namespace
}  // namespace ssf2t::character

REX_EXTERN(__imp__base013_sub_823C8E80);
REX_HOOK_RAW(base013_sub_823C8E80) {
  ssf2t::character::PlayerCharacterIndex<ssf2t::netplay::Base013Netplay>(ctx, base, __imp__base013_sub_823C8E80);
}

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_823C9478);
REX_HOOK_RAW(tu023_sub_823C9478) {
  ssf2t::character::PlayerCharacterIndex<ssf2t::netplay::TitleUpdate023Netplay>(ctx, base, __imp__tu023_sub_823C9478);
}
#endif
