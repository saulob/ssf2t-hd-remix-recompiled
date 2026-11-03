#include "fight_area.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <span>
#include <string>
#include <type_traits>

#include <rex/hook.h>
#include <rex/logging.h>

#include "debug_saulo_capture.h"
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
bool ScrollingBackgroundFillActive(const uint8_t* base) {
  return g_fill.load(std::memory_order_relaxed) && PreFightPathMatches<G>(base) &&
         ssf2t::ScrollingBackgroundFillActive<G>(true, base);
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

// DEBUG_SAULO victory_capture begin
struct DebugOutcome {
  bool scrolling = false;
  bool extension_ok = false;
  size_t pieces = 0;
  bool character_select_warp = false;
};
thread_local DebugOutcome g_debug_outcome;

struct DebugTexture {
  uint32_t device = 0;
  uint32_t d3d = 0;
  ssf2t::TextureFetch fetch{};
  ssf2t::TextureSize size{};
  bool has_size = false;
  const char* tag = "none";
};

struct DebugQuadProbe {
  bool active = false;
  uint32_t caller = 0;
  uint32_t up1 = 0;
  uint32_t up2 = 0;
  uint32_t primitive = 0;
  uint32_t count = 0;
  uint32_t address = 0;
  bool has_quad = false;
  ssf2t::QuadVertices quad{};
  DebugTexture texture;
  ssf2t::MatchFlow flow{};
  bool flow_known = false;
  uint32_t background = 0;
  uint8_t background_enabled = 0;
};

struct DebugCapture {
  uint32_t id = 0;
  std::chrono::steady_clock::time_point begin;
  uint32_t scene_draws = 0, mask_calls = 0, mask_skips = 0, quad_calls = 0, scrolling_calls = 0;
  uint32_t extension_attempts = 0, extension_successes = 0, pieces_drawn = 0;
  uint32_t snapshots = 0;
  uint64_t last_state = 0;
  ssf2t::debug_saulo::UniqueKeys<ssf2t::debug_saulo::kMaxQuadLines> quads;
  ssf2t::debug_saulo::UniqueKeys<64> callers;
  ssf2t::debug_saulo::UniqueKeys<24> masks;
  ssf2t::debug_saulo::UniqueKeys<24> textures;
  std::string caller_list;
  std::string texture_list;
};

std::atomic<bool> g_debug_armed{false};
std::atomic<bool> g_debug_active{false};
std::mutex g_debug_mutex;
DebugCapture g_debug;
uint32_t g_debug_serial = 0;

template <typename G>
constexpr const char* DebugGuestName() {
  return std::is_same_v<G, ssf2t::Base013Fight> ? "base013" : "tu023";
}

bool DebugWatching() {
  return g_debug_armed.load(std::memory_order_relaxed) || g_debug_active.load(std::memory_order_relaxed);
}

uint32_t DebugReturnAbove(const uint8_t* base, uint32_t stack) {
  if (!ssf2t::debug_saulo::PlausibleGuestPointer(stack)) {
    return 0;
  }
  const uint32_t chain = ssf2t::LoadBE32(base, stack);
  if (chain <= stack || chain - stack > 0x10000 || !ssf2t::debug_saulo::PlausibleGuestPointer(chain)) {
    return 0;
  }
  const uint32_t address = ssf2t::LoadBE32(base, chain - 8);
  return ssf2t::debug_saulo::InGuestImage(address) ? address : 0;
}

uint32_t DebugNextFrame(const uint8_t* base, uint32_t stack) {
  const uint32_t chain = ssf2t::debug_saulo::PlausibleGuestPointer(stack) ? ssf2t::LoadBE32(base, stack) : 0;
  return chain > stack ? chain : 0;
}

template <typename G>
DebugTexture DebugBoundTexture(const uint8_t* base) {
  DebugTexture texture;
  texture.device = ssf2t::LoadBE32(base, G::kDevicePointer);
  if (!ssf2t::debug_saulo::PlausibleGuestPointer(texture.device)) {
    return texture;
  }
  const uint8_t* device = ssf2t::GuestHost(base, texture.device);
  texture.d3d = ssf2t::LoadBE32(device, ssf2t::kDeviceTextures);
  texture.fetch = ssf2t::LoadTextureFetch(device + ssf2t::kDeviceFetchConstants);
  texture.has_size = ssf2t::FetchTextureSize(texture.fetch, texture.size);
  std::array<uint32_t, ssf2t::debug_saulo::kBackgroundHolders> holders{};
  for (size_t i = 0; i < holders.size(); ++i) {
    holders[i] = ssf2t::LoadBE32(base, G::kCharacterSelectTexture + 4 * uint32_t(i));
  }
  texture.tag = ssf2t::debug_saulo::TextureTag(texture.d3d, holders);
  return texture;
}

template <typename G>
void DebugLogState(const char* reason, PPCContext& ctx, const uint8_t* base) {
  ssf2t::MatchFlow flow{};
  const bool known = ssf2t::LoadMatchFlow<G>(base, flow);
  const uint32_t ram = ssf2t::LoadBE32(base, G::kGameRam);
  std::string words;
  if (known) {
    const uint8_t* host = ssf2t::GuestHost(base, ram);
    for (uint32_t offset = 0; offset < 0x20; offset += 2) {
      words += fmt::format("{:04X}{}", ssf2t::LoadBE16(host, offset), offset + 2 < 0x20 ? "," : "");
    }
  }
  const float aspect = std::bit_cast<float>(ssf2t::LoadBE32(base, ssf2t::kWidescreenAspect));
  const float camera = std::bit_cast<float>(ssf2t::LoadBE32(base, G::kFightCameraX));
  const bool fill = g_fill.load(std::memory_order_relaxed);
  REXLOG_WARN(
      "DEBUG_SAULO victory_capture STATE id={} reason={} guest={} main={} game={} match={} flow_known={} ram={:08X} "
      "ram_words=[{}] fight_active={} ws_display={} ws_mode={} aspect={:.4f} camera_x={:.3f} hd_enabled={} hd_bg={} "
      "arcade_cb={:08X} fill_setting={} render_path={} prefight_path={} fight_fill={} prefight_fill={} select_fill={} "
      "versus_fill={} victory={} scrolling_fill={} scene_depth={} in_projection={} lr={:08X}",
      g_debug.id, reason, DebugGuestName<G>(), known ? int(flow.main_mode) : -1, known ? int(flow.game_mode) : -1,
      known ? int(flow.match_step) : -1, known, ram, words, base[G::kFightActiveFlag], base[G::kWidescreenDisplayFlag],
      ssf2t::LoadBE32(base, G::kWidescreenMode), aspect, camera, base[ssf2t::kHdBackgroundEnabled],
      ssf2t::LoadBE32(base, ssf2t::kHdBackground), ssf2t::LoadBE32(base, G::kWidescreenDisplayFlag - 8), fill,
      RenderPathMatches<G>(base), PreFightPathMatches<G>(base), ssf2t::FightFillActive<G>(true, base),
      ssf2t::PreFightFillActive<G>(true, base), ssf2t::CharacterSelectFillActive<G>(true, base),
      ssf2t::VersusFillActive<G>(true, base), known && ssf2t::IsVictory(flow),
      ssf2t::ScrollingBackgroundFillActive<G>(true, base), g_scene_depth, g_in_projection_setup, uint32_t(ctx.lr));
}

template <typename G>
uint64_t DebugStateKey(const uint8_t* base) {
  ssf2t::MatchFlow flow{};
  const bool known = ssf2t::LoadMatchFlow<G>(base, flow);
  uint64_t key = known;
  for (const uint64_t part : {uint64_t(flow.main_mode), uint64_t(flow.game_mode), uint64_t(flow.match_step),
                              uint64_t(ssf2t::LoadBE32(base, ssf2t::kHdBackground)),
                              uint64_t(base[ssf2t::kHdBackgroundEnabled]), uint64_t(base[G::kFightActiveFlag])}) {
    key = ssf2t::debug_saulo::Mix(key, part);
  }
  return key;
}

template <typename G>
void DebugEnd() {
  REXLOG_WARN(
      "DEBUG_SAULO victory_capture END id={} guest={} scene_draws={} mask_calls={} mask_skips={} quad_calls={} "
      "unique_callers={} scrolling_calls={} extension_attempts={} extension_successes={} pieces_drawn={} "
      "unique_textures={} unique_quads={} callers=[{}] textures=[{}]",
      g_debug.id, DebugGuestName<G>(), g_debug.scene_draws, g_debug.mask_calls, g_debug.mask_skips, g_debug.quad_calls,
      g_debug.callers.size(), g_debug.scrolling_calls, g_debug.extension_attempts, g_debug.extension_successes,
      g_debug.pieces_drawn, g_debug.textures.size(), g_debug.quads.size(), g_debug.caller_list, g_debug.texture_list);
  g_debug_active.store(false, std::memory_order_relaxed);
}

// Starts a pending capture, ends an expired one, and reports whether one is running. Needs g_debug_mutex.
template <typename G>
bool DebugTick(PPCContext& ctx, const uint8_t* base) {
  const auto now = std::chrono::steady_clock::now();
  if (g_debug_armed.exchange(false, std::memory_order_relaxed)) {
    g_debug = {};
    g_debug.id = ++g_debug_serial;
    g_debug.begin = now;
    g_debug_active.store(true, std::memory_order_relaxed);
    REXLOG_WARN("DEBUG_SAULO victory_capture BEGIN guest={} id={} window_ms={}", DebugGuestName<G>(), g_debug.id,
                ssf2t::debug_saulo::kCaptureNanos / 1'000'000);
    g_debug.last_state = DebugStateKey<G>(base);
    ++g_debug.snapshots;
    DebugLogState<G>("f9", ctx, base);
  }
  if (!g_debug_active.load(std::memory_order_relaxed)) {
    return false;
  }
  const int64_t elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - g_debug.begin).count();
  if (ssf2t::debug_saulo::CaptureExpired(elapsed, 0)) {
    DebugEnd<G>();
    return false;
  }
  const uint64_t state = DebugStateKey<G>(base);
  if (state != g_debug.last_state && g_debug.snapshots < ssf2t::debug_saulo::kMaxSnapshots) {
    g_debug.last_state = state;
    ++g_debug.snapshots;
    DebugLogState<G>("changed", ctx, base);
  }
  return true;
}

