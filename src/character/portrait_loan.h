#pragma once

#include <array>
#include <cstdint>
#include <cstring>

#include "character/ultra_select_presentation.h"
#include "guest_memory.h"

namespace ssf2t::character {

inline constexpr std::array<uint32_t, 2> kSelectHoverWords = {0x83A21374, 0x83A21378};
inline constexpr uint32_t kPortraitPaletteColumns = 10;
inline constexpr uint32_t kPortraitRowBytes = kPortraitPaletteColumns * 4;

// Guest tables indexed by portrait id, named by their entries for id kEvilPortraitSlot.
struct PortraitTables {
  uint32_t mystery_large = 0;
  uint32_t mystery_loser = 0;
  uint32_t palette_rows = 0;
  std::array<uint32_t, 2> hover = kSelectHoverWords;
};

struct LoanArt {
  uint32_t large = 0;
  uint32_t beaten = 0;
};

// Lends one portrait id's entries to an Ultra fighter's texture for the length of a draw. An id past the large table
// would write into the guest's neighbouring tables, so it is refused.
class PortraitLoan {
 public:
  PortraitLoan(const PortraitTables& tables, uint32_t id, const LoanArt& art, uint8_t* base)
      : base_(base), id_(id), active_(art.large != 0 && id < kLargePortraitIds) {
    if (!active_) return;
    large_ = tables.mystery_large + id * 4 - kEvilPortraitSlot * 4;
    loser_ = tables.mystery_loser + id * 4 - kEvilPortraitSlot * 4;
    row_ = tables.palette_rows + id * kPortraitRowBytes;
    saved_large_ = LoadBE32(GuestHost(base, large_), 0);
    saved_loser_ = LoadBE32(GuestHost(base, loser_), 0);
    std::memcpy(saved_row_.data(), GuestHost(base, row_), kPortraitRowBytes);
    GuestStoreBE32(base, large_, art.large);
    GuestStoreBE32(base, loser_, art.beaten != 0 ? art.beaten : art.large);
    for (uint32_t column = 0; column < kPortraitPaletteColumns; ++column) {
      GuestStoreBE32(base, row_ + column * 4, art.large);
    }
  }

  ~PortraitLoan() {
    if (!active_) return;
    GuestStoreBE32(base_, large_, saved_large_);
    GuestStoreBE32(base_, loser_, saved_loser_);
    std::memcpy(GuestHost(base_, row_), saved_row_.data(), kPortraitRowBytes);
  }

  PortraitLoan(const PortraitLoan&) = delete;
  PortraitLoan& operator=(const PortraitLoan&) = delete;

  uint32_t Replacement() const { return active_ ? id_ : kNoLargePortrait; }

 private:
  uint8_t* base_;
  uint32_t id_;
  bool active_;
  uint32_t large_ = 0;
  uint32_t loser_ = 0;
  uint32_t row_ = 0;
  uint32_t saved_large_ = 0;
  uint32_t saved_loser_ = 0;
  std::array<uint8_t, kPortraitRowBytes> saved_row_{};
};

// Runs one portraits pass with each Ultra side pointed at its lent id; every lent word is back when it returns.
template <typename Draw>
void DrawWithPortraitLoans(const PortraitTables& tables, const SelectPresentation& presentation,
                           const std::array<uint32_t, 2>& hover, const LoanArt& evil, const LoanArt& violent,
                           uint8_t* base, Draw&& draw) {
  PortraitLoan evil_loan(tables, kEvilPortraitSlot, evil, base);
  PortraitLoan violent_loan(tables, ViolentPortraitId(presentation, hover), violent, base);
  for (size_t side = 0; side < 2; ++side) {
    if (presentation.evil[side]) GuestStoreBE32(base, tables.hover[side], evil_loan.Replacement());
    if (presentation.violent[side]) GuestStoreBE32(base, tables.hover[side], violent_loan.Replacement());
    if (presentation.hide[side]) GuestStoreBE32(base, tables.hover[side], kNoLargePortrait);
  }
  draw();
  for (size_t side = 0; side < 2; ++side) {
    if (presentation.evil[side] || presentation.violent[side] || presentation.hide[side]) {
      GuestStoreBE32(base, tables.hover[side], hover[side]);
    }
  }
}

}  // namespace ssf2t::character
