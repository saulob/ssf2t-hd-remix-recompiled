#include <array>
#include <cstdint>

#include <rex/hook.h>

#include "character/evil_ryu_palette.h"
#include "character/ultra_fight.h"
#include "character/violent_ken_data.h"
#include "guest_memory.h"
#include "netplay/fight_snapshot.h"
#include "netplay/guest_netplay.h"
#include "title_update_image.h"

namespace ssf2t::character {
namespace {

struct Base013Palette {
  static constexpr uint32_t kPaletteRamPointer = 0x83E09CE8;
};

struct TitleUpdate023Palette {
  static constexpr uint32_t kPaletteRamPointer = 0x83E0A200;
};

constexpr uint32_t kColourOffset = 0x3B1;
constexpr std::array<uint32_t, 2> kPaletteRamPage = {0x20, 0x200};

// Runs after the guest copies each fighter's colour into palette RAM and replaces Evil Ryu's body pens.
template <typename G, typename P>
void ApplyEvilRyuPalette(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  original(ctx, base);
  if (!UltraFightersEnabled()) return;
  const uint32_t block = LoadBE32(GuestHost(base, G::kFighterBlockPointer), 0);
  const uint32_t ram = LoadBE32(GuestHost(base, P::kPaletteRamPointer), 0) & ~0xFFu;
  if (block < 0x10000 || block >= 0xFFFF0000u || ram < 0x10000 || ram >= 0xFFFF0000u) return;
  for (int side = 0; side < 2; ++side) {
    const bool evil = EvilRyuOnSide<G>(base, side);
    const bool violent = ViolentKenOnSide<G>(base, side);
    if (!evil && !violent) continue;
    const EvilRyuPalette* palette = evil ? LoadedEvilRyuPalette() : LoadedViolentKenPalette();
    if (!palette) continue;
    const uint32_t page = ram + kPaletteRamPage[side];
    std::array<uint16_t, kPaletteWords> current{};
    for (int word = 0; word < kPaletteWords; ++word) current[word] = LoadBE16(GuestHost(base, page + word * 2), 0);
    const int colour = MatchRyuColour(*palette, current.data());
    if (colour < 0) continue;
    for (int word = 0; word < kPaletteWords; ++word) GuestStoreBE16(base, page + word * 2, palette->evil[colour][word]);
  }
}

}  // namespace
}  // namespace ssf2t::character

REX_EXTERN(__imp__base013_sub_82318EF0);
REX_HOOK_RAW(base013_sub_82318EF0) {
  ssf2t::character::ApplyEvilRyuPalette<ssf2t::netplay::Base013Netplay, ssf2t::character::Base013Palette>(
      ctx, base, __imp__base013_sub_82318EF0);
}

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_82318E30);
REX_HOOK_RAW(tu023_sub_82318E30) {
  ssf2t::character::ApplyEvilRyuPalette<ssf2t::netplay::TitleUpdate023Netplay, ssf2t::character::TitleUpdate023Palette>(
      ctx, base, __imp__tu023_sub_82318E30);
}
#endif
