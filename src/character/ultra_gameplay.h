#pragma once

#include "character/fighter_identity.h"

namespace ssf2t::character {

// USF2 Evil Ryu has no 236236+P super and Violent Ken's is 236236+K, so the guest's own super start is refused for both.
inline constexpr bool CanStartShinkuHadoken(FighterIdentity identity) {
  return identity != FighterIdentity::kEvilRyu && identity != FighterIdentity::kViolentKen;
}

}  // namespace ssf2t::character
