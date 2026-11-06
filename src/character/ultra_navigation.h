#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "character/akuma_navigation.h"
#include "character/fighter_identity.h"

namespace ssf2t::character {

enum class PadDirection { kUp, kDown, kLeft, kRight };

enum class UltraMoveAction {
  kNone,    // The guest handles the move.
  kEnter,   // Right from a row's last original cell: the guest moves, the host selects that row's Ultra fighter.
  kLeave,   // Left from an Ultra cell: back to that row's last original cell, the guest move is skipped.
  kSwitch,  // Up or Down between the two Ultra cells: the carrier changes, the guest move is skipped.
  kHold,    // Any other move on an Ultra cell: the guest move is skipped.
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

inline constexpr uint8_t kViolentKenCarrierLive = uint8_t(kKenGuestCharacter);
// Id 8 ends the bottom row in both layouts.
inline constexpr uint8_t kViolentKenNeighbourLive = 8;
inline constexpr uint8_t kViolentKenCursorLive = 19;
inline constexpr uint16_t kViolentKenCursorY = 16;

constexpr uint16_t EvilRyuCursorX(SelectLayout layout) {
  return layout == SelectLayout::kArcade ? kEvilRyuCursorX : kEvilRyuCursorXVersus;
}

enum class UltraClassicGuard {
  kNone,
  kClearClassic,  // Remix style: the fighter keeps the cell and loses the guest's classic flag.
  kLeaveCell,     // Classic style: the side goes back to its plain carrier, which keeps any classic flag.
};

// USF2 has no classic Ultra fighter. Pure in guest state and the rollback-saved cell, so both peers agree on every frame.
constexpr UltraClassicGuard GuardUltraClassic(uint8_t hd_style, bool on_ultra_cell, uint8_t classic_flag) {
  if (!on_ultra_cell) return UltraClassicGuard::kNone;
  if (hd_style == 0) return UltraClassicGuard::kLeaveCell;
  return classic_flag != 0 ? UltraClassicGuard::kClearClassic : UltraClassicGuard::kNone;
}

// Host-only Ultra cells. While one is selected the guest selection stays on its carrier, Ryu or Ken. Each cell has its
// own availability: online, the two are negotiated separately.
class UltraNavigation {
 public:
  UltraMove Resolve(bool evil_ryu_available, bool violent_ken_available, int side, uint8_t live,
                    PadDirection direction);
  bool OnEvilRyu(int side) const;
  bool OnViolentKen(int side) const;
  bool OnUltraCell(int side) const { return OnEvilRyu(side) || OnViolentKen(side); }
  void Leave(int side);
  void Reset();
  uint8_t Mask() const { return mask_.load(std::memory_order_relaxed); }
  // Puts the cell selection back as a rollback saved it, requested identities included.
  void Restore(uint8_t mask);

 private:
  void Set(int side, FighterIdentity identity);

  std::atomic<uint8_t> mask_{0};
};

struct SelectEntrySide {
  bool joined = false;
  bool played = false;
  int guest_character = -1;
  bool hidden = false;
};

struct SelectEntry {
  bool arcade = false;
  bool evil_ryu_available = false;
  bool violent_ken_available = false;
  std::array<SelectEntrySide, 2> sides{};
};

// A challenger joining an Arcade fight restarts Character Select for that side alone: the guest keeps the defender's
// carrier, and only the cell mask says whether that carrier is an Ultra fighter. Returns the bits the defender keeps.
uint8_t UltraMaskKeptByDefender(uint8_t mask, const SelectEntry& entry);

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
