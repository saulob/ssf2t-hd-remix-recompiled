#include <array>
#include <cstdio>
#include <string_view>
#include <tuple>

#include "character/akuma_navigation.h"
#include "character/fighter_identity.h"
#include "character/roster_cells.h"
#include "character/secret_select.h"
#include "character/select_mode.h"
#include "character/ultra_navigation.h"

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
  if (condition) return;
  std::printf("FAIL %s\n", what);
  ++failures;
}

using namespace ssf2t::character;

constexpr std::array<uint8_t, 4> kGameModes = {kArcadeGameMode, kLocalVersusGameMode, kTrainingGameMode,
                                               kOnlineSelectMode};
constexpr uint8_t kClassicStyle = 0;
constexpr uint8_t kRemixStyle = 1;
constexpr uint8_t kGuile = 3;

// The gate this generalisation replaced, kept as the negative control.
constexpr bool PreviousGateOpen(uint8_t game_mode, uint8_t hd_style) {
  return hd_style != 0 || game_mode == kArcadeGameMode;
}

struct Fighter {
  uint8_t live = 0;
  uint8_t character = 0;
  uint8_t window = 1;
  uint8_t timer = 0;
  uint8_t hidden = 0;
  uint8_t old_version = 0;
  uint8_t colour = 0;
  uint8_t akuma_table = 0xAA;
  uint16_t pad = 0;
};

// The guest commit's Classic branch, proved on both images by the fingerprint tests.
void NativeCommit(Fighter& fighter, uint8_t game_mode, uint8_t hd_style, uint32_t online_gate) {
  if (OnlineSelectMode(game_mode) && ClassicStyle(hd_style) && online_gate == 0) return;
  if (!ClassicStyle(hd_style) || (fighter.character != 0 && fighter.character != 4) || fighter.window != 2) return;
  if (++fighter.timer >= 30) {
    fighter.window = 3;
    return;
  }
  if ((fighter.pad & kAkumaCommitButtons) != kAkumaCommitButtons) return;
  fighter.akuma_table = 0;
  fighter.colour = (fighter.pad & 0x700) != 0 ? 11 : 10;
  fighter.hidden = 1;
  fighter.character = 0;
  fighter.old_version = 0;
  fighter.window = 3;
}

void ConfirmCell(Fighter& fighter, uint8_t hd_style, bool available) {
  if (!ClassicAkumaConfirmed(ClassicStyle(hd_style), available, fighter.live, true)) return;
  fighter.character = kAkumaClassicCarrier;
  fighter.window = kAkumaWindowOpen;
  fighter.timer = 0;
}

// One guest window call as the hook wraps it: the punches and the session word live only for the call.
void WindowTick(Fighter& fighter, uint8_t game_mode, uint8_t hd_style, uint32_t& online_gate) {
  const bool due = ClassicAkumaCommitDue(ClassicStyle(hd_style), fighter.live, fighter.window, fighter.hidden);
  const uint16_t pad = fighter.pad;
  const uint32_t gate = online_gate;
  if (due) {
    fighter.pad = uint16_t(pad | kAkumaCommitButtons);
    online_gate = 1;
  }
  NativeCommit(fighter, game_mode, hd_style, online_gate);
  fighter.pad = pad;
  online_gate = gate;
}

struct Availability {
  const char* name;
  SecretCellInputs in;
  bool akuma;
  bool evil_ryu;
  bool violent_ken;
};

SecretCellInputs AllLocal() {
  SecretCellInputs in;
  in.akuma_option = true;
  in.ultra_option = true;
  in.evil_ryu_packages = true;
  in.violent_ken_packages = true;
  in.violent_ken_gameplay = true;
  in.negotiated = {true, true, true};
  return in;
}

