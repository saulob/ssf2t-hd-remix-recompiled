#include <array>
#include <cstdint>

#include <rex/hook.h>

#include "character/ultra_fighters.h"
#include "character/ultra_navigation.h"
#include "guest_memory.h"
#include "title_update_image.h"

namespace ssf2t::character {
namespace {

constexpr std::array<uint32_t, 2> kHoverWords = {0x83A21374, 0x83A21378};
constexpr uint32_t kNoPortrait = 0xFFFFFFFF;

// The guest skips a player's large portrait and frame while that player's hover word is -1.
void DrawWithoutHostPortraits(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  std::array<uint32_t, 2> saved{};
  std::array<bool, 2> hidden{};
  if (UltraFightersEnabled()) {
    for (int side = 0; side < 2; ++side) {
      hidden[side] = GlobalUltraNavigation().OnEvilRyu(side);
      if (!hidden[side]) continue;
      saved[side] = LoadBE32(GuestHost(base, kHoverWords[side]), 0);
      StoreBE32(base, kHoverWords[side], kNoPortrait);
    }
  }
  original(ctx, base);
  for (int side = 0; side < 2; ++side) {
    if (hidden[side]) StoreBE32(base, kHoverWords[side], saved[side]);
  }
}

}  // namespace
}  // namespace ssf2t::character

REX_EXTERN(__imp__base013_sub_82179ED8);
REX_HOOK_RAW(base013_sub_82179ED8) {
  ssf2t::character::DrawWithoutHostPortraits(ctx, base, __imp__base013_sub_82179ED8);
}

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_82179EF8);
REX_HOOK_RAW(tu023_sub_82179EF8) {
  ssf2t::character::DrawWithoutHostPortraits(ctx, base, __imp__tu023_sub_82179EF8);
}
#endif
