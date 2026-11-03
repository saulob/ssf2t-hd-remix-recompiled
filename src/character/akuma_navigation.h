#pragma once

#include <array>
#include <cstdint>

namespace ssf2t::character {

inline constexpr uint8_t kAkumaSelect = 32;
inline constexpr int kRosterColumns = 8;
inline constexpr int kNoColumn = -1;

// Roster rows of the HD Character Select, by column.
inline constexpr std::array<uint8_t, kRosterColumns> kTopRow = {9, 0, 1, 2, 3, 13, 14, 10};
inline constexpr std::array<uint8_t, kRosterColumns> kBottomRow = {8, 4, 5, 6, 7, 12, 15, 11};
inline constexpr int kGuileColumn = 4;

enum class NavDirection { kUp = 0, kDown = 1 };

inline int TopRowColumn(uint8_t live) {
  for (int column = 0; column < kRosterColumns; ++column) {
    if (kTopRow[column] == live) return column;
  }
  return kNoColumn;
}

struct AkumaNavigation {
  std::array<int, 2> column = {{kNoColumn, kNoColumn}};

  // Returns true and the fighter to select when the move is redirected through Akuma.
  bool Resolve(bool enabled, int side, uint8_t live, NavDirection direction, uint8_t& next) {
    if (!enabled || side < 0 || side > 1) return false;
    if (live == kAkumaSelect) return LeaveAkuma(side, direction, next);
    const int top = TopRowColumn(live);
    if (direction != NavDirection::kUp || top == kNoColumn) return false;
    column[side] = top;
    next = kAkumaSelect;
    return true;
  }

 private:
  bool LeaveAkuma(int side, NavDirection direction, uint8_t& next) {
    const int remembered = column[side];
    const int source = remembered >= 0 && remembered < kRosterColumns ? remembered : kGuileColumn;
    next = direction == NavDirection::kUp ? kBottomRow[source] : kTopRow[source];
    column[side] = kNoColumn;
    return true;
  }
};

}  // namespace ssf2t::character
