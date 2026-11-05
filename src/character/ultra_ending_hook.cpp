#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#include "character/character_moves.h"
#include "character/fighter_identity.h"
#include "character/guest_argb_texture.h"
#include "character/ultra_ending.h"
#include "character/ultra_fighters.h"
#include "character/ultra_navigation.h"
#include "guest_memory.h"
#include "title_update_image.h"

REX_EXTERN(base013_sub_823CBCE0);
REX_EXTERN(base013_sub_824308A0);
REX_EXTERN(base013_sub_8243E780);
REX_EXTERN(base013_sub_8243F158);
REX_EXTERN(base013_sub_82441828);
REX_EXTERN(base013_sub_82376740);
REX_EXTERN(base013_sub_82376908);
REX_EXTERN(base013_sub_823C66D0);
REX_EXTERN(base013_sub_823FDF88);
REX_EXTERN(base013_sub_823F92E0);
#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(tu023_sub_823CC2C0);
REX_EXTERN(tu023_sub_82430FE0);
REX_EXTERN(tu023_sub_8243EF50);
REX_EXTERN(tu023_sub_8243F928);
REX_EXTERN(tu023_sub_82441FF8);
REX_EXTERN(tu023_sub_82376810);
REX_EXTERN(tu023_sub_823769D8);
REX_EXTERN(tu023_sub_823C6CF8);
REX_EXTERN(tu023_sub_823FE638);
REX_EXTERN(tu023_sub_823F9A50);
#endif

