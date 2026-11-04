#include "character/ultra_fighters.h"

namespace ssf2t::character {

REXCVAR_DEFINE_BOOL(ssf2t_enable_ultra_fighters, false, "SSF2T",
                    "Enable Evil Ryu and Violent Ken as selectable fighters");

bool UltraFightersEnabled() { return REXCVAR_GET(ssf2t_enable_ultra_fighters); }

}  // namespace ssf2t::character
