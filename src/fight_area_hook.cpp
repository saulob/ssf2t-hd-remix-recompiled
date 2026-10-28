#include "fight_area.h"

#include <atomic>
#include <span>

#include <rex/hook.h>

REX_EXTERN(__imp__sub_823F7D48);
REX_EXTERN(__imp__sub_821738C0);
REX_EXTERN(__imp__sub_82173910);

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

bool PreFightPathMatches(const uint8_t* base) {
  static const bool matches = ssf2t::PreFightPathMatches(base);
  return matches;
}

bool PreFightFillActive(const uint8_t* base) {
  return g_fill.load(std::memory_order_relaxed) && PreFightPathMatches(base) && ssf2t::PreFightFillActive(true, base);
}

bool CharacterSelectFillActive(const uint8_t* base) {
  return g_fill.load(std::memory_order_relaxed) && PreFightPathMatches(base) &&
         ssf2t::CharacterSelectFillActive(true, base);
}

bool VersusFillActive(const uint8_t* base) {
  return g_fill.load(std::memory_order_relaxed) && PreFightPathMatches(base) && ssf2t::VersusFillActive(true, base);
}

bool TextureBindPathMatches(const uint8_t* base) {
  static const bool matches = ssf2t::TextureBindPathMatches(base);
  return matches;
}

bool SidePanelPathMatches(const uint8_t* base) {
  static const bool matches = ssf2t::SidePanelPathMatches(base);
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
void DrawStrip(PPCContext& ctx, uint8_t* base, std::span<const ssf2t::QuadVertex> vertices) {
  const uint32_t bytes = uint32_t(vertices.size()) * ssf2t::kQuadVertexStride;
  ctx.r3.u64 = bytes;
  __imp__sub_821738C0(ctx, base);
  const uint32_t address = ctx.r3.u32;
  WriteVertices(base, address, vertices);
  ctx.r3.u64 = bytes;
  __imp__sub_82173910(ctx, base);
  ctx.r3.u64 = ssf2t::kQuadPrimitive;
  ctx.r4.u64 = vertices.size();
  ctx.r5.u64 = address;
  __imp__sub_823F7D48(ctx, base);
}

bool IsQuadCall(const PPCContext& ctx) {
  return ctx.r3.u32 == ssf2t::kQuadPrimitive && ctx.r4.u32 == ssf2t::kQuadVertices && ctx.r5.u32 != 0;
}

// Fill draws the Character Select background as a warped mesh of the same texture: the protected part exactly
// where the game's quad puts it, the rest eased out to the visible edges. Otherwise the game's quad is drawn as built.
void DrawCharacterSelectBackground(PPCContext& ctx, uint8_t* base) {
  ssf2t::QuadVertices original{};
  ssf2t::WarpMesh mesh{};
  bool fill = false;
  if (IsQuadCall(ctx) && TextureBindPathMatches(base)) {
    original = ReadQuad(base, ctx.r5.u32);
    const float aspect = std::bit_cast<float>(ssf2t::LoadBE32(base, ssf2t::kWidescreenAspect));
    ssf2t::TextureSize texture{};
    fill = CharacterSelectFillActive(base) && ssf2t::LoadCharacterSelectTextureSize(base, texture) &&
           ssf2t::CharacterSelectWarp(original, aspect, texture, mesh);
  }
  if (!fill) {
    __imp__sub_823F7D48(ctx, base);
    return;
  }
  const uint32_t caller = uint32_t(ctx.lr);
  for (const auto& strip : mesh.strips) {
    DrawStrip(ctx, base, strip);
  }
  ctx.lr = caller;
}

void DrawCharacterSelectSidePanel(PPCContext& ctx, uint8_t* base, uint32_t caller) {
  const bool skip = IsQuadCall(ctx) &&
                    ssf2t::SkipsSidePanel(CharacterSelectFillActive(base), SidePanelPathMatches(base), caller);
  if (!skip) {
    __imp__sub_823F7D48(ctx, base);
  }
}

// The VS background quad itself is drawn exactly as the game built it; Fill surrounds it with edge extensions.
void DrawVersusBackground(PPCContext& ctx, uint8_t* base) {
  const bool quad_call = IsQuadCall(ctx);
  ssf2t::QuadVertices center{};
  ssf2t::BackgroundExtensions extensions{};
  bool extend = false;
  if (quad_call) {
    center = ReadQuad(base, ctx.r5.u32);
    const float aspect = std::bit_cast<float>(ssf2t::LoadBE32(base, ssf2t::kWidescreenAspect));
    extend = VersusFillActive(base) && ssf2t::BackgroundExtensionPieces(center, aspect, extensions);
  }
  const uint32_t caller = uint32_t(ctx.lr);
  __imp__sub_823F7D48(ctx, base);
  if (!extend) {
    return;
  }
  for (size_t i = 0; i < extensions.count; ++i) {
    DrawStrip(ctx, base, extensions.pieces[i].quad);
  }
  ctx.lr = caller;
}

}  // namespace

