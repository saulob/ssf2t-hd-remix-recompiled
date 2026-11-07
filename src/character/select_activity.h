#pragma once

#include <cstdint>

namespace ssf2t::character {

// True while the guest Character Select screen is running and either Akuma or Evil Ryu and Violent Ken is enabled.
// A rough, late-clearing signal for game logic: drawing belongs in the guest's own passes.
bool CharacterSelectActive();

// Changes each time Character Select starts running after being idle.
uint32_t CharacterSelectSession();

// The sides, as bits, that the latest Character Select entry starts over because they just joined.
uint8_t CharacterSelectJoinedSides();

}  // namespace ssf2t::character
