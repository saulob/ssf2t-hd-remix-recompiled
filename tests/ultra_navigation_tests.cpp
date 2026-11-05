#include <array>
#include <cstdio>

#include "character/akuma_navigation.h"
#include "character/fighter_identity.h"
#include "character/ultra_navigation.h"

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
  if (condition) return;
  std::printf("FAIL %s\n", what);
  ++failures;
}

}  // namespace

int main() {
  using namespace ssf2t::character;

  constexpr std::array<uint8_t, 8> kVisualTop = {0, 1, 2, 3, 13, 14, 10, 9};
  constexpr std::array<uint8_t, 8> kVisualBottom = {4, 5, 6, 7, 12, 15, 11, 8};
  constexpr std::array<uint8_t, 6> kVersusMiddleRow = {10, 4, 5, 6, 7, 9};
  constexpr uint8_t kLastTop = kVisualTop[7];
  Check(kLastTop == kEvilRyuNeighbourLive, "the Evil Ryu cell follows the rightmost top-row cell");
  constexpr uint8_t kRyu = 0;

  {
    ResetIdentities();
    UltraNavigation off;
    Check(off.Resolve(false, false, 0, kLastTop, PadDirection::kRight).action == UltraMoveAction::kNone,
          "Off: Right from the last top-row cell stays with the guest");
    Check(!off.OnEvilRyu(0), "Off: Evil Ryu is not selected");
    Check(RequestedIdentity(0) == FighterIdentity::kGuest, "Off: no Evil Ryu identity is requested");
    Check(ResolveFighterIdentity(kRyu, false, false, RequestedIdentity(0)).identity == FighterIdentity::kRyu,
          "Off: Ryu stays Ryu");
  }

  {
    ResetIdentities();
    UltraNavigation nav;
    for (int column = 0; column < 7; ++column) {
      Check(nav.Resolve(true, false, 0, kVisualTop[column], PadDirection::kRight).action == UltraMoveAction::kNone,
            "On: Right from an inner top-row cell stays with the guest");
    }
    for (int column = 0; column < kRosterColumns; ++column) {
      Check(nav.Resolve(true, false, 0, kVisualBottom[column], PadDirection::kRight).action == UltraMoveAction::kNone,
            "On: bottom row never enters Evil Ryu");
    }
    Check(nav.Resolve(true, false, 0, kLastTop, PadDirection::kUp).action == UltraMoveAction::kNone,
          "On: Up from the last top-row cell stays with the guest");
    Check(nav.Resolve(true, false, 0, kLastTop, PadDirection::kLeft).action == UltraMoveAction::kNone,
          "On: Left from the last top-row cell stays with the guest");
    Check(!nav.OnEvilRyu(0), "On: guest moves do not select Evil Ryu");

    const UltraMove enter = nav.Resolve(true, false, 0, kLastTop, PadDirection::kRight);
    Check(enter.action == UltraMoveAction::kEnter, "On: Right from the last top-row cell enters Evil Ryu");
    Check(enter.live == kRyu, "Evil Ryu rests the guest cursor on the Ryu carrier");
    Check(nav.OnEvilRyu(0), "Evil Ryu is selected for player 1");
    Check(!nav.OnEvilRyu(1), "Player 2 is not affected by player 1");
    Check(RequestedIdentity(0) == FighterIdentity::kEvilRyu, "Player 1 requests Evil Ryu");
    Check(RequestedIdentity(1) == FighterIdentity::kGuest, "Player 2 requests nothing");

    Check(nav.Resolve(true, false, 0, kRyu, PadDirection::kDown).action == UltraMoveAction::kHold,
          "Down on Evil Ryu holds while Violent Ken is unavailable");
    Check(nav.Resolve(true, false, 0, kRyu, PadDirection::kRight).action == UltraMoveAction::kHold,
          "Right on Evil Ryu stays");
    Check(nav.Resolve(true, false, 0, kRyu, PadDirection::kUp).action == UltraMoveAction::kHold, "Up on Evil Ryu stays");
    Check(nav.OnEvilRyu(0), "Held moves keep Evil Ryu selected");

    Check(nav.Resolve(true, false, 1, kVisualBottom[1], PadDirection::kRight).action == UltraMoveAction::kNone,
          "Player 2 navigates independently");
    Check(nav.Resolve(true, false, 1, kRyu, PadDirection::kRight).action == UltraMoveAction::kNone,
          "Player 2 on guest Ryu is not held by player 1's Evil Ryu");

    const UltraMove leave = nav.Resolve(true, false, 0, kRyu, PadDirection::kLeft);
    Check(leave.action == UltraMoveAction::kLeave, "Left on Evil Ryu leaves");
    Check(leave.live == kLastTop, "Left on Evil Ryu returns to the last original top-row cell");
    Check(!nav.OnEvilRyu(0), "Evil Ryu is no longer selected after Left");
    Check(RequestedIdentity(0) == FighterIdentity::kGuest, "Leaving clears the Evil Ryu request");
  }

  {
    ResetIdentities();
    UltraNavigation nav;
    nav.Resolve(true, false, 0, kLastTop, PadDirection::kRight);
    nav.Resolve(true, false, 1, kLastTop, PadDirection::kRight);
    Check(nav.OnEvilRyu(0) && nav.OnEvilRyu(1), "Both players can select Evil Ryu independently");
    nav.Resolve(true, false, 1, kRyu, PadDirection::kLeft);
    Check(nav.OnEvilRyu(0) && !nav.OnEvilRyu(1), "Player 2 leaving keeps player 1 on Evil Ryu");
    Check(RequestedIdentity(0) == FighterIdentity::kEvilRyu && RequestedIdentity(1) == FighterIdentity::kGuest,
          "Requests follow each player");
  }

  {
    ResetIdentities();
    UltraNavigation nav;
    nav.Resolve(true, false, 0, kLastTop, PadDirection::kRight);
    const FighterIdentityInfo fight = ResolveFighterIdentity(kRyu, false, true, RequestedIdentity(0));
    Check(fight.identity == FighterIdentity::kEvilRyu, "Confirming Evil Ryu yields the Evil Ryu fight identity");
    Check(fight.guest_character == kRyu, "The guest still fights as Ryu");
    const FighterIdentityInfo other = ResolveFighterIdentity(kRyu, false, true, RequestedIdentity(1));
    Check(other.identity == FighterIdentity::kRyu, "Player 2 as Ryu stays Ryu while player 1 is Evil Ryu");
    Check(ResolveFighterIdentity(kRyu, false, false, RequestedIdentity(0)).identity == FighterIdentity::kRyu,
          "Turning the option off before the fight removes the Evil Ryu identity");
    Check(ResolveFighterIdentity(kAkumaGuestCharacter, false, true, RequestedIdentity(0)).identity ==
              FighterIdentity::kGuest,
          "A non-Ryu guest fighter never becomes Evil Ryu");

    nav.Reset();
    Check(!nav.OnEvilRyu(0) && RequestedIdentity(0) == FighterIdentity::kGuest, "Reset clears the selection");
    Check(ResolveFighterIdentity(kRyu, false, true, RequestedIdentity(0)).identity == FighterIdentity::kRyu,
          "After a reset Ryu is Ryu");
  }

  {
    ResetIdentities();
    UltraNavigation nav;
    nav.Resolve(true, false, 0, kLastTop, PadDirection::kRight);
    nav.Resolve(false, false, 0, kRyu, PadDirection::kLeft);
    Check(!nav.OnEvilRyu(0), "Turning the option off clears the selection");
    Check(nav.Resolve(true, false, 2, kLastTop, PadDirection::kRight).action == UltraMoveAction::kNone,
          "Invalid sides are ignored");
  }

  {
    // Local Match: both players navigate the same select, each with their own cell, cursor and requested identity.
    ResetIdentities();
    UltraNavigation nav;
    for (const SelectLayout layout : {SelectLayout::kArcade, SelectLayout::kVersus}) {
      nav.Reset();
      const uint8_t last = layout == SelectLayout::kArcade ? kTopRow[7] : kVersusMiddleRow.back();
      Check(last == kEvilRyuNeighbourLive, "Evil Ryu follows id 9 in both grids");
      Check(nav.Resolve(true, false, 0, last, PadDirection::kRight).action == UltraMoveAction::kEnter, "P1 enters Evil Ryu");
      Check(nav.OnEvilRyu(0) && !nav.OnEvilRyu(1), "only P1 is on Evil Ryu");
      Check(RequestedIdentity(0) == FighterIdentity::kEvilRyu && RequestedIdentity(1) == FighterIdentity::kGuest,
            "P1 asks for Evil Ryu, P2 asks for nothing");

      Check(nav.Resolve(true, false, 1, last, PadDirection::kRight).action == UltraMoveAction::kEnter, "P2 enters Evil Ryu");
      Check(nav.OnEvilRyu(0) && nav.OnEvilRyu(1), "both players can stand on Evil Ryu");
      Check(RequestedIdentity(0) == FighterIdentity::kEvilRyu && RequestedIdentity(1) == FighterIdentity::kEvilRyu,
            "both ask for Evil Ryu");

      Check(nav.Resolve(true, false, 0, kRyu, PadDirection::kLeft).action == UltraMoveAction::kLeave, "P1 leaves");
      Check(!nav.OnEvilRyu(0) && nav.OnEvilRyu(1), "P1 leaving keeps P2 on Evil Ryu");
      Check(RequestedIdentity(0) == FighterIdentity::kGuest && RequestedIdentity(1) == FighterIdentity::kEvilRyu,
            "P1 leaving clears only P1's request");

      Check(nav.Resolve(true, false, 1, kRyu, PadDirection::kLeft).action == UltraMoveAction::kLeave, "P2 leaves");
      Check(nav.Resolve(true, false, 1, last, PadDirection::kRight).action == UltraMoveAction::kEnter, "P2 can re-enter");
      Check(!nav.OnEvilRyu(0), "P2 entering never selects P1");
      Check(nav.Resolve(true, false, 0, kRyu, PadDirection::kRight).action == UltraMoveAction::kNone,
            "P1 on guest Ryu is not held by P2's Evil Ryu");
      Check(nav.Resolve(true, false, 1, kRyu, PadDirection::kRight).action == UltraMoveAction::kHold, "P2 holds on Evil Ryu");

      for (const PadDirection direction :
           {PadDirection::kUp, PadDirection::kDown, PadDirection::kLeft, PadDirection::kRight}) {
        nav.Resolve(true, false, 1, kRyu, direction);
        Check(RequestedIdentity(0) != FighterIdentity::kViolentKen && RequestedIdentity(1) != FighterIdentity::kViolentKen,
              "no move selects Violent Ken");
        nav.Resolve(true, false, 1, last, PadDirection::kRight);
      }
      Check(ResolveFighterIdentity(kRyu, false, true, RequestedIdentity(1)).identity == FighterIdentity::kEvilRyu,
            "P2's confirmed Evil Ryu resolves to the Evil Ryu fighter");
      Check(ResolveFighterIdentity(kRyu, false, true, RequestedIdentity(0)).identity == FighterIdentity::kRyu,
            "P1's ordinary Ryu is untouched by P2's confirm");
    }

    Check(EvilRyuCursorX(SelectLayout::kArcade) == 336 && EvilRyuCursorX(SelectLayout::kVersus) == 304,
          "the cursor sits one cell right of the grid's last column");
    Check(EvilRyuCursorX(SelectLayout::kArcade) - 304 == 32 && EvilRyuCursorX(SelectLayout::kVersus) - 272 == 32,
          "both grids put the Evil Ryu cell one 32-unit pitch right of id 9");
  }

  {
    // Akuma and Evil Ryu share the select: Up and Down belong to Akuma, Right from id 9 and everything on the host cell
    // to Evil Ryu, in both grids.
    for (const SelectLayout layout : {SelectLayout::kArcade, SelectLayout::kVersus}) {
      for (int column = 0; column < LayoutColumns(layout); ++column) {
        ResetIdentities();
        UltraNavigation ultra;
        AkumaNavigation akuma;
        const uint8_t top = TopRowAt(layout, column);
        uint8_t next = 0;
        Check(ultra.Resolve(true, false, 0, top, PadDirection::kUp).action == UltraMoveAction::kNone,
              "Evil Ryu never takes Up from the top row");
        Check(akuma.Resolve(true, layout, 0, top, NavDirection::kUp, next) && next == kAkumaSelect,
              "Up from every top-row cell reaches Akuma while Evil Ryu is on");
        Check(ultra.Resolve(true, false, 0, kAkumaSelect, PadDirection::kDown).action == UltraMoveAction::kNone,
              "Evil Ryu leaves Akuma's exits alone");
      }
      ResetIdentities();
      UltraNavigation ultra;
      AkumaNavigation akuma;
      uint8_t next = 0;
      Check(ultra.Resolve(true, false, 0, kEvilRyuNeighbourLive, PadDirection::kRight).action == UltraMoveAction::kEnter,
            "Right from id 9 reaches Evil Ryu while Akuma is on");
      Check(akuma.Resolve(true, layout, 0, kEvilRyuNeighbourLive, NavDirection::kUp, next) ==
                (layout == SelectLayout::kArcade),
            "id 9 sits on the arcade top row but on the versus middle row");
      Check(ultra.Resolve(true, false, 0, kRyu, PadDirection::kUp).action == UltraMoveAction::kHold,
            "Up on Evil Ryu holds instead of reaching Akuma");
    }
  }

  {
    constexpr uint8_t kRemix = 1;
    constexpr uint8_t kClassicStyle = 0;
    Check(GuardUltraClassic(kRemix, true, 0) == UltraClassicGuard::kNone, "a Remixed Ultra fighter needs no guard");
    Check(GuardUltraClassic(kRemix, true, 1) == UltraClassicGuard::kClearClassic, "an Ultra cell cannot go classic");
    Check(GuardUltraClassic(kClassicStyle, true, 0) == UltraClassicGuard::kLeaveCell,
          "the classic style has no Ultra cell");
    Check(GuardUltraClassic(kClassicStyle, true, 1) == UltraClassicGuard::kLeaveCell,
          "in the classic style the side keeps its classic carrier");
    Check(GuardUltraClassic(kRemix, false, 1) == UltraClassicGuard::kNone, "plain Ken and Ryu keep their classic version");
    Check(GuardUltraClassic(kClassicStyle, false, 1) == UltraClassicGuard::kNone,
          "every original fighter keeps the classic style untouched");

    ResetIdentities();
    UltraNavigation evil;
    evil.Resolve(true, true, 0, kEvilRyuNeighbourLive, PadDirection::kRight);
    evil.Resolve(true, true, 1, kViolentKenNeighbourLive, PadDirection::kRight);
    evil.Leave(0);
    Check(!evil.OnUltraCell(0) && RequestedIdentity(0) == FighterIdentity::kGuest, "Leave ends that side's Ultra choice");
    Check(evil.OnViolentKen(1) && RequestedIdentity(1) == FighterIdentity::kViolentKen, "Leave spares the other side");
    evil.Leave(2);
    evil.Leave(-1);
    Check(evil.OnViolentKen(1), "a side out of range changes nothing");
    const uint8_t saved = evil.Mask();
    UltraNavigation peer;
    peer.Restore(saved);
    Check(peer.Mask() == saved && peer.OnViolentKen(1), "the guard's cell state is what the rollback blob carries");
    evil.Reset();
  }

  {
    using ssf2t::character::SelectSession;
    SelectSession session;
    Check(session.Touch(1000), "the first touch starts a session");
    Check(!session.Touch(1016), "the next frame continues it");
    Check(!session.Touch(3900), "a pause under the gap continues it");
    Check(session.Touch(9000), "a later Character Select is a new session");
    Check(session.id() == 2, "sessions are counted");
  }

  if (failures == 0) std::printf("ultra navigation tests passed\n");
  return failures == 0 ? 0 : 1;
}
