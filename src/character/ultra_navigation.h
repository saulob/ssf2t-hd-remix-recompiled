#pragma once

#include <atomic>
#include <cstdint>

#include "character/akuma_navigation.h"
#include "character/fighter_identity.h"

namespace ssf2t::character {

enum class PadDirection { kUp, kDown, kLeft, kRight };

enum class UltraMoveAction {
  kNone,   // The guest handles the move.
  kEnter,  // Right from the last original top-row cell: the guest moves, the host selects Evil Ryu.
  kLeave,  // Left from Evil Ryu: back to the last original top-row cell, the guest move is skipped.
  kHold,   // Any other move while Evil Ryu is selected: the guest move is skipped.
};

struct UltraMove {
  UltraMoveAction action = UltraMoveAction::kNone;
  uint8_t live = 0;
};

inline constexpr uint8_t kEvilRyuCarrierLive = uint8_t(kRyuGuestCharacter);
// Id 9 ends the row Evil Ryu joins in both layouts; Right from it would wrap to the row's first fighter.
inline constexpr uint8_t kEvilRyuNeighbourLive = 9;

// Unused guest ids 17..31 park the cursor offscreen; id 18 is moved onto the host cell while it is shown.
inline constexpr uint8_t kEvilRyuCursorLive = 18;
inline constexpr uint16_t kEvilRyuCursorX = 336;
inline constexpr uint16_t kEvilRyuCursorXVersus = 304;
inline constexpr uint16_t kEvilRyuCursorY = 48;

constexpr uint16_t EvilRyuCursorX(SelectLayout layout) {
  return layout == SelectLayout::kArcade ? kEvilRyuCursorX : kEvilRyuCursorXVersus;
}

// Host-only Evil Ryu cell. While it is selected the guest selection stays on Ryu, the carrier of the fight.
class UltraNavigation {
 public:
  UltraMove Resolve(bool enabled, int side, uint8_t live, PadDirection direction);
  bool OnEvilRyu(int side) const;
  void Reset();

 private:
  void Set(int side, bool selected);

  std::atomic<uint8_t> mask_{0};
};

// The interactive player select only runs for human players, so a gap in it means a new Character Select; the shared
// select states also run between Arcade matches and must not end the player's choice.
class SelectSession {
 public:
  // Returns true when this touch starts a new session.
  bool Touch(int64_t now_ms);
  uint32_t id() const { return id_; }

 private:
  int64_t last_ms_ = 0;
  uint32_t id_ = 0;
};

inline constexpr int64_t kSelectSessionGapMilliseconds = 3000;

UltraNavigation& GlobalUltraNavigation();
SelectSession& GlobalSelectSession();

}  // namespace ssf2t::character
