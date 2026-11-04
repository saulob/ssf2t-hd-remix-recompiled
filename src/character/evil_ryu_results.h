#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace ssf2t::character {

inline constexpr int kResultsNativeRows = 17;
inline constexpr int kResultsRows = kResultsNativeRows + 1;
inline constexpr int kResultsColumns = 2;
// Evil Ryu sits directly under Ryu; the guest's rows follow in their own order.
inline constexpr int kEvilRyuResultsRow = 1;
// Value of GetPlayerCharacterResultsScreen for Evil Ryu; the patched scripts move the row highlight on it.
inline constexpr int kEvilRyuResultsCharacter = 17;

constexpr int ResultsDisplayRow(int native_row) { return native_row < kEvilRyuResultsRow ? native_row : native_row + 1; }
constexpr int ResultsNativeRow(int display_row) {
  if (display_row == kEvilRyuResultsRow) return -1;
  return display_row < kEvilRyuResultsRow ? display_row : display_row - 1;
}

// Which side's carrier Ryu count the guest just raised and Evil Ryu must take back.
struct ResultsCorrection {
  int ryu_win_side = -1;
  int ryu_loss_side = -1;
};

// Per-session record of Evil Ryu, keyed by side. The guest's array stops at 17 rows, so it lives on the host and is
// never saved; a side's carrier Ryu count stays the record of the real Ryu.
class EvilResultsBook {
 public:
  ResultsCorrection Record(int winner, const std::array<bool, 2>& evil);
  void Reset(int side);
  int wins(int side) const { return wins_[size_t(side)]; }
  int losses(int side) const { return losses_[size_t(side)]; }
  bool last_match_evil(int side) const { return last_match_evil_[size_t(side)]; }

 private:
  std::array<int, 2> wins_{};
  std::array<int, 2> losses_{};
  std::array<bool, 2> last_match_evil_{};
};

EvilResultsBook& GlobalEvilResults();

// Each edits one guest document in place: false and `xml` unchanged when it is another document or another layout.
bool PatchResultsScreen(std::string& xml);
bool PatchResultsScripts(std::string& xml);
bool PatchResultsGrid(std::string& xml);
bool PatchResultsAnimations(std::string& xml);

// True once all four documents are patched; the row list may only grow then, since the grid reads rows x columns.
bool ResultsRowReady();

}  // namespace ssf2t::character
