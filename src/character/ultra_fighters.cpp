#include "character/ultra_fighters.h"

#include "character/akuma.h"
#include "character/character_packages.h"
#include "character/secret_select.h"
#include "character/violent_ken_data.h"
#include "netplay_setting.h"
#include "online_runtime.h"

namespace ssf2t::character {

REXCVAR_DEFINE_BOOL(ssf2t_enable_ultra_fighters, false, "SSF2T",
                    "Enable Evil Ryu and Violent Ken as selectable fighters");

bool UltraFightersEnabled() { return REXCVAR_GET(ssf2t_enable_ultra_fighters); }

SecretCells SecretCellsFor(uint8_t game_mode) {
  SecretCellInputs in;
  in.online_select = OnlineSelectMode(game_mode);
  in.developer_netplay = NetplayTestRole(rex::cvar::GetFlagByName(kNetplayRoleSetting));
  in.akuma_option = AkumaEnabled();
  in.ultra_option = UltraFightersEnabled();
  in.evil_ryu_packages = EvilRyuPackagesReady();
  in.violent_ken_packages = ViolentKenPackagesReady();
  in.violent_ken_gameplay = ViolentKenGameplayReady();
  if (in.online_select) in.negotiated = ActiveOnlineSecrets();
  return ResolveSecretCells(in);
}

}  // namespace ssf2t::character
