// A CPU opponent in a Local Match: the guest's own Arcade AI on one side of a game mode 1 match, chosen explicitly in the
// lobby. Everything that needs the real game reads the images and the archive; running it is a manual test.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "character/cpu_marker.h"
#include "character/versus_cpu.h"
#include "updater/third_party/miniz/miniz.h"

#ifndef SSF2T_SOURCE_DIR
#error SSF2T_SOURCE_DIR must name the repository root
#endif

namespace {

using namespace ssf2t::character;
namespace fs = std::filesystem;

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

fs::path Root() { return fs::path(SSF2T_SOURCE_DIR); }

std::string ReadText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const std::string& needle) { return text.find(needle) != std::string::npos; }

size_t Count(const std::string& text, const std::string& needle) {
  size_t count = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size())) ++count;
  return count;
}

constexpr int32_t kPad = 0;
constexpr int32_t kOtherPad = 1;
constexpr uint8_t kArcade = 0;
constexpr uint8_t kVersus = 1;
constexpr uint8_t kTraining = 2;
constexpr uint8_t kOnline = 3;

constexpr size_t kCpuNameBytesForTest = 0x20;

void TestLocalMatchIsHumanByDefault() {
  const VersusCpuOpponent opponent;
  CHECK(opponent.level() == 0);
  for (const uint8_t mode : {kArcade, kVersus, kTraining, kOnline}) CHECK(opponent.SideIn(mode) == -1);
}

void TestNoSecondControllerIsNotACpu() {
  VersusCpuOpponent opponent;
  opponent.FollowSeats(EmptyLobbySide(kPad, kEmptyLobbySlot));
  CHECK(opponent.SideIn(kVersus) == -1);
  CHECK(opponent.level() == 0);
}

void TestPlayerOneVersusCpu() {
  VersusCpuOpponent opponent;
  opponent.Cycle(EmptyLobbySide(kPad, kEmptyLobbySlot));
  CHECK(opponent.level() == 1);
  CHECK(opponent.SideIn(kVersus) == 1);
}

void TestCpuVersusPlayerTwo() {
  VersusCpuOpponent opponent;
  opponent.Cycle(EmptyLobbySide(kEmptyLobbySlot, kPad));
  CHECK(opponent.SideIn(kVersus) == 0);
}

void TestTheOptionChangesOnlyTheEmptySide() {
  VersusCpuOpponent opponent;
  opponent.Cycle(EmptyLobbySide(kPad, kEmptyLobbySlot));
  const int cpu = opponent.SideIn(kVersus);
  CHECK(cpu == 1);
  CHECK(cpu != 0);
}

void TestTwoPlayersLeaveNoCpuSide() {
  CHECK(EmptyLobbySide(kPad, kOtherPad) == -1);
  CHECK(EmptyLobbySide(kEmptyLobbySlot, kEmptyLobbySlot) == -1);
  VersusCpuOpponent opponent;
  opponent.Cycle(EmptyLobbySide(kPad, kOtherPad));
  CHECK(opponent.level() == 0);
  CHECK(opponent.SideIn(kVersus) == -1);
}

void TestDifficultyCycleWrapsAndNeverRemovesTheCpu() {
  VersusCpuOpponent opponent;
  for (int level = 1; level <= kCpuDifficultyLevels; ++level) {
    opponent.Cycle(1);
    CHECK(opponent.level() == level);
    CHECK(opponent.SideIn(kVersus) == 1);
  }
  opponent.Cycle(1);
  CHECK(opponent.level() == 1);
  CHECK(opponent.SideIn(kVersus) == 1);
}

void TestSwapMovesTheCpu() {
  VersusCpuOpponent opponent;
  opponent.Cycle(1);
  opponent.FollowSeats(EmptyLobbySide(kEmptyLobbySlot, kPad));
  CHECK(opponent.SideIn(kVersus) == 0);
  CHECK(opponent.level() == 1);
}

void TestLeavingTheLobbyDropsTheCpu() {
  VersusCpuOpponent opponent;
  opponent.Cycle(1);
  opponent.FollowSeats(EmptyLobbySide(kEmptyLobbySlot, kEmptyLobbySlot));
  CHECK(opponent.level() == 0);
  opponent.Cycle(1);
  opponent.Reset();
  CHECK(opponent.SideIn(kVersus) == -1);
}

void TestOwnershipStaysInLocalMatch() {
  VersusCpuOpponent opponent;
  opponent.Cycle(1);
  CHECK(opponent.SideIn(kArcade) == -1);
  CHECK(opponent.SideIn(kTraining) == -1);
  CHECK(opponent.SideIn(kOnline) == -1);
  CHECK(opponent.SideIn(kVersus) == 1);
}

void TestDifficultyReachesTheRealRange() {
  CHECK(CpuGuestDifficulty(1) == 0);
  CHECK(CpuGuestDifficulty(kCpuDifficultyLevels) == 7);
  for (int level = 1; level <= kCpuDifficultyLevels; ++level) {
    CHECK(CpuLadderStep(level) < kCpuLadderSteps);
    if (level > 1) CHECK(CpuLadderStep(level) > CpuLadderStep(level - 1));
  }
  CHECK(CpuLadderStep(kCpuDifficultyLevels) == kCpuLadderSteps - 1);
}

void TestCycleCommandIsOutsideTheMenuRange() {
  const std::string script = std::string(kLobbyCycleCpu);
  CHECK(Has(script, "SetGameDifficulty(" + std::to_string(kCycleCpuOpponentArgument) + ")"));
  CHECK(Has(script, "IsNewlyPressed(BUTTON3, 0)") && !Has(script, "IsNewlyPressedPF"));
  CHECK(kCycleCpuOpponentArgument > 3);
  CHECK(kRemoveCpuOpponentArgument == 9 && Has(script, "SetGameDifficulty(9);"));
  CHECK(kCpuActiveQuerySeat == 8 && Has(script, "IsSlotOccupied(8)"));
  CHECK(Has(std::string(kLobbyCpuFooter), "IsSlotOccupied(8)"));
}

void TestCpuCursorFollowsThePlayerOnlyAfterTheirPick() {
  const SelectPads player{0x0010, 0x0000};
  const SelectPads before = CpuSidePads(false, player);
  CHECK(before.current == 0 && before.previous == 0);
  const SelectPads after = CpuSidePads(true, player);
  CHECK(after.current == player.current && after.previous == player.previous);
  CHECK(after.Pressed() == 0x0010);
  CHECK((SelectPads{0x0010, 0x0010}.Pressed() == 0));
}

void TestPickFinished() {
  CHECK(!PickFinished(0, 0, 0));
  CHECK(PickFinished(1, 0, 0));
  CHECK(PickFinished(0, 1, 0) && !PickFinished(0, 1, 1));
  CHECK(PickFinished(0, 2, 1) && !PickFinished(0, 2, 0));
  CHECK(PickFinished(0, 3, 0) && PickFinished(0, 3, 1));
}

constexpr uint16_t kConfirm = 0x0010;
constexpr uint16_t kRight = 0x0008;

enum class Copy { kNone, kBeforeSelect, kAfterSelect, kCurrentOnly, kStaleSource };

// One frame of the guest in the order the images run it: the pad refresh gives every side prev = old cur and cur =
// its controller (none for the CPU seat), then the select update derives each side's press edges from those words and
// runs the side steps. A side locks on a confirm edge and moves on a direction edge.
struct SelectSim {
  SelectPads pad[2];
  uint8_t final_state[2]{};
  uint8_t complete_mask = 0;
  int cursor[2]{0, 4};
  int cpu_moves = 0;
  SelectPads frozen_source;
  bool source_frozen = false;
  bool cpu_has_controller = true;

  bool Complete() const { return complete_mask == 3; }

  void Step(int side) {
    const uint16_t pressed = pad[side].Pressed();
    if ((pressed & kRight) != 0) {
      ++cursor[side];
      if (side == 1) ++cpu_moves;
    }
    if ((pressed & kConfirm) != 0 && ((complete_mask >> side) & 1) == 0) {
      complete_mask |= uint8_t(1 << side);
      final_state[side] = 1;
    }
  }

  void Share(int cpu, int player) {
    const bool shared = PickFinished(final_state[player], complete_mask, player);
    SelectPads source = pad[player];
    if (source_frozen) source = frozen_source;
    pad[cpu] = CpuSidePads(shared, source);
  }