template <typename G>
void DebugSceneDraw(PPCContext& ctx, const uint8_t* base) {
  if (!DebugWatching()) {
    return;
  }
  std::lock_guard lock(g_debug_mutex);
  if (DebugTick<G>(ctx, base)) {
    ++g_debug.scene_draws;
  }
}

template <typename G>
void DebugMask(PPCContext& ctx, const uint8_t* base, bool fill, bool pre_fight, bool skip) {
  if (!DebugWatching()) {
    return;
  }
  std::lock_guard lock(g_debug_mutex);
  if (!DebugTick<G>(ctx, base)) {
    return;
  }
  ++g_debug.mask_calls;
  g_debug.mask_skips += skip;
  ssf2t::MatchFlow flow{};
  const bool known = ssf2t::LoadMatchFlow<G>(base, flow);
  const uint32_t caller = uint32_t(ctx.lr);
  uint64_t key = ssf2t::debug_saulo::Mix(0xCBF29CE484222325ull, caller);
  for (const uint64_t part :
       {uint64_t(known), uint64_t(flow.main_mode), uint64_t(flow.game_mode), uint64_t(flow.match_step), uint64_t(fill),
        uint64_t(pre_fight), uint64_t(skip), uint64_t(base[G::kFightActiveFlag]),
        uint64_t(ssf2t::LoadBE32(base, ssf2t::kHdBackground))}) {
    key = ssf2t::debug_saulo::Mix(key, part);
  }
  if (g_debug.masks.Add(key)) {
    REXLOG_WARN(
        "DEBUG_SAULO victory_capture MASK id={} caller={:08X} main={} game={} match={} fill={} pre={} fight_active={} "
        "ws_display={} ws_mode={} hd_bg={} action={}",
        g_debug.id, caller, known ? int(flow.main_mode) : -1, known ? int(flow.game_mode) : -1,
        known ? int(flow.match_step) : -1, fill, pre_fight, base[G::kFightActiveFlag], base[G::kWidescreenDisplayFlag],
        ssf2t::LoadBE32(base, G::kWidescreenMode), ssf2t::LoadBE32(base, ssf2t::kHdBackground),
        skip ? "SKIP" : "CALL_ORIGINAL");
  }
}