// Visible, reachable and confirmable from one policy answer, on the real roster plan and the real navigation.
void CheckCellsAgree(const char* name, uint8_t mode, uint8_t style, int side, const SecretCellInputs& in, bool want_akuma,
                     bool want_evil, bool want_violent) {
  const SecretCells cells = ResolveSecretCells(in);
  Check(cells.akuma == want_akuma && cells.evil_ryu == want_evil && cells.violent_ken == want_violent, name);
  RosterCellInputs roster;
  roster.akuma = cells.akuma;
  roster.evil_ryu = cells.evil_ryu;
  roster.violent_ken = cells.violent_ken;
  const RosterCellPlan plan = PlanRosterCells(roster);

  const SelectLayout layout = SelectLayoutForMode(mode);
  AkumaNavigation akuma;
  uint8_t next = 0;
  const bool akuma_reached = akuma.Resolve(cells.akuma, layout, side, kGuile, NavDirection::kUp, next);
  Fighter fighter;
  fighter.live = akuma_reached ? next : kGuile;
  fighter.character = fighter.live;
  uint32_t gate = 0;
  if (ClassicStyle(style)) {
    ConfirmCell(fighter, style, cells.akuma);
    WindowTick(fighter, mode, style, gate);
  } else {
    fighter.hidden = akuma_reached ? 1 : 0;
  }
  Check(plan.akuma == cells.akuma && akuma_reached == cells.akuma && (fighter.hidden != 0) == cells.akuma,
        "Akuma: visible, reachable and confirmable agree");

  ResetIdentities();
  UltraNavigation evil;
  const bool evil_reached = evil.Resolve(cells.evil_ryu, cells.violent_ken, side, kEvilRyuNeighbourLive,
                                         PadDirection::kRight).action == UltraMoveAction::kEnter;
  const bool evil_fights = ResolveFighterIdentity(kRyuGuestCharacter, false, true, RequestedIdentity(side)).identity ==
                           FighterIdentity::kEvilRyu;
  Check(plan.evil_ryu == cells.evil_ryu && evil_reached == cells.evil_ryu && evil_fights == cells.evil_ryu,
        "Evil Ryu: visible, reachable and confirmable agree");

  ResetIdentities();
  UltraNavigation violent;
  const bool violent_reached = violent.Resolve(cells.evil_ryu, cells.violent_ken, side, kViolentKenNeighbourLive,
                                               PadDirection::kRight).action == UltraMoveAction::kEnter;
  const bool violent_fights = ResolveFighterIdentity(kKenGuestCharacter, false, true, RequestedIdentity(side)).identity ==
                              FighterIdentity::kViolentKen;
  Check(plan.violent_ken == cells.violent_ken && violent_reached == cells.violent_ken &&
            violent_fights == cells.violent_ken,
        "Violent Ken: visible, reachable and confirmable agree");
  ResetIdentities();
}

void TestSecretCellMatrix() {
  for (const uint8_t mode : kGameModes) {
    for (const uint8_t style : {kClassicStyle, kRemixStyle}) {
      for (const int side : {0, 1}) {
        const bool online = OnlineSelectMode(mode);
        SecretCellInputs in = AllLocal();
        in.online_select = online;
        CheckCellsAgree("every enabled cell works in every mode and style", mode, style, side, in, true, true, true);

        SecretCellInputs off = in;
        off.akuma_option = off.ultra_option = false;
        CheckCellsAgree("a disabled option removes the local cells", mode, style, side, off, online, online, online);

        SecretCellInputs broken = in;
        broken.evil_ryu_packages = broken.violent_ken_packages = false;
        CheckCellsAgree("missing packages remove the local Ultra cells", mode, style, side, broken, true, online, online);

        SecretCellInputs no_gameplay = in;
        no_gameplay.violent_ken_gameplay = false;
        CheckCellsAgree("Violent Ken needs his gameplay data", mode, style, side, no_gameplay, true, true, false);

        SecretCellInputs peer = in;
        peer.negotiated = {};
        CheckCellsAgree("an unsupported peer removes every online cell", mode, style, side, peer, !online, !online,
                        !online);

        for (const auto [akuma, evil, violent] :
             {std::tuple{true, false, false}, std::tuple{false, true, false}, std::tuple{false, false, true}}) {
          SecretCellInputs one = in;
          one.negotiated = {akuma, evil, violent};
          CheckCellsAgree("online each fighter is negotiated alone", mode, style, side, one, !online || akuma,
                          !online || evil, !online || violent);
        }

        SecretCellInputs developer = in;
        developer.developer_netplay = true;
        CheckCellsAgree("the developer test has no Ultra identity to agree on, so no cell", mode, style, side, developer,
                        true, false, false);
      }
    }
  }
}

