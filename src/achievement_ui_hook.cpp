#include "achievement_ui.h"

#include <utility>

#include <rex/hook.h>
#include <rex/logging.h>

namespace {

std::function<void()> g_open;

}  // namespace

namespace ssf2t {

void SetAchievementsOpener(std::function<void()> open) {
  g_open = std::move(open);
}

}  // namespace ssf2t

// XamShowAchievementsUI(user): opens the overlay instead of ReXGlue's stub. The game ignores the result.
REX_EXTERN(__imp__sub_839C3D20);
REX_HOOK_RAW(sub_839C3D20) {
  static const bool wrapper_matches = ssf2t::IsShowAchievementsWrapper(base + ssf2t::kShowAchievementsWrapper);
  if (!g_open || !wrapper_matches) {
    __imp__sub_839C3D20(ctx, base);
    return;
  }
  REXLOG_INFO("DEBUG_SAULO: game Achievements menu item opens the overlay");
  g_open();
  ctx.r3.u64 = 0;
}