template <typename G>
DebugQuadProbe DebugBeginQuad(PPCContext& ctx, uint8_t* base) {
  DebugQuadProbe probe;
  if (!DebugWatching()) {
    return probe;
  }
  std::lock_guard lock(g_debug_mutex);
  if (!DebugTick<G>(ctx, base)) {
    return probe;
  }
  g_debug_outcome = {};
  ++g_debug.quad_calls;
  probe.active = true;
  probe.caller = uint32_t(ctx.lr);
  probe.up1 = DebugReturnAbove(base, ctx.r1.u32);
  probe.up2 = DebugReturnAbove(base, DebugNextFrame(base, ctx.r1.u32));
  probe.primitive = ctx.r3.u32;
  probe.count = ctx.r4.u32;
  probe.address = ctx.r5.u32;
  probe.flow_known = ssf2t::LoadMatchFlow<G>(base, probe.flow);
  probe.background = ssf2t::LoadBE32(base, ssf2t::kHdBackground);
  probe.background_enabled = base[ssf2t::kHdBackgroundEnabled];
  probe.texture = DebugBoundTexture<G>(base);
  if (IsQuadCall(ctx) && ssf2t::debug_saulo::PlausibleGuestPointer(probe.address)) {
    probe.quad = ReadQuad(base, probe.address);
    probe.has_quad = ssf2t::debug_saulo::IsFiniteQuad(probe.quad);
  }
  return probe;
}