void TestPreviousGateNegativeControls() {
  const auto reachable = [](bool gate, uint8_t mode, int side) {
    AkumaNavigation navigation;
    uint8_t next = 0;
    return navigation.Resolve(gate, SelectLayoutForMode(mode), side, kGuile, NavDirection::kUp, next);
  };
  for (const uint8_t mode : {kTrainingGameMode, kLocalVersusGameMode, kOnlineSelectMode}) {
    SecretCellInputs in = AllLocal();
    in.online_select = OnlineSelectMode(mode);
    const SecretCells cells = ResolveSecretCells(in);
    Check(!reachable(PreviousGateOpen(mode, kClassicStyle) && cells.akuma, mode, 0),
          "negative control: the previous Arcade gate left this Classic mode's cell unreachable");
    Check(reachable(cells.akuma, mode, 0), "the policy reaches the same cell");
    UltraNavigation dead;
    const bool gate = PreviousGateOpen(mode, kClassicStyle);
    Check(dead.Resolve(gate && cells.evil_ryu, gate && cells.violent_ken, 0, kEvilRyuNeighbourLive,
                       PadDirection::kRight).action == UltraMoveAction::kNone,
          "negative control: the previous gate kept Evil Ryu out of this Classic mode");
    ResetIdentities();
  }
  for (const uint8_t style : {kClassicStyle, kRemixStyle}) {
    Check(PreviousGateOpen(kArcadeGameMode, style), "Arcade worked before");
  }
  for (const uint8_t mode : kGameModes) Check(PreviousGateOpen(mode, kRemixStyle), "Remix worked before");
}

