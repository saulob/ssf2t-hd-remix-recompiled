#include "achievement_ui.h"

#include <utility>

#include <rex/hook.h>

#include "title_update_image.h"

namespace {

std::function<void()> g_open;

template <typename G>
void ShowAchievements(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  static const bool wrapper_matches = ssf2t::IsShowAchievementsWrapper<G>(base + G::kShowAchievementsWrapper);
  if (!g_open || !wrapper_matches) {
    original(ctx, base);
    return;
  }
  g_open();
  ctx.r3.u64 = 0;
}

}  // namespace

namespace ssf2t {

void SetAchievementsOpener(std::function<void()> open) {
  g_open = std::move(open);
}

}  // namespace ssf2t

// XamShowAchievementsUI(user): opens the overlay instead of ReXGlue's stub. The game ignores the result.
REX_EXTERN(__imp__base013_sub_839C3D20);
REX_HOOK_RAW(base013_sub_839C3D20) {
  ShowAchievements<ssf2t::Base013Achievements>(ctx, base, __imp__base013_sub_839C3D20);
}

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_839C4480);
REX_HOOK_RAW(tu023_sub_839C4480) {
  ShowAchievements<ssf2t::TitleUpdate023Achievements>(ctx, base, __imp__tu023_sub_839C4480);
}
#endif