template <typename G>
void DebugEndQuad(const DebugQuadProbe& probe, const uint8_t* base) {
  if (!probe.active) {
    return;
  }
  std::lock_guard lock(g_debug_mutex);
  if (!g_debug_active.load(std::memory_order_relaxed)) {
    return;
  }
  const DebugOutcome outcome = g_debug_outcome;
  g_debug.scrolling_calls += outcome.scrolling;
  g_debug.extension_attempts += outcome.scrolling;
  g_debug.extension_successes += outcome.extension_ok;
  g_debug.pieces_drawn += uint32_t(outcome.pieces);
  const auto kind = ssf2t::debug_saulo::ClassifyCaller<G>(probe.caller);
  if (g_debug.callers.Add(probe.caller)) {
    g_debug.caller_list += fmt::format("{}{:08X}", g_debug.caller_list.empty() ? "" : ",", probe.caller);
  }
  if (probe.texture.d3d != 0 && g_debug.textures.Add(probe.texture.d3d)) {
    g_debug.texture_list +=
        fmt::format("{}{:08X}:{}", g_debug.texture_list.empty() ? "" : ",", probe.texture.d3d, probe.texture.tag);
  }
  ssf2t::debug_saulo::QuadExtent extent{};
  uint64_t key = 0;
  bool extension_candidate = false;
  if (probe.has_quad) {
    extent = ssf2t::debug_saulo::ExtentOf(probe.quad);
    key = ssf2t::debug_saulo::QuadKey(probe.caller, probe.texture.d3d, extent);
    ssf2t::BackgroundExtensions candidate{};
    extension_candidate = ssf2t::BackgroundExtensionPieces(
        probe.quad, std::bit_cast<float>(ssf2t::LoadBE32(base, ssf2t::kWidescreenAspect)), candidate);
  } else {
    key = ssf2t::debug_saulo::QuadKey(probe.caller, probe.texture.d3d, extent);
    key = ssf2t::debug_saulo::Mix(key, (uint64_t(probe.primitive) << 32) | probe.count);
  }
  if (!g_debug.quads.Add(key)) {
    return;
  }
  REXLOG_WARN(
      "DEBUG_SAULO victory_capture QUAD id={} caller={:08X} kind={} up1={:08X} up2={:08X} prim={} verts={} addr={:08X} "
      "main={} game={} match={} hd_enabled={} hd_bg={} x=[{:.2f},{:.2f}] y=[{:.2f},{:.2f}] u=[{:.3f},{:.3f}] "
      "v=[{:.3f},{:.3f}] tex={:08X} tag={} size={}x{} fetch=[{:08X},{:08X},{:08X},{:08X},{:08X},{:08X}] "
      "scrolling_entered={} ext_pieces_ok={} ext_pieces_drawn={} ext_candidate={} select_warp={}",
      g_debug.id, probe.caller, ssf2t::debug_saulo::CallerKindName(kind), probe.up1, probe.up2, probe.primitive,
      probe.count, probe.address, probe.flow_known ? int(probe.flow.main_mode) : -1,
      probe.flow_known ? int(probe.flow.game_mode) : -1, probe.flow_known ? int(probe.flow.match_step) : -1,
      probe.background_enabled, probe.background, extent.min_x, extent.max_x, extent.min_y, extent.max_y, extent.min_u,
      extent.max_u, extent.min_v, extent.max_v, probe.texture.d3d, probe.texture.tag,
      probe.texture.has_size ? probe.texture.size.width : 0, probe.texture.has_size ? probe.texture.size.height : 0,
      probe.texture.fetch[0], probe.texture.fetch[1], probe.texture.fetch[2], probe.texture.fetch[3],
      probe.texture.fetch[4], probe.texture.fetch[5], outcome.scrolling, outcome.extension_ok, outcome.pieces,
      extension_candidate, outcome.character_select_warp);
}
// DEBUG_SAULO victory_capture end

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
  g_debug_outcome.character_select_warp = fill;  // DEBUG_SAULO
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

