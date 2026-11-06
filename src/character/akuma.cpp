#include "character/akuma.h"

#include <cstdint>

#include <rex/hook.h>

#include "character/akuma_select.h"
#include "character/secret_select.h"
#include "guest_memory.h"
#include "netplay/guest_netplay.h"
#include "title_update_image.h"

namespace ssf2t::character {

REXCVAR_DEFINE_BOOL(ssf2t_enable_akuma, false, "SSF2T",
                    "Enable the original HD Remix Akuma without the secret input sequence");

bool AkumaEnabled() { return REXCVAR_GET(ssf2t_enable_akuma); }

namespace {

template <typename G>
void UnlockAkuma(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  original(ctx, base);
  const uint8_t game_mode = *GuestHost(base, G::kGameMode);
  ctx.r3.u64 = AkumaUnlockFor(OnlineSelectMode(game_mode), SecretCellsFor(game_mode).akuma, uint32_t(ctx.r3.u32));
}

}  // namespace
}  // namespace ssf2t::character

REX_EXTERN(__imp__base013_sub_823C9668);
REX_HOOK_RAW(base013_sub_823C9668) {
  ssf2t::character::UnlockAkuma<ssf2t::netplay::Base013Netplay>(ctx, base, __imp__base013_sub_823C9668);
}

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_823C9C60);
REX_HOOK_RAW(tu023_sub_823C9C60) {
  ssf2t::character::UnlockAkuma<ssf2t::netplay::TitleUpdate023Netplay>(ctx, base, __imp__tu023_sub_823C9C60);
}
#endif
