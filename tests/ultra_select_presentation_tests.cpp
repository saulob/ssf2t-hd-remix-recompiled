#include <array>
#include <cstdio>
#include <vector>

#include "character/fighter_identity.h"
#include "character/portrait_loan.h"
#include "character/ultra_navigation.h"
#include "character/ultra_select_presentation.h"
#include "guest_memory.h"

namespace {

using namespace ssf2t::character;

int failures = 0;

void Check(bool condition, const char* what) {
  if (condition) return;
  std::printf("FAIL %s\n", what);
  ++failures;
}

constexpr uint32_t kRyu = 0;
constexpr uint32_t kHonda = 1;

SelectPresentation Present(const UltraNavigation& nav, std::array<uint32_t, 2> hover) {
  return ResolveSelectPresentation({RequestedIdentity(0), RequestedIdentity(1)},
                                   {nav.OnEvilRyu(0), nav.OnEvilRyu(1)}, hover);
}

void Enter(UltraNavigation& nav, int side) {
  nav.Resolve(true, false, side, kEvilRyuNeighbourLive, PadDirection::kRight);
}

void TestCombinations() {
  ResetIdentities();
  UltraNavigation nav;
  SelectPresentation p = Present(nav, {kRyu, kRyu});
  Check(!p.evil[0] && !p.evil[1] && !p.hide[0] && !p.hide[1], "Ryu vs Ryu shows no Evil Ryu");

  Enter(nav, 0);
  p = Present(nav, {kRyu, kRyu});
  Check(p.evil[0] && !p.evil[1], "P1 Evil / P2 Ryu: only P1 is Evil Ryu");
  Check(RequestedIdentity(0) == FighterIdentity::kEvilRyu && RequestedIdentity(1) == FighterIdentity::kGuest,
        "P1 Evil / P2 Ryu: only P1 asks for Evil Ryu");

  Enter(nav, 1);
  p = Present(nav, {kRyu, kRyu});
  Check(p.evil[0] && p.evil[1], "Evil vs Evil shows both");

  nav.Resolve(true, false, 0, kRyuGuestCharacter, PadDirection::kLeft);
  p = Present(nav, {kHonda, kRyu});
  Check(!p.evil[0] && p.evil[1], "P1 Ryu / P2 Evil: only P2 is Evil Ryu");

  nav.Resolve(true, false, 1, kRyuGuestCharacter, PadDirection::kLeft);
  p = Present(nav, {kRyu, kRyu});
  Check(!p.evil[0] && !p.evil[1], "both leaving clears both");
}

void TestIsolation() {
  ResetIdentities();
  UltraNavigation nav;
  Enter(nav, 0);
  Check(!Present(nav, {kRyu, kRyu}).evil[1], "P2 on carrier Ryu is not Evil Ryu because P1 is");
  Check(!Present(nav, {kRyu, 5}).evil[1], "P2 on another fighter is not Evil Ryu");
  Check(!Present(nav, {kHonda, kRyu}).evil[0], "P1 off the carrier shows no Evil Ryu");

  nav.Resolve(true, false, 1, kEvilRyuNeighbourLive, PadDirection::kUp);
  Check(RequestedIdentity(0) == FighterIdentity::kEvilRyu, "P2 moving does not clear P1");

  Enter(nav, 1);
  nav.Resolve(true, false, 1, kRyuGuestCharacter, PadDirection::kLeft);
  Check(RequestedIdentity(0) == FighterIdentity::kEvilRyu && RequestedIdentity(1) == FighterIdentity::kGuest,
        "P2 backing out never clears P1 or leaves P2 requested");
  Check(Present(nav, {kRyu, kRyu}).evil[0] && !Present(nav, {kRyu, kRyu}).evil[1], "presentation follows");

  nav.Resolve(true, false, 0, kRyuGuestCharacter, PadDirection::kLeft);
  Enter(nav, 1);
  Check(RequestedIdentity(0) == FighterIdentity::kGuest && RequestedIdentity(1) == FighterIdentity::kEvilRyu,
        "P1 backing out never touches P2");
  nav.Reset();
  Check(!Present(nav, {kRyu, kRyu}).evil[0] && !Present(nav, {kRyu, kRyu}).evil[1], "a new select starts clean");

  ResetIdentities();
  Check(!ResolveSelectPresentation({FighterIdentity::kEvilRyu, FighterIdentity::kGuest}, {false, false}, {kRyu, kRyu})
             .evil[0],
        "a request without its own cell does not show Evil Ryu");
}

void TestSharedSlot() {
  ResetIdentities();
  UltraNavigation nav;
  Enter(nav, 0);
  SelectPresentation p = Present(nav, {kRyu, kEvilPortraitSlot});
  Check(p.evil[0] && !p.evil[1] && p.hide[1], "a side on the Evil Ryu slot id is hidden, not shown as Evil Ryu");
  p = Present(nav, {kRyu, kRyu});
  Check(!p.hide[0] && !p.hide[1], "an ordinary Ryu is never hidden");
  ResetIdentities();
  UltraNavigation none;
  p = Present(none, {kEvilPortraitSlot, kEvilPortraitSlot});
  Check(!p.hide[0] && !p.hide[1] && !p.any(), "nothing changes while no side is Evil Ryu");
}

void TestNameTiles() {
  constexpr uint16_t kNameAttr = 13;
  constexpr int kP1NameX = 16;
  constexpr int kP2NameX = 304;
  Check(RyuNameSide(kRyuNameFirstTile, kNameAttr, kP1NameX) == 0, "the first tile on the left is P1's name");
  Check(RyuNameSide(kRyuNameFirstTile, kNameAttr, kP2NameX) == 1, "the first tile on the right is P2's name");
  Check(RyuNameSide(kRyuNameLastTile, kNameAttr, kP1NameX) == 0, "the last tile on the left is P1's name");
  Check(RyuNameSide(kRyuNameLastTile, kNameAttr, kP2NameX) == 1, "the last tile on the right is P2's name");
  Check(RyuNameSide(kRyuNameFirstTile, 0x0500 | kNameAttr, kP1NameX) == 0,
        "the block size bits of a VS object do not hide the name");
  Check(RyuNameSide(kRyuNameFirstTile, 0x8000 | kNameAttr, kP2NameX) == 1, "a flipped name keeps its side");
  Check(RyuNameSide(0x710, kNameAttr, kP1NameX) == -1, "another fighter's name is left alone");
  Check(RyuNameSide(kRyuNameFirstTile, 12, kP1NameX) == -1, "another colour of the tile is not a name");
  Check(RyuNameSide(kRyuHudNameTile, 12, 40) == 0 && RyuNameSide(kRyuHudNameTile, 12, 300) == 1, "HUD names keep sides");

  ResetIdentities();
  UltraNavigation nav;
  Enter(nav, 0);
  const int p2_side = RyuNameSide(kRyuNameFirstTile, kNameAttr, kP2NameX);
  Check(RequestedIdentity(p2_side) != FighterIdentity::kEvilRyu, "P2's Ryu name never answers to P1's Evil Ryu");
  ResetIdentities();
  UltraNavigation other;
  Enter(other, 1);
  Check(RequestedIdentity(RyuNameSide(kRyuNameFirstTile, kNameAttr, kP2NameX)) == FighterIdentity::kEvilRyu,
        "P2's Evil Ryu gets its name whatever tile the object walk meets");
  Check(RequestedIdentity(RyuNameSide(kRyuNameFirstTile, kNameAttr, kP1NameX)) != FighterIdentity::kEvilRyu,
        "P1's Ryu name stays Ryu");
}

void TestBeatenPortraitSide() {
  using F = FighterIdentity;
  constexpr uint32_t kKen = kKenGuestCharacter;
  SelectPresentation p = ResolveSelectPresentation({F::kViolentKen, F::kGuest}, {true, false}, {kKen, kKen});
  Check(p.violent[0] && !p.violent[1], "only the side that chose Violent Ken loses as him");
  p = ResolveSelectPresentation({F::kGuest, F::kViolentKen}, {false, true}, {kKen, kKen});
  Check(!p.violent[0] && p.violent[1], "a Ken on the other side keeps Ken's beaten face");
  p = ResolveSelectPresentation({F::kViolentKen, F::kGuest}, {false, false}, {kKen, 2});
  Check(!p.violent[0], "a stale request without its cell shows Ken");
  p = ResolveSelectPresentation({F::kEvilRyu, F::kGuest}, {true, false}, {kRyu, 2});
  Check(p.evil[0] && !p.violent[0], "Evil Ryu takes the same path with his own art");
}

void TestFightIdentity() {
  ResetIdentities();
  UltraNavigation nav;
  Enter(nav, 0);
  Check(ResolveFighterIdentity(kRyuGuestCharacter, false, true, RequestedIdentity(0)).identity ==
            FighterIdentity::kEvilRyu,
        "P1's confirmed Evil Ryu fights as Evil Ryu");
  Check(ResolveFighterIdentity(kRyuGuestCharacter, false, true, RequestedIdentity(1)).identity ==
            FighterIdentity::kRyu,
        "P2's ordinary Ryu fights as Ryu");
}

// The guest's portrait tables at their real relative positions, each word filled with a value naming its address.
struct GuestPortraits {
  static constexpr uint32_t kOrigin = 0x83BE0000;
  static constexpr uint32_t kRows = 0x83BE3608 - kOrigin;
  static constexpr uint32_t kRowsNeighbour = kRows + kLargePortraitIds * kPortraitRowBytes;
  static constexpr uint32_t kLarge = 0x83BE59A0 - kOrigin;
  static constexpr uint32_t kMini = kLarge + kLargePortraitIds * 4;
  static constexpr uint32_t kLoser = 0x83BE5A30 - kOrigin;
  static constexpr uint32_t kHoverP1 = 0x7000;
  static constexpr uint32_t kBytes = 0x8000;
  static constexpr uint32_t kEvilArt = 0xE0000001;
  static constexpr uint32_t kViolentArt = 0xE0000002;
  static constexpr uint32_t kEvilBeaten = 0xE0000003;
  static constexpr uint32_t kViolentBeaten = 0xE0000004;