namespace ssf2t::character {
namespace {

struct Base013Ending {
  static constexpr uint32_t kEndingCharacter = 0x83BE5904;
  static void LoadScreen(PPCContext& ctx, uint8_t* base) { base013_sub_823CBCE0(ctx, base); }
  static void Hash(PPCContext& ctx, uint8_t* base) { base013_sub_824308A0(ctx, base); }
  static void FindElement(PPCContext& ctx, uint8_t* base) { base013_sub_8243E780(ctx, base); }
  static void SetAlpha(PPCContext& ctx, uint8_t* base) { base013_sub_8243F158(ctx, base); }
  static void SendEvent(PPCContext& ctx, uint8_t* base) { base013_sub_82441828(ctx, base); }
  static void PrepareMusic(PPCContext& ctx, uint8_t* base) { base013_sub_82376740(ctx, base); }
  static void PlayMusic(PPCContext& ctx, uint8_t* base) { base013_sub_82376908(ctx, base); }
  static void FinishCinematic(PPCContext& ctx, uint8_t* base) { base013_sub_823C66D0(ctx, base); }
  static void CreateTexture(PPCContext& ctx, uint8_t* base) { base013_sub_823FDF88(ctx, base); }
  static void ReadTexture(PPCContext& ctx, uint8_t* base) { base013_sub_823F92E0(ctx, base); }
};

#if SSF2T_TITLE_UPDATE_IMAGE
struct TitleUpdate023Ending {
  static constexpr uint32_t kEndingCharacter = 0x83BE5984;
  static void LoadScreen(PPCContext& ctx, uint8_t* base) { tu023_sub_823CC2C0(ctx, base); }
  static void Hash(PPCContext& ctx, uint8_t* base) { tu023_sub_82430FE0(ctx, base); }
  static void FindElement(PPCContext& ctx, uint8_t* base) { tu023_sub_8243EF50(ctx, base); }
  static void SetAlpha(PPCContext& ctx, uint8_t* base) { tu023_sub_8243F928(ctx, base); }
  static void SendEvent(PPCContext& ctx, uint8_t* base) { tu023_sub_82441FF8(ctx, base); }
  static void PrepareMusic(PPCContext& ctx, uint8_t* base) { tu023_sub_82376810(ctx, base); }
  static void PlayMusic(PPCContext& ctx, uint8_t* base) { tu023_sub_823769D8(ctx, base); }
  static void FinishCinematic(PPCContext& ctx, uint8_t* base) { tu023_sub_823C6CF8(ctx, base); }
  static void CreateTexture(PPCContext& ctx, uint8_t* base) { tu023_sub_823FE638(ctx, base); }
  static void ReadTexture(PPCContext& ctx, uint8_t* base) { tu023_sub_823F9A50(ctx, base); }
};
#endif

constexpr std::array<uint32_t, 2> kHoverWords = {0x83A21374, 0x83A21378};
constexpr uint32_t kFinishWithoutCinematic = 1;
constexpr uint32_t kElementType = 20;
constexpr uint32_t kElementTypeGraphic = 3;
constexpr uint32_t kGraphicKind = 368;
constexpr uint32_t kGraphicKindImage = 0;
constexpr uint32_t kImageTextureCell = 384;
constexpr uint8_t kEndingFileFlags = 0;

uint32_t GuestText(uint8_t* base, std::string_view text) {
  const uint32_t name = REX_KERNEL_MEMORY()->SystemHeapAlloc(uint32_t(text.size() + 1));
  if (name == 0) return 0;
  std::memcpy(GuestHost(base, name), text.data(), text.size());
  GuestStore8(base, name + uint32_t(text.size()), 0);
  return name;
}

template <typename H>
uint32_t HashOf(uint32_t text, PPCContext& ctx, uint8_t* base) {
  ctx.r3.u64 = text;
  H::Hash(ctx, base);
  return ctx.r3.u32;
}

template <typename H>
uint32_t ElementNamed(uint32_t text, PPCContext& ctx, uint8_t* base) {
  ctx.r3.u64 = HashOf<H>(text, ctx, base);
  H::FindElement(ctx, base);
  return ctx.r3.u32;
}

// One wrapper and one cell per cut, kept for the session: tearing a screen down zeroes the cell without freeing either.
template <typename H>
bool ShowCuts(const UltraEnding& ending, size_t slot, const UltraEndingArt& art, PPCContext& ctx, uint8_t* base) {
  struct Cut {
    uint32_t name = 0;
    uint32_t wrapper = 0;
    uint32_t cell = 0;
  };
  static std::array<std::array<Cut, kEndingCuts>, kUltraEndings.size()> all;
  std::array<Cut, kEndingCuts>& cuts = all[slot];
  for (int index = 0; index < kEndingCuts; ++index) {
    Cut& cut = cuts[size_t(index)];
    if (cut.name == 0) cut.name = GuestText(base, EndingFrameName(ending, index));
    if (cut.wrapper == 0) {
      cut.wrapper = BuildArgbTexture<H>(art.Cut(index), kEndingCutWidth, kEndingCutHeight, kEndingFileFlags, ctx, base);
    }
    if (cut.cell == 0) cut.cell = REX_KERNEL_MEMORY()->SystemHeapAlloc(4);
    if (cut.name == 0 || cut.wrapper == 0 || cut.cell == 0) return false;
    const uint32_t element = ElementNamed<H>(cut.name, ctx, base);
    if (!PlausibleGuestAddress(element) || LoadBE32(GuestHost(base, element + kElementType), 0) != kElementTypeGraphic ||
        LoadBE32(GuestHost(base, element + kGraphicKind), 0) != kGraphicKindImage) {
      return false;
    }
    GuestStoreBE32(base, cut.cell, cut.wrapper);
    GuestStoreBE32(base, element + kImageTextureCell, cut.cell);
  }
  return true;
}

void CallWith(void (*function)(PPCContext&, uint8_t*), uint32_t value, PPCContext& ctx, uint8_t* base) {
  ctx.r3.u64 = value;
  function(ctx, base);
}

bool OnUltraCell(FighterIdentity identity, int side) {
  return identity == FighterIdentity::kEvilRyu ? GlobalUltraNavigation().OnEvilRyu(side)
                                               : GlobalUltraNavigation().OnViolentKen(side);
}

// PostGame_PlayCinematic, the carrier's case with the Ultra fighter's group in place of the carrier's own. Both share the
// carrier's screen set, whose art loads either way; the group and the cuts exist only after the guest has parsed the patched
// document. If they are not there, the carrier's own ending plays rather than none.
template <typename H>
void PlayEnding(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  const std::array<FighterIdentity, 2> requested{RequestedIdentity(0), RequestedIdentity(1)};
  const std::array<uint32_t, 2> hover{LoadBE32(GuestHost(base, kHoverWords[0]), 0),
                                      LoadBE32(GuestHost(base, kHoverWords[1]), 0)};
  const uint32_t winner = LoadBE32(GuestHost(base, H::kEndingCharacter), 0);
  std::array<std::array<bool, 2>, kUltraEndings.size()> on_cell{};
  for (size_t slot = 0; slot < kUltraEndings.size(); ++slot) {
    for (int side = 0; side < 2; ++side) on_cell[slot][size_t(side)] = OnUltraCell(kUltraEndings[slot]->identity, side);
  }
  const UltraEndingChoice choice = ChooseUltraEnding(winner, requested, on_cell, hover);
  if (choice.ending == nullptr || !UltraFightersEnabled() || !CharacterMovesPageReady()) return original(ctx, base);
  const UltraEnding& ending = *choice.ending;
  const size_t slot = choice.slot;
  const int side = choice.side;
  const UltraEndingArt* art = LoadUltraEndingArt(ending);
  if (art == nullptr) return original(ctx, base);

  static std::array<uint32_t, kUltraEndings.size()> group_names{};
  if (group_names[slot] == 0) group_names[slot] = GuestText(base, ending.group);
  if (group_names[slot] == 0) return original(ctx, base);

  const PPCContext saved = ctx;
  const uint32_t screen_hash = HashOf<H>(ending.guest_screen_name, ctx, base);
  ctx.r3.u64 = ending.guest_set_name;
  ctx.r4.u64 = screen_hash;
  ctx.r5.u64 = 1;
  H::LoadScreen(ctx, base);

  uint32_t name = group_names[slot];
  uint32_t group = ElementNamed<H>(name, ctx, base);
  if (!PlausibleGuestAddress(group) || !ShowCuts<H>(ending, slot, *art, ctx, base)) {
    REXLOG_ERROR("DEBUG_SAULO {} ending unavailable, playing the carrier's", ending.log_name);
    name = ending.guest_set_name;
    group = ElementNamed<H>(name, ctx, base);
  }
  if (PlausibleGuestAddress(group)) {
    ctx.r3.u64 = group;
    ctx.f1.f64 = 1.0;
    H::SetAlpha(ctx, base);
    CallWith(H::SendEvent, HashOf<H>(name, ctx, base), ctx, base);
    CallWith(H::PrepareMusic, ending.guest_music, ctx, base);
    CallWith(H::PlayMusic, ending.guest_music, ctx, base);
    REXLOG_INFO("DEBUG_SAULO {} ending side={} group={:08X}", ending.log_name, side + 1, group);
  } else {
    CallWith(H::FinishCinematic, kFinishWithoutCinematic, ctx, base);
  }
  ctx = saved;
}

}  // namespace
}  // namespace ssf2t::character

REX_EXTERN(__imp__base013_sub_823CBE80);
REX_HOOK_RAW(base013_sub_823CBE80) {
  ssf2t::character::PlayEnding<ssf2t::character::Base013Ending>(ctx, base, __imp__base013_sub_823CBE80);
}

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_823CC460);
REX_HOOK_RAW(tu023_sub_823CC460) {
  ssf2t::character::PlayEnding<ssf2t::character::TitleUpdate023Ending>(ctx, base, __imp__tu023_sub_823CC460);
}
#endif
