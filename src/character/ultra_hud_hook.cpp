#include <array>
#include <cstdint>
#include <cstring>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#include "character/evil_ryu_sprites.h"
#include "character/fighter_identity.h"
#include "character/guest_argb_texture.h"
#include "character/ultra_fighters.h"
#include "character/violent_ken_data.h"
#include "guest_memory.h"
#include "title_update_image.h"

REX_EXTERN(__imp__base013_sub_823D01A0);
REX_EXTERN(base013_sub_82450A88);
REX_EXTERN(base013_sub_824325A0);
REX_EXTERN(base013_sub_823FDF88);
REX_EXTERN(base013_sub_823F92E0);
#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_823D0780);
REX_EXTERN(tu023_sub_824515C8);
REX_EXTERN(tu023_sub_82432CD8);
REX_EXTERN(tu023_sub_823FE638);
REX_EXTERN(tu023_sub_823F9A50);
#endif

namespace ssf2t::character {
namespace {

struct Base013Hud {
  static constexpr uint32_t kWidgets = 0x83C27568;
  static constexpr uint32_t kFightHudActive = 0x83E09DB0;
  static constexpr std::array<uint32_t, 2> kCharacter = {0x83C133A0, 0x83C1339C};
  static void SetText(PPCContext& ctx, uint8_t* base) { base013_sub_82450A88(ctx, base); }
  static void FindElement(PPCContext& ctx, uint8_t* base) { base013_sub_824325A0(ctx, base); }
  static void CreateTexture(PPCContext& ctx, uint8_t* base) { base013_sub_823FDF88(ctx, base); }
  static void ReadTexture(PPCContext& ctx, uint8_t* base) { base013_sub_823F92E0(ctx, base); }
};

#if SSF2T_TITLE_UPDATE_IMAGE
struct TitleUpdate023Hud {
  static constexpr uint32_t kWidgets = 0x83C278E0;
  static constexpr uint32_t kFightHudActive = 0x83E0A2C8;
  static constexpr std::array<uint32_t, 2> kCharacter = {0x83C13420, 0x83C1341C};
  static void SetText(PPCContext& ctx, uint8_t* base) { tu023_sub_824515C8(ctx, base); }
  static void FindElement(PPCContext& ctx, uint8_t* base) { tu023_sub_82432CD8(ctx, base); }
  static void CreateTexture(PPCContext& ctx, uint8_t* base) { tu023_sub_823FE638(ctx, base); }
  static void ReadTexture(PPCContext& ctx, uint8_t* base) { tu023_sub_823F9A50(ctx, base); }
};
#endif

constexpr std::array<uint32_t, 2> kFirstNameWidget = {8, 32};
constexpr int kNameWidgets = 6;
constexpr std::array<uint32_t, 2> kRyuPortrait = {ElementHash("p1_portrait_Ryu"), ElementHash("p2_portrait_Ryu")};
constexpr std::array<uint32_t, 2> kKenPortrait = {ElementHash("p1_portrait_Ken"), ElementHash("p2_portrait_Ken")};
constexpr char kViolentKenHudName[] = "Violent Ken";

constexpr uint32_t kElementType = 20;
constexpr uint32_t kElementTypeGraphic = 3;
constexpr uint32_t kGraphicKind = 368;
constexpr uint32_t kGraphicKindImage = 0;
constexpr uint32_t kImageTextureCell = 384;

constexpr uint8_t kIconFileFlags = 2;

bool PlausibleGuestPointer(uint32_t address) { return address >= 0x10000 && address < 0xFFFF0000u; }

uint32_t GuestAlloc(uint32_t bytes) { return REX_KERNEL_MEMORY()->SystemHeapAlloc(bytes); }

template <typename H>
struct IconState {
  uint32_t wrapper = 0;
  uint32_t cell = 0;
  bool failed = false;
};

template <typename H>
uint32_t IconCell(bool violent, PPCContext& ctx, uint8_t* base) {
  static std::array<IconState<H>, 2> states;
  IconState<H>& state = states[violent ? 1 : 0];
  if (state.failed) return 0;
  if (state.cell == 0) {
    const EvilHudIcon* icon = violent ? LoadedViolentKenHudIcon() : LoadedEvilHudIcon();
    state.wrapper = icon ? BuildArgbTexture<H>(icon->data(), kHudIconSize, kHudIconSize, kIconFileFlags, ctx, base) : 0;
    state.cell = state.wrapper != 0 ? GuestAlloc(4) : 0;
    if (state.cell == 0) {
      state.failed = true;
      REXLOG_ERROR("DEBUG_SAULO EVIL_RYU hud-icon unavailable");
      return 0;
    }
    REXLOG_INFO("DEBUG_SAULO EVIL_RYU hud-icon wrapper={:08X}", state.wrapper);
  }
  // Tearing down an element zeroes the cell it holds.
  GuestStoreBE32(base, state.cell, state.wrapper);
  return state.cell;
}

template <typename H>
uint32_t FindImage(uint32_t hash, PPCContext& ctx, uint8_t* base) {
  const PPCContext saved = ctx;
  ctx.r4.u64 = hash;
  H::FindElement(ctx, base);
  const uint32_t element = ctx.r3.u32;
  ctx = saved;
  if (!PlausibleGuestPointer(element)) return 0;
  const bool image = LoadBE32(GuestHost(base, element + kElementType), 0) == kElementTypeGraphic &&
                     LoadBE32(GuestHost(base, element + kGraphicKind), 0) == kGraphicKindImage;
  return image ? element : 0;
}

template <typename H>
void ApplyPortrait(int side, bool evil, bool violent, PPCContext& ctx, uint8_t* base) {
  static std::array<std::array<uint32_t, 2>, 2> native_cells{};
  const uint32_t element = FindImage<H>((violent ? kKenPortrait : kRyuPortrait)[size_t(side)], ctx, base);
  if (element == 0) return;
  std::array<uint32_t, 2>& native_cell = native_cells[violent ? 1 : 0];
  const uint32_t cell = IconCell<H>(violent, ctx, base);
  if (cell == 0) return;
  const uint32_t current = LoadBE32(GuestHost(base, element + kImageTextureCell), 0);
  const uint32_t routed = RoutePortraitCell(evil, current, cell, native_cell[size_t(side)]);
  if (routed != current) GuestStoreBE32(base, element + kImageTextureCell, routed);
}

template <typename H>
uint32_t HudNameText(bool violent, uint8_t* base) {
  static std::array<uint32_t, 2> names{};
  uint32_t& name = names[violent ? 1 : 0];
  if (name != 0) return name;
  const char* text = violent ? kViolentKenHudName : kEvilRyuHudName;
  const size_t bytes = std::strlen(text) + 1;
  name = GuestAlloc(uint32_t(bytes));
  if (name != 0) std::memcpy(GuestHost(base, name), text, bytes);
  return name;
}

template <typename H>
void ApplyName(int side, bool violent, PPCContext& ctx, uint8_t* base) {
  const uint32_t name = HudNameText<H>(violent, base);
  if (name == 0) return;
  for (int i = 0; i < kNameWidgets; ++i) {
    const uint32_t slot = H::kWidgets + kFirstNameWidget[size_t(side)] + uint32_t(i) * 4;
    const uint32_t widget = LoadBE32(GuestHost(base, slot), 0);
    if (!PlausibleGuestPointer(widget)) continue;
    const PPCContext saved = ctx;
    ctx.r3.u64 = widget;
    ctx.r4.u64 = name;
    ctx.r5.u64 = 1;
    H::SetText(ctx, base);
    ctx = saved;
  }
}

// Runs after the guest has written the Ryu name and shown the Ryu portrait for the frame.
template <typename H>
void UpdateHud(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  original(ctx, base);
  if (LoadBE32(GuestHost(base, H::kWidgets), 0) == 0 || *GuestHost(base, H::kFightHudActive) == 0) return;
  static std::array<std::array<bool, 2>, 2> applied{};
  for (int side = 0; side < 2; ++side) {
    const int character = int(LoadBE32(GuestHost(base, H::kCharacter[size_t(side)]), 0));
    const FighterIdentity requested = RequestedIdentity(side);
    const std::array<bool, 2> shown = {HudShowsEvilRyu(UltraFightersEnabled(), requested, character),
                                       HudShowsViolentKen(UltraFightersEnabled(), requested, character)};
    for (int kind = 0; kind < 2; ++kind) {
      const bool show = shown[size_t(kind)];
      bool& was = applied[size_t(kind)][size_t(side)];
      if (!show && !was) continue;
      ApplyPortrait<H>(side, show, kind == 1, ctx, base);
      if (show) ApplyName<H>(side, kind == 1, ctx, base);
      if (show != was) {
        REXLOG_INFO("DEBUG_SAULO ULTRA hud side={} kind={} action={}", side + 1, kind, show ? "ultra" : "restore");
      }
      was = show;
    }
  }
}

}  // namespace
}  // namespace ssf2t::character

REX_HOOK_RAW(base013_sub_823D01A0) {
  ssf2t::character::UpdateHud<ssf2t::character::Base013Hud>(ctx, base, __imp__base013_sub_823D01A0);
}

#if SSF2T_TITLE_UPDATE_IMAGE
REX_HOOK_RAW(tu023_sub_823D0780) {
  ssf2t::character::UpdateHud<ssf2t::character::TitleUpdate023Hud>(ctx, base, __imp__tu023_sub_823D0780);
}
#endif
