// A challenger joining an Arcade fight must not cost the defender an Ultra fighter: the guest keeps the carrier, the
// host cell mask keeps the identity.

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "character/akuma_navigation.h"
#include "character/fighter_identity.h"
#include "character/ultra_navigation.h"
#include "character/ultra_select_presentation.h"

#ifndef SSF2T_SOURCE_DIR
#error SSF2T_SOURCE_DIR must name the repository root
#endif

namespace {

using namespace ssf2t::character;
namespace fs = std::filesystem;

int failures = 0;

void Check(bool condition, const char* what) {
  if (condition) return;
  std::printf("FAIL %s\n", what);
  ++failures;
}

constexpr int kHonda = 2;
constexpr int kP1 = 0;
constexpr int kP2 = 1;

// The Character Select the guest rebuilds after `joiner` pressed Start during the fight.
SelectEntry Join(int joiner, int defender_character) {
  SelectEntry entry;
  entry.arcade = true;
  entry.evil_ryu_available = true;
  entry.violent_ken_available = true;
  entry.sides[size_t(joiner)] = {true, true, joiner == kP1 ? kRyuGuestCharacter : kKenGuestCharacter, false};
  entry.sides[size_t(1 - joiner)] = {false, true, defender_character, false};
  return entry;
}

void Enter(UltraNavigation& nav, const SelectEntry& entry) { nav.Restore(UltraMaskKeptByDefender(nav.Mask(), entry)); }

void Pick(UltraNavigation& nav, int side, FighterIdentity identity) {
  if (identity == FighterIdentity::kEvilRyu) nav.Resolve(true, true, side, kEvilRyuNeighbourLive, PadDirection::kRight);
  if (identity == FighterIdentity::kViolentKen) nav.Resolve(true, true, side, kViolentKenNeighbourLive, PadDirection::kRight);
}

SelectPresentation Present(const UltraNavigation& nav, uint32_t hover_p1, uint32_t hover_p2) {
  return ResolveSelectPresentation({RequestedIdentity(kP1), RequestedIdentity(kP2)},
                                   {nav.OnUltraCell(kP1), nav.OnUltraCell(kP2)}, {hover_p1, hover_p2});
}

FighterIdentity FightIdentity(int side, int guest_character) {
  return ResolveFighterIdentity(guest_character, false, true, RequestedIdentity(side)).identity;
}

void TestNegativeControl() {
  ResetIdentities();
  UltraNavigation nav;
  Pick(nav, kP1, FighterIdentity::kViolentKen);
  Check(FightIdentity(kP1, kKenGuestCharacter) == FighterIdentity::kViolentKen, "control: the fight is Violent Ken");
  nav.Reset();
  Check(RequestedIdentity(kP1) == FighterIdentity::kGuest && !nav.OnUltraCell(kP1),
        "control: an unconditional reset on re-entry is the reported identity loss");
  Check(FightIdentity(kP1, kKenGuestCharacter) == FighterIdentity::kKen, "control: the carrier survives as plain Ken");

  ResetIdentities();
  UltraNavigation evil;
  Pick(evil, kP1, FighterIdentity::kEvilRyu);
  evil.Reset();
  Check(FightIdentity(kP1, kRyuGuestCharacter) == FighterIdentity::kRyu, "control: the carrier survives as plain Ryu");
}

void TestViolentKenSurvivesTheJoin() {
  ResetIdentities();
  UltraNavigation nav;
  Pick(nav, kP1, FighterIdentity::kViolentKen);
  Enter(nav, Join(kP2, kKenGuestCharacter));
  Check(nav.OnViolentKen(kP1) && RequestedIdentity(kP1) == FighterIdentity::kViolentKen, "Violent Ken stays selected");
  const uint32_t ken = uint32_t(kKenGuestCharacter);
  const SelectPresentation shown = Present(nav, ken, ken);
  Check(shown.violent[kP1], "large and mini portrait and name follow Violent Ken");
  Check(!shown.violent[kP2], "the joiner's default Ken stays plain Ken");
  Check(ViolentPortraitId(shown, {ken, ken}) == kViolentSpareLargeId, "both Kens keep separate large portraits");
  Check(FightIdentity(kP1, kKenGuestCharacter) == FighterIdentity::kViolentKen, "the next fight is Violent Ken");
}

void TestEvilRyuSurvivesTheJoin() {
  ResetIdentities();
  UltraNavigation nav;
  Pick(nav, kP1, FighterIdentity::kEvilRyu);
  Enter(nav, Join(kP2, kRyuGuestCharacter));
  Check(nav.OnEvilRyu(kP1) && RequestedIdentity(kP1) == FighterIdentity::kEvilRyu, "Evil Ryu stays selected");
  Check(Present(nav, uint32_t(kRyuGuestCharacter), uint32_t(kKenGuestCharacter)).evil[kP1],
        "large and mini portrait and name follow Evil Ryu");
  Check(FightIdentity(kP1, kRyuGuestCharacter) == FighterIdentity::kEvilRyu, "the next fight is Evil Ryu");
}

void TestOrdinaryFightersAreUntouched() {
  for (const int character : {kRyuGuestCharacter, kKenGuestCharacter, kHonda, kAkumaGuestCharacter}) {
    ResetIdentities();
    UltraNavigation nav;
    Enter(nav, Join(kP2, character));
    Check(nav.Mask() == 0, "an ordinary defender gains no Ultra cell");
    Check(RequestedIdentity(kP1) == FighterIdentity::kGuest, "an ordinary defender gains no Ultra identity");
  }
  Check(FightIdentity(kP1, kRyuGuestCharacter) == FighterIdentity::kRyu, "plain Ryu stays Ryu");
  Check(FightIdentity(kP1, kKenGuestCharacter) == FighterIdentity::kKen, "plain Ken stays Ken");
}

void TestSidesAreIndependent() {
  ResetIdentities();
  UltraNavigation nav;
  Pick(nav, kP2, FighterIdentity::kEvilRyu);
  Enter(nav, Join(kP1, kRyuGuestCharacter));
  Check(nav.OnEvilRyu(kP2), "a defending player 2 keeps Evil Ryu");
  Check(!nav.OnUltraCell(kP1), "the joining player 1 starts plain");

  ResetIdentities();
  UltraNavigation both;
  Pick(both, kP1, FighterIdentity::kViolentKen);
  Pick(both, kP2, FighterIdentity::kEvilRyu);
  Enter(both, Join(kP2, kKenGuestCharacter));
  Check(both.OnViolentKen(kP1), "the defender keeps Violent Ken");
  Check(!both.OnUltraCell(kP2) && RequestedIdentity(kP2) == FighterIdentity::kGuest,
        "a side that joins again loses its old Ultra cell");
}

void TestNavigationAfterRestore() {
  ResetIdentities();
  UltraNavigation nav;
  Pick(nav, kP1, FighterIdentity::kViolentKen);
  Enter(nav, Join(kP2, kKenGuestCharacter));
  Check(nav.Resolve(true, true, kP1, kViolentKenCarrierLive, PadDirection::kRight).action == UltraMoveAction::kHold,
        "the restored cell holds Right");
  const UltraMove up = nav.Resolve(true, true, kP1, kViolentKenCarrierLive, PadDirection::kUp);
  Check(up.action == UltraMoveAction::kSwitch && up.live == kEvilRyuCarrierLive && nav.OnEvilRyu(kP1),
        "Up reaches the Evil Ryu cell");
  nav.Resolve(true, true, kP1, kEvilRyuCarrierLive, PadDirection::kDown);
  const UltraMove left = nav.Resolve(true, true, kP1, kViolentKenCarrierLive, PadDirection::kLeft);
  Check(left.action == UltraMoveAction::kLeave && left.live == kViolentKenNeighbourLive, "Left leaves to the neighbour");
  Check(RequestedIdentity(kP1) == FighterIdentity::kGuest, "leaving replaces the restored selection");
  Check(FightIdentity(kP1, kKenGuestCharacter) == FighterIdentity::kKen, "a new selection takes over");
  Check(nav.Resolve(true, true, kP1, left.live, PadDirection::kRight).action == UltraMoveAction::kEnter,
        "moving back re-enters the cell");
  Check(nav.OnViolentKen(kP1), "the cell is selected again");
  Check(!nav.OnUltraCell(kP2), "the other side never moved");
}

void TestNothingIsForced() {
  UltraNavigation nav;
  SelectEntry entry = Join(kP2, kRyuGuestCharacter);
  ResetIdentities();
  Pick(nav, kP1, FighterIdentity::kEvilRyu);
  entry.evil_ryu_available = false;
  Check(UltraMaskKeptByDefender(nav.Mask(), entry) == 0, "disabled Evil Ryu is not restored");

  ResetIdentities();
  nav.Reset();
  Pick(nav, kP1, FighterIdentity::kViolentKen);
  entry = Join(kP2, kKenGuestCharacter);
  entry.violent_ken_available = false;
  Check(UltraMaskKeptByDefender(nav.Mask(), entry) == 0, "Violent Ken with no gameplay package is not restored");

  entry = Join(kP2, kKenGuestCharacter);
  entry.arcade = false;
  Check(UltraMaskKeptByDefender(nav.Mask(), entry) == 0, "no other mode keeps a cell, online included");

  entry = Join(kP2, kHonda);
  Check(UltraMaskKeptByDefender(nav.Mask(), entry) == 0, "a defender off the carrier keeps nothing");

  entry = Join(kP2, kKenGuestCharacter);
  entry.sides[kP1].hidden = true;
  Check(UltraMaskKeptByDefender(nav.Mask(), entry) == 0, "a hidden carrier keeps nothing");

  entry = Join(kP2, kKenGuestCharacter);
  entry.sides[kP1].played = false;
  Check(UltraMaskKeptByDefender(nav.Mask(), entry) == 0, "the CPU side keeps nothing");

  entry = Join(kP2, kKenGuestCharacter);
  entry.sides[kP2].joined = false;
  Check(UltraMaskKeptByDefender(nav.Mask(), entry) == 0, "a fresh Character Select is not polluted");

  entry = Join(kP2, kKenGuestCharacter);
  entry.sides[kP1].joined = true;
  Check(UltraMaskKeptByDefender(nav.Mask(), entry) == 0, "two new sides leave no defender");

  Check(UltraMaskKeptByDefender(nav.Mask(), SelectEntry{}) == 0, "an unreadable entry keeps nothing");
}

std::string Source(const char* relative) {
  std::ifstream in(fs::path(SSF2T_SOURCE_DIR) / relative, std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

void TestHooksAreWired() {
  const std::string confirm = Source("src/character/ultra_confirm_hook.cpp");
  Check(Has(confirm, "UltraMaskKeptByDefender(GlobalUltraNavigation().Mask(), entry)"), "session start uses the policy");
  Check(!Has(confirm, "GlobalUltraNavigation().Reset()"), "session start no longer resets unconditionally");
  Check(Has(confirm, "if (UltraFightersEnabled() && !online) TouchInteractiveSelect<G>(base);"),
        "the online select never reaches the policy");
  const std::string activity = Source("src/character/select_activity.cpp");
  Check(Has(activity, "EnterSelect<ssf2t::netplay::Base013Netplay, ssf2t::character::Base013Select>"), "Base013 hook");
  Check(Has(activity, "EnterSelect<ssf2t::netplay::TitleUpdate023Netplay, ssf2t::character::TitleUpdate023Select>"),
        "TU023 hook");
  const std::string cursor = Source("src/character/ultra_cursor_hook.cpp");
  Check(Has(cursor, "GlobalUltraNavigation().OnUltraCell(side)") && Has(cursor, "kViolentKenCursorLive"),
        "the cursor follows the restored cell");
}

constexpr uint32_t kImageBase = 0x82000000;
constexpr uint32_t kOpLbz = 34;
constexpr uint32_t kOpStb = 38;
constexpr uint32_t kSelectStates = 0x83A2D530;
constexpr uint32_t kFighter1 = 0x700;
constexpr uint32_t kFighter2 = 0xB00;

struct GuestFacts {
  const char* file;
  uint32_t enter_select;
  uint32_t finish_select;
  uint32_t join_a;
  uint32_t join_b;
};

constexpr GuestFacts kBase013{"default-0.0.1.3.image.bin", 0x8223DF78, 0x82242588, 0x82259760, 0x8225A840};
constexpr GuestFacts kTitleUpdate023{"default.image.bin", 0x8223E0A8, 0x822425F0, 0x82259858, 0x8225A938};

uint32_t Word(const std::vector<char>& image, uint32_t address) {
  const size_t at = address - kImageBase;
  if (at + 4 > image.size()) return 0;
  const auto byte = [&](size_t index) { return uint32_t(uint8_t(image[at + index])); };
  return byte(0) << 24 | byte(1) << 16 | byte(2) << 8 | byte(3);
}

bool HasAccess(const std::vector<char>& image, uint32_t function, uint32_t words, uint32_t op, uint32_t offset) {
  for (uint32_t index = 0; index < words; ++index) {
    const uint32_t word = Word(image, function + index * 4);
    if (word >> 26 == op && (word & 0xFFFF) == offset) return true;
  }
  return false;
}

void CheckImage(const GuestFacts& facts) {
  const fs::path path = fs::path(SSF2T_SOURCE_DIR) / "game" / "analysis" / facts.file;
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::printf("skipped guest proof: %s is not present\n", facts.file);
    return;
  }
  const std::vector<char> image{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  Check(Word(image, kSelectStates + 4) == facts.enter_select, "the entry state follows the select init");
  Check(Word(image, kSelectStates + 0x38) == facts.finish_select, "the finish state ends the select");
  for (const uint32_t join : {facts.join_a, facts.join_b}) {
    Check(HasAccess(image, join, 80, kOpStb, 1168), "pressing Start sets the side's join bit");
  }
  Check(HasAccess(image, facts.enter_select, 60, kOpLbz, 1168), "the entry state branches on the join bits");
  Check(HasAccess(image, facts.enter_select, 380, kOpLbz, 898), "the entry state reads the defender's human flag");
  Check(HasAccess(image, facts.enter_select, 380, kOpLbz, 942), "the entry state reads the defender's played flag");
  Check(HasAccess(image, facts.enter_select, 660, kOpStb, 1168), "the entry state clears the join bits");
  Check(HasAccess(image, facts.finish_select, 150, kOpStb, kFighter1 + 942), "player 1's played flag is kept");
  Check(HasAccess(image, facts.finish_select, 150, kOpStb, kFighter2 + 942), "player 2's played flag is kept");
  Check(HasAccess(image, facts.finish_select, 150, kOpStb, kFighter1 + 898), "player 1's human flag ends with the select");
  Check(HasAccess(image, facts.finish_select, 150, kOpStb, kFighter2 + 898), "player 2's human flag ends with the select");
}

}  // namespace

int main() {
  TestNegativeControl();
  TestViolentKenSurvivesTheJoin();
  TestEvilRyuSurvivesTheJoin();
  TestOrdinaryFightersAreUntouched();
  TestSidesAreIndependent();
  TestNavigationAfterRestore();
  TestNothingIsForced();
  TestHooksAreWired();
  CheckImage(kBase013);
  CheckImage(kTitleUpdate023);
  if (failures == 0) std::printf("local join select tests passed\n");
  return failures == 0 ? 0 : 1;
}