  void Frame(int cpu_side, Copy copy, uint16_t controller) {
    const int cpu = cpu_side;
    const int player = cpu < 0 ? 0 : 1 - cpu;
    for (int side = 0; side < 2; ++side) {
      pad[side].previous = pad[side].current;
      pad[side].current = side == player ? controller : uint16_t(0);
    }
    if (cpu < 0) {
      Step(0);
      return;
    }
    const bool cpu_finished = PickFinished(final_state[cpu], complete_mask, cpu);
    const bool player_finished = PickFinished(final_state[player], complete_mask, player);
    if (copy == Copy::kBeforeSelect && !cpu_finished) Share(cpu, player);
    if (copy == Copy::kCurrentOnly && !cpu_finished) {
      Share(cpu, player);
      pad[cpu].previous = pad[cpu].current;
    }
    if (copy == Copy::kStaleSource && !cpu_finished) {
      if (player_finished && !source_frozen) {
        source_frozen = true;
        frozen_source = pad[player];
      }
      Share(cpu, player);
    }
    Step(player);
    if (cpu_has_controller) Step(cpu);
    if (copy == Copy::kAfterSelect && !cpu_finished) Share(cpu, player);
  }
};

// A key held for `hold` frames, as a human presses it.
uint16_t Held(int frame, int start, int hold, uint16_t button) {
  return frame >= start && frame < start + hold ? button : uint16_t(0);
}

struct Outcome {
  bool complete;
  int cpu_moves;
  int cursor;
};

// The player locks at frame 20, taps right (held 3 frames) at 60 and 100, then confirms the CPU at 140.
Outcome RunSelect(Copy copy, int cpu_side = 1, bool cpu_controller = true) {
  SelectSim select;
  select.cpu_has_controller = cpu_controller;
  for (int frame = 0; frame < 200 && !select.Complete(); ++frame) {
    const uint16_t controller = uint16_t(Held(frame, 20, 5, kConfirm) | Held(frame, 60, 3, kRight) |
                                         Held(frame, 100, 3, kRight) | Held(frame, 140, 5, kConfirm));
    select.Frame(cpu_side, copy, controller);
  }
  return {select.Complete(), select.cpu_moves, select.cursor[1]};
}

void TestLobbyToSelectLifecycle() {
  VersusCpuOpponent opponent;
  int slots[2] = {kPad, kEmptyLobbySlot};

  opponent.Cycle(EmptyLobbySide(slots[0], slots[1]));
  CHECK(opponent.level() == 1 && opponent.SideIn(kVersus) == 1);
  for (int tick = 0; tick < 300; ++tick) {
    opponent.FollowSeats(EmptyLobbySide(slots[0], slots[1]));
    CHECK(opponent.SideIn(kVersus) == 1);
  }
  const int cpu = opponent.SideIn(kVersus);
  CHECK(cpu == 1);

  const Outcome run = RunSelect(Copy::kBeforeSelect, cpu);
  CHECK(run.complete);
  CHECK(run.cpu_moves == 2);
  CHECK(run.cursor == 6);
  CHECK(opponent.SideIn(kVersus) == 1 && opponent.level() == 1);
}

void TestSharedInputMovesTheCpuCursorOncePerPress() {
  SelectSim select;
  for (int frame = 0; frame < 200; ++frame) {
    const uint16_t controller = uint16_t(Held(frame, 20, 5, kConfirm) | Held(frame, 60, 30, kRight));
    select.Frame(1, Copy::kBeforeSelect, controller);
    if (frame < 25) CHECK(select.cursor[1] == 4);
  }
  CHECK(select.cpu_moves == 1);
}

void TestCpuSidePicksOnlyAfterThePlayer() {
  SelectSim select;
  for (int frame = 0; frame < 19; ++frame) select.Frame(1, Copy::kBeforeSelect, Held(frame, 5, 5, kConfirm));
  CHECK((select.complete_mask & 1) != 0);
  CHECK((select.complete_mask & 2) == 0);
  for (int frame = 0; frame < 40 && !select.Complete(); ++frame) {
    select.Frame(1, Copy::kBeforeSelect, Held(frame, 3, 5, kConfirm));
  }
  CHECK(select.Complete());
}

void TestCpuOnFirstSideUsesTheSecondPlayersPad() {
  const Outcome run = RunSelect(Copy::kBeforeSelect, 0);
  CHECK(run.complete);
}

void TestCopyAfterTheSelectUpdateNeverMovesTheCpu() {
  const Outcome run = RunSelect(Copy::kAfterSelect);
  CHECK(!run.complete);
  CHECK(run.cpu_moves == 0);
}

void TestCopyingOnlyCurrentErasesTheEdge() {
  const Outcome run = RunSelect(Copy::kCurrentOnly);
  CHECK(!run.complete);
  CHECK(run.cpu_moves == 0);
}

void TestStaleSourceAfterTheLockNeverMovesTheCpu() {
  const Outcome run = RunSelect(Copy::kStaleSource);
  CHECK(!run.complete);
  CHECK(run.cpu_moves == 0);
}

void TestAGatedCpuSideNeverMovesEvenWithSharedInput() {
  const Outcome run = RunSelect(Copy::kBeforeSelect, 1, false);
  CHECK(!run.complete);
  CHECK(run.cpu_moves == 0);
}

void TestNoSharingNeverCompletes() {
  const Outcome run = RunSelect(Copy::kNone);
  CHECK(!run.complete);
  CHECK(run.cpu_moves == 0);
}

void TestHumanVersusHumanIsUntouched() {
  VersusCpuOpponent opponent;
  opponent.FollowSeats(EmptyLobbySide(kPad, kOtherPad));
  CHECK(opponent.SideIn(kVersus) == -1);
  CHECK((CpuSidePads(false, SelectPads{kConfirm, 0}).current == 0));
  SelectSim select;
  select.Frame(opponent.SideIn(kVersus), Copy::kBeforeSelect, kConfirm);
  CHECK(select.complete_mask == 1);
  CHECK(select.cpu_moves == 0);
}

void TestLeavingTheLobbyClearsTheCpu() {
  VersusCpuOpponent opponent;
  opponent.Cycle(1);
  opponent.Reset();
  CHECK(opponent.SideIn(kVersus) == -1 && opponent.level() == 0);
}

void TestCpuName() {
  CHECK(CpuOpponentName(4, true, false) == "CPU Difficulty 4");
  CHECK(CpuOpponentName(8, true, true) == "Dificuldade CPU 8");
  CHECK(CpuOpponentName(kCpuDifficultyLevels, true, true).size() < 32);
  CHECK(CpuOpponentName(4, false, false) == "CPU");
  CHECK(CpuOpponentName(4, false, true) == "CPU");
}

void TestCpuNameFollowsTheLobby() {
  VersusCpuOpponent opponent;
  opponent.Reset();
  CHECK(opponent.in_lobby());
  opponent.Cycle(1);
  opponent.LeaveLobby();
  CHECK(!opponent.in_lobby());
  CHECK(opponent.level() == 1);
  opponent.Reset();
  CHECK(opponent.in_lobby() && opponent.level() == 0);
}

constexpr int32_t kNameTableRowBytes = 17;
constexpr int32_t kNameTableBytes = 4 * kNameTableRowBytes;

// Where the native HUD resolver reads a seat's name, relative to the guest's name table.
constexpr int32_t NativeNameOffset(int32_t seat) { return seat * kNameTableRowBytes; }

std::string HudLabel(const VersusCpuOpponent& opponent, int side, const std::string& native) {
  return ReadsAsCpu(side, opponent.SideIn(kVersus)) ? std::string(kCpuLabel) : native;
}

void TestAnEmptySeatFallsOutOfTheNameTable() {
  CHECK(NativeNameOffset(kEmptyLobbySlot) < 0);
  CHECK(NativeNameOffset(0) == 0 && NativeNameOffset(3) + kNameTableRowBytes == kNameTableBytes);
}

void TestHudLabelOnEachCpuSide() {
  for (const int cpu : {0, 1}) {
    VersusCpuOpponent opponent;
    opponent.Reset();
    opponent.Cycle(cpu);
    opponent.LeaveLobby();
    CHECK(HudLabel(opponent, cpu, "saulo1") == "CPU");
    CHECK(HudLabel(opponent, 1 - cpu, "saulo1") == "saulo1");
  }
}

void TestHudLabelIsTheWholeToken() {
  const std::string label = kCpuLabel;
  CHECK(label == "CPU" && label.size() == 3);
  for (const char c : label) CHECK(c >= 'A' && c <= 'Z');
  CHECK(label.size() + 1 <= kCpuNameBytesForTest);
}