void TestClassicAkumaEveryMode() {
  for (const uint8_t mode : kGameModes) {
    for (const uint32_t session_gate : {0u, 1u}) {
      Fighter akuma;
      akuma.live = kAkumaSelect;
      akuma.character = kAkumaSelect;
      akuma.colour = 2;
      akuma.old_version = 1;
      akuma.pad = 0x10;
      ConfirmCell(akuma, kClassicStyle, true);
      Check(akuma.character == 0 && akuma.window == kAkumaWindowOpen && akuma.timer == 0,
            "confirm hands the guest the Ryu slot and the open window");
      Check(akuma.hidden == 0, "the commit stays the guest's");
      uint32_t gate = session_gate;
      WindowTick(akuma, mode, kClassicStyle, gate);
      Check(akuma.hidden == 1, "the guest's window call commits Akuma in every mode");
      Check(gate == session_gate, "the session word is put back");
      Check(akuma.akuma_table == 0, "the fight is Classic Akuma, not Remix Akuma");
      Check(akuma.character == 0 && akuma.old_version == 0 && akuma.colour == 10 && akuma.window == 3,
            "the state equals the original secret's");
      Check(akuma.pad == 0x10, "no other code sees the commit's buttons");
      Check(!ClassicAkumaCommitDue(true, akuma.live, akuma.window, akuma.hidden), "the commit runs once");
    }
  }

  Fighter refused;
  refused.window = kAkumaWindowOpen;
  refused.pad = kAkumaCommitButtons;
  NativeCommit(refused, kOnlineSelectMode, kClassicStyle, 0);
  Check(refused.hidden == 0, "negative control: online Classic refuses the commit while the session word is clear");

  Fighter dead;
  dead.live = kAkumaSelect;
  dead.character = kAkumaSelect;
  dead.window = kAkumaWindowOpen;
  dead.pad = kAkumaCommitButtons;
  NativeCommit(dead, kArcadeGameMode, kClassicStyle, 0);
  Check(dead.hidden == 0 && dead.window == kAkumaWindowOpen,
        "negative control: on id 32 the guest commit refuses even the full secret, so the cell alone stays dead");

  Fighter kick;
  kick.live = kAkumaSelect;
  kick.pad = 0x100;
  ConfirmCell(kick, kClassicStyle, true);
  uint32_t gate = 0;
  WindowTick(kick, kTrainingGameMode, kClassicStyle, gate);
  Check(kick.colour == 11, "a kick confirm takes the guest's alternate Akuma colour");

  Fighter secret;
  secret.window = kAkumaWindowOpen;
  secret.pad = 0x10;
  WindowTick(secret, kArcadeGameMode, kClassicStyle, gate);
  Check(secret.hidden == 0 && secret.timer == 1, "the original window is not committed for the player");
  secret.pad = kAkumaCommitButtons;
  WindowTick(secret, kArcadeGameMode, kClassicStyle, gate);
  Check(secret.hidden == 1 && secret.akuma_table == 0, "the original three punches still commit Classic Akuma");

  Fighter unavailable;
  unavailable.live = kAkumaSelect;
  unavailable.character = kAkumaSelect;
  ConfirmCell(unavailable, kClassicStyle, false);
  Check(unavailable.character == kAkumaSelect && unavailable.window == 1, "an unavailable Akuma opens no window");
  Check(!ClassicAkumaCommitDue(ClassicStyle(kRemixStyle), kAkumaSelect, kAkumaWindowOpen, 0), "Remix keeps its own commit");
}

void TestNormalFightersAreUntouched() {
  for (const uint8_t mode : {kArcadeGameMode, kTrainingGameMode, kLocalVersusGameMode}) {
    for (const uint8_t carrier : {uint8_t(kRyuGuestCharacter), uint8_t(kKenGuestCharacter)}) {
      Fighter normal;
      normal.live = carrier;
      normal.character = carrier;
      normal.old_version = 1;
      normal.pad = 0x10;
      ConfirmCell(normal, kClassicStyle, true);
      uint32_t gate = 0;
      WindowTick(normal, mode, kClassicStyle, gate);
      Check(normal.hidden == 0 && normal.window == 1 && normal.character == carrier,
            "normal Classic Ryu and Ken keep their guest id and window");
      Check(!UltraDropsOldVersion(false, normal.old_version) && normal.old_version == 1,
            "normal Classic Ryu and Ken keep their old-version flag");
    }
    ResetIdentities();
    UltraNavigation nav;
    const UltraMove move = nav.Resolve(true, true, 0, 2, PadDirection::kRight);
    Check(move.action == UltraMoveAction::kNone && !nav.OnUltraCell(0), "an ordinary move stays the guest's");
    Check(ResolveFighterIdentity(kRyuGuestCharacter, false, true, RequestedIdentity(0)).identity == FighterIdentity::kRyu,
          "normal Ryu stays Ryu");
    Check(ResolveFighterIdentity(kKenGuestCharacter, false, true, RequestedIdentity(0)).identity == FighterIdentity::kKen,
          "normal Ken stays Ken");
  }
}

