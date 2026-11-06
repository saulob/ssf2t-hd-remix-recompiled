#pragma once

#include <cstdint>

namespace ssf2t::character {

// The values the menu scripts pass to the guest's SetGameMode, which refuses anything from 4 up.
inline constexpr uint8_t kArcadeGameMode = 0;
inline constexpr uint8_t kLocalVersusGameMode = 1;
inline constexpr uint8_t kTrainingGameMode = 2;
inline constexpr uint8_t kOnlineSelectMode = 3;

constexpr bool OnlineSelectMode(uint8_t game_mode) { return game_mode == kOnlineSelectMode; }

constexpr bool ClassicStyle(uint8_t hd_style) { return hd_style == 0; }

constexpr const char* GameModeName(uint8_t game_mode) {
  switch (game_mode) {
    case kArcadeGameMode: return "arcade";
    case kLocalVersusGameMode: return "versus";
    case kTrainingGameMode: return "training";
    case kOnlineSelectMode: return "online";
    default: return "unknown";
  }
}

constexpr const char* HdStyleName(uint8_t hd_style) { return ClassicStyle(hd_style) ? "classic" : "remix"; }

}  // namespace ssf2t::character