namespace ssf2t {

void SetFightAreaFill(bool fill) {
  g_fill.store(fill, std::memory_order_relaxed);
}

}  // namespace ssf2t

// The fight world is drawn inside these two calls; the HUD is drawn after them.
REX_EXTERN(__imp__sub_82181AB8);
REX_HOOK_RAW(sub_82181AB8) {
  ++g_scene_depth;
  __imp__sub_82181AB8(ctx, base);
  --g_scene_depth;
}

REX_EXTERN(__imp__sub_823EDC68);
REX_HOOK_RAW(sub_823EDC68) {
  ++g_scene_depth;
  __imp__sub_823EDC68(ctx, base);
  --g_scene_depth;
}

REX_EXTERN(__imp__sub_823EC218);
REX_HOOK_RAW(sub_823EC218) {
  g_in_projection_setup = true;
  __imp__sub_823EC218(ctx, base);
  g_in_projection_setup = false;
}

// Ortho(left f1, right f2, ...): Fill slides the window with the camera and keeps its width.
REX_EXTERN(__imp__sub_823F5888);
REX_HOOK_RAW(sub_823F5888) {
  if (g_scene_depth > 0 && g_in_projection_setup && FillActive(base)) {
    const float left = float(ctx.f1.f64);
    const float right = float(ctx.f2.f64);
    const float shift = ssf2t::OrthoShift(left, right, std::bit_cast<float>(ssf2t::LoadBE32(base, ssf2t::kFightCameraX)));
    ctx.f1.f64 = double(left + shift);
    ctx.f2.f64 = double(right + shift);
  }
  __imp__sub_823F5888(ctx, base);
}

// Draws the black side quads of the fight, Character Select and VS; Fill 16:9 leaves them out.
REX_EXTERN(__imp__sub_821775B0);
REX_HOOK_RAW(sub_821775B0) {
  const bool pre_fight = PreFightFillActive(base);
  if (FillActive(base) || pre_fight) {
    return;
  }
  __imp__sub_821775B0(ctx, base);
}

// Draws one quad from user vertices; Fill warps the Character Select background, drops its side panels and extends
// the VS background here.
REX_HOOK_RAW(sub_823F7D48) {
  const uint32_t caller = uint32_t(ctx.lr);
  if (caller == ssf2t::kCharacterSelectQuadReturn) {
    DrawCharacterSelectBackground(ctx, base);
  } else if (caller == ssf2t::kScrollingQuadReturn) {
    DrawVersusBackground(ctx, base);
  } else if (ssf2t::SidePanelAt(caller) != ssf2t::SidePanel::kNone) {
    DrawCharacterSelectSidePanel(ctx, base, caller);
  } else {
    __imp__sub_823F7D48(ctx, base);
  }
}
