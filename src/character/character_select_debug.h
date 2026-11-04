#pragma once

#include "character/fighter_identity.h"

namespace ssf2t::character::debug {

void LogSetup(int side, int guest, bool hidden, FighterIdentity identity, FighterIdentity requested);

}  // namespace ssf2t::character::debug
