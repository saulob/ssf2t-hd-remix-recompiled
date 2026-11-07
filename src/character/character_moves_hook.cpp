#include <cstdint>

#include <rex/hook.h>

#include "character/character_moves.h"
#include "character/fighter_identity.h"
#include "character/ultra_fight.h"
#include "netplay/guest_netplay.h"
#include "title_update_image.h"

namespace ssf2t::character {
namespace {

// The script native behind GetPlayerCharacterIndex(side). An Ultra fighter fights as its carrier, so the screen reports
// the carrier's page unless the fighter on that side is the Ultra fighter.
template <typename G>
void PlayerCharacterIndex(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  const int side = int(ctx.r3.u32);
  original(ctx, base);
  if (!CharacterMovesPageReady() || !UltraFightersEnabled()) return;
  const bool evil = ctx.r3.u32 == uint32_t(kRyuGuestCharacter) && EvilRyuOnSide<G>(base, side);
  const bool violent = ctx.r3.u32 == uint32_t(kKenGuestCharacter) && ViolentKenOnSide<G>(base, side);
  if (!evil && !violent) return;
  ctx.r3.u64 = uint64_t(evil ? kEvilRyuMoveListIndex : kViolentKenMoveListIndex);
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