void TestHudLabelNeverShowsTheDifficulty() {
  VersusCpuOpponent opponent;
  opponent.Reset();
  opponent.Cycle(1);
  opponent.Cycle(1);
  CHECK(opponent.in_lobby());
  CHECK(HudLabel(opponent, 1, "teste2") == "CPU");
}

void TestHudLabelLeavesHumansAlone() {
  VersusCpuOpponent opponent;
  opponent.Reset();
  CHECK(HudLabel(opponent, 0, "saulo1") == "saulo1");
  CHECK(HudLabel(opponent, 1, "teste2") == "teste2");
  opponent.Cycle(1);
  opponent.Reset();
  CHECK(HudLabel(opponent, 1, "teste2") == "teste2");
  CHECK(!ReadsAsCpu(-1, -1));
}

void TestHudVariablesOfOtherSeatsAreNotTheCpu() {
  VersusCpuOpponent opponent;
  opponent.Reset();
  opponent.Cycle(1);
  for (const int seat : {3, 5}) CHECK(HudLabel(opponent, seat, "") == "");
}

enum class Prompt { kNone, kNormalSuper };

// The guest after a confirm: a window that commits a secret fighter on its first frame, the lock, then the side states
// with an eight frame delay between them, stopping on NORMAL / SUPER until it is answered.
struct ConfirmSim {
  uint8_t window = 0;
  uint8_t commit = 0;
  uint8_t locked = 0;
  uint8_t side_state = 0;
  int delay = 0;
  bool answered = false;
  Prompt prompt = Prompt::kNone;
  uint8_t colour_prompt = 0;
  uint8_t mask = 0;

  void Confirm(uint8_t commit_state) {
    window = kConfirmWindowFrames;
    commit = commit_state;
  }

  void Tick() {
    if (locked == 0) {
      if (window == 0) return;
      if (commit == kAkumaCommitPending) commit = 3;
      if (--window == 0) locked = 1;
      return;
    }
    if (delay > 0) {
      --delay;
      return;
    }
    if (side_state == 2 && prompt == Prompt::kNormalSuper && !answered) return;
    if (side_state == 6) {
      mask = 1;
      return;
    }
    ++side_state;
    if (side_state % 2 == 1) delay = 8;
  }
};

int FramesToHandover(ConfirmSim sim, bool shorten, int limit = 400) {
  for (int frame = 0; frame < limit; ++frame) {
    if (PlayerHandedOver(sim.locked, sim.side_state, sim.colour_prompt, sim.mask != 0)) return frame;
    if (shorten && ShortensConfirmWindow(sim.window, sim.commit)) sim.window = 1;
    sim.Tick();
  }
  return -1;
}

void TestHandoverIsAlmostImmediate() {
  ConfirmSim sim;
  sim.Confirm(0);
  const int frames = FramesToHandover(sim, true);
  CHECK(frames > 0 && frames <= 15);
}

void TestHandoverDoesNotWaitForTheLateMask() {
  ConfirmSim sim;
  sim.Confirm(0);
  CHECK(FramesToHandover(sim, false) > kConfirmWindowFrames);
  CHECK(FramesToHandover(sim, true) * 4 < FramesToHandover(sim, false));
}

void TestConfirmWindowIsKeptForItsFirstFrame() {
  CHECK(!ShortensConfirmWindow(kConfirmWindowFrames, 0));
  CHECK(ShortensConfirmWindow(kConfirmWindowFrames - 1, 0));
  CHECK(!ShortensConfirmWindow(1, 0));
  CHECK(!ShortensConfirmWindow(0, 0));
}

void TestPendingAkumaCommitHoldsTheWindow() {
  CHECK(!ShortensConfirmWindow(80, kAkumaCommitPending));
  CHECK(ShortensConfirmWindow(80, 3));
  ConfirmSim sim;
  sim.Confirm(kAkumaCommitPending);
  sim.Tick();
  CHECK(sim.commit == 3);
  CHECK(FramesToHandover(sim, true) <= 15);
}

void TestNormalSuperBlocksTheHandover() {
  ConfirmSim sim;
  sim.prompt = Prompt::kNormalSuper;
  sim.Confirm(0);
  CHECK(FramesToHandover(sim, true) == -1);
  sim.answered = true;
  const int frames = FramesToHandover(sim, true);
  CHECK(frames > 0 && frames <= 15);
}

void TestAColourPromptWaitsForTheMask() {
  CHECK(!PlayerHandedOver(1, 3, 1, false));
  CHECK(PlayerHandedOver(1, 3, 1, true));
  CHECK(PlayerHandedOver(1, 3, 0, false));
  CHECK(!PlayerHandedOver(1, 2, 0, false));
  CHECK(!PlayerHandedOver(0, 3, 0, false));
}

void TestMarkerFollowsTheCpuSide() {
  const int p1_red = CpuMarkerIndex(0x3361, 10);
  const int p1_white = CpuMarkerIndex(0x3361, 26);
  const int p2_yellow = CpuMarkerIndex(0x335A, 10);
  const int p2_gray = CpuMarkerIndex(0x335A, 26);
  CHECK(p1_red >= 0 && p1_white >= 0 && p2_yellow >= 0 && p2_gray >= 0);
  CHECK(CpuMarkerIndex(0x335A, 13) == -1);
  CHECK(CpuMarkerIndex(0x700, 10) == -1);

  CHECK(CpuMarkerOnSide(p2_yellow, 1) && CpuMarkerOnSide(p2_gray, 1));
  CHECK(!CpuMarkerOnSide(p1_red, 1) && !CpuMarkerOnSide(p1_white, 1));
  CHECK(CpuMarkerOnSide(p1_red, 0) && CpuMarkerOnSide(p1_white, 0));
  CHECK(!CpuMarkerOnSide(p2_yellow, 0));

  const VersusCpuOpponent human;
  for (const int index : {p1_red, p1_white, p2_yellow, p2_gray}) {
    CHECK(!CpuMarkerOnSide(index, human.SideIn(kVersus)));
  }
  CHECK(!CpuMarkerOnSide(-1, 1));
}

constexpr std::string_view kScriptShape =
    "<script name=\"LocalLobbyUpdateData\">\n"
    "\t\tUpdateLocalLobby();\n"
    "\t\tif (IsNewlyPressed(LEFT, 0) or IsNewlyPressed(RIGHT, 0))\n"
    "\t\t{\n\t\t\tSwapPlayerSlots();\n\t\t}\n</script>\n"
    "<script name=\"Other\">if (IsNewlyPressed(LEFT, 0) or IsNewlyPressed(RIGHT, 0)) {}</script>";

void TestLobbyScriptPatch() {
  std::string xml(kScriptShape);
  CHECK(PatchLobbyCpuScript(xml));
  CHECK(Count(xml, "SetGameDifficulty(8);") == 1);
  CHECK(xml.find("SetGameDifficulty(8);") < xml.find("SwapPlayerSlots();"));
  CHECK(xml.find("SetGameDifficulty(8);") > xml.find("UpdateLocalLobby();"));

  CHECK(Count(xml, "SetGameDifficulty(9);") == 1);
  CHECK(xml.find("IsSlotOccupied(8)") < xml.find("SetGameDifficulty(9);"));
  CHECK(xml.find("IsNewlyPressed(BUTTON4, 0)") < xml.find("IsSlotOccupied(8)"));
  CHECK(xml.find("SetGameDifficulty(8);") < xml.find("IsNewlyPressed(BUTTON4, 0)"));
  CHECK(Count(xml, "GoBack") == 0);

  std::string other = "<script name=\"Other\">if (IsNewlyPressed(LEFT, 0) or IsNewlyPressed(RIGHT, 0)) {}</script>";
  const std::string untouched = other;
  CHECK(!PatchLobbyCpuScript(other));
  CHECK(other == untouched);
}

constexpr std::string_view kFooterScriptShape =
    "<script name=\"UI_LocalLobbyUpdateData\">\n"
    "\t  if ( IsSlotOccupied(0) and IsSlotOccupied(1) )\n\t  {\n"
    "\t\t  UIText_SetString_TextID( selectionHelpText, tid_LocalMatchLobbyHelpReady );\n\t  }\n"
    "\t  else\n\t  {\n"
    "\t\t  UIText_SetString_TextID( selectionHelpText, tid_LocalMatchLobbyHelp );\n\t  }\n"
    "</script>\n<script name=\"Other\">if ( IsSlotOccupied(0) and IsSlotOccupied(1) ) {}</script>";

