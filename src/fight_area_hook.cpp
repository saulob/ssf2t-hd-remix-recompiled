#include "fight_area.h"

#include <atomic>

#include <rex/hook.h>

namespace {

std::atomic<bool> g_fill{false};
thread_local int g_scene_depth = 0;
thread_local bool g_in_projection_setup = false;

bool RenderPathMatches(const uint8_t* base) {
  static const bool matches = ssf2t::FightRenderPathMatches(base);
  return matches;
}

bool FillActive(const uint8_t* base) {
  return g_fill.load(std::memory_order_relaxed) && RenderPathMatches(base) && ssf2t::FightFillActive(true, base);
}

}  // namespace

namespace ssf2t {

void SetFightAreaFill(bool fill) {
  g_fill.store(fill, std::memory_order_relaxed);
}

}  // namespace ssf2t

// The fight world is drawn inside these two calls; the HUD is drawn after them.
REX_EXTERN(__imp__sub_82181AA8);
REX_HOOK_RAW(sub_82181AA8) {
  ++g_scene_depth;
  __imp__sub_82181AA8(ctx, base);
  --g_scene_depth;
}

REX_EXTERN(__imp__sub_823ED578);
REX_HOOK_RAW(sub_823ED578) {
  ++g_scene_depth;
  __imp__sub_823ED578(ctx, base);
  --g_scene_depth;
}

REX_EXTERN(__imp__sub_823EBB28);
REX_HOOK_RAW(sub_823EBB28) {
  g_in_projection_setup = true;
  __imp__sub_823EBB28(ctx, base);
  g_in_projection_setup = false;
}

// Ortho(left f1, right f2, ...): Fill slides the window with the camera and keeps its width.
REX_EXTERN(__imp__sub_823F5118);
REX_HOOK_RAW(sub_823F5118) {
  if (g_scene_depth > 0 && g_in_projection_setup && FillActive(base)) {
    const float left = float(ctx.f1.f64);
    const float right = float(ctx.f2.f64);
    const float shift = ssf2t::OrthoShift(left, right, std::bit_cast<float>(ssf2t::LoadBE32(base, ssf2t::kFightCameraX)));
    ctx.f1.f64 = double(left + shift);
    ctx.f2.f64 = double(right + shift);
  }
  __imp__sub_823F5118(ctx, base);
}

// Draws the fight's black side quads; Fill 16:9 leaves them out.
REX_EXTERN(__imp__sub_82177590);
REX_HOOK_RAW(sub_82177590) {
  if (FillActive(base)) {
    return;
  }
  __imp__sub_82177590(ctx, base);
}