// Local Versus and Training: each side picks alone, and neither pick disturbs the other.
void TestTwoSidesAreIndependent() {
  enum class Pick { kAkuma, kEvilRyu, kViolentKen };
  for (const uint8_t style : {kClassicStyle, kRemixStyle}) {
    for (const auto [p1, p2] :
         {std::tuple{Pick::kEvilRyu, Pick::kViolentKen}, std::tuple{Pick::kAkuma, Pick::kEvilRyu},
          std::tuple{Pick::kAkuma, Pick::kViolentKen}, std::tuple{Pick::kEvilRyu, Pick::kEvilRyu},
          std::tuple{Pick::kViolentKen, Pick::kViolentKen}, std::tuple{Pick::kAkuma, Pick::kAkuma}}) {
      ResetIdentities();
      UltraNavigation nav;
      std::array<Fighter, 2> fighters{};
      const std::array<Pick, 2> picks = {p1, p2};
      for (int side = 0; side < 2; ++side) {
        Fighter& fighter = fighters[size_t(side)];
        if (picks[size_t(side)] == Pick::kAkuma) {
          fighter.live = kAkumaSelect;
          if (ClassicStyle(style)) {
            ConfirmCell(fighter, style, true);
            uint32_t gate = 0;
            WindowTick(fighter, kLocalVersusGameMode, style, gate);
          } else {
            fighter.hidden = 1;
          }
          continue;
        }
        const bool evil = picks[size_t(side)] == Pick::kEvilRyu;
        const UltraMove move = nav.Resolve(true, true, side, evil ? kEvilRyuNeighbourLive : kViolentKenNeighbourLive,
                                           PadDirection::kRight);
        fighter.live = move.live;
        fighter.character = move.live;
      }
      for (int side = 0; side < 2; ++side) {
        const Fighter& fighter = fighters[size_t(side)];
        const FighterIdentity identity =
            ResolveFighterIdentity(fighter.character, fighter.hidden != 0, true, RequestedIdentity(side)).identity;
        switch (picks[size_t(side)]) {
          case Pick::kAkuma:
            Check(fighter.hidden == 1 && !nav.OnUltraCell(side), "Akuma beside any secret fighter");
            break;
          case Pick::kEvilRyu:
            Check(identity == FighterIdentity::kEvilRyu && fighter.hidden == 0, "Evil Ryu beside any secret fighter");
            break;
          case Pick::kViolentKen:
            Check(identity == FighterIdentity::kViolentKen && fighter.hidden == 0, "Violent Ken beside any secret fighter");
            break;
        }
      }
    }
  }
  ResetIdentities();
}

void TestTrainingKeepsTheChoice() {
  ResetIdentities();
  UltraNavigation nav;
  SelectSession session;
  Check(session.Touch(1000), "Training's Character Select starts a session");
  nav.Resolve(true, true, 0, kEvilRyuNeighbourLive, PadDirection::kRight);
  Check(!session.Touch(2000), "the dummy's turn follows inside the same session, so player 1 keeps the cell");
  nav.Resolve(true, true, 1, kViolentKenNeighbourLive, PadDirection::kRight);
  Check(nav.OnEvilRyu(0) && nav.OnViolentKen(1), "both Training sides hold their cells");
  for (int reset = 0; reset < 3; ++reset) {
    ResetFightIdentities();
    SetIdentityForSide(0, ResolveFighterIdentity(kRyuGuestCharacter, false, true, RequestedIdentity(0)));
    SetIdentityForSide(1, ResolveFighterIdentity(kKenGuestCharacter, false, true, RequestedIdentity(1)));
    Check(IdentityForSide(0).identity == FighterIdentity::kEvilRyu, "a Training reset keeps Evil Ryu");
    Check(IdentityForSide(1).identity == FighterIdentity::kViolentKen, "a Training reset keeps Violent Ken");
  }
  SelectEntry entry;
  entry.evil_ryu_available = entry.violent_ken_available = true;
  entry.sides[0] = {false, true, kRyuGuestCharacter, false};
  entry.sides[1] = {true, true, kKenGuestCharacter, false};
  Check(session.Touch(90000), "returning to Character Select after a fight is a new session");
  Check(UltraMaskKeptByDefender(nav.Mask(), entry) == 0, "Training keeps no stale cell into the next Character Select");
  nav.Reset();
  ResetIdentities();
}

}  // namespace