void TestLobbyFooterScriptPatch() {
  std::string xml(kFooterScriptShape);
  CHECK(PatchLobbyFooterScript(xml));
  CHECK(Has(xml, "else if ( IsSlotOccupied(0) and IsSlotOccupied(1) )"));
  CHECK(Count(xml, "tid_NetOptionsHelp") == 1);
  CHECK(xml.find("tid_NetOptionsHelp") < xml.find("tid_LocalMatchLobbyHelpReady"));
  CHECK(Count(xml, "tid_LocalMatchLobbyHelpReady") == 1 && Count(xml, "tid_LocalMatchLobbyHelp )") == 1);
  CHECK(Count(xml, "if ( IsSlotOccupied(0) and IsSlotOccupied(1) ) {}") == 1);

  std::string other = "<script name=\"GameLobbyUpdate\">if ( IsSlotOccupied(0) and IsSlotOccupied(1) ) {}</script>";
  const std::string untouched = other;
  CHECK(!PatchLobbyFooterScript(other));
  CHECK(other == untouched);
}

enum class Footer { kAddCpu, kStock, kCpuActive };

// The patched script's order: the CPU question first, then the stock seat check.
Footer FooterShown(const VersusCpuOpponent& opponent, uint8_t mode, bool seat0, bool seat1) {
  if (opponent.SideIn(mode) >= 0) return Footer::kCpuActive;
  return seat0 && seat1 ? Footer::kStock : Footer::kAddCpu;
}

void TestFooterFollowsTheCpu() {
  VersusCpuOpponent opponent;
  opponent.Reset();
  CHECK(FooterShown(opponent, kVersus, true, false) == Footer::kAddCpu);
  opponent.Cycle(1);
  CHECK(opponent.level() == 1);
  CHECK(FooterShown(opponent, kVersus, true, false) == Footer::kCpuActive);
  for (int press = 0; press < kCpuDifficultyLevels * 2; ++press) {
    opponent.Cycle(1);
    CHECK(opponent.level() >= 1 && opponent.level() <= kCpuDifficultyLevels);
    CHECK(FooterShown(opponent, kVersus, true, false) == Footer::kCpuActive);
  }
  opponent.Reset();
  CHECK(opponent.SideIn(kVersus) == -1 && opponent.level() == 0);
  CHECK(FooterShown(opponent, kVersus, true, false) == Footer::kAddCpu);
}

void TestFooterAndDifficultySurviveSwapSlots() {
  VersusCpuOpponent opponent;
  opponent.Reset();
  for (int press = 0; press < 5; ++press) opponent.Cycle(1);
  opponent.FollowSeats(EmptyLobbySide(kEmptyLobbySlot, kPad));
  CHECK(opponent.SideIn(kVersus) == 0 && opponent.level() == 5);
  CHECK(FooterShown(opponent, kVersus, false, true) == Footer::kCpuActive);
  opponent.Cycle(EmptyLobbySide(kEmptyLobbySlot, kPad));
  CHECK(opponent.SideIn(kVersus) == 0 && opponent.level() == 6);
}

void TestHumanVersusHumanKeepsTheStockFooter() {
  VersusCpuOpponent opponent;
  opponent.Reset();
  CHECK(FooterShown(opponent, kVersus, true, true) == Footer::kStock);
  opponent.Cycle(1);
  opponent.FollowSeats(EmptyLobbySide(kPad, kPad));
  CHECK(opponent.level() == 0 && FooterShown(opponent, kVersus, true, true) == Footer::kStock);
}

void TestOnlyLocalMatchSeesAnActiveCpu() {
  VersusCpuOpponent opponent;
  opponent.Reset();
  opponent.Cycle(1);
  for (const uint8_t mode : {uint8_t(0), uint8_t(2), uint8_t(3)}) {
    CHECK(opponent.SideIn(mode) == -1);
    CHECK(FooterShown(opponent, mode, true, true) == Footer::kStock);
  }
}

std::string HelpLine(const std::string& accept) {
  return "[color=0xFFFFFFFF]\xC2\x80[color=0x01010101] " + accept + "          [color=0xFFFFFFFF]\xC2\x8A" +
         "[color=0x01010101] Swap Slots          [color=0xFFFFFFFF]\xC2\x81[color=0x01010101] Back";
}

std::string FooterLine(const std::string& accept, const std::string& swap, const std::string& back) {
  return "[color=0xFFFFFFFF]\xC2\x80[color=0x01010101] " + accept + "          [color=0xFFFFFFFF]\xC2\x8A" +
         "[color=0x01010101] " + swap + "          [color=0xFFFFFFFF]\xC2\x81[color=0x01010101] " + back;
}

void TestAddCpuFooterLine() {
  std::string line = HelpLine("Join Game");
  CHECK(PatchAddCpuHelpLine(line));
  CHECK(Has(line, "\xC2\x82[color=0x01010101] Add CPU"));
  CHECK(line.find("\xC2\x82") < line.find("\xC2\x8A"));
  CHECK(!Has(line, "\xC2\x83") && !Has(line, "Remove CPU") && !Has(line, "Change Difficulty"));
  CHECK(!Has(line, std::string(kHelpWideGap)));

  std::string plain = "No glyphs here";
  CHECK(!PatchAddCpuHelpLine(plain));
  CHECK(plain == "No glyphs here");
}

void TestCpuActiveFooterLine() {
  std::string line = HelpLine("Start Game");
  CHECK(PatchCpuActiveHelpLine(line));
  const size_t accept = line.find("\xC2\x80[color=0x01010101] Start Game");
  const size_t change = line.find("\xC2\x82[color=0x01010101] Change Difficulty");
  const size_t remove = line.find("\xC2\x83[color=0x01010101] Remove CPU");
  const size_t swap = line.find("\xC2\x8A[color=0x01010101] Swap Slots");
  const size_t back = line.find("\xC2\x81[color=0x01010101] Back");
  CHECK(accept < change && change < remove && remove < swap && swap < back && back != std::string::npos);
  CHECK(!Has(line, "Add CPU") && !Has(line, std::string(kHelpWideGap)));
}

void TestFooterSwapLabelIsReplacedOnlyWhereNeeded() {
  std::string french = FooterLine("Commencer la partie", "Échanger de places", "Retour");
  CHECK(PatchCpuActiveHelpLine(french));
  CHECK(Has(french, "\xC2\x8A[color=0x01010101] Échanger     [color=0xFFFFFFFF]\xC2\x81[color=0x01010101] Retour"));
  std::string italian = FooterLine("Inizia gioco", "Scambia Posto", "Indietro");
  CHECK(PatchCpuActiveHelpLine(italian));
  CHECK(Has(italian, "Scambia Posto"));
}

void TestFooterWordsAreComplete() {
  for (const LobbyFooterWords& words : kLobbyFooterWords) {
    CHECK(!words.back.empty() && !words.add.empty() && !words.change.empty() && !words.remove.empty());
    CHECK(Has(std::string(words.add), "CPU") && Has(std::string(words.remove), "CPU"));
    CHECK(words.add != words.remove);
  }
  CHECK(&FooterWordsFor("anything else") == &kLobbyFooterWords[0]);
}

void TestLobbyHelpTableUsesTheLobbyLines() {
  std::string table;
  for (size_t i = 0; i < kLobbyJoinHelpLine; ++i) table += "line\n";
  table += HelpLine("Join Game") + "\n" + HelpLine("Start Game") + "\n";
  for (size_t i = kLobbyReadyHelpLine + 1; i < kLobbyCpuHelpLine; ++i) table += "line\n";
  table += "spare\nlast\n";
  const std::string stock_ready = HelpLine("Start Game");
  CHECK(PatchLobbyHelpText(table) == 2);
  const TextLine join = FindTextLine(table, kLobbyJoinHelpLine);
  const TextLine ready = FindTextLine(table, kLobbyReadyHelpLine);
  const TextLine cpu = FindTextLine(table, kLobbyCpuHelpLine);
  CHECK(Has(table.substr(join.start, join.size), "Add CPU"));
  CHECK(table.substr(ready.start, ready.size) == stock_ready);
  CHECK(Has(table.substr(cpu.start, cpu.size), "Remove CPU"));
  CHECK(!Has(table, "spare") && Has(table, "\nlast\n"));

  std::string other = "short\ntable\n";
  CHECK(PatchLobbyHelpText(other) == 0);
  CHECK(other == "short\ntable\n");
}

