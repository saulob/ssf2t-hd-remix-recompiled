#pragma once

#include <cstdint>

#include "character/fighter_identity.h"
#include "character/ultra_fighters.h"
#include "guest_memory.h"
#include "netplay/fight_snapshot.h"

namespace ssf2t::character {

// The guest stamps fighters from several places, so Evil Ryu is read from the fighter itself on every use: carrier Ryu
// on a side whose player chose the host cell. A fighter's first byte is set when its init begins; the round reset
// clears it.
template <typename G>
bool EvilRyuOnSide(const uint8_t* base, int side) {
  if (side < 0 || side > 1 || RequestedIdentity(side) != FighterIdentity::kEvilRyu) return false;
  const uint32_t block = LoadBE32(GuestHost(base, G::kFighterBlockPointer), 0);
  if (block < 0x10000 || block >= 0xFFFF0000u) return false;
  const uint32_t fighter = block + netplay::kFighterOffsets[side];
  const bool started = *GuestHost(base, fighter) != 0;
  const int guest = int(int8_t(*GuestHost(base, fighter + netplay::kCharacterOffset)));
  const bool hidden = *GuestHost(base, fighter + netplay::kHiddenCharacterOffset) != 0;
  return StartedFighterIdentity(started, guest, hidden, UltraFightersEnabled(), FighterIdentity::kEvilRyu) ==
         FighterIdentity::kEvilRyu;
}

template <typename G>
int EvilRyuSide(const uint8_t* base, uint32_t fighter) {
  const uint32_t block = LoadBE32(GuestHost(base, G::kFighterBlockPointer), 0);
  for (int side = 0; side < 2; ++side) {
    if (fighter == block + netplay::kFighterOffsets[side] && EvilRyuOnSide<G>(base, side)) return side;
  }
  return -1;
}

}  // namespace ssf2t::character
