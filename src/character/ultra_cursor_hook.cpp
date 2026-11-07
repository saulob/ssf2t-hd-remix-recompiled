#include <cstdint>
#include <utility>

#include <rex/hook.h>

#include "character/ultra_fighters.h"
#include "character/ultra_navigation.h"
#include "guest_memory.h"
#include "netplay/guest_netplay.h"
#include "title_update_image.h"

namespace ssf2t::character {
namespace {

struct Base013Cursor {
  static constexpr uint32_t kLive = 0x83CA6C9C;
  static constexpr uint32_t kHdPositions = 0x83A330B8;
  static constexpr uint32_t kVersusPositions = 0x83A33140;
};

struct TitleUpdate023Cursor {
  static constexpr uint32_t kLive = 0x83CA71B4;
  static constexpr uint32_t kHdPositions = 0x83A330B8;
  static constexpr uint32_t kVersusPositions = 0x83A33140;
};

constexpr uint32_t kObjectSideOffset = 33;

// The guest places the cursor from live[side]; the carrier stays Ryu, so the cursor object is drawn at the host cell
// by pointing it at an unused id for the duration of its update. Each grid has its own position table.
template <typename G, typename C>
void PlaceCursor(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (!UltraFightersEnabled()) return original(ctx, base);
  const uint32_t object = LoadBE32(GuestHost(base, G::kFightWork - 4), 0);
  if (object < 0x10000 || object >= 0xFFFF0000u) return original(ctx, base);
  const int side = *GuestHost(base, object + kObjectSideOffset);
  if (side < 0 || side > 1 || !GlobalUltraNavigation().OnUltraCell(side)) return original(ctx, base);

  const bool violent = GlobalUltraNavigation().OnViolentKen(side);
  const uint8_t cursor = violent ? kViolentKenCursorLive : kEvilRyuCursorLive;
  for (const auto [table, layout] : {std::pair{C::kHdPositions, SelectLayout::kArcade},
                                     std::pair{C::kVersusPositions, SelectLayout::kVersus}}) {
    const uint32_t entry = table + uint32_t(cursor) * 4;
    StoreBE16(base, entry, EvilRyuCursorX(layout));
    StoreBE16(base, entry + 2, violent ? kViolentKenCursorY : kEvilRyuCursorY);
  }
  uint8_t* live = base + C::kLive + side;
  const uint8_t carrier = *live;
  *live = cursor;
  original(ctx, base);
  *live = carrier;
}

}  // namespace
}  // namespace ssf2t::character

REX_EXTERN(__imp__base013_sub_8232DF78);
REX_HOOK_RAW(base013_sub_8232DF78) {
  ssf2t::character::PlaceCursor<ssf2t::netplay::Base013Netplay, ssf2t::character::Base013Cursor>(
      ctx, base, __imp__base013_sub_8232DF78);
}

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_8232DE58);
REX_HOOK_RAW(tu023_sub_8232DE58) {
  ssf2t::character::PlaceCursor<ssf2t::netplay::TitleUpdate023Netplay, ssf2t::character::TitleUpdate023Cursor>(
      ctx, base, __imp__tu023_sub_8232DE58);
}
#endif