// Helvetica Bold per mille advances; accents take their base letter's width.
int LetterWidth(unsigned char c) {
  static constexpr int kLower[] = {556, 611, 556, 611, 556, 333, 611, 611, 278, 278, 556, 278, 889,
                                   611, 611, 611, 611, 389, 556, 333, 611, 556, 778, 556, 556, 500};
  static constexpr int kUpper[] = {722, 722, 722, 722, 667, 611, 778, 722, 278, 556, 722, 611, 833,
                                   722, 778, 667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611};
  if (c >= 'a' && c <= 'z') return kLower[c - 'a'];
  if (c >= 'A' && c <= 'Z') return kUpper[c - 'A'];
  return c == ' ' ? 278 : 611;
}

constexpr double kFooterEm = 39.0;
constexpr double kFooterIconWidth = 46.0;
constexpr double kFooterSafeWidth = 1728.0;

double FooterWidth(const std::string& line) {
  double width = 0;
  for (size_t at = 0; at < line.size();) {
    if (line.compare(at, 7, "[color=") == 0) {
      at = line.find(']', at) + 1;
    } else if (uint8_t(line[at]) == 0xC2 && uint8_t(line[at + 1]) < 0xA0) {
      width += kFooterIconWidth;
      at += 2;
    } else if (uint8_t(line[at]) >= 0xC0) {
      width += LetterWidth(0) * kFooterEm / 1000;
      at += 2;
    } else {
      width += LetterWidth(uint8_t(line[at])) * kFooterEm / 1000;
      ++at;
    }
  }
  return width;
}

struct Archive {
  std::ifstream in;

  std::optional<std::string> Entry(uint32_t index) {
    uint32_t record[4]{};
    in.clear();
    in.seekg(0x800 + index * 16);
    in.read(reinterpret_cast<char*>(record), sizeof(record));
    if (!in) return std::nullopt;
    std::vector<unsigned char> stored(record[2]);
    in.seekg(record[1]);
    in.read(reinterpret_cast<char*>(stored.data()), std::streamsize(stored.size()));
    std::string text(16u << 20, '\0');
    mz_ulong size = mz_ulong(text.size());
    if (!in || mz_uncompress(reinterpret_cast<unsigned char*>(text.data()), &size, stored.data(),
                             mz_ulong(stored.size())) != MZ_OK) {
      return std::nullopt;
    }
    text.resize(size);
    return text;
  }
};

constexpr uint32_t kScriptsEntry = 40;
constexpr uint32_t kResultsScriptsEntry = 44;
constexpr uint32_t kFirstTextEntry = 30;
constexpr uint32_t kTextEntries = 5;
constexpr uint32_t kDescriptorsEntry = 191;
constexpr uint32_t kVersusFsmEntry = 465;
constexpr uint32_t kTrainingScriptsEntry = 43;
constexpr uint32_t kReplacementsEntry = 768;

uint32_t PathKey(std::string path) {
  for (char& c : path) c = char(std::toupper(static_cast<unsigned char>(c)));
  return uint32_t(mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(path.data()), path.size()));
}

uint32_t ReadLE32(const std::string& s, size_t at) {
  return uint32_t(uint8_t(s[at])) | uint32_t(uint8_t(s[at + 1])) << 8 | uint32_t(uint8_t(s[at + 2])) << 16 |
         uint32_t(uint8_t(s[at + 3])) << 24;
}

constexpr uint32_t kScreenSetEntry = 52;
constexpr uint32_t kTextIdsEntry = 29;
constexpr uint32_t kGeneralScriptsEntry = 37;
constexpr uint32_t kUiScriptsEntry = 42;
constexpr uint32_t kHelpIdsEntry = 51;

std::string ElementBlock(const std::string& screens, const std::string& name) {
  const size_t at = screens.find("<name>" + name + "</name>");
  if (at == std::string::npos) return {};
  return screens.substr(at, screens.find("</UIText>", at) - at);
}

void TestRealArchive() {
  Archive archive{std::ifstream(Root() / "game/extracted/flog_x360.fpg", std::ios::binary)};
  if (!archive.in) {
    std::printf("note: game/extracted/flog_x360.fpg is not on this machine, the real documents are not checked\n");
    return;
  }

  const auto replacements = archive.Entry(kReplacementsEntry);
  CHECK(replacements.has_value());
  if (replacements) {
    for (const CpuMarkerArt& art : kCpuMarkerArt) {
      char line[64];
      std::snprintf(line, sizeof(line), "%x\t%d\t", art.tile, art.colour);
      const size_t at = replacements->find(line);
      CHECK(at != std::string::npos);
      if (at != std::string::npos) {
        CHECK(replacements->substr(at, replacements->find('\n', at) - at).find(std::string(art.texture)) !=
              std::string::npos);
      }
    }
  }

  std::string index(0x10000 * 16, '\0');
  archive.in.clear();
  archive.in.seekg(0x800);
  archive.in.read(index.data(), std::streamsize(index.size()));
  for (const CpuMarkerArt& art : kCpuMarkerArt) {
    const uint32_t key = PathKey("textures/in_game/" + std::string(art.texture) + ".texture.x360");
    std::optional<std::string> file;
    for (uint32_t i = 0; i < 0x10000 && !file; ++i) {
      if (ReadLE32(index, size_t(i) * 16) == key) file = archive.Entry(i);
    }
    CHECK(file.has_value());
    if (!file) continue;
    const auto* bytes = reinterpret_cast<const uint8_t*>(file->data());
    CHECK(TextureFileName(bytes, file->size()) == art.texture);
    EvilSpriteImage image;
    CHECK(BuildCpuMarker(bytes, file->size(), art, &image));
    CHECK(image.width == 128 && image.height == 256);
    CHECK(image.has_palette);
    CHECK(image.indices.size() == size_t(image.width) * image.height);
    int label = 0;
    int bracket = 0;
    for (int y = 0; y < image.height; ++y) {
      for (int x = 0; x < image.width; ++x) {
        const bool drawn = image.indices[size_t(y) * image.width + x] != 0;
        const bool in_label = y >= art.label_top && y < art.label_bottom && x >= 33 && x < 95;
        if (drawn) ++(in_label ? label : bracket);
      }
    }
    CHECK(label > 500);
    CHECK(bracket > 500);
    CHECK(image.palette[0] == 0);
    EvilSpriteImage again;
    CHECK(BuildCpuMarker(bytes, file->size(), art, &again));
    CHECK(again.indices == image.indices);
    if (const char* dump = std::getenv("SSF2T_CPU_MARKER_DUMP")) {
      std::ofstream out(fs::path(dump) / (std::string(art.texture) + ".raw"), std::ios::binary);
      out.write(reinterpret_cast<const char*>(image.palette.data()), std::streamsize(image.palette.size()));
      out.write(reinterpret_cast<const char*>(image.indices.data()), std::streamsize(image.indices.size()));
    }
  }

  auto scripts = archive.Entry(kScriptsEntry);
  CHECK(scripts.has_value());
  if (scripts) {
    // Y belongs to nothing here: neither the lobby's scripts nor the only input script its control state runs.
    CHECK(!Has(*scripts, "BUTTON4"));
    CHECK(PatchLobbyCpuScript(*scripts));
    CHECK(Has(*scripts, "IsNewlyPressed(BUTTON3, 0)"));
    CHECK(Has(*scripts, "IsNewlyPressed(LEFT, 0) or IsNewlyPressed(RIGHT, 0)"));
  }

  const auto general = archive.Entry(kGeneralScriptsEntry);
  CHECK(general.has_value());
  if (general) {
    const size_t input = general->find("<script name=\"GeneralScreenInput\">");
    CHECK(input != std::string::npos);
    const std::string body = general->substr(input, general->find("</script>", input) - input);
    CHECK(Has(body, "BUTTON2") && !Has(body, "BUTTON4"));
  }

  auto ui_scripts = archive.Entry(kUiScriptsEntry);
  CHECK(ui_scripts.has_value());
  if (ui_scripts) {
    const size_t open = ui_scripts->find(kLobbyFooterScriptOpen);
    CHECK(open != std::string::npos);
    CHECK(!Has(ui_scripts->substr(open, ui_scripts->find("</script>", open) - open), "BUTTON4"));
    CHECK(PatchLobbyFooterScript(*ui_scripts));
    CHECK(Has(*ui_scripts, "tid_LocalMatchLobbyHelpReady"));
  }

  const auto ids = archive.Entry(kTextIdsEntry);
  CHECK(ids.has_value());
  if (ids) {
    const auto id_at = [&](size_t index) {
      const TextLine line = FindTextLine(*ids, index);
      return ids->substr(line.start, line.size);
    };
    CHECK(id_at(kLobbyJoinHelpLine) == "tid_LocalMatchLobbyHelp");
    CHECK(id_at(kLobbyReadyHelpLine) == "tid_LocalMatchLobbyHelpReady");
    CHECK(id_at(kLobbyCpuHelpLine) == "tid_NetOptionsHelp");
  }
  for (const uint32_t entry : {kScriptsEntry, kUiScriptsEntry, kScreenSetEntry, kHelpIdsEntry, kVersusFsmEntry}) {
    const auto document = archive.Entry(entry);
    CHECK(document.has_value());
    if (document) CHECK(!Has(*document, "tid_NetOptionsHelp"));
  }

  for (uint32_t i = 0; i < kTextEntries; ++i) {
    auto table = archive.Entry(kFirstTextEntry + i);
    CHECK(table.has_value());
    if (!table) continue;
    const TextLine stock = FindTextLine(*table, kLobbyReadyHelpLine);
    const std::string ready = table->substr(stock.start, stock.size);
    CHECK(PatchLobbyHelpText(*table) == 2);
    const LobbyFooterWords& words = FooterWordsFor(ready);
    CHECK(words.back == "Back" ? i == 0 : i != 0);
    const TextLine after = FindTextLine(*table, kLobbyReadyHelpLine);
    CHECK(table->substr(after.start, after.size) == ready);
    const TextLine add = FindTextLine(*table, kLobbyJoinHelpLine);
    const TextLine cpu = FindTextLine(*table, kLobbyCpuHelpLine);
    const std::string add_line = table->substr(add.start, add.size);
    const std::string cpu_line = table->substr(cpu.start, cpu.size);
    CHECK(Has(add_line, std::string(words.add)) && !Has(add_line, std::string(words.remove)));
    CHECK(Has(cpu_line, std::string(words.change)) && Has(cpu_line, std::string(words.remove)));
    CHECK(FooterWidth(add_line) <= kFooterSafeWidth);
    CHECK(FooterWidth(cpu_line) <= kFooterSafeWidth);
    for (const std::string_view text : {words.add, words.change, words.remove, words.swap}) {
      for (size_t at = 0; at + 1 < text.size(); ++at) {
        if (uint8_t(text[at]) >= 0xC0) CHECK(Has(*table, std::string(text.substr(at, 2))));
      }
    }
  }

  const auto fsm = archive.Entry(kVersusFsmEntry);
  CHECK(fsm.has_value());
  if (fsm) {
    CHECK(Has(*fsm, "<FSM name=\"versus_mode_fsm\">"));
    CHECK(Has(*fsm, "game_finished"));
    CHECK(Has(*fsm, "GameFlowPush(results_page_fsm);"));
    CHECK(!Has(*fsm, "Arcade"));
  }

  const auto results = archive.Entry(kResultsScriptsEntry);
  CHECK(results.has_value());
  if (results) {
    CHECK(Has(*results, "RestartMatch();"));
    CHECK(Has(*results, "TransitionToCharacterSelect();"));
    CHECK(Has(*results, "GetPlayerName(1);"));
    CHECK(Has(*results, "UIText_SetString(gamertag1, result);"));
  }

  const auto descriptors = archive.Entry(kDescriptorsEntry);
  CHECK(descriptors.has_value());
  if (descriptors) CHECK(Has(*descriptors, "<textID>CPU_CONTROLLED</textID>"));

  const auto text = archive.Entry(kFirstTextEntry);
  CHECK(text.has_value());
  if (text) {
    CHECK(Has(*text, "$gamertag1\n") || Has(*text, "$gamertag1\r\n"));
    CHECK(Has(*text, "$gamertag2HUD"));
  }

  const auto screens = archive.Entry(kScreenSetEntry);
  CHECK(screens.has_value());
  if (screens) {
    const std::string left = ElementBlock(*screens, "p1_gamertag");
    const std::string right = ElementBlock(*screens, "p2_gamertag");
    CHECK(Has(left, "<textID>P1_GAMERTAG</textID>") && !Has(left, "<hAlign>Right</hAlign>"));
    CHECK(Has(right, "<textID>P2_GAMERTAGHUD</textID>") && Has(right, "<hAlign>Right</hAlign>"));
    CHECK(Has(left, "HelveticaBold60") && Has(right, "HelveticaBold60"));
    CHECK(Has(ElementBlock(*screens, "p2_character"), "<textID>PLAYER_NAME</textID>"));
  }

  const auto training = archive.Entry(kTrainingScriptsEntry);
  CHECK(training.has_value());
  if (training) CHECK(Has(*training, "Options_SetTrainingCharacter(result);"));
}

