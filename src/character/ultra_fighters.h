#pragma once

#include <cstdint>

#include <rex/cvar.h>

namespace ssf2t::character {

inline constexpr char kEnableUltraFightersSetting[] = "ssf2t_enable_ultra_fighters";
inline constexpr bool kDefaultEnableUltraFighters = false;
inline constexpr char kEnableUltraFightersLabel[] = "Enable Evil Ryu and Violent Ken";
inline constexpr char kEnableUltraFightersHelp[] =
    "Adds Evil Ryu and Violent Ken as selectable fighters. The original roster stays available.";

REXCVAR_DECLARE(bool, ssf2t_enable_ultra_fighters);

bool UltraFightersEnabled();
// Whether the Evil Ryu cell can be used now: the local option in Local and Arcade play, the negotiated result in an
// Online Match. Violent Ken is never available online.
bool EvilRyuAvailableFor(bool online_select);
bool EvilRyuAvailable(uint8_t game_mode);

}  // namespace ssf2t::character
