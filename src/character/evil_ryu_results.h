#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "character/fighter_identity.h"

namespace ssf2t::character {

inline constexpr int kResultsNativeRows = 17;
inline constexpr int kResultsHostRows = 2;
inline constexpr int kResultsRows = kResultsNativeRows + kResultsHostRows;
inline constexpr int kResultsColumns = 2;
inline constexpr int kEvilRyuResultsRow = 1;
inline constexpr int kViolentKenResultsRow = 3;

// `character` is the value of GetPlayerCharacterResultsScreen for the fighter; the patched scripts move the row
// highlight on it. `carrier` is the guest character, which is also the guest's row index.
struct HostResultsRow {
  std::string_view label;
  std::string_view text_id;
  FighterIdentity identity;
  int carrier;
  int display_row;
  int character;
};

inline constexpr std::array<HostResultsRow, kResultsHostRows> kHostResultsRows = {{
    {"evilryu", "Ryu_Hadouken", FighterIdentity::kEvilRyu, kRyuGuestCharacter, kEvilRyuResultsRow, 17},
    {"violentken", "Ken_SpecialKick1", FighterIdentity::kViolentKen, kKenGuestCharacter, kViolentKenResultsRow, 18},
}};

// Ryu, Evil Ryu, Ken, Violent Ken, then the guest's other rows in their own order.
constexpr int ResultsDisplayRow(int native_row) {
  if (native_row == kRyuGuestCharacter) return 0;
  if (native_row == kKenGuestCharacter) return kEvilRyuResultsRow + 1;
  return native_row < kKenGuestCharacter ? native_row + 3 : native_row + 2;
}
constexpr int ResultsNativeRow(int display_row) {
  for (int row = 0; row < kResultsNativeRows; ++row) {
    if (ResultsDisplayRow(row) == display_row) return row;
  }
  return -1;
}
// Index into kHostResultsRows, -1 for a guest row.
constexpr int ResultsHostRow(int display_row) {
  for (int host = 0; host < kResultsHostRows; ++host) {
    if (kHostResultsRows[size_t(host)].display_row == display_row) return host;
  }
  return -1;
}

// Which side's carrier count the guest just raised and the Ultra fighter must take back.
struct ResultsCorrection {
  int carrier_win_side = -1;
  int carrier_loss_side = -1;
};

// Per-session record of one Ultra fighter, keyed by side. The guest's array stops at 17 rows, so it lives on the host and
// is never saved; a side's carrier count stays the record of the real carrier.
class HostResultsBook {
 public:
  ResultsCorrection Record(int winner, const std::array<bool, 2>& fought);
  void Reset(int side);
  int wins(int side) const { return wins_[size_t(side)]; }
  int losses(int side) const { return losses_[size_t(side)]; }
  bool last_match(int side) const { return last_match_[size_t(side)]; }

 private:
  std::array<int, 2> wins_{};
  std::array<int, 2> losses_{};
  std::array<bool, 2> last_match_{};
};

HostResultsBook& GlobalHostResults(int host_row);

// Each edits one guest document in place: false and `xml` unchanged when it is another document or another layout.
bool PatchResultsScreen(std::string& xml);
bool PatchResultsScripts(std::string& xml);
bool PatchResultsGrid(std::string& xml);
bool PatchResultsAnimations(std::string& xml);

// True once all four documents are patched; the list may only grow then, since the grid reads rows x columns.
bool ResultsRowReady();

}  // namespace ssf2t::character