struct Image {
  std::vector<uint8_t> bytes;

  uint32_t Word(uint32_t address) const {
    const size_t at = address - 0x82000000u;
    return uint32_t(bytes[at]) << 24 | uint32_t(bytes[at + 1]) << 16 | uint32_t(bytes[at + 2]) << 8 | bytes[at + 3];
  }
};

std::optional<Image> Load(const char* relative) {
  std::ifstream in(Root() / relative, std::ios::binary);
  if (!in) return std::nullopt;
  Image image;
  image.bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return image;
}

constexpr uint32_t kLwz = 32;
constexpr uint32_t kLbz = 34;
constexpr uint32_t kStb = 38;
constexpr uint32_t kLhz = 40;
constexpr uint32_t kBlr = 0x4E800020;

// A D-form access with this primary opcode and displacement, in the function starting at `start`.
bool Accesses(const Image& image, uint32_t start, uint32_t opcode, uint32_t displacement) {
  for (uint32_t at = start; at < start + 0x4000; at += 4) {
    const uint32_t word = image.Word(at);
    if (word >> 26 == opcode && (word & 0xFFFF) == displacement) return true;
    if (word == kBlr) return false;
  }
  return false;
}

constexpr uint32_t kBctrl = 0x4E800421;
constexpr uint32_t kSelectStateTableLow = 0xD55C;

template <typename Predicate>
uint32_t FirstWord(const Image& image, uint32_t start, Predicate predicate) {
  for (uint32_t at = start; at < start + 0x4000; at += 4) {
    if (predicate(image.Word(at))) return at;
    if (image.Word(at) == kBlr) return 0;
  }
  return 0;
}

// The target of a bl (opcode 18, LK set) at `at`.
uint32_t CallTarget(uint32_t at, uint32_t word) {
  const int32_t displacement = int32_t(word << 6) >> 6;
  return at + uint32_t(displacement & ~3);
}

// An addi with this immediate: how the guest forms the base of an indexed table.
bool Adds(const Image& image, uint32_t start, uint32_t immediate) {
  for (uint32_t at = start; at < start + 0x4000; at += 4) {
    const uint32_t word = image.Word(at);
    if (word >> 26 == 14 && (word & 0xFFFF) == immediate) return true;
    if (word == kBlr) return false;
  }
  return false;
}

// The lis/D-form pair the guest compiles for the fixed address.
bool AccessesAddress(const Image& image, uint32_t start, uint32_t opcode, uint32_t address) {
  const uint32_t high = (address + 0x8000) >> 16;
  bool loaded = false;
  for (uint32_t at = start; at < start + 0x4000; at += 4) {
    const uint32_t word = image.Word(at);
    if (word >> 26 == 15 && (word >> 16 & 0x1F) == 0 && (word & 0xFFFF) == high) loaded = true;
    if (loaded && word >> 26 == opcode && (word & 0xFFFF) == (address & 0xFFFF)) return true;
    if (word == kBlr) return false;
  }
  return false;
}

bool Calls(const Image& image, uint32_t start, uint32_t target) {
  for (uint32_t at = start; at < start + 0x4000; at += 4) {
    const uint32_t word = image.Word(at);
    if (word >> 26 == 18 && (word & 3) == 1 && CallTarget(at, word) == target) return true;
    if (word == kBlr) return false;
  }
  return false;
}

struct TextVariable {
  uint32_t resolver = 0;
  uint32_t argument = 0;
  uint32_t reserved = 0;
};

// An entry of the guest's $variable table: name, resolver, argument, cache, reserved bytes.
TextVariable FindTextVariable(const Image& image, const std::string& name) {
  const std::string key(name.c_str(), name.size() + 1);
  const auto end = image.bytes.end();
  for (auto text = std::search(image.bytes.begin(), end, key.begin(), key.end()); text != end;
       text = std::search(text + 1, end, key.begin(), key.end())) {
    const uint32_t address = 0x82000000u + uint32_t(text - image.bytes.begin());
    for (uint32_t at = 0x83A30000; at < 0x83A40000; at += 4) {
      if (image.Word(at) == address) return {image.Word(at + 4), image.Word(at + 8), image.Word(at + 16)};
    }
  }
  return {};
}

