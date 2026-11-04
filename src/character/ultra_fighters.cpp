#include "character/ultra_fighters.h"

#include "character/secret_select.h"
#include "online_runtime.h"

namespace ssf2t::character {

REXCVAR_DEFINE_BOOL(ssf2t_enable_ultra_fighters, false, "SSF2T",
                    "Enable Evil Ryu and Violent Ken as selectable fighters");

bool UltraFightersEnabled() { return REXCVAR_GET(ssf2t_enable_ultra_fighters); }

bool EvilRyuAvailableFor(bool online_select) {
  return SecretAvailable(online_select, UltraFightersEnabled(), online_select && ActiveOnlineSecrets().evil_ryu);
}

bool EvilRyuAvailable(uint8_t game_mode) { return EvilRyuAvailableFor(OnlineSelectMode(game_mode)); }

}  // namespace ssf2t::character
