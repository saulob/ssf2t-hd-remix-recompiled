#include <array>
#include <cstdint>
#include <cstring>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#include "character/akuma_overlay.h"
#include "character/evil_ryu_actions.h"
#include "character/evil_ryu_moves.h"
#include "character/evil_ryu_palette.h"
#include "character/evil_ryu_sprites.h"
#include "character/fighter_identity.h"
#include "character/ultra_fight.h"
#include "character/ultra_fighters.h"
#include "character/ultra_navigation.h"
#include "character/ultra_select_presentation.h"
#include "guest_memory.h"
#include "netplay/fight_snapshot.h"
#include "netplay/guest_netplay.h"
#include "title_update_image.h"

REX_EXTERN(__imp__base013_sub_8217AC00);
REX_EXTERN(base013_sub_823FDF88);
REX_EXTERN(base013_sub_823F88C8);
REX_EXTERN(base013_sub_823F8938);
REX_EXTERN(base013_sub_823F8798);
REX_EXTERN(base013_sub_823F8EE8);
#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_8217AC20);
REX_EXTERN(tu023_sub_823FE638);
REX_EXTERN(tu023_sub_823F9038);
REX_EXTERN(tu023_sub_823F90A8);
REX_EXTERN(tu023_sub_823F8F08);
REX_EXTERN(tu023_sub_823F9658);
#endif

