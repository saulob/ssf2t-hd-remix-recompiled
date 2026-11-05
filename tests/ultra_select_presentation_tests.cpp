#include <array>
#include <cstdio>

#include "character/fighter_identity.h"
#include "character/ultra_navigation.h"
#include "character/ultra_select_presentation.h"

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

}  // namespace

int main() {
  TestCombinations();
  TestIsolation();
  TestSharedSlot();
  TestNameTiles();
  TestBeatenPortraitSide();
  TestFightIdentity();
  if (failures == 0) std::printf("ultra select presentation tests passed\n");
  return failures == 0 ? 0 : 1;
}
