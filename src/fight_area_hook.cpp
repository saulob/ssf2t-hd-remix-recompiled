#include "fight_area.h"

#include <atomic>
#include <span>

#include <rex/hook.h>

#include "title_update_image.h"

REX_EXTERN(__imp__base013_sub_823F75D8);
REX_EXTERN(__imp__base013_sub_821738A0);
REX_EXTERN(__imp__base013_sub_821738F0);
REX_EXTERN(__imp__tu023_sub_823F7D48);
REX_EXTERN(__imp__tu023_sub_821738C0);
REX_EXTERN(__imp__tu023_sub_82173910);

namespace {

// The unhooked draw and the vertex scratch of one game version.
struct DrawCalls {
  PPCFunc* quad_draw;
  PPCFunc* vertex_reserve;
  PPCFunc* vertex_commit;
};

std::atomic<bool> g_fill{false};
thread_local int g_scene_depth = 0;
thread_local bool g_in_projection_setup = false;

template <typename G>
bool RenderPathMatches(const uint8_t* base) {
  static const bool matches = ssf2t::FightRenderPathMatches<G>(base);
  return matches;
}

template <typename G>
bool FillActive(const uint8_t* base) {
  return g_fill.load(std::memory_order_relaxed) && RenderPathMatches<G>(base) && ssf2t::FightFillActive<G>(true, base);
}

template <typename G>
bool PreFightPathMatches(const uint8_t* base) {
  static const bool matches = ssf2t::PreFightPathMatches<G>(base);
  return matches;
}

template <typename G>
bool PreFightFillActive(const uint8_t* base) {
  return g_fill.load(std::memory_order_relaxed) && PreFightPathMatches<G>(base) &&
         ssf2t::PreFightFillActive<G>(true, base);
}

template <typename G>
bool CharacterSelectFillActive(const uint8_t* base) {
  return g_fill.load(std::memory_order_relaxed) && PreFightPathMatches<G>(base) &&
         ssf2t::CharacterSelectFillActive<G>(true, base);
}

template <typename G>
bool VersusFillActive(const uint8_t* base) {
  return g_fill.load(std::memory_order_relaxed) && PreFightPathMatches<G>(base) &&
         ssf2t::VersusFillActive<G>(true, base);
}

template <typename G>
bool TextureBindPathMatches(const uint8_t* base) {
  static const bool matches = ssf2t::TextureBindPathMatches<G>(base);
  return matches;
}

template <typename G>
bool SidePanelPathMatches(const uint8_t* base) {
  static const bool matches = ssf2t::SidePanelPathMatches<G>(base);
  return matches;
}

uint8_t* VertexHost(uint8_t* base, uint32_t address) {
  return base + address + ssf2t::GuestHostOffset(address);
}

ssf2t::QuadVertices ReadQuad(uint8_t* base, uint32_t address) {
  const uint8_t* data = VertexHost(base, address);
  ssf2t::QuadVertices quad{};
  for (uint32_t i = 0; i < ssf2t::kQuadVertices; ++i) {
    const uint32_t at = i * ssf2t::kQuadVertexStride;
    quad[i] = {std::bit_cast<float>(ssf2t::LoadBE32(data, at)),      std::bit_cast<float>(ssf2t::LoadBE32(data, at + 4)),
               std::bit_cast<float>(ssf2t::LoadBE32(data, at + 8)),  ssf2t::LoadBE32(data, at + 12),
               std::bit_cast<float>(ssf2t::LoadBE32(data, at + 16)), std::bit_cast<float>(ssf2t::LoadBE32(data, at + 20))};
  }
  return quad;
}

void WriteVertices(uint8_t* base, uint32_t address, std::span<const ssf2t::QuadVertex> vertices) {
  uint8_t* data = VertexHost(base, address);
  for (uint32_t i = 0; i < vertices.size(); ++i) {
    const uint32_t at = i * ssf2t::kQuadVertexStride;
    ssf2t::StoreBE32(data, at, std::bit_cast<uint32_t>(vertices[i].x));
    ssf2t::StoreBE32(data, at + 4, std::bit_cast<uint32_t>(vertices[i].y));
    ssf2t::StoreBE32(data, at + 8, std::bit_cast<uint32_t>(vertices[i].z));
    ssf2t::StoreBE32(data, at + 12, vertices[i].color);
    ssf2t::StoreBE32(data, at + 16, std::bit_cast<uint32_t>(vertices[i].u));
    ssf2t::StoreBE32(data, at + 20, std::bit_cast<uint32_t>(vertices[i].v));
  }
}

// Draws one triangle strip from fresh vertex scratch with whatever texture and states are bound. Calls the unhooked
// draw, so it never re-enters the hook.
void DrawStrip(PPCContext& ctx, uint8_t* base, const DrawCalls& calls, std::span<const ssf2t::QuadVertex> vertices) {
  const uint32_t bytes = uint32_t(vertices.size()) * ssf2t::kQuadVertexStride;
  ctx.r3.u64 = bytes;
  calls.vertex_reserve(ctx, base);
  const uint32_t address = ctx.r3.u32;
  WriteVertices(base, address, vertices);
  ctx.r3.u64 = bytes;
  calls.vertex_commit(ctx, base);
  ctx.r3.u64 = ssf2t::kQuadPrimitive;
  ctx.r4.u64 = vertices.size();
  ctx.r5.u64 = address;
  calls.quad_draw(ctx, base);
}

bool IsQuadCall(const PPCContext& ctx) {
  return ctx.r3.u32 == ssf2t::kQuadPrimitive && ctx.r4.u32 == ssf2t::kQuadVertices && ctx.r5.u32 != 0;
}

// Fill draws the Character Select background as a warped mesh of the same texture: the protected part exactly
// where the game's quad puts it, the rest eased out to the visible edges. Otherwise the game's quad is drawn as built.
template <typename G>
void DrawCharacterSelectBackground(PPCContext& ctx, uint8_t* base, const DrawCalls& calls) {
  ssf2t::QuadVertices original{};
  ssf2t::WarpMesh mesh{};
  bool fill = false;
  if (IsQuadCall(ctx) && TextureBindPathMatches<G>(base)) {
    original = ReadQuad(base, ctx.r5.u32);
    const float aspect = std::bit_cast<float>(ssf2t::LoadBE32(base, ssf2t::kWidescreenAspect));
    ssf2t::TextureSize texture{};
    fill = CharacterSelectFillActive<G>(base) && ssf2t::LoadCharacterSelectTextureSize<G>(base, texture) &&
           ssf2t::CharacterSelectWarp(original, aspect, texture, mesh);
  }
  if (!fill) {
    calls.quad_draw(ctx, base);
    return;
  }
  const uint32_t caller = uint32_t(ctx.lr);
  for (const auto& strip : mesh.strips) {
    DrawStrip(ctx, base, calls, strip);
  }
  ctx.lr = caller;
}

template <typename G>
void DrawCharacterSelectSidePanel(PPCContext& ctx, uint8_t* base, const DrawCalls& calls, uint32_t caller) {
  const bool skip = IsQuadCall(ctx) && ssf2t::SkipsSidePanel<G>(CharacterSelectFillActive<G>(base),
                                                                 SidePanelPathMatches<G>(base), caller);
  if (!skip) {
    calls.quad_draw(ctx, base);
  }
}

// The VS background quad itself is drawn exactly as the game built it; Fill surrounds it with edge extensions.
template <typename G>
void DrawVersusBackground(PPCContext& ctx, uint8_t* base, const DrawCalls& calls) {
  const bool quad_call = IsQuadCall(ctx);
  ssf2t::QuadVertices center{};
  ssf2t::BackgroundExtensions extensions{};
  bool extend = false;
  if (quad_call) {
    center = ReadQuad(base, ctx.r5.u32);
    const float aspect = std::bit_cast<float>(ssf2t::LoadBE32(base, ssf2t::kWidescreenAspect));
    extend = VersusFillActive<G>(base) && ssf2t::BackgroundExtensionPieces(center, aspect, extensions);
  }
  const uint32_t caller = uint32_t(ctx.lr);
  calls.quad_draw(ctx, base);
  if (!extend) {
    return;
  }
  for (size_t i = 0; i < extensions.count; ++i) {
    DrawStrip(ctx, base, calls, extensions.pieces[i].quad);
  }
  ctx.lr = caller;
}

void InScene(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  ++g_scene_depth;
  original(ctx, base);
  --g_scene_depth;
}

void InProjectionSetup(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  g_in_projection_setup = true;
  original(ctx, base);
  g_in_projection_setup = false;
}

// Ortho(left f1, right f2, ...): Fill slides the window with the camera and keeps its width.
template <typename G>
void Ortho(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (g_scene_depth > 0 && g_in_projection_setup && FillActive<G>(base)) {
    const float left = float(ctx.f1.f64);
    const float right = float(ctx.f2.f64);
    const float shift = ssf2t::OrthoShift(left, right, std::bit_cast<float>(ssf2t::LoadBE32(base, G::kFightCameraX)));
    ctx.f1.f64 = double(left + shift);
    ctx.f2.f64 = double(right + shift);
  }
  original(ctx, base);
}

// Draws the black side quads of the fight, Character Select and VS; Fill 16:9 leaves them out.
template <typename G>
void MaskDrawer(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  const bool pre_fight = PreFightFillActive<G>(base);
  if (FillActive<G>(base) || pre_fight) {
    return;
  }
  original(ctx, base);
}

// Draws one quad from user vertices; Fill warps the Character Select background, drops its side panels and extends
// the VS background here.
template <typename G>
void QuadDraw(PPCContext& ctx, uint8_t* base, const DrawCalls& calls) {
  const uint32_t caller = uint32_t(ctx.lr);
  if (caller == G::kCharacterSelectQuadReturn) {
    DrawCharacterSelectBackground<G>(ctx, base, calls);
  } else if (caller == G::kScrollingQuadReturn) {
    DrawVersusBackground<G>(ctx, base, calls);
  } else if (ssf2t::SidePanelAt<G>(caller) != ssf2t::SidePanel::kNone) {
    DrawCharacterSelectSidePanel<G>(ctx, base, calls, caller);
  } else {
    calls.quad_draw(ctx, base);
  }
}

}  // namespace

