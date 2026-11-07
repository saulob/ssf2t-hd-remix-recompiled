#include <cstdint>

#include <rex/hook.h>
#include <rex/logging.h>

#include "guest_memory.h"
#include "settings.h"
#include "skip_intro.h"
#include "title_update_image.h"

namespace {

struct TextureCalls {
  PPCFunc* ref_count;
  PPCFunc* release;
};

// Releases a logo texture the way the game does when its step is over.
void ReleaseTexture(PPCContext& ctx, uint8_t* base, const TextureCalls& calls, uint32_t slot) {
  const uint32_t texture = ssf2t::LoadBE32(base, slot);
  if (texture == 0) {
    return;
  }
  for (bool last = false; !last;) {
    ctx.r3.u64 = texture;
    calls.ref_count(ctx, base);
    last = ctx.r3.s32 <= 1;
    ctx.r3.u64 = texture;
    calls.release(ctx, base);
  }
  ssf2t::StoreBE32(base, slot, 0);
}

template <typename G>
void BootLogoStep(PPCContext& ctx, uint8_t* base, PPCFunc* original, const TextureCalls& textures) {
  const uint32_t step = ssf2t::LoadBE32(base, G::kBootLogoState);
  if (ssf2t::SkipsBootLogos(REXCVAR_GET(ssf2t_skip_intro), step, base[G::kBootLogoFinished])) {
    const uint64_t caller = ctx.lr;
    ReleaseTexture(ctx, base, textures, G::kCapcomLogo);
    ssf2t::StoreBE32(base, G::kBootLogoState, ssf2t::kBootLogosEndStep);
    ctx.lr = caller;
    REXLOG_INFO("skip intro: startup presentation bypassed guest={}", G::kName);
  }
  original(ctx, base);
}

}  // namespace

REX_EXTERN(__imp__base013_sub_823ECB08);
REX_EXTERN(__imp__base013_sub_82173990);
REX_EXTERN(__imp__base013_sub_82173948);
REX_HOOK_RAW(base013_sub_823ECB08) {
  BootLogoStep<ssf2t::Base013SkipIntro>(ctx, base, __imp__base013_sub_823ECB08,
                                        {__imp__base013_sub_82173990, __imp__base013_sub_82173948});
}

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_823ED1F8);
REX_EXTERN(__imp__tu023_sub_821739B0);
REX_EXTERN(__imp__tu023_sub_82173968);
REX_HOOK_RAW(tu023_sub_823ED1F8) {
  BootLogoStep<ssf2t::TitleUpdate023SkipIntro>(ctx, base, __imp__tu023_sub_823ED1F8,
                                               {__imp__tu023_sub_821739B0, __imp__tu023_sub_82173968});
}
#endif
