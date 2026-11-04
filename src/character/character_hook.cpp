#include <cstdint>

#include <rex/hook.h>
#include <rex/logging.h>

#include "character/evil_ryu_moves.h"
#include "character/fighter_identity.h"
#include "character/ultra_fighters.h"
#include "guest_memory.h"
#include "netplay/fight_snapshot.h"
#include "netplay/guest_netplay.h"
#include "title_update_image.h"

namespace {

using ssf2t::character::FighterIdentity;
using ssf2t::character::FighterIdentityInfo;

bool ReadFighter(uint8_t* base, uint32_t block, int side, FighterIdentityInfo* info) {
  if (block < 0x10000 || block >= 0xFFFF0000u) return false;
  const uint32_t fighter = block + ssf2t::netplay::kFighterOffsets[side];
  const int guest = int(int8_t(*ssf2t::GuestHost(base, fighter + ssf2t::netplay::kCharacterOffset)));
  const bool hidden = *ssf2t::GuestHost(base, fighter + ssf2t::netplay::kHiddenCharacterOffset) != 0;
  *info = ssf2t::character::ResolveFighterIdentity(guest, hidden, ssf2t::character::UltraFightersEnabled(),
                                                   ssf2t::character::RequestedIdentity(side));
  return true;
}

template <typename G>
void ObserveCharacterSetup(uint8_t* base) {
  const uint32_t block = ssf2t::LoadBE32(ssf2t::GuestHost(base, G::kFighterBlockPointer), 0);
  if (block < 0x10000 || block >= 0xFFFF0000u) {
    ssf2t::character::ResetFightIdentities();
    return;
  }
  ssf2t::character::GlobalHostMoves().Reset();
  for (int side = 0; side < 2; ++side) {
    FighterIdentityInfo info;
    if (!ReadFighter(base, block, side, &info)) continue;
    const FighterIdentity previous = ssf2t::character::IdentityForSide(side).identity;
    ssf2t::character::SetIdentityForSide(side, info);
    if (info.identity == previous) continue;
    REXLOG_INFO("character: side={} identity={} guest={} hidden={} ultra={}", side + 1,
                ssf2t::character::IdentityName(info.identity), info.guest_character, info.hidden ? 1 : 0,
                info.ultra_fighters ? 1 : 0);
    if (info.identity == FighterIdentity::kEvilRyu) {
      REXLOG_INFO("DEBUG_SAULO EVIL_RYU fight-setup side={} identity=EvilRyu carrier=Ryu", side + 1);
    }
  }
}

}  // namespace

REX_EXTERN(__imp__base013_sub_823C6FC0);
REX_HOOK_RAW(base013_sub_823C6FC0) {
  __imp__base013_sub_823C6FC0(ctx, base);
  ObserveCharacterSetup<ssf2t::netplay::Base013Netplay>(base);
}

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_823C75B8);
REX_HOOK_RAW(tu023_sub_823C75B8) {
  __imp__tu023_sub_823C75B8(ctx, base);
  ObserveCharacterSetup<ssf2t::netplay::TitleUpdate023Netplay>(base);
}
#endif
