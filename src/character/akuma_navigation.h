#pragma once

#include <array>
#include <cstdint>

namespace ssf2t::character {

inline constexpr uint8_t kAkumaSelect = 32;
inline constexpr int kRosterColumns = 8;
inline constexpr int kVersusColumns = 6;
inline constexpr int kNoColumn = -1;

// Game mode 0 selects on the 2x8 HD grid; every other mode (Local Match, online) uses the 3x6 grid with the two
// random cells on the top row.
enum class SelectLayout : uint8_t { kArcade, kVersus };

inline constexpr uint8_t kArcadeGameMode = 0;

constexpr SelectLayout SelectLayoutForMode(uint8_t game_mode) {
  return game_mode == kArcadeGameMode ? SelectLayout::kArcade : SelectLayout::kVersus;
}

// Roster rows by visual column, left to right.
inline constexpr std::array<uint8_t, kRosterColumns> kTopRow = {0, 1, 2, 3, 13, 14, 10, 9};
inline constexpr std::array<uint8_t, kRosterColumns> kBottomRow = {4, 5, 6, 7, 12, 15, 11, 8};
inline constexpr int kGuileColumn = 3;

inline constexpr std::array<uint8_t, kVersusColumns> kVersusTopRow = {16, 0, 1, 2, 3, 17};
inline constexpr std::array<uint8_t, kVersusColumns> kVersusBottomRow = {11, 13, 14, 15, 12, 8};
inline constexpr int kHondaColumn = 2;

enum class NavDirection { kUp = 0, kDown = 1 };

constexpr int LayoutColumns(SelectLayout layout) {
  return layout == SelectLayout::kArcade ? kRosterColumns : kVersusColumns;
}

// The column under the guest's own Akuma cell.
constexpr int NativeAkumaColumn(SelectLayout layout) {
  return layout == SelectLayout::kArcade ? kGuileColumn : kHondaColumn;
}

constexpr uint8_t TopRowAt(SelectLayout layout, int column) {
  return layout == SelectLayout::kArcade ? kTopRow[size_t(column)] : kVersusTopRow[size_t(column)];
}

constexpr uint8_t BottomRowAt(SelectLayout layout, int column) {
  return layout == SelectLayout::kArcade ? kBottomRow[size_t(column)] : kVersusBottomRow[size_t(column)];
}

inline int TopRowColumn(SelectLayout layout, uint8_t live) {
  for (int column = 0; column < LayoutColumns(layout); ++column) {
    if (TopRowAt(layout, column) == live) return column;
  }
  return kNoColumn;
}

inline int TopRowColumn(uint8_t live) { return TopRowColumn(SelectLayout::kArcade, live); }

struct AkumaNavigation {
  std::array<int, 2> column = {{kNoColumn, kNoColumn}};
  std::array<SelectLayout, 2> layout = {{SelectLayout::kArcade, SelectLayout::kArcade}};

  bool Resolve(bool enabled, int side, uint8_t live, NavDirection direction, uint8_t& next) {
    return Resolve(enabled, SelectLayout::kArcade, side, live, direction, next);
  }

  // Returns true and the fighter to select when the move is redirected through Akuma.
  bool Resolve(bool enabled, SelectLayout grid, int side, uint8_t live, NavDirection direction, uint8_t& next) {
    if (!enabled || side < 0 || side > 1) return false;
    if (live == kAkumaSelect) return LeaveAkuma(grid, side, direction, next);
    const int top = TopRowColumn(grid, live);
    if (direction != NavDirection::kUp || top == kNoColumn) return false;
    column[side] = top;
    layout[side] = grid;
    next = kAkumaSelect;
    return true;
  }

 private:
  bool LeaveAkuma(SelectLayout grid, int side, NavDirection direction, uint8_t& next) {
    const int remembered = column[side];
    const bool known = layout[side] == grid && remembered >= 0 && remembered < LayoutColumns(grid);
    const int source = known ? remembered : NativeAkumaColumn(grid);
    next = direction == NavDirection::kUp ? BottomRowAt(grid, source) : TopRowAt(grid, source);
    column[side] = kNoColumn;
    return true;
  }
};

}  // namespace ssf2t::character
