#pragma once

#include "character/fighter_identity.h"

namespace ssf2t::character {

// USF2 Evil Ryu has no 236236+P super; the guest's Shinku Hadoken start is refused for him.
inline constexpr bool CanStartShinkuHadoken(FighterIdentity identity) { return identity != FighterIdentity::kEvilRyu; }

}  // namespace ssf2t::character