int main() {

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
    Check(!UltraDropsOldVersion(true, 0), "an Ultra cell with no old-version flag is left alone");
    Check(UltraDropsOldVersion(true, 1), "an Ultra cell cannot go old-version");
    Check(!UltraDropsOldVersion(false, 1), "plain Ken and Ryu keep their old version");
    Check(!UltraDropsOldVersion(false, 0), "normal fighters are never touched");
    Check(HdStyleName(0) == std::string_view("classic") && HdStyleName(1) == std::string_view("remix"),
          "the style byte names the diagnostic mode");
    Check(GameModeName(kArcadeGameMode) == std::string_view("arcade") &&
              GameModeName(kLocalVersusGameMode) == std::string_view("versus") &&
              GameModeName(kTrainingGameMode) == std::string_view("training") &&
              GameModeName(kOnlineSelectMode) == std::string_view("online") &&
              GameModeName(4) == std::string_view("unknown"),
          "the mode byte names the diagnostic mode");
    Check(SelectLayoutForMode(kTrainingGameMode) == SelectLayout::kVersus, "Training selects on the 3x6 grid");
    Check(SelectLayoutForMode(kLocalVersusGameMode) == SelectLayout::kVersus, "Local Versus selects on the 3x6 grid");
    Check(SelectLayoutForMode(kOnlineSelectMode) == SelectLayout::kVersus, "Online selects on the 3x6 grid");
    Check(SelectLayoutForMode(kArcadeGameMode) == SelectLayout::kArcade, "Arcade selects on the 2x8 grid");

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
    ResetIdentities();
    UltraNavigation nav;
    Check(nav.Resolve(false, false, 0, kViolentKenNeighbourLive, PadDirection::kRight).action == UltraMoveAction::kNone,
          "a disabled Violent Ken cell cannot be entered");
    Check(nav.Resolve(true, false, 0, kViolentKenNeighbourLive, PadDirection::kRight).action == UltraMoveAction::kNone,
          "an unavailable Violent Ken cell cannot be entered");
    Check(RequestedIdentity(0) == FighterIdentity::kGuest, "no Violent Ken is requested while unavailable");

    const UltraMove enter = nav.Resolve(false, true, 0, kViolentKenNeighbourLive, PadDirection::kRight);
    Check(enter.action == UltraMoveAction::kEnter && enter.live == kViolentKenCarrierLive,
          "Right from id 8 enters Violent Ken with the Ken carrier");
    Check(nav.OnViolentKen(0) && !nav.OnEvilRyu(0), "Violent Ken is selected for player 1");
    Check(RequestedIdentity(0) == FighterIdentity::kViolentKen, "Player 1 requests Violent Ken");
    const FighterIdentityInfo fight = ResolveFighterIdentity(kKenGuestCharacter, false, true, RequestedIdentity(0));
    Check(fight.identity == FighterIdentity::kViolentKen && fight.guest_character == kKenGuestCharacter,
          "Confirming Violent Ken yields the Violent Ken fight identity on Ken");
    Check(nav.Resolve(false, true, 0, kViolentKenCarrierLive, PadDirection::kDown).action == UltraMoveAction::kHold,
          "Down on Violent Ken holds");

    const UltraMove leave = nav.Resolve(false, true, 0, kViolentKenCarrierLive, PadDirection::kLeft);
    Check(leave.action == UltraMoveAction::kLeave && leave.live == kViolentKenNeighbourLive,
          "Left from Violent Ken returns to id 8");
    Check(!nav.OnViolentKen(0) && RequestedIdentity(0) == FighterIdentity::kGuest, "Leaving clears the Violent Ken request");
  }

  TestSecretCellMatrix();
  TestPreviousGateNegativeControls();
  TestClassicAkumaEveryMode();
  TestNormalFightersAreUntouched();
  TestTwoSidesAreIndependent();
  TestTrainingKeepsTheChoice();

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