  std::vector<uint8_t> memory = std::vector<uint8_t>(kBytes);

  GuestPortraits() {
    for (uint32_t at = 0; at < kBytes; at += 4) ssf2t::StoreBE32(memory.data(), at, 0x10000000u | at);
  }

  uint8_t* base() { return memory.data(); }
  uint32_t Word(uint32_t at) const { return ssf2t::LoadBE32(memory.data(), at); }
  uint32_t Mini(uint32_t id) const { return Word(kMini + id * 4); }
  uint32_t LargeOfHover(int side) const { return Word(kLarge + Word(kHoverP1 + uint32_t(side) * 4) * 4); }
  void SetHover(std::array<uint32_t, 2> hover) {
    ssf2t::StoreBE32(memory.data(), kHoverP1, hover[0]);
    ssf2t::StoreBE32(memory.data(), kHoverP1 + 4, hover[1]);
  }

  static PortraitTables Tables() {
    return {0x83BE59E4 - kOrigin, 0x83BE5A74 - kOrigin, kRows, {kHoverP1, kHoverP1 + 4}};
  }
};

template <typename Pass>
void RunPass(GuestPortraits& guest, std::array<FighterIdentity, 2> requested, std::array<bool, 2> on_cell,
             std::array<uint32_t, 2> hover, Pass&& pass) {
  guest.SetHover(hover);
  const SelectPresentation presentation = ResolveSelectPresentation(requested, on_cell, hover);
  const bool any_evil = presentation.evil[0] || presentation.evil[1];
  const bool any_violent = presentation.violent[0] || presentation.violent[1];
  const LoanArt evil = any_evil ? LoanArt{GuestPortraits::kEvilArt, GuestPortraits::kEvilBeaten} : LoanArt{};
  const LoanArt violent = any_violent ? LoanArt{GuestPortraits::kViolentArt, GuestPortraits::kViolentBeaten} : LoanArt{};
  if (!presentation.any()) return pass();
  DrawWithPortraitLoans(GuestPortraits::Tables(), presentation, hover, evil, violent, guest.base(), pass);
}

void CheckRosterIntact(const GuestPortraits& guest, const GuestPortraits& untouched, const char* what) {
  bool intact = true;
  for (uint32_t id = 0; id <= 16; ++id) intact = intact && guest.Mini(id) == untouched.Mini(id);
  Check(intact, what);
}

void TestRosterNeverChanges() {
  using F = FighterIdentity;
  constexpr uint32_t kKen = kKenGuestCharacter;
  constexpr uint32_t kNobody = kNoLargePortrait;
  const GuestPortraits clean;
  GuestPortraits guest;
  const auto during = [&](const char* what) {
    return [&, what] {
      CheckRosterIntact(guest, clean, what);
      Check(guest.Mini(kRyu) == clean.Mini(kRyu) && guest.Mini(kKen) == clean.Mini(kKen), what);
    };
  };

  RunPass(guest, {F::kGuest, F::kGuest}, {false, false}, {kRyu, kKen}, during("Ultra Off keeps the roster"));
  RunPass(guest, {F::kGuest, F::kGuest}, {false, false}, {kNobody, kNobody}, during("Ultra On with no pick keeps the roster"));
  RunPass(guest, {F::kEvilRyu, F::kGuest}, {true, false}, {kRyu, kHonda}, [&] {
    during("Evil Ryu keeps the roster")();
    Check(guest.LargeOfHover(0) == GuestPortraits::kEvilArt, "Evil Ryu shows his large portrait");
  });

  RunPass(guest, {F::kViolentKen, F::kGuest}, {true, false}, {kKen, kHonda}, [&] {
    during("P1 Violent Ken keeps the roster")();
    Check(guest.LargeOfHover(0) == GuestPortraits::kViolentArt, "P1 Violent Ken shows his large portrait");
    Check(guest.LargeOfHover(1) == clean.Word(GuestPortraits::kLarge + kHonda * 4), "P2's Honda keeps his large portrait");
  });
  RunPass(guest, {F::kGuest, F::kViolentKen}, {false, true}, {kHonda, kKen}, [&] {
    during("P2 Violent Ken keeps the roster")();
    Check(guest.LargeOfHover(1) == GuestPortraits::kViolentArt, "P2 Violent Ken shows his large portrait");
    Check(guest.LargeOfHover(0) == clean.Word(GuestPortraits::kLarge + kHonda * 4), "P1's Honda keeps his large portrait");
  });
  RunPass(guest, {F::kViolentKen, F::kGuest}, {true, false}, {kKen, kKen}, [&] {
    during("Violent Ken beside Ken keeps the roster")();
    Check(guest.LargeOfHover(0) == GuestPortraits::kViolentArt, "Violent Ken stays himself beside Ken");
    Check(guest.LargeOfHover(1) == clean.Word(GuestPortraits::kLarge + kKen * 4), "Ken beside Violent Ken stays Ken");
  });
  RunPass(guest, {F::kEvilRyu, F::kViolentKen}, {true, true}, {kRyu, kKen}, [&] {
    during("Evil Ryu against Violent Ken keeps the roster")();
    Check(guest.LargeOfHover(0) == GuestPortraits::kEvilArt && guest.LargeOfHover(1) == GuestPortraits::kViolentArt,
          "each side keeps its own large portrait");
  });
  RunPass(guest, {F::kViolentKen, F::kViolentKen}, {true, true}, {kKen, kKen}, [&] {
    during("the Violent Ken mirror keeps the roster")();
    Check(guest.LargeOfHover(0) == GuestPortraits::kViolentArt && guest.LargeOfHover(1) == GuestPortraits::kViolentArt,
          "both sides of the mirror show Violent Ken");
  });
}

void TestNeighbourTablesNeverChange() {
  using F = FighterIdentity;
  constexpr uint32_t kKen = kKenGuestCharacter;
  const GuestPortraits clean;
  GuestPortraits guest;
  RunPass(guest, {F::kViolentKen, F::kGuest}, {true, false}, {kKen, kHonda}, [&] {
    bool intact = true;
    for (uint32_t word = 0; word < kPortraitPaletteColumns; ++word) {
      const uint32_t at = GuestPortraits::kRowsNeighbour + word * 4;
      intact = intact && guest.Word(at) == clean.Word(at);
    }
    Check(intact, "the table after the palette rows is not lent");
    Check(guest.Word(GuestPortraits::kLoser + 18 * 4) == clean.Word(GuestPortraits::kLoser + 18 * 4),
          "the word past the loser table is not lent");
  });
}

void TestStateIsRestored() {
  using F = FighterIdentity;
  constexpr uint32_t kKen = kKenGuestCharacter;
  GuestPortraits guest;
  guest.SetHover({kKen, kRyu});
  const std::vector<uint8_t> before = guest.memory;
  const auto noop = [] {};
  for (int visit = 0; visit < 8; ++visit) {
    RunPass(guest, {F::kViolentKen, F::kGuest}, {true, false}, {kKen, kRyu}, noop);
    Check(guest.memory == before, "leaving Violent Ken restores every guest word");
    RunPass(guest, {F::kGuest, F::kGuest}, {false, false}, {kKen, kRyu}, noop);
    RunPass(guest, {F::kViolentKen, F::kEvilRyu}, {true, true}, {kKen, kRyu}, noop);
    Check(guest.memory == before, "re-entering Violent Ken accumulates nothing");
  }
  guest.SetHover({kRyu, kKen});
  const std::vector<uint8_t> beside = guest.memory;
  RunPass(guest, {F::kGuest, F::kViolentKen}, {false, true}, {kRyu, kKen}, noop);
  Check(guest.memory == beside, "P2's Violent Ken restores every guest word");
}

void TestLoanRefusesForeignIds() {
  GuestPortraits guest;
  const std::vector<uint8_t> before = guest.memory;
  {
    PortraitLoan loan(GuestPortraits::Tables(), kLargePortraitIds, {GuestPortraits::kViolentArt, 0}, guest.base());
    Check(guest.memory == before, "an id past the large table writes nothing");
    Check(loan.Replacement() == kNoLargePortrait, "a refused id shows no portrait");
  }
  Check(guest.memory == before, "a refused loan restores nothing");
}

void TestViolentIdIsAlwaysSafe() {
  using F = FighterIdentity;
  constexpr uint32_t kKen = kKenGuestCharacter;
  constexpr uint32_t kBison = 8;
  constexpr uint32_t kAkuma = 16;
  for (uint32_t other = 0; other <= kEvilPortraitSlot; ++other) {
    for (const int side : {0, 1}) {
      std::array<F, 2> requested{F::kGuest, F::kGuest};
      std::array<bool, 2> on_cell{false, false};
      std::array<uint32_t, 2> hover{other, other};
      requested[size_t(side)] = F::kViolentKen;
      on_cell[size_t(side)] = true;
      hover[size_t(side)] = kKen;
      const SelectPresentation presentation = ResolveSelectPresentation(requested, on_cell, hover);
      const uint32_t id = ViolentPortraitId(presentation, hover);
      Check(id < kLargePortraitIds, "Violent Ken's id stays inside the large table");
      Check(id != kBison && id != kAkuma, "Violent Ken's id has no guest special case");
      Check(id != hover[size_t(1 - side)], "Violent Ken never shares an id the other side really shows");
    }
  }
}

}  // namespace

int main() {
  TestCombinations();
  TestIsolation();
  TestSharedSlot();
  TestNameTiles();
  TestBeatenPortraitSide();
  TestFightIdentity();
  TestRosterNeverChanges();
  TestNeighbourTablesNeverChange();
  TestStateIsRestored();
  TestLoanRefusesForeignIds();
  TestViolentIdIsAlwaysSafe();
  if (failures == 0) std::printf("ultra select presentation tests passed\n");
  return failures == 0 ? 0 : 1;
}
