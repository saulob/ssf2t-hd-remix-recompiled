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

}  // namespace ssf2t::character