namespace ssf2t {

void SetFightAreaFill(bool fill) {
  g_fill.store(fill, std::memory_order_relaxed);
}

}  // namespace ssf2t

// The fight world is drawn inside the scene draw and the fight render; the HUD is drawn after them.
REX_EXTERN(__imp__base013_sub_82181AA8);
REX_HOOK_RAW(base013_sub_82181AA8) { InScene(ctx, base, __imp__base013_sub_82181AA8); }
REX_EXTERN(__imp__base013_sub_823ED578);
REX_HOOK_RAW(base013_sub_823ED578) { InScene(ctx, base, __imp__base013_sub_823ED578); }
REX_EXTERN(__imp__base013_sub_823EBB28);
REX_HOOK_RAW(base013_sub_823EBB28) { InProjectionSetup(ctx, base, __imp__base013_sub_823EBB28); }
REX_EXTERN(__imp__base013_sub_823F5118);
REX_HOOK_RAW(base013_sub_823F5118) { Ortho<ssf2t::Base013Fight>(ctx, base, __imp__base013_sub_823F5118); }
REX_EXTERN(__imp__base013_sub_82177590);
REX_HOOK_RAW(base013_sub_82177590) { MaskDrawer<ssf2t::Base013Fight>(ctx, base, __imp__base013_sub_82177590); }
REX_HOOK_RAW(base013_sub_823F75D8) {
  QuadDraw<ssf2t::Base013Fight>(
      ctx, base, {__imp__base013_sub_823F75D8, __imp__base013_sub_821738A0, __imp__base013_sub_821738F0});
}

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_82181AB8);
REX_HOOK_RAW(tu023_sub_82181AB8) { InScene(ctx, base, __imp__tu023_sub_82181AB8); }
REX_EXTERN(__imp__tu023_sub_823EDC68);
REX_HOOK_RAW(tu023_sub_823EDC68) { InScene(ctx, base, __imp__tu023_sub_823EDC68); }
REX_EXTERN(__imp__tu023_sub_823EC218);
REX_HOOK_RAW(tu023_sub_823EC218) { InProjectionSetup(ctx, base, __imp__tu023_sub_823EC218); }
REX_EXTERN(__imp__tu023_sub_823F5888);
REX_HOOK_RAW(tu023_sub_823F5888) { Ortho<ssf2t::TitleUpdate023Fight>(ctx, base, __imp__tu023_sub_823F5888); }
REX_EXTERN(__imp__tu023_sub_821775B0);
REX_HOOK_RAW(tu023_sub_821775B0) { MaskDrawer<ssf2t::TitleUpdate023Fight>(ctx, base, __imp__tu023_sub_821775B0); }
REX_HOOK_RAW(tu023_sub_823F7D48) {
  QuadDraw<ssf2t::TitleUpdate023Fight>(
      ctx, base, {__imp__tu023_sub_823F7D48, __imp__tu023_sub_821738C0, __imp__tu023_sub_82173910});
}
#endif