struct Base013Proof {
  static constexpr const char* kImage = "game/analysis/default-0.0.1.3.image.bin";
  static constexpr uint32_t kHudNameCopy = 0x823CD698;
  static constexpr uint32_t kLobbyAcceptScan = 0x823D3F08;
  static constexpr uint32_t kLobbyStartScan = 0x823D3548;
  static constexpr uint32_t kCopySideTable = 0x82248D10;
  static constexpr uint32_t kFightInit = 0x822392F0;
  static constexpr uint32_t kRestartMatch = 0x823C6FC0;
  static constexpr uint32_t kTrainingOwners = 0x823B9990;
  static constexpr uint32_t kAiLevel = 0x8238F8C0;
  static constexpr uint32_t kAkumaAssets = 0x823B5EA0;
  static constexpr uint32_t kBoundsTable = 0x820E2848;
  static constexpr uint32_t kColorState = 0x82244288;
  static constexpr uint32_t kFinalState = 0x82246798;
  static constexpr uint32_t kCursorInput = 0x82245748;
  static constexpr uint32_t kSelectUpdate = 0x82247E90;
  static constexpr uint32_t kSelectTick = 0x82247620;
  static constexpr uint32_t kSideStep = 0x82246D38;
  static constexpr uint32_t kFrameStep = 0x8223DDB8;
  static constexpr uint32_t kPadRefresh = 0x8237B170;
  static constexpr uint32_t kPhaseDispatch = 0x8223DD58;
};

struct TitleUpdate023Proof {
  static constexpr const char* kImage = "game/analysis/default.image.bin";
  static constexpr uint32_t kHudNameCopy = 0x823CDC78;
  static constexpr uint32_t kLobbyAcceptScan = 0x823D45D0;
  static constexpr uint32_t kLobbyStartScan = 0x823D3BC0;
  static constexpr uint32_t kCopySideTable = 0x82248D58;
  static constexpr uint32_t kFightInit = 0x82239418;
  static constexpr uint32_t kRestartMatch = 0x823C75B8;
  static constexpr uint32_t kTrainingOwners = 0x823B98D0;
  static constexpr uint32_t kAiLevel = 0x8238F8B0;
  static constexpr uint32_t kAkumaAssets = 0x823B5ED0;
  static constexpr uint32_t kBoundsTable = 0x820E29E8;
  static constexpr uint32_t kColorState = 0x822442F0;
  static constexpr uint32_t kFinalState = 0x822467E0;
  static constexpr uint32_t kCursorInput = 0x822457B0;
  static constexpr uint32_t kSelectUpdate = 0x82247ED8;
  static constexpr uint32_t kSelectTick = 0x82247668;
  static constexpr uint32_t kSideStep = 0x82246D80;
  static constexpr uint32_t kFrameStep = 0x8223DEE0;
  static constexpr uint32_t kPadRefresh = 0x8237B240;
  static constexpr uint32_t kPhaseDispatch = 0x8223DE80;
};

void TestPortugueseFooterFits() {
  std::string join = FooterLine("Entrar na Partida", "Trocar Vagas", "Voltar");
  std::string ready = FooterLine("Iniciar Partida", "Trocar Vagas", "Voltar");
  CHECK(PatchAddCpuHelpLine(join) && PatchCpuActiveHelpLine(ready));
  CHECK(Has(join, "Adicionar CPU"));
  CHECK(Has(ready, "Mudar Dificuldade") && Has(ready, "Remover CPU") && Has(ready, "Trocar Vagas"));
  CHECK(FooterWidth(join) <= kFooterSafeWidth && FooterWidth(ready) <= kFooterSafeWidth);
}

constexpr uint32_t kPadActionStart = 130;
constexpr uint32_t kPadActionAccept = 136;
constexpr uint32_t kP1Human = 2690;
constexpr uint32_t kP2Human = 3714;

template <typename T, typename P>
void TestGuestPaths() {
  const auto image = Load(P::kImage);
  if (!image) {
    std::printf("note: %s is not on this machine, the guest paths are not checked\n", P::kImage);
    return;
  }

  // Local Match already owns the side table: the select resets it and the load copies it into the fighters.
  CHECK(AccessesAddress(*image, P::kCopySideTable, kLbz, T::kGameMode));
  CHECK(AccessesAddress(*image, P::kCopySideTable, kLbz, T::kSideHuman));
  CHECK(Accesses(*image, P::kCopySideTable, kStb, kP1Human));
  CHECK(Accesses(*image, P::kCopySideTable, kStb, kP2Human));

  // Rounds and rematches keep the owner: fight init stores it and RestartMatch puts it back.
  CHECK(Accesses(*image, P::kFightInit, kLbz, kP1Human));
  CHECK(Accesses(*image, P::kFightInit, kLbz, kP2Human));
  CHECK(Accesses(*image, P::kRestartMatch, kStb, kFighterHumanOffset));

  // The Training CPU option is the same flag, and the AI reads the difficulty that hook writes.
  CHECK(Accesses(*image, P::kTrainingOwners, kStb, kP2Human));
  CHECK(Accesses(*image, P::kAiLevel, kLhz, kGameDifficultyOffset - 1));
  CHECK(Accesses(*image, P::kAiLevel, kLhz, kGameLadderOffset));
  CHECK(AccessesAddress(*image, P::kAiLevel, kLwz, T::kGameState));

  // The select's own cursor reads the side's pad words, so a copy of them is all a shared pad needs, and a side is
  // finished through the mask or the final-state marker the sharing waits on.
  CHECK(Accesses(*image, P::kCursorInput, kLhz, kFighterPadOffset));
  CHECK(Accesses(*image, P::kCursorInput, kLhz, kFighterPadPreviousOffset));
  CHECK(AccessesAddress(*image, P::kColorState, kStb, T::kSelectMask));
  CHECK(Adds(*image, P::kFinalState, T::kSelectDone & 0xFFFF));

  // The select update reads both sides' pad words, then dispatches the select state, so the words have to be in place
  // when it is entered; the frame refreshes them before it runs.
  const uint32_t first_read = FirstWord(*image, P::kSelectUpdate, [](uint32_t w) {
    return w >> 26 == kLhz && ((w & 0xFFFF) == 2704 || (w & 0xFFFF) == 3728);
  });
  const uint32_t dispatch = FirstWord(*image, P::kSelectUpdate, [](uint32_t w) { return w == kBctrl; });
  CHECK(first_read != 0 && dispatch != 0 && first_read < dispatch);
  CHECK(Adds(*image, P::kSelectUpdate, kSelectStateTableLow));
  uint32_t refresh = 0;
  uint32_t phases = 0;
  for (uint32_t at = P::kFrameStep; at < P::kFrameStep + 0x400 && image->Word(at) != kBlr; at += 4) {
    const uint32_t w = image->Word(at);
    if (w >> 26 != 18 || (w & 3) != 1) continue;
    const uint32_t target = CallTarget(at, w);
    if (target == P::kPadRefresh) refresh = at;
    if (target == P::kPhaseDispatch) phases = at;
  }
  CHECK(refresh != 0 && phases != 0 && refresh < phases);

  // Nothing in the tick or the side step asks the lobby seats or a controller: a side runs while its human flag is set.
  CHECK(Accesses(*image, P::kSelectTick, kLbz, kP2Human));
  for (const uint32_t function : {P::kSelectTick, P::kSideStep}) {
    CHECK(!AccessesAddress(*image, function, kLwz, T::kLobbySlots));
    CHECK(!AccessesAddress(*image, function, kLwz, T::kLobbySlots + 4));
  }

  // A confirm opens the fixed window, and the lock and the done mask both come after it.
  CHECK(Accesses(*image, P::kSideStep, kStb, kFighterConfirmWindowOffset));
  CHECK(FirstWord(*image, P::kSideStep, [](uint32_t w) { return w >> 26 == 14 && (w & 0xFFFF) == kConfirmWindowFrames; }) != 0);
  CHECK(Accesses(*image, P::kSideStep, kStb, kFighterLockedOffset));
  CHECK(AccessesAddress(*image, P::kColorState, kLbz, T::kColourPrompt) ||
        Adds(*image, P::kColorState, (T::kColourPrompt - 4) & 0xFFFF));

  // The query seat is past the native's own bound, and the lobby's pad scans ask only for start and accept.
  CHECK(FirstWord(*image, T::kIsSlotOccupied, [](uint32_t w) {
          return w >> 26 == 11 && (w & 0xFFFF) == uint32_t(kCpuActiveQuerySeat);
        }) != 0);
  CHECK(Calls(*image, T::kUpdateLocalLobby, P::kLobbyAcceptScan));
  CHECK(Calls(*image, T::kUpdateLocalLobby, P::kLobbyStartScan));
  CHECK(Adds(*image, P::kLobbyAcceptScan, kPadActionAccept));
  CHECK(Adds(*image, P::kLobbyAcceptScan, kPadActionAccept + 1));
  CHECK(Adds(*image, P::kLobbyStartScan, kPadActionStart));
  for (const uint32_t scan : {P::kLobbyAcceptScan, P::kLobbyStartScan}) {
    for (uint32_t action = kPadActionAccept + 2; action < kPadActionAccept + 16; ++action) {
      CHECK(!Adds(*image, scan, action));
    }
  }

  // Both name variables go to a hooked resolver, and only the HUD one has its own copy of the name read: it asks the
  // occupied hook, then indexes the name table by the seat.
  const TextVariable first = FindTextVariable(*image, "gamertag1");
  const TextVariable second = FindTextVariable(*image, "gamertag2");
  const TextVariable hud = FindTextVariable(*image, "gamertag2HUD");
  CHECK(first.resolver == T::kTextPlayerName && first.argument == 0);
  CHECK(second.resolver == T::kTextPlayerName && second.argument == 1);
  CHECK(hud.resolver == T::kTextHudPlayerName && hud.argument == 1);
  CHECK(hud.reserved == kCpuNameBytesForTest);
  CHECK(Calls(*image, T::kTextPlayerName, T::kGetPlayerName));
  CHECK(Calls(*image, T::kTextHudPlayerName, P::kHudNameCopy));
  CHECK(!Calls(*image, T::kTextHudPlayerName, T::kGetPlayerName));
  CHECK(Calls(*image, P::kHudNameCopy, T::kIsSlotOccupied));
  CHECK(Adds(*image, P::kHudNameCopy, T::kLobbySlots & 0xFFFF));
  CHECK(FirstWord(*image, P::kHudNameCopy,
                  [](uint32_t w) { return w >> 26 == 7 && (w & 0xFFFF) == uint32_t(kNameTableRowBytes); }) != 0);

  // CPU Akuma's assets are chosen by the owner flag, which is why it clears before the load.
  CHECK(Accesses(*image, P::kAkumaAssets, kLbz, kFighterHumanOffset));

  // Eight rows of sixteen steps, with real bounds in the rows the Arcade menu cannot reach.
  for (int row = 0; row < kCpuDifficultyLevels; ++row) {
    CHECK(image->Word(P::kBoundsTable + uint32_t(row) * 64) != 0);
    CHECK((image->Word(P::kBoundsTable + uint32_t(row) * 64 + (kCpuLadderSteps - 1) * 4) & 0xFFFF) >= 0x64);
  }
}

