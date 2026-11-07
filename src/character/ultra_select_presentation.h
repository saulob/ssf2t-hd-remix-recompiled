#pragma once

#include <array>
#include <cstdint>

#include "character/fighter_identity.h"

namespace ssf2t::character {

// The guest slot that holds the Evil Ryu large portrait is the unused id after Akuma; any side hovering that id draws it.
inline constexpr uint32_t kEvilPortraitSlot = 17;
inline constexpr uint32_t kNoLargePortrait = 0xFFFFFFFF;
// The large table ends at the Evil Ryu slot and the mini portraits follow it directly: id 18 would be Ryu's mini.
inline constexpr uint32_t kLargePortraitIds = 18;
// Only an id a side hovers reads its large entry, and neither this id nor Ken's carries a special case in the guest.
inline constexpr uint32_t kViolentSpareLargeId = 1;

// Both sides draw Ryu's name from the one tile string 0700 0701 0702 0703, so a tile says nothing about the side; the HD
// art replaces whichever end of the string the object walk meets first. The object's place on screen names the side.
inline constexpr uint16_t kRyuNameFirstTile = 0x700;
inline constexpr uint16_t kRyuNameLastTile = 0x703;
inline constexpr uint16_t kRyuHudNameTile = 0x408;
inline constexpr uint16_t kNameTileColour = 13;
inline constexpr uint16_t kHudNameTileColour = 12;
inline constexpr uint16_t kTileColourMask = 0x1F;
inline constexpr int kScreenCentreX = 192;

inline constexpr uint16_t kKenNameFirstTile = 0x740;
inline constexpr uint16_t kKenNameLastTile = 0x743;
inline constexpr uint16_t kKenHudNameTile = 0x40E;

constexpr int KenNameSide(uint16_t code, uint16_t attr, int x) {
  const uint16_t colour = attr & kTileColourMask;
  const bool name = (code == kKenNameFirstTile || code == kKenNameLastTile) && colour == kNameTileColour;
  const bool hud = code == kKenHudNameTile && colour == kHudNameTileColour;
  if (!name && !hud) return -1;
  return x < kScreenCentreX ? 0 : 1;
}

constexpr int RyuNameSide(uint16_t code, uint16_t attr, int x) {
  const uint16_t colour = attr & kTileColourMask;
  const bool name = (code == kRyuNameFirstTile || code == kRyuNameLastTile) && colour == kNameTileColour;
  const bool hud = code == kRyuHudNameTile && colour == kHudNameTileColour;
  if (!name && !hud) return -1;
  return x < kScreenCentreX ? 0 : 1;
}

struct SelectPresentation {
  std::array<bool, 2> evil{};
  std::array<bool, 2> violent{};
  std::array<bool, 2> hide{};

  bool any() const { return evil[0] || evil[1] || violent[0] || violent[1]; }
};

// Carrier Ryu is guest id 0 for both Ryu and Evil Ryu, so a side shows Evil Ryu only on its own host request, its own
// host cell and its own carrier hover. A side that is not Evil Ryu never shares the Evil Ryu slot. Violent Ken and Ken
// follow the same rule with their own slot.
inline SelectPresentation ResolveSelectPresentation(const std::array<FighterIdentity, 2>& requested,
                                                    const std::array<bool, 2>& on_cell,
                                                    const std::array<uint32_t, 2>& hover) {
  SelectPresentation result;
  for (int side = 0; side < 2; ++side) {
    result.evil[side] = requested[side] == FighterIdentity::kEvilRyu && on_cell[side] &&
                        hover[side] == uint32_t(kRyuGuestCharacter);
    result.violent[side] = requested[side] == FighterIdentity::kViolentKen && on_cell[side] &&
                           hover[side] == uint32_t(kKenGuestCharacter);
  }
  if (!result.any()) return result;
  for (int side = 0; side < 2; ++side) {
    result.hide[side] = !result.evil[side] && !result.violent[side] && hover[side] == kEvilPortraitSlot;
  }
  return result;
}

// Violent Ken has no large id of his own, so he borrows Ken's unless a side is really showing Ken.
constexpr uint32_t ViolentPortraitId(const SelectPresentation& presentation, const std::array<uint32_t, 2>& hover) {
  const uint32_t ken = uint32_t(kKenGuestCharacter);
  const bool ken_shown = (!presentation.violent[0] && hover[0] == ken) || (!presentation.violent[1] && hover[1] == ken);
  return ken_shown ? kViolentSpareLargeId : ken;
}

}  // namespace ssf2t::character