namespace ssf2t::character {
namespace {

struct Base013Hd {
  using Netplay = netplay::Base013Netplay;
  static constexpr uint32_t kColourP1 = 0x83C13392;
  static constexpr uint32_t kColourP2 = 0x83C13391;
  static constexpr uint32_t kRyuPaletteSlot10 = 0x83BE3300;
  static constexpr uint32_t kMysteryLargePortrait = 0x83BE59E4;
  static constexpr uint32_t kLoserPortraitSlot = 0x83BE5A74;
  static constexpr uint32_t kPortraitPaletteRows = 0x83BE3608;
  static void CreateSheet(PPCContext& ctx, uint8_t* base) { base013_sub_823FDF88(ctx, base); }
  static void LockPalette(PPCContext& ctx, uint8_t* base) { base013_sub_823F88C8(ctx, base); }
  static void UnlockPalette(PPCContext& ctx, uint8_t* base) { base013_sub_823F8938(ctx, base); }
  static void LockPixels(PPCContext& ctx, uint8_t* base) { base013_sub_823F8798(ctx, base); }
  static void UnlockPixels(PPCContext& ctx, uint8_t* base) { base013_sub_823F8EE8(ctx, base); }
};

#if SSF2T_TITLE_UPDATE_IMAGE
struct TitleUpdate023Hd {
  using Netplay = netplay::TitleUpdate023Netplay;
  static constexpr uint32_t kColourP1 = 0x83C13412;
  static constexpr uint32_t kColourP2 = 0x83C13411;
  static constexpr uint32_t kRyuPaletteSlot10 = 0x83BE3380;
  static constexpr uint32_t kMysteryLargePortrait = 0x83BE5A64;
  static constexpr uint32_t kLoserPortraitSlot = 0x83BE5AF4;
  static constexpr uint32_t kPortraitPaletteRows = 0x83BE3688;
  static void CreateSheet(PPCContext& ctx, uint8_t* base) { tu023_sub_823FE638(ctx, base); }
  static void LockPalette(PPCContext& ctx, uint8_t* base) { tu023_sub_823F9038(ctx, base); }
  static void UnlockPalette(PPCContext& ctx, uint8_t* base) { tu023_sub_823F90A8(ctx, base); }
  static void LockPixels(PPCContext& ctx, uint8_t* base) { tu023_sub_823F8F08(ctx, base); }
  static void UnlockPixels(PPCContext& ctx, uint8_t* base) { tu023_sub_823F9658(ctx, base); }
};
#endif

constexpr uint32_t kSpriteCodeOffset = 8;
constexpr uint32_t kSpriteAttrOffset = 10;
constexpr uint32_t kSpriteXOffset = 12;
constexpr uint32_t kSpriteYOffset = 14;
constexpr uint32_t kSpriteTextureOffset = 24;
constexpr uint32_t kSpriteRecordOffset = 28;
constexpr uint16_t kSpriteFlipped = 0x8000;
constexpr uint32_t kRecordXOffset = 12;
constexpr uint32_t kRecordYOffset = 14;
constexpr uint32_t kRecordWidthOffset = 16;
constexpr uint32_t kRecordHeightOffset = 18;
constexpr uint32_t kRecordTextureOffset = 24;
constexpr uint32_t kRecordBytes = 32;
constexpr uint32_t kTextureSizeOffset = 20;
constexpr uint32_t kLockScratchBytes = 16;
constexpr uint32_t kLockForWrite = 4;
constexpr uint32_t kAnimationRecordOffset = 0x1C;
constexpr uint32_t kFighterClassOffset = 3;
constexpr uint8_t kFighterClassSpecial = 12;
constexpr uint32_t kPaletteBytes = 1024;
constexpr uint32_t kPalettedFormat = 5;
constexpr uint32_t kIndexedType = 3;
constexpr uint32_t kSheetWrapperSize = 84;
constexpr uint32_t kSheetObjectOffset = 4;
constexpr uint32_t kDummySheetName = 0x82000A1C;
constexpr int kEvilPaletteSlot = 10;

bool PlausibleGuestPointer(uint32_t address) { return address >= 0x10000 && address < 0xFFFF0000u; }

bool IsPlayerOneFamily(uint16_t code) {
  return (code & 0xF100) == 0xE000 || (code & 0xFF00) == 0xCA00 || (code & 0xFF00) == 0xCC00;
}

constexpr uint16_t kRyuBodySlot = 0x00;
constexpr uint16_t kRyuBodySlotAlt = 0x11;

bool IsRyuBody(uint16_t code) {
  if ((code & 0xFE00) != 0xCA00) return false;
  const uint16_t slot = code & 0xFF;
  return slot == kRyuBodySlot || slot == kRyuBodySlotAlt;
}

struct EvilSheet {
  uint32_t wrapper = 0;
  int written_colour = -1;
  bool failed = false;
};

// The guest picks a sprite's palette texture from a per-colour table; a spare table slot holds a palette texture
// built with the guest's own texture constructor so it can be rewritten for the Evil Ryu colour.
template <typename H>
bool PrepareSheet(EvilSheet& sheet, int side, int index, const EvilRyuHdPalette& palette, PPCContext& ctx,
                  uint8_t* base) {
  if (sheet.failed) return false;
  if (sheet.wrapper == 0) {
    const PPCContext saved = ctx;
    ctx.r3.u64 = 32;
    ctx.r4.u64 = 32;
    ctx.r5.u64 = 5;
    ctx.r6.u64 = 3;
    ctx.r7.u64 = 0;
    ctx.r8.u64 = kDummySheetName;
    H::CreateSheet(ctx, base);
    const uint32_t wrapper = ctx.r3.u32;
    ctx = saved;
    if (!PlausibleGuestPointer(wrapper) || LoadBE32(GuestHost(base, wrapper), 0) != kSheetWrapperSize) {
      sheet.failed = true;
      REXLOG_ERROR("DEBUG_SAULO EVIL_RYU hd-palette sheet creation failed side={}", side + 1);
      return false;
    }
    sheet.wrapper = wrapper;
    REXLOG_INFO("DEBUG_SAULO EVIL_RYU hd-palette sheet side={} created", side + 1);
  }
  // The guest clears this row whenever it rebuilds its sprite tables.
  GuestStoreBE32(base, H::kRyuPaletteSlot10 + uint32_t(side) * 4, sheet.wrapper);
  if (sheet.written_colour == index) return true;
  const PPCContext saved = ctx;
  ctx.r3.u64 = sheet.wrapper + kSheetObjectOffset;
  ctx.r4.u64 = 0;
  H::LockPalette(ctx, base);
  const uint32_t memory = ctx.r3.u32;
  ctx = saved;
  if (!PlausibleGuestPointer(memory)) {
    sheet.failed = true;
    REXLOG_ERROR("DEBUG_SAULO EVIL_RYU hd-palette lock failed side={}", side + 1);
    return false;
  }
  uint8_t* target = GuestHost(base, memory);
  std::memcpy(target, palette.colours[index].data(), kHdPaletteBytes);
  ClearPaletteColourKey(target, kHdPaletteBytes);
  ctx.r3.u64 = sheet.wrapper + kSheetObjectOffset;
  H::UnlockPalette(ctx, base);
  ctx = saved;
  sheet.written_colour = index;
  REXLOG_INFO("DEBUG_SAULO EVIL_RYU hd-palette side={} palette={} applied", side + 1, index);
  return true;
}

struct EvilTexture {
  uint32_t wrapper = 0;
  uint32_t object = 0;
  uint32_t record = 0;
  bool failed = false;
};

// Mirrors the guest texture file loader so the art is an ordinary indexed texture every guest draw path accepts.
template <typename H>
bool UploadArt(EvilTexture& texture, const EvilSpriteImage& image, bool mirrored, PPCContext& ctx, uint8_t* base) {
  const PPCContext saved = ctx;
  ctx.r3.u64 = uint32_t(image.width);
  ctx.r4.u64 = uint32_t(image.height);
  ctx.r5.u64 = kPalettedFormat;
  ctx.r6.u64 = kIndexedType;
  ctx.r7.u64 = 0;
  ctx.r8.u64 = kDummySheetName;
  H::CreateSheet(ctx, base);
  const uint32_t wrapper = ctx.r3.u32;
  ctx = saved;
  if (!PlausibleGuestPointer(wrapper) || LoadBE32(GuestHost(base, wrapper), 0) != kSheetWrapperSize) return false;
  const uint32_t object = wrapper + kSheetObjectOffset;
  const uint32_t size = LoadBE32(GuestHost(base, object + kTextureSizeOffset), 0);
  const uint32_t scratch = REX_KERNEL_MEMORY()->SystemHeapAlloc(kLockScratchBytes);
  const uint32_t record = REX_KERNEL_MEMORY()->SystemHeapAlloc(kRecordBytes);
  if (scratch == 0 || record == 0) return false;
  GuestStoreBE32(base, scratch, 0);
  ctx.r3.u64 = object;
  ctx.r4.u64 = 0;
  ctx.r5.u64 = scratch;
  ctx.r6.u64 = kLockForWrite;
  H::LockPixels(ctx, base);
  const uint32_t pixels = ctx.r3.u32;
  ctx = saved;
  if (!PlausibleGuestPointer(pixels)) return false;
  const uint32_t pitch = LoadBE32(GuestHost(base, scratch), 0);
  const bool fits = pitch >= uint32_t(image.width) && size >= pitch * uint32_t(image.height);
  if (fits) {
    uint8_t* target = GuestHost(base, pixels);
    std::memset(target, 0, size);
    WriteIndexedRows(target, pitch, image, mirrored);
  } else {
    REXLOG_ERROR("DEBUG_SAULO EVIL_RYU art layout pitch={} size={}", pitch, size);
  }
  REX_KERNEL_MEMORY()->SystemHeapFree(scratch);
  ctx.r3.u64 = object;
  ctx.r4.u64 = 0;
  ctx.r5.u64 = 0;
  H::UnlockPixels(ctx, base);
  ctx = saved;
  if (!fits) return false;
  if (image.has_palette) {
    ctx.r3.u64 = object;
    ctx.r4.u64 = 0;
    H::LockPalette(ctx, base);
    const uint32_t memory = ctx.r3.u32;
    ctx = saved;
    if (!PlausibleGuestPointer(memory)) return false;
    uint8_t* target = GuestHost(base, memory);
    std::memcpy(target, image.palette.data(), kPaletteBytes);
    ClearPaletteColourKey(target, kPaletteBytes);
    ctx.r3.u64 = object;
    H::UnlockPalette(ctx, base);
    ctx = saved;
  }
  texture.wrapper = wrapper;
  texture.object = object;
  texture.record = record;
  return true;
}

template <typename H>
const EvilTexture* TextureFor(EvilArt art, PPCContext& ctx, uint8_t* base) {
  static std::array<EvilTexture, kEvilArtCount> textures;
  EvilTexture& texture = textures[size_t(art)];
  if (texture.failed) return nullptr;
  if (texture.object != 0) return &texture;
  const EvilSpriteImage* image = LoadedEvilArt(art);
  if (!image || !UploadArt<H>(texture, *image, art == EvilArt::kLargePortrait, ctx, base)) {
    texture.failed = true;
    REXLOG_ERROR("DEBUG_SAULO EVIL_RYU art={} unavailable", int(art));
    return nullptr;
  }
  REXLOG_INFO("DEBUG_SAULO EVIL_RYU art={} uploaded", int(art));
  return &texture;
}

struct SpriteRestore {
  bool active = false;
  uint16_t x = 0;
  uint16_t y = 0;
  uint32_t texture = 0;
  uint32_t record = 0;
};

template <typename H>
EvilPose PoseOf(const uint8_t* base, int side) {
  const uint32_t block = LoadBE32(GuestHost(base, H::Netplay::kFighterBlockPointer), 0);
  if (!PlausibleGuestPointer(block)) return EvilPose::kNone;
  const uint32_t fighter = block + netplay::kFighterOffsets[side];
  return EvilPoseAtRecord(LoadBE32(GuestHost(base, fighter + kAnimationRecordOffset), 0));
}

template <typename H>
EvilRenderState RenderStateOf(const uint8_t* base, int side, EvilPose pose) {
  const uint32_t block = LoadBE32(GuestHost(base, H::Netplay::kFighterBlockPointer), 0);
  const uint32_t fighter = block + netplay::kFighterOffsets[side];
  const bool special = *GuestHost(base, fighter + kFighterClassOffset) == kFighterClassSpecial;
  return RenderStateFor(GlobalHostMoves().For(side), special, pose != EvilPose::kNone);
}

// The guest places a sprite at the object position plus the record offset, or minus offset and width when mirrored,
// so the carrier is shifted to where the Evil Ryu art's own record would have put it. A placement without a width keeps
// the carrier's geometry.
SpriteRestore ReplaceSprite(uint32_t sprite, const EvilTexture& texture, const EvilSpritePlacement& place, uint8_t* base) {
  SpriteRestore restore;
  const uint32_t carrier = LoadBE32(GuestHost(base, sprite + kSpriteRecordOffset), 0);
  if (!PlausibleGuestPointer(carrier)) return restore;
  restore.active = true;
  restore.x = LoadBE16(GuestHost(base, sprite + kSpriteXOffset), 0);
  restore.y = LoadBE16(GuestHost(base, sprite + kSpriteYOffset), 0);
  restore.texture = LoadBE32(GuestHost(base, sprite + kSpriteTextureOffset), 0);
  restore.record = carrier;
  std::memcpy(GuestHost(base, texture.record), GuestHost(base, carrier), kRecordBytes);
  if (place.width != 0) {
    const int carrier_x = int16_t(LoadBE16(GuestHost(base, carrier + kRecordXOffset), 0));
    const int carrier_y = int16_t(LoadBE16(GuestHost(base, carrier + kRecordYOffset), 0));
    const int carrier_width = LoadBE16(GuestHost(base, carrier + kRecordWidthOffset), 0);
    const bool flipped = (LoadBE16(GuestHost(base, sprite + kSpriteAttrOffset), 0) & kSpriteFlipped) != 0;
    const int shift = flipped ? (carrier_x + carrier_width) - (place.x + place.width) : place.x - carrier_x;
    GuestStoreBE16(base, texture.record + kRecordXOffset, uint16_t(place.x));
    GuestStoreBE16(base, texture.record + kRecordYOffset, uint16_t(place.y));
    GuestStoreBE16(base, texture.record + kRecordWidthOffset, place.width);
    GuestStoreBE16(base, texture.record + kRecordHeightOffset, place.height);
    GuestStoreBE16(base, sprite + kSpriteXOffset, uint16_t(int(restore.x) + shift));
    GuestStoreBE16(base, sprite + kSpriteYOffset, uint16_t(int(restore.y) + place.y - carrier_y));
  }
  GuestStoreBE32(base, texture.record + kRecordTextureOffset, texture.object);
  GuestStoreBE32(base, sprite + kSpriteTextureOffset, texture.object);
  GuestStoreBE32(base, sprite + kSpriteRecordOffset, texture.record);
  return restore;
}

void RestoreSprite(uint32_t sprite, const SpriteRestore& restore, uint8_t* base) {
  if (!restore.active) return;
  GuestStoreBE16(base, sprite + kSpriteXOffset, restore.x);
  GuestStoreBE16(base, sprite + kSpriteYOffset, restore.y);
  GuestStoreBE32(base, sprite + kSpriteTextureOffset, restore.texture);
  GuestStoreBE32(base, sprite + kSpriteRecordOffset, restore.record);
}

void LogNameTile(uint16_t code, uint16_t attr, int x, int side) {
  if (side < 0) return;
  static std::array<uint32_t, 2> last{{~0u, ~0u}};
  const uint32_t state = uint32_t(code) << 16 | attr;
  if (state == last[side]) return;
  last[side] = state;
  REXLOG_INFO("DEBUG_SAULO EVIL_RYU name-tile side={} code={:03X} attr={:04X} x={} identity={}", side + 1, code, attr,
              x, int(RequestedIdentity(side)));
}

template <typename H>
bool DrawEvilName(uint32_t sprite, uint16_t code, PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  const uint16_t attr = LoadBE16(GuestHost(base, sprite + kSpriteAttrOffset), 0);
  const int x = int16_t(LoadBE16(GuestHost(base, sprite + kSpriteXOffset), 0));
  const int side = RyuNameSide(code, attr, x);
  LogNameTile(code, attr, x, side);
  if (side < 0 || RequestedIdentity(side) != FighterIdentity::kEvilRyu) return false;
  const EvilTexture* texture = TextureFor<H>(code == kRyuHudNameTile ? EvilArt::kHudName : EvilArt::kName, ctx, base);
  if (!texture) return false;
  const SpriteRestore restore = ReplaceSprite(sprite, *texture, {}, base);
  original(ctx, base);
  RestoreSprite(sprite, restore, base);
  NoteGuestNameDrawn(side);
  return true;
}

void LogBodyDraw(int side, uint16_t code, EvilRenderState state, const uint8_t* base, uint32_t sprite) {
  static std::array<std::array<bool, 6>, 2> logged{};
  bool& seen = logged[size_t(side)][size_t(state)];
  if (seen) return;
  seen = true;
  REXLOG_INFO("DEBUG_SAULO EVIL_RYU body-draw side={} code={:04X} attr={:04X} state={}", side + 1, code,
              LoadBE16(GuestHost(base, sprite + kSpriteAttrOffset), 0), int(state));
}

template <typename H>
void DrawSprite(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (!UltraFightersEnabled()) return original(ctx, base);
  const uint32_t sprite = ctx.r3.u32;
  if (!PlausibleGuestPointer(sprite)) return original(ctx, base);
  const uint16_t code = LoadBE16(GuestHost(base, sprite + kSpriteCodeOffset), 0);
  if (DrawEvilName<H>(sprite, code, ctx, base, original)) return;
  if (CharacterSelectActive()) return original(ctx, base);
  const bool evil_p1 = EvilRyuOnSide<typename H::Netplay>(base, 0);
  const bool evil_p2 = EvilRyuOnSide<typename H::Netplay>(base, 1);
  if (!evil_p1 && !evil_p2) return original(ctx, base);
  const int side = IsPlayerOneFamily(code) ? 0 : 1;
  if (!(side == 0 ? evil_p1 : evil_p2)) return original(ctx, base);
  const bool body = IsRyuBody(code);
  const EvilPose pose = body ? PoseOf<H>(base, side) : EvilPose::kNone;
  if (body) {
    const EvilRenderState state = RenderStateOf<H>(base, side, pose);
    LogBodyDraw(side, code, state, base, sprite);
    if (BodySuppressed(state, GlobalHostMoves().For(side).ticks)) return;
  }
  const EvilRyuHdPalette* palette = LoadedEvilRyuHdPalette();
  uint8_t* colour_byte = base + (side == 0 ? H::kColourP1 : H::kColourP2);
  const int colour = *colour_byte;
  if (!palette || colour >= kPaletteColours) {
    static bool logged = false;
    if (!logged) REXLOG_ERROR("DEBUG_SAULO EVIL_RYU hd-palette unavailable colour={}", colour);
    logged = true;
    return original(ctx, base);
  }
  static std::array<EvilSheet, 2> sheets;
  const int entry = HdPaletteEntry(colour, side, evil_p1 && evil_p2);
  if (!PrepareSheet<H>(sheets[side], side, entry, *palette, ctx, base)) return original(ctx, base);
  // The USF2 sprites carry no palette of their own, so they are only shown once the Evil Ryu palette is in place.
  SpriteRestore restore;
  if (pose != EvilPose::kNone) {
    if (const EvilTexture* texture = TextureFor<H>(ArtFor(pose), ctx, base)) {
      restore = ReplaceSprite(sprite, *texture, SpritePlacement(pose), base);
    }
  }
  *colour_byte = uint8_t(kEvilPaletteSlot + side);
  original(ctx, base);
  *colour_byte = uint8_t(colour);
  RestoreSprite(sprite, restore, base);
}

constexpr std::array<uint32_t, 2> kHoverWords = {0x83A21374, 0x83A21378};
constexpr uint32_t kPresentationMode = 0x83A2130C;
constexpr uint32_t kNoPortrait = 0xFFFFFFFF;
constexpr uint32_t kEvilPortraitId = kEvilPortraitSlot;
constexpr uint32_t kPortraitPaletteColumns = 10;
constexpr uint32_t kPortraitRowBytes = kPortraitPaletteColumns * 4;
constexpr std::array<uint32_t, 4> kSharedPortraitModes = {2, 3, 5, 9};

bool UsesSharedPortraits(uint32_t mode) {
  for (const uint32_t shared : kSharedPortraitModes) {
    if (mode == shared) return true;
  }
  return false;
}

void LogSelectPresentation(const SelectPresentation& presentation, const std::array<FighterIdentity, 2>& requested,
                           const std::array<bool, 2>& on_cell, const std::array<uint32_t, 2>& hover) {
  static std::array<uint32_t, 2> last{{~0u, ~0u}};
  for (int side = 0; side < 2; ++side) {
    const uint32_t state = uint32_t(presentation.evil[side]) | uint32_t(presentation.hide[side]) << 1 |
                           uint32_t(requested[side]) << 2 | uint32_t(on_cell[side]) << 8 | (hover[side] & 0xFF) << 16;
    if (state == last[side]) continue;
    last[side] = state;
    REXLOG_INFO("DEBUG_SAULO EVIL_RYU select-portrait side={} evil={} hidden={} requested={} cell={} hover={}",
                side + 1, presentation.evil[side], presentation.hide[side], int(requested[side]), on_cell[side],
                hover[side]);
  }
}

// Both sides read the same per-character portrait tables, so an Evil Ryu player gets the unused id after Akuma: its
// large portrait slot holds the Evil Ryu texture. Its palette row names that same texture, which keeps the texture's own
// palette; the winner screen only draws the native portrait frame for a character whose row is filled. That screen
// reads the loser from a second table, loaded for the guest's hover ids only, so its id 17 entry is lent as well.
template <typename H>
void DrawPortraits(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (!UltraFightersEnabled() || !UsesSharedPortraits(LoadBE32(GuestHost(base, kPresentationMode), 0))) {
    return original(ctx, base);
  }
  std::array<FighterIdentity, 2> requested{RequestedIdentity(0), RequestedIdentity(1)};
  std::array<bool, 2> on_cell{GlobalUltraNavigation().OnEvilRyu(0), GlobalUltraNavigation().OnEvilRyu(1)};
  std::array<uint32_t, 2> hover{LoadBE32(GuestHost(base, kHoverWords[0]), 0),
                                LoadBE32(GuestHost(base, kHoverWords[1]), 0)};
  const SelectPresentation presentation = ResolveSelectPresentation(requested, on_cell, hover);
  LogSelectPresentation(presentation, requested, on_cell, hover);
  if (!presentation.any()) return original(ctx, base);
  const std::array<bool, 2>& evil = presentation.evil;
  const EvilTexture* texture = TextureFor<H>(EvilArt::kLargePortrait, ctx, base);
  const uint32_t replacement = texture ? kEvilPortraitId : kNoPortrait;
  const uint32_t mystery = LoadBE32(GuestHost(base, H::kMysteryLargePortrait), 0);
  const uint32_t loser = LoadBE32(GuestHost(base, H::kLoserPortraitSlot), 0);
  const uint32_t row = H::kPortraitPaletteRows + kEvilPortraitId * kPortraitRowBytes;
  std::array<uint8_t, kPortraitRowBytes> saved_row;
  std::memcpy(saved_row.data(), GuestHost(base, row), kPortraitRowBytes);
  if (texture) {
    GuestStoreBE32(base, H::kMysteryLargePortrait, texture->wrapper);
    GuestStoreBE32(base, H::kLoserPortraitSlot, texture->wrapper);
    for (uint32_t column = 0; column < kPortraitPaletteColumns; ++column) {
      GuestStoreBE32(base, row + column * 4, texture->wrapper);
    }
  }
  for (int side = 0; side < 2; ++side) {
    if (evil[side]) GuestStoreBE32(base, kHoverWords[side], replacement);
    if (presentation.hide[side]) GuestStoreBE32(base, kHoverWords[side], kNoPortrait);
  }
  original(ctx, base);
  for (int side = 0; side < 2; ++side) {
    if (evil[side]) GuestStoreBE32(base, kHoverWords[side], uint32_t(kRyuGuestCharacter));
    if (presentation.hide[side]) GuestStoreBE32(base, kHoverWords[side], hover[side]);
  }
  if (texture) {
    GuestStoreBE32(base, H::kMysteryLargePortrait, mystery);
    GuestStoreBE32(base, H::kLoserPortraitSlot, loser);
    std::memcpy(base + row, saved_row.data(), kPortraitRowBytes);
  }
}

}  // namespace
}  // namespace ssf2t::character

REX_HOOK_RAW(base013_sub_8217AC00) {
  ssf2t::character::DrawSprite<ssf2t::character::Base013Hd>(ctx, base, __imp__base013_sub_8217AC00);
}
REX_EXTERN(__imp__base013_sub_82179ED8);
REX_HOOK_RAW(base013_sub_82179ED8) {
  ssf2t::character::DrawPortraits<ssf2t::character::Base013Hd>(ctx, base, __imp__base013_sub_82179ED8);
}

#if SSF2T_TITLE_UPDATE_IMAGE
REX_HOOK_RAW(tu023_sub_8217AC20) {
  ssf2t::character::DrawSprite<ssf2t::character::TitleUpdate023Hd>(ctx, base, __imp__tu023_sub_8217AC20);
}
REX_EXTERN(__imp__tu023_sub_82179EF8);
REX_HOOK_RAW(tu023_sub_82179EF8) {
  ssf2t::character::DrawPortraits<ssf2t::character::TitleUpdate023Hd>(ctx, base, __imp__tu023_sub_82179EF8);
}
#endif
