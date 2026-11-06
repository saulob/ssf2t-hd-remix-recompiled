#include <array>
#include <cstdint>
#include <filesystem>

#include <rex/hook.h>
#include <rex/logging.h>

#include "character/akuma_navigation.h"
#include "character/evil_ryu_sprites.h"
#include "character/guest_argb_texture.h"
#include "character/roster_cells.h"
#include "character/roster_cells_hook.h"
#include "character/secret_select.h"
#include "character/ultra_navigation.h"
#include "guest_memory.h"
#include "title_update_image.h"

REX_EXTERN(__imp__base013_sub_82177728);
REX_EXTERN(base013_sub_82175E48);
REX_EXTERN(base013_sub_823F8550);
REX_EXTERN(base013_sub_823FDF88);
REX_EXTERN(base013_sub_823F92E0);
#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_82177748);
REX_EXTERN(tu023_sub_82175E68);
REX_EXTERN(tu023_sub_823F8CC0);
REX_EXTERN(tu023_sub_823FE638);
REX_EXTERN(tu023_sub_823F9A50);
#endif

namespace ssf2t::character {
namespace {

struct Base013Roster {
  static constexpr uint32_t kLive = 0x83CA6C9C;
  static constexpr uint32_t kLayout = 0x83CA290E;
  static constexpr uint32_t kMode = 0x83CA294F;
  static void DrawQuad(PPCContext& ctx, uint8_t* base) { base013_sub_82175E48(ctx, base); }
  static void BindTexture(PPCContext& ctx, uint8_t* base) { base013_sub_823F8550(ctx, base); }
  static void CreateTexture(PPCContext& ctx, uint8_t* base) { base013_sub_823FDF88(ctx, base); }
  static void ReadTexture(PPCContext& ctx, uint8_t* base) { base013_sub_823F92E0(ctx, base); }
};

#if SSF2T_TITLE_UPDATE_IMAGE
struct TitleUpdate023Roster {
  static constexpr uint32_t kLive = 0x83CA71B4;
  static constexpr uint32_t kLayout = 0x83CA2E26;
  static constexpr uint32_t kMode = 0x83CA2E67;
  static void DrawQuad(PPCContext& ctx, uint8_t* base) { tu023_sub_82175E68(ctx, base); }
  static void BindTexture(PPCContext& ctx, uint8_t* base) { tu023_sub_823F8CC0(ctx, base); }
  static void CreateTexture(PPCContext& ctx, uint8_t* base) { tu023_sub_823FE638(ctx, base); }
  static void ReadTexture(PPCContext& ctx, uint8_t* base) { tu023_sub_823F9A50(ctx, base); }
};
#endif

constexpr uint32_t kOpaqueWhite = 0xFFFFFFFFu;
constexpr uint8_t kCellFileFlags = 0;

std::filesystem::path g_packages;
std::filesystem::path g_game;

PortraitImage LoadUltraCell(UltraPortrait which) {
  PortraitImage image = LoadUltraPortrait(g_packages / kUltraPortraitFile, which);
  if (!image.error) ApplyRosterFrame(image, LoadAkumaPortrait(g_game / "flog_x360.fpg"));
  return image;
}

PortraitImage LoadCellArt(RosterArt art) {
  switch (art) {
    case RosterArt::kAkuma:
      return LoadAkumaPortrait(g_game / "flog_x360.fpg");
    case RosterArt::kEvilRyu:
      return LoadUltraCell(UltraPortrait::kEvilRyu);
    case RosterArt::kViolentKen:
      return LoadUltraCell(UltraPortrait::kViolentKen);
    case RosterArt::kEvilRyuName:
      return LoadEvilRyuAsset(g_packages / kEvilRyuAssetFile, EvilRyuAsset::kNameGraphic);
    case RosterArt::kViolentKenName:
      return LoadEvilRyuAsset(g_packages / kViolentKenAssetFile, EvilRyuAsset::kNameGraphic);
  }
  return {};
}

struct CellTexture {
  bool attempted = false;
  uint32_t wrapper = 0;
  GuestCellImage image;
};

template <typename H>
const CellTexture* TextureFor(RosterArt art, PPCContext& ctx, uint8_t* base) {
  static std::array<CellTexture, kRosterArtCount> textures;
  CellTexture& texture = textures[size_t(art)];
  if (!texture.attempted) {
    texture.attempted = true;
    const PortraitImage image = LoadCellArt(art);
    if (image.error) {
      REXLOG_ERROR("Roster cell {} failed at '{}'", int(art), image.error);
      return nullptr;
    }
    texture.image = ToGuestCellImage(image);
    texture.wrapper = BuildArgbTexture<H>(texture.image.argb.data(), uint32_t(texture.image.width),
                                          uint32_t(texture.image.height), kCellFileFlags, ctx, base);
    texture.image.argb = {};
    if (texture.wrapper == 0) REXLOG_ERROR("Roster cell {} failed at 'create texture'", int(art));
  }
  return texture.wrapper != 0 ? &texture : nullptr;
}

template <typename H>
void DrawCell(uint32_t wrapper, const ArcadeQuad& quad, PPCContext& ctx, uint8_t* base) {
  ctx.r3.u64 = wrapper + kArgbTextureObject;
  H::BindTexture(ctx, base);
  ctx.f1.f64 = quad.x;
  ctx.f2.f64 = quad.y;
  ctx.f3.f64 = quad.width;
  ctx.f4.f64 = quad.height;
  ctx.r7.u64 = kOpaqueWhite;
  ctx.r8.u64 = 0;
  H::DrawQuad(ctx, base);
}

template <typename H>
void DrawPictureCell(RosterArt art, const ViewRect& picture, PPCContext& ctx, uint8_t* base) {
  if (const CellTexture* texture = TextureFor<H>(art, ctx, base)) {
    DrawCell<H>(texture->wrapper, PictureToArcade(PaddedPicture(picture, texture->image)), ctx, base);
  }
}

template <typename H>
void DrawNamePlate(int side, RosterArt art, PPCContext& ctx, uint8_t* base) {
  if (const CellTexture* texture = TextureFor<H>(art, ctx, base)) {
    const ViewRect plate = NamePlatePicture(side, texture->image.art_width, texture->image.art_height);
    DrawCell<H>(texture->wrapper, PictureToArcade(PaddedPicture(plate, texture->image)), ctx, base);
  }
}

template <typename H>
RosterCellInputs ReadInputs(const uint8_t* base) {
  const SecretCells cells = SecretCellsFor(*GuestHost(base, H::kMode));
  RosterCellInputs in;
  in.akuma = cells.akuma;
  in.evil_ryu = cells.evil_ryu;
  in.violent_ken = cells.violent_ken;
  in.live1 = *GuestHost(base, H::kLive);
  in.live2 = *GuestHost(base, H::kLive + 1);
  for (int side = 0; side < 2; ++side) {
    in.on_evil_ryu[size_t(side)] = GlobalUltraNavigation().OnEvilRyu(side);
    in.on_violent_ken[size_t(side)] = GlobalUltraNavigation().OnViolentKen(side);
    in.guest_name_drawn[size_t(side)] = GuestNameDrawnRecently(side);
  }
  return in;
}

// Runs at the end of the pass that draws the original cells, so the cells share the pass's lifetime and every later
// layer: the cursors, the markers and the guest's full screen fade quad all land on top of them.
template <typename H>
void DrawRoster(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  original(ctx, base);
  const RosterCellPlan plan = PlanRosterCells(ReadInputs<H>(base));
  if (!plan.any()) return;
  const SelectLayout layout = *GuestHost(base, H::kLayout) != 0 ? SelectLayout::kVersus : SelectLayout::kArcade;
  const PPCContext saved = ctx;
  if (plan.akuma) DrawPictureCell<H>(RosterArt::kAkuma, AkumaPortraitRect(kGuestPicture, kPictureWidth), ctx, base);
  if (plan.evil_ryu) {
    DrawPictureCell<H>(RosterArt::kEvilRyu,
                       UltraPortraitRect(kGuestPicture, kPictureWidth, UltraPortrait::kEvilRyu, layout), ctx, base);
  }
  if (plan.violent_ken) {
    DrawPictureCell<H>(RosterArt::kViolentKen,
                       UltraPortraitRect(kGuestPicture, kPictureWidth, UltraPortrait::kViolentKen, layout), ctx, base);
  }
  for (int side = 0; side < 2; ++side) {
    if (plan.names[size_t(side)]) DrawNamePlate<H>(side, *plan.names[size_t(side)], ctx, base);
  }
  ctx = saved;
}

}  // namespace

void SetRosterCellDirectories(const std::filesystem::path& packages, const std::filesystem::path& game) {
  g_packages = packages;
  g_game = game;
}

}  // namespace ssf2t::character

REX_HOOK_RAW(base013_sub_82177728) {
  ssf2t::character::DrawRoster<ssf2t::character::Base013Roster>(ctx, base, __imp__base013_sub_82177728);
}

#if SSF2T_TITLE_UPDATE_IMAGE
REX_HOOK_RAW(tu023_sub_82177748) {
  ssf2t::character::DrawRoster<ssf2t::character::TitleUpdate023Roster>(ctx, base, __imp__tu023_sub_82177748);
}
#endif