// The VS and winner screen background quad is drawn exactly as the game built it; Fill surrounds it with edge
// extensions.
template <typename G>
void DrawScrollingBackground(PPCContext& ctx, uint8_t* base, const DrawCalls& calls) {
  const bool quad_call = IsQuadCall(ctx);
  ssf2t::QuadVertices center{};
  ssf2t::BackgroundExtensions extensions{};
  bool extend = false;
  if (quad_call) {
    center = ReadQuad(base, ctx.r5.u32);
    const float aspect = std::bit_cast<float>(ssf2t::LoadBE32(base, ssf2t::kWidescreenAspect));
    extend = ScrollingBackgroundFillActive<G>(base) && ssf2t::BackgroundExtensionPieces(center, aspect, extensions);
  }
  const uint32_t caller = uint32_t(ctx.lr);
  g_debug_outcome = {true, extend, extend ? extensions.count : 0, false};  // DEBUG_SAULO
  calls.quad_draw(ctx, base);
  if (!extend) {
    return;
  }
  for (size_t i = 0; i < extensions.count; ++i) {
    DrawStrip(ctx, base, calls, extensions.pieces[i].quad);
  }
  ctx.lr = caller;
}

template <typename G>
void InScene(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  DebugSceneDraw<G>(ctx, base);  // DEBUG_SAULO
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

// Draws the black side quads of the fight, Character Select, VS and the winner screen; Fill 16:9 leaves them out.
template <typename G>
void MaskDrawer(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  const bool pre_fight = PreFightFillActive<G>(base);
  const bool fill = FillActive<G>(base);
  DebugMask<G>(ctx, base, fill, pre_fight, fill || pre_fight);  // DEBUG_SAULO
  if (fill || pre_fight) {
    return;
  }
  original(ctx, base);
}

// Draws one quad from user vertices; Fill warps the Character Select background, drops its side panels and extends
// the VS and winner screen background here.
template <typename G>
void QuadDraw(PPCContext& ctx, uint8_t* base, const DrawCalls& calls) {
  const uint32_t caller = uint32_t(ctx.lr);
  const DebugQuadProbe probe = DebugBeginQuad<G>(ctx, base);  // DEBUG_SAULO
  if (caller == G::kCharacterSelectQuadReturn) {
    DrawCharacterSelectBackground<G>(ctx, base, calls);
  } else if (caller == G::kScrollingQuadReturn) {
    DrawScrollingBackground<G>(ctx, base, calls);
  } else if (ssf2t::SidePanelAt<G>(caller) != ssf2t::SidePanel::kNone) {
    DrawCharacterSelectSidePanel<G>(ctx, base, calls, caller);
  } else {
    calls.quad_draw(ctx, base);
  }
  DebugEndQuad<G>(probe, base);  // DEBUG_SAULO
}

}  // namespace

namespace ssf2t {

void SetFightAreaFill(bool fill) {
  g_fill.store(fill, std::memory_order_relaxed);
}

void DebugSauloArmVictoryCapture() {  // DEBUG_SAULO
  g_debug_armed.store(true, std::memory_order_relaxed);
  REXLOG_WARN("DEBUG_SAULO F9_ARMED capture_pending=true");
}

}  // namespace ssf2t

// The fight world is drawn inside the scene draw and the fight render; the HUD is drawn after them.
REX_EXTERN(__imp__base013_sub_82181AA8);
REX_HOOK_RAW(base013_sub_82181AA8) { InScene<ssf2t::Base013Fight>(ctx, base, __imp__base013_sub_82181AA8); }
REX_EXTERN(__imp__base013_sub_823ED578);
REX_HOOK_RAW(base013_sub_823ED578) { InScene<ssf2t::Base013Fight>(ctx, base, __imp__base013_sub_823ED578); }
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
REX_HOOK_RAW(tu023_sub_82181AB8) { InScene<ssf2t::TitleUpdate023Fight>(ctx, base, __imp__tu023_sub_82181AB8); }
REX_EXTERN(__imp__tu023_sub_823EDC68);
REX_HOOK_RAW(tu023_sub_823EDC68) { InScene<ssf2t::TitleUpdate023Fight>(ctx, base, __imp__tu023_sub_823EDC68); }
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