template <typename T>
void TestHookedOnEachBuild(const std::string& hook, const char* prefix) {
  for (const uint32_t address : {T::kUpdateLocalLobby, T::kInitLocalLobby, T::kIsSlotOccupied, T::kIsSlotOccupiedResults,
                                 T::kGetPlayerName, T::kTextPlayerName, T::kTextHudPlayerName, T::kSetGameDifficulty,
                                 T::kSelectInput, T::kEnterSelect,
                                 T::kLoadMatch}) {
    char symbol[64];
    std::snprintf(symbol, sizeof(symbol), "REX_HOOK_RAW(%s_sub_%08X)", prefix, address);
    CHECK(Count(hook, symbol) == 1);
  }
}

void TestSource() {
  const std::string hook = ReadText(Root() / "src/character/versus_cpu_hook.cpp");
  TestHookedOnEachBuild<Base013VersusCpu>(hook, "base013");
  TestHookedOnEachBuild<TitleUpdate023VersusCpu>(hook, "tu023");

  CHECK(Count(hook, "::TextVariableName(ctx, base, __imp__") == 4);
  for (const char* configured : {"XamUserGetName", "UserDisplayName", "kNameTable", "user_name"}) {
    CHECK(!Has(hook, configured));
  }
  CHECK(!Has(hook, "DEBUG_SAULO"));
  CHECK(Has(hook, "ctx.r3.s32 == kCpuActiveQuerySeat") && Has(hook, "ctx.r3.u32 == kRemoveCpuOpponentArgument"));

  CHECK(Has(hook, "kNetplayRoleSetting"));
  CHECK(!Has(hook, "SetGameMode"));
  CHECK(!Has(hook, "kGameMode, "));
  CHECK(!Has(hook, "base[G::kGameMode] ="));
  CHECK(Has(hook, "GuestStore8(base, fighter + kFighterHumanOffset, 0);"));
  CHECK(Has(hook, "base[G::kSideHuman + uint32_t(cpu)] = 0;"));

  const std::string confirm = ReadText(Root() / "src/character/ultra_confirm_hook.cpp");
  CHECK(Has(confirm, "GlobalVersusCpu().SideIn(base[S::kGameMode]) == side"));

  const std::string cmake = ReadText(Root() / "CMakeLists.txt");
  CHECK(Has(cmake, "src/character/versus_cpu_hook.cpp"));

  const std::string parity = ReadText(Root() / "src/feature_parity.h");
  CHECK(Has(parity, "tests/versus_cpu_tests.cpp"));

  for (const char* online : {"src/online_runtime.cpp", "src/netplay_hook.cpp", "src/character/host_rollback.cpp"}) {
    CHECK(!Has(ReadText(Root() / online), "VersusCpu"));
  }
}

}  // namespace

int main() {
  TestLocalMatchIsHumanByDefault();
  TestNoSecondControllerIsNotACpu();
  TestPlayerOneVersusCpu();
  TestCpuVersusPlayerTwo();
  TestTheOptionChangesOnlyTheEmptySide();
  TestTwoPlayersLeaveNoCpuSide();
  TestDifficultyCycleWrapsAndNeverRemovesTheCpu();
  TestSwapMovesTheCpu();
  TestLeavingTheLobbyDropsTheCpu();
  TestOwnershipStaysInLocalMatch();
  TestDifficultyReachesTheRealRange();
  TestCycleCommandIsOutsideTheMenuRange();
  TestCpuCursorFollowsThePlayerOnlyAfterTheirPick();
  TestPickFinished();
  TestLobbyToSelectLifecycle();
  TestSharedInputMovesTheCpuCursorOncePerPress();
  TestCpuSidePicksOnlyAfterThePlayer();
  TestCpuOnFirstSideUsesTheSecondPlayersPad();
  TestCopyAfterTheSelectUpdateNeverMovesTheCpu();
  TestCopyingOnlyCurrentErasesTheEdge();
  TestStaleSourceAfterTheLockNeverMovesTheCpu();
  TestAGatedCpuSideNeverMovesEvenWithSharedInput();
  TestNoSharingNeverCompletes();
  TestHumanVersusHumanIsUntouched();
  TestLeavingTheLobbyClearsTheCpu();
  TestCpuName();
  TestCpuNameFollowsTheLobby();
  TestAnEmptySeatFallsOutOfTheNameTable();
  TestHudLabelOnEachCpuSide();
  TestHudLabelIsTheWholeToken();
  TestHudLabelNeverShowsTheDifficulty();
  TestHudLabelLeavesHumansAlone();
  TestHudVariablesOfOtherSeatsAreNotTheCpu();
  TestHandoverIsAlmostImmediate();
  TestHandoverDoesNotWaitForTheLateMask();
  TestConfirmWindowIsKeptForItsFirstFrame();
  TestPendingAkumaCommitHoldsTheWindow();
  TestNormalSuperBlocksTheHandover();
  TestAColourPromptWaitsForTheMask();
  TestMarkerFollowsTheCpuSide();
  TestLobbyScriptPatch();
  TestLobbyFooterScriptPatch();
  TestFooterFollowsTheCpu();
  TestFooterAndDifficultySurviveSwapSlots();
  TestHumanVersusHumanKeepsTheStockFooter();
  TestOnlyLocalMatchSeesAnActiveCpu();
  TestAddCpuFooterLine();
  TestCpuActiveFooterLine();
  TestFooterSwapLabelIsReplacedOnlyWhereNeeded();
  TestFooterWordsAreComplete();
  TestPortugueseFooterFits();
  TestLobbyHelpTableUsesTheLobbyLines();
  TestRealArchive();
  TestGuestPaths<Base013VersusCpu, Base013Proof>();
  TestGuestPaths<TitleUpdate023VersusCpu, TitleUpdate023Proof>();
  TestSource();
  std::printf("versus cpu: %d checks, %d failed\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
