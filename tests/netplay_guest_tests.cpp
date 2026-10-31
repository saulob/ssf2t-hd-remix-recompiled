// Guest binding tests: the analysed addresses and fingerprints of both images against the repository's analysis
// images (skipped when absent) and the generated trees, plus the determinism helpers. Headless.
// Run: ssf2t_netplay_guest_tests

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <fstream>
#include <string>
#include <vector>

#include "netplay/determinism.h"
#include "netplay/guest_netplay.h"
#include "netplay_test_support.h"

using namespace ssf2t::netplay;
using namespace netplay_test;

namespace {

namespace fs = std::filesystem;

constexpr uint32_t kImageBase = 0x82000000;

fs::path Root() { return fs::path(SSF2T_SOURCE_DIR); }

bool ReadWords(const fs::path& image, uint32_t address, std::span<const uint32_t> expected, bool& readable) {
  std::ifstream in(image, std::ios::binary);
  readable = in.good();
  if (!readable) return false;
  in.seekg(address - kImageBase);
  for (const uint32_t word : expected) {
    uint8_t b[4];
    in.read(reinterpret_cast<char*>(b), 4);
    if (!in) return false;
    if ((uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | b[3]) != word) return false;
  }
  return true;
}

template <typename G>
void CheckFingerprintsAgainstImage(const fs::path& image) {
  bool readable = false;
  ReadWords(image, G::kCallback, G::kCallbackWords, readable);
  if (!readable) {
    std::printf("  SKIP %s image: %s not found\n", G::kName, image.string().c_str());
    return;
  }
  bool r;
  CHECK(ReadWords(image, G::kCallback, G::kCallbackWords, r));
  CHECK(ReadWords(image, G::kSimStep, G::kSimStepWords, r));
  CHECK(ReadWords(image, G::kEffectsGate, G::kEffectsGateWords, r));
  CHECK(ReadWords(image, G::kFightLoop, G::kFightLoopWords, r));
  CHECK(ReadWords(image, G::kInputDecode, G::kInputDecodeWords, r));
  CHECK(ReadWords(image, G::kInputHistoryStep, G::kInputHistoryStepWords, r));
  CHECK(ReadWords(image, G::kRandStep, G::kRandStepWords, r));
  CHECK(ReadWords(image, G::kSideSetup, G::kSideSetupWords, r));
  CHECK(ReadWords(image, G::kNetActiveQuery, G::kNetActiveQueryWords, r));
  CHECK(ReadWords(image, G::kFrameTailNetCheck, G::kFrameTailNetCheckWords, r));
  CHECK(ReadWords(image, G::kFramePump, G::kFramePumpWords, r));
}

bool ReadImageWords(const fs::path& image, uint32_t address, uint32_t* out, size_t count) {
  std::ifstream in(image, std::ios::binary);
  if (!in) return false;
  in.seekg(address - kImageBase);
  for (size_t i = 0; i < count; ++i) {
    uint8_t b[4];
    in.read(reinterpret_cast<char*>(b), 4);
    if (!in) return false;
    out[i] = uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | b[3];
  }
  return true;
}

// `lis rX,hi` followed by a D-form instruction with `lo`: the address the pair builds.
uint32_t PairAddress(uint32_t lis, uint32_t low) {
  return (lis << 16) + uint32_t(int32_t(int16_t(low & 0xFFFF)));
}
uint32_t Opcode(uint32_t word) { return word >> 26; }

// The rollback callback's command 3 (the game's own local input) reads the active user and indexes the controller
// table with it; GetUIControllerUsed loads the menus' input object.
template <typename G>
void CheckInputRouteAgainstImage(const fs::path& image, uint32_t command3, uint32_t ui_controller_used) {
  uint32_t w[5];
  if (!ReadImageWords(image, command3, w, 5)) {
    std::printf("  SKIP %s input route: %s not found\n", G::kName, image.string().c_str());
    return;
  }
  CHECK(Opcode(w[0]) == 15 && Opcode(w[1]) == 32 && Opcode(w[2]) == 7 && Opcode(w[3]) == 15 && Opcode(w[4]) == 14);
  CHECK(PairAddress(w[0], w[1]) == G::kActiveUser);
  CHECK((w[2] & 0xFFFF) == kInputEntryStride);
  CHECK(PairAddress(w[3], w[4]) == G::kControllerInputTable);
  uint32_t ui[2];
  CHECK(ReadImageWords(image, ui_controller_used + 0x14, ui, 2));
  CHECK(Opcode(ui[0]) == 15 && Opcode(ui[1]) == 32);
  CHECK(PairAddress(ui[0], ui[1]) == G::kUiInputPointer);
}

uint32_t BranchTarget(uint32_t address, uint32_t word) {
  return address + uint32_t(int32_t(word << 6) >> 6 & ~3);
}
uint32_t ConditionalTarget(uint32_t address, uint32_t word) {
  return address + uint32_t(int32_t(int16_t(word & 0xFFFC)));
}
bool IsCall(uint32_t word) { return Opcode(word) == 18 && (word & 3) == 1; }

std::vector<uint32_t> CallsIn(const fs::path& image, uint32_t begin, uint32_t end) {
  std::vector<uint32_t> calls;
  for (uint32_t at = begin; at < end; at += 4) {
    uint32_t word;
    if (ReadImageWords(image, at, &word, 1) && IsCall(word)) calls.push_back(BranchTarget(at, word));
  }
  return calls;
}

// The frame tail calls the net-active query and, when it answers true, branches past the per-frame simulation work.
// The step command (callback command 5) runs the sim step and the same post-step updates, so a frame the rollback core
// runs through it needs the tail to skip them.
template <typename G>
void CheckFrameTailAgainstImage(const fs::path& image) {
  uint32_t w[3];
  if (!ReadImageWords(image, G::kFrameTailNetCheck, w, 3)) {
    std::printf("  SKIP %s frame tail: %s not found\n", G::kName, image.string().c_str());
    return;
  }
  CHECK(IsCall(w[0]) && BranchTarget(G::kFrameTailNetCheck, w[0]) == G::kNetActiveQuery);
  CHECK(Opcode(w[2]) == 16 && (w[2] & 3) == 0);
  const uint32_t skipped_begin = G::kFrameTailNetCheck + 12;
  const uint32_t skipped_end = ConditionalTarget(G::kFrameTailNetCheck + 8, w[2]);
  CHECK(skipped_end > skipped_begin && skipped_end - skipped_begin < 0x200);
  const std::vector<uint32_t> tail = CallsIn(image, skipped_begin, skipped_end);
  const std::vector<uint32_t> step = CallsIn(image, G::kCallback, G::kCallback + 0x400);
  CHECK(std::find(tail.begin(), tail.end(), G::kInputDecode) != tail.end());
  CHECK(std::find(step.begin(), step.end(), G::kSimStep) != step.end());
  size_t shared = 0;
  for (const uint32_t target : tail) shared += std::count(step.begin(), step.end(), target) > 0 ? 1 : 0;
  CHECK(shared >= 4);
  CHECK(FrameTailNetCheckReturn<G>() == G::kFrameTailNetCheck + 4);
}

bool ContainsWord(const fs::path& image, uint32_t begin, uint32_t end, uint32_t wanted) {
  for (uint32_t at = begin; at < end; at += 4) {
    uint32_t word;
    if (ReadImageWords(image, at, &word, 1) && word == wanted) return true;
  }
  return false;
}

bool Calls(const fs::path& image, uint32_t begin, uint32_t end, uint32_t target) {
  const std::vector<uint32_t> calls = CallsIn(image, begin, end);
  return std::find(calls.begin(), calls.end(), target) != calls.end();
}

struct PresentationWait {
  uint32_t frame_tail;
  uint32_t stage_pick_call;
  uint32_t wait;
  uint32_t yield;
};

// The stage pick (before the airplane) waits on a presentation request: it sets fight work +1639 and presents frames
// through the frame pump until the byte is clear again. Only the frame tail's simulation block clears it, and the step
// command does not, so a pumped tail must run that block or the wait never ends.
template <typename G>
void CheckPresentationWaitAgainstImage(const fs::path& image, const PresentationWait& at) {
  uint32_t pump[6];
  if (!ReadImageWords(image, G::kFramePump, pump, 6)) {
    std::printf("  SKIP %s presentation wait: %s not found\n", G::kName, image.string().c_str());
    return;
  }
  constexpr uint32_t kSetRequest = 0x994B0667;  // stb r10,1639(r11)
  constexpr uint32_t kReadRequest = 0x896B0667;  // lbz r11,1639(r11)
  CHECK(IsCall(pump[4]) && BranchTarget(G::kFramePump + 16, pump[4]) == at.frame_tail);
  CHECK(IsCall(pump[5]) && BranchTarget(G::kFramePump + 20, pump[5]) == G::kNetActiveQuery);
  uint32_t call;
  CHECK(ReadImageWords(image, at.stage_pick_call, &call, 1) && IsCall(call) &&
        BranchTarget(at.stage_pick_call, call) == at.wait);
  CHECK(ContainsWord(image, at.wait, at.wait + 0x80, kSetRequest));
  CHECK(ContainsWord(image, at.wait, at.wait + 0x80, kReadRequest));
  CHECK(Calls(image, at.wait, at.wait + 0x80, at.yield));
  CHECK(Calls(image, at.yield, at.yield + 0x20, G::kFramePump));
  uint32_t check[3];
  CHECK(ReadImageWords(image, G::kFrameTailNetCheck, check, 3));
  const uint32_t skipped_end = ConditionalTarget(G::kFrameTailNetCheck + 8, check[2]);
  CHECK(ContainsWord(image, G::kFrameTailNetCheck + 12, skipped_end, kSetRequest));
  CHECK(!ContainsWord(image, G::kCallback, G::kCallback + 0x400, kSetRequest));
}

// lis rD,hi ; addi rD,rD,lo
bool LoadsAddress(const fs::path& image, uint32_t begin, uint32_t end, uint32_t reg, uint32_t address) {
  const uint32_t lis = 0x3C000000 | reg << 21 | ((address + 0x8000) >> 16);
  const uint32_t addi = 0x38000000 | reg << 21 | reg << 16 | (address & 0xFFFF);
  return ContainsWord(image, begin, end, lis) && ContainsWord(image, begin, end, addi);
}

struct SetupSites {
  uint32_t stage_pick;
  uint32_t button_config;
};

// The setup words' fields are what the fight reads outside the snapshot: in online mode (game mode 3) the stage pick
// takes the stage from the online match data, and the input decode maps fighter slots 0 and 1 through one of two
// button tables, chosen by a flag.
template <typename G>
void CheckSetupWordsAgainstImage(const fs::path& image, const SetupSites& at) {
  uint32_t first;
  if (!ReadImageWords(image, at.stage_pick, &first, 1)) {
    std::printf("  SKIP %s setup words: %s not found\n", G::kName, image.string().c_str());
    return;
  }
  constexpr uint32_t kLoadStage = 0x814A0008;  // lwz r10,8(r10)
  constexpr uint32_t kOnlineMode = 0x2B0B0003;  // cmplwi cr6,r11,3
  constexpr uint32_t kReadFlag = 0x896B0022;  // lbz r11,34(r11)
  constexpr uint32_t kSlotTimes10 = 0x1D6B000A;  // mulli r11,r11,10
  constexpr uint32_t kSlotTimes8 = 0x556B1838;  // rlwinm r11,r11,3,0,28
  const uint32_t mode_read = 0x896B0000 | (G::kGameMode & 0xFFFF);  // lbz r11,mode(r11)
  CHECK(ContainsWord(image, at.stage_pick, at.stage_pick + 0x100, mode_read));
  CHECK(ContainsWord(image, at.stage_pick, at.stage_pick + 0x100, kOnlineMode));
  CHECK(LoadsAddress(image, at.stage_pick, at.stage_pick + 0x100, 10, G::kOnlineStage - 8));
  CHECK(ContainsWord(image, at.stage_pick, at.stage_pick + 0x100, kLoadStage));
  CHECK(LoadsAddress(image, at.button_config, at.button_config + 0x80, 11, G::kButtonConfigFlag - 34));
  CHECK(ContainsWord(image, at.button_config, at.button_config + 0x80, kReadFlag));
  CHECK(LoadsAddress(image, at.button_config, at.button_config + 0x80, 10, G::kButtonConfigA));
  CHECK(ContainsWord(image, at.button_config, at.button_config + 0x80, kSlotTimes10));
  CHECK(LoadsAddress(image, at.button_config, at.button_config + 0x80, 10, G::kButtonConfigB));
  CHECK(ContainsWord(image, at.button_config, at.button_config + 0x80, kSlotTimes8));
  CHECK(Calls(image, G::kInputDecode, G::kInputDecode + 0x200, at.button_config));
  CHECK(ContainsWord(image, G::kInputDecode, G::kInputDecode + 0x200, 0x38A00000));  // li r5,0
  CHECK(ContainsWord(image, G::kInputDecode, G::kInputDecode + 0x200, 0x38A00001));  // li r5,1
}

template <typename G>
void CheckSetupLayout() {
  const uint32_t snapshot_regions[][2] = {{G::kGlobalsSnapshot, kGlobalsSaveSize}, {G::kInputTable, kInputTableStateBytes},
                                          {G::kInputRing, kInputRingBytes},        {G::kRngState, kRngStateBytes},
                                          {G::kSideOrder, kSideOrderBytes}};
  const uint32_t setup_fields[][2] = {{G::kOnlineStage, 4},
                                      {G::kButtonConfigFlag, 1},
                                      {G::kButtonConfigA, 2 * 10},
                                      {G::kButtonConfigB, 2 * 8}};
  for (const auto& field : setup_fields) {
    for (const auto& region : snapshot_regions) {
      CHECK(field[0] + field[1] <= region[0] || field[0] >= region[0] + region[1]);
    }
  }
  CHECK(ButtonConfigAddress<G>(true, 1) == G::kButtonConfigA + 10);
  CHECK(ButtonConfigAddress<G>(false, 1) == G::kButtonConfigB + 8);
  CHECK(ButtonConfigAddress<G>(false, 0) == G::kButtonConfigB);
}

template <typename G>
void CheckLayout() {
  CHECK(G::kGlobalsSnapshot + kGlobalsSaveSize == G::kSnapshotPointer);
  CHECK(G::kCallback % 4 == 0 && G::kSimStep % 4 == 0 && G::kEffectsGate % 4 == 0 && G::kFightLoop % 4 == 0);
  CHECK(G::kArenaPointer % 4 == 0 && G::kSnapshotPointer % 4 == 0 && G::kFrameCounter % 4 == 0);
  CHECK(G::kInputTable % 4 == 0);
  CHECK(G::kNetActive >= 0x84000000);
  CHECK(G::kInputDecode % 4 == 0 && G::kInputHistoryStep % 4 == 0 && G::kInputRing % 4 == 0);
  CHECK(G::kInputRing >= kDiagWindowLow && G::kInputRing + kInputRingBytes <= kDiagWindowHigh);
  CHECK(G::kInputTable >= kDiagWindowLow && G::kInputTable + kInputTableStateBytes <= kDiagWindowHigh);
  CHECK(G::kGlobalsSnapshot >= kDiagWindowLow && G::kGlobalsSnapshot + kGlobalsSaveSize <= kDiagWindowHigh);
  CHECK(G::kFrameCounter >= kDiagWindowLow && G::kFrameCounter < kDiagWindowHigh);
  CHECK(kPlayersInTable * kInputEntryStride == 4 * 156);
  CHECK(G::kRandStep % 4 == 0 && G::kSideSetup % 4 == 0 && G::kRngState % 4 == 0 && G::kSideOrder % 4 == 0);
  CHECK(G::kRngState >= kDiagWindowLow && G::kRngState + kRngStateBytes <= kDiagWindowHigh);
  CHECK(G::kSideOrder >= kDiagWindowLow && G::kSideOrder + kSideOrderBytes <= kDiagWindowHigh);
  CHECK(G::kPaceCounter >= kDiagWindowLow && G::kPaceCounter < kDiagWindowHigh);
  const uint32_t restored[][2] = {{G::kInputTable, kInputTableStateBytes}, {G::kInputRing, kInputRingBytes},
                                  {G::kRngState, kRngStateBytes},          {G::kSideOrder, kSideOrderBytes},
                                  {G::kGlobalsSnapshot, kGlobalsSaveSize}};
  for (const auto& r : restored) {
    CHECK(G::kPaceCounter + 4 <= r[0] || G::kPaceCounter >= r[0] + r[1]);
    CHECK(G::kDrawListHigh <= r[0] || G::kDrawListLow >= r[0] + r[1]);
  }
  for (size_t i = 0; i < 5; ++i) {
    for (size_t j = i + 1; j < 5; ++j) {
      CHECK(restored[i][0] + restored[i][1] <= restored[j][0] || restored[j][0] + restored[j][1] <= restored[i][0]);
    }
  }
}

bool GeneratedContains(const char* tree, const std::string& needle) {
  const fs::path dir = Root() / "generated" / tree;
  std::error_code ec;
  if (!fs::exists(dir, ec)) return true;
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (entry.path().extension() != ".cpp") continue;
    std::ifstream in(entry.path());
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.find(needle) != std::string::npos) return true;
  }
  return false;
}

bool GeneratedDefines(const char* tree, const std::string& symbol) {
  return GeneratedContains(tree, "DEFINE_REX_FUNC(" + symbol + ")");
}

char Hex(uint32_t v) { return "0123456789ABCDEF"[v & 15]; }

std::string Name(const char* prefix, uint32_t address) {
  std::string s = std::string(prefix) + "sub_";
  for (int shift = 28; shift >= 0; shift -= 4) s += Hex(address >> shift);
  return s;
}

void TestTraitsAndImages() {
  std::printf("  traits\n");
  CheckLayout<Base013Netplay>();
  CheckLayout<TitleUpdate023Netplay>();
  CHECK(Base013Netplay::kGuest != TitleUpdate023Netplay::kGuest);
  CheckFingerprintsAgainstImage<Base013Netplay>(Root() / "game" / "analysis" / "default-0.0.1.3.image.bin");
  CheckFingerprintsAgainstImage<TitleUpdate023Netplay>(Root() / "game" / "analysis" / "default.image.bin");
  CheckInputRouteAgainstImage<Base013Netplay>(Root() / "game" / "analysis" / "default-0.0.1.3.image.bin", 0x823E92F4,
                                              0x823CB740);
  CheckInputRouteAgainstImage<TitleUpdate023Netplay>(Root() / "game" / "analysis" / "default.image.bin", 0x823E9A2C,
                                                     0x823CBD20);
  CHECK(Base013Netplay::kControllerInputTable != Base013Netplay::kInputTable);
  CHECK(TitleUpdate023Netplay::kControllerInputTable - Base013Netplay::kControllerInputTable == 0x518);
  CHECK(TitleUpdate023Netplay::kActiveUser - Base013Netplay::kActiveUser == 0x78);
  CheckFrameTailAgainstImage<Base013Netplay>(Root() / "game" / "analysis" / "default-0.0.1.3.image.bin");
  CheckFrameTailAgainstImage<TitleUpdate023Netplay>(Root() / "game" / "analysis" / "default.image.bin");
  CheckPresentationWaitAgainstImage<Base013Netplay>(Root() / "game" / "analysis" / "default-0.0.1.3.image.bin",
                                                    {0x823EDB18, 0x822427CC, 0x8237B020, 0x821AC648});
  CheckPresentationWaitAgainstImage<TitleUpdate023Netplay>(Root() / "game" / "analysis" / "default.image.bin",
                                                           {0x823EE208, 0x82242834, 0x8237B0F0, 0x821AC4A0});
  CheckSetupLayout<Base013Netplay>();
  CheckSetupLayout<TitleUpdate023Netplay>();
  CheckSetupWordsAgainstImage<Base013Netplay>(Root() / "game" / "analysis" / "default-0.0.1.3.image.bin",
                                              {0x82242588, 0x82250598});
  CheckSetupWordsAgainstImage<TitleUpdate023Netplay>(Root() / "game" / "analysis" / "default.image.bin",
                                                     {0x822425F0, 0x822505E0});
  CHECK(kCmdStep == 5);
}

void TestSetupWordsPacking() {
  std::printf("  setup words\n");
  SetupFields f;
  f.mode = 3;
  f.phase[0] = 2;
  f.phase[3] = 0x1234;
  f.stage = 7;
  const uint8_t p1[kButtonConfigBytes] = {1, 2, 3, 4, 5, 6};
  const uint8_t p2[kButtonConfigBytes] = {6, 5, 4, 3, 2, 1};
  std::memcpy(f.buttons[0], p1, sizeof p1);
  std::memcpy(f.buttons[1], p2, sizeof p2);
  const std::vector<uint32_t> words = SetupWords(f);
  const std::vector<uint32_t> expected = {3, 0x00020000, 0x00001234, 7, 0x01020304, 0x0506, 0x06050403, 0x0201};
  CHECK(words == expected);
  CHECK(words.size() == kSetupWordCount && kSetupWordCount <= kMaxSetupWords);
  CHECK(std::string(kSetupWordNames[3]) == "stage" && std::string(kSetupWordNames[6]) == "buttons_p2");
  SetupFields other = f;
  other.buttons[1][5] = 9;
  CHECK(SetupWords(other) != words);
}

// Only the fight loop's own tail is told NetSync is active, and only while the rollback runtime owns the fight; the
// frame pump's tails, the pump's own question, the random generator and the effects gate keep the real answer.
template <typename G>
void CheckFrameTailAnswer() {
  const uint32_t tail = FrameTailNetCheckReturn<G>();
  CHECK(AnswerFrameTailNetActive<G>(tail, false, true));
  CHECK(!AnswerFrameTailNetActive<G>(tail, true, true));
  CHECK(!AnswerFrameTailNetActive<G>(tail, false, false));
  CHECK(!AnswerFrameTailNetActive<G>(tail, true, false));
  CHECK(!AnswerFrameTailNetActive<G>(G::kFramePump + 24, false, true));
  CHECK(!AnswerFrameTailNetActive<G>(G::kFramePump + 24, true, true));
  CHECK(!AnswerFrameTailNetActive<G>(G::kFightLoop + 0x58, false, true));
  CHECK(!AnswerFrameTailNetActive<G>(0, false, true));
}

void TestFrameTailAnswerScope() {
  std::printf("  frame tail answer scope\n");
  CheckFrameTailAnswer<Base013Netplay>();
  CheckFrameTailAnswer<TitleUpdate023Netplay>();
}

void TestGeneratedTreesDefineTheHookedFunctions() {
  std::printf("  generated trees\n");
  CHECK(GeneratedDefines("base", Name("base013_", Base013Netplay::kCallback)));
  CHECK(GeneratedDefines("base", Name("base013_", Base013Netplay::kSimStep)));
  CHECK(GeneratedDefines("base", Name("base013_", Base013Netplay::kEffectsGate)));
  CHECK(GeneratedDefines("tu", Name("tu023_", TitleUpdate023Netplay::kCallback)));
  CHECK(GeneratedDefines("tu", Name("tu023_", TitleUpdate023Netplay::kSimStep)));
  CHECK(GeneratedDefines("tu", Name("tu023_", TitleUpdate023Netplay::kEffectsGate)));
  CHECK(GeneratedDefines("base", Name("base013_", Base013Netplay::kNetActiveQuery)));
  CHECK(GeneratedDefines("tu", Name("tu023_", TitleUpdate023Netplay::kNetActiveQuery)));
  // The net-active hook tells the frame tail's call apart by the return address the generated call sets.
  CHECK(GeneratedContains("base", "ctx.lr = 0x823EDF08;\n\t" + Name("base013_", Base013Netplay::kNetActiveQuery) +
                                      "(ctx, base);"));
  CHECK(GeneratedContains("tu", "ctx.lr = 0x823EE674;\n\t" + Name("tu023_", TitleUpdate023Netplay::kNetActiveQuery) +
                                    "(ctx, base);"));
  CHECK(FrameTailNetCheckReturn<Base013Netplay>() == 0x823EDF08);
  CHECK(FrameTailNetCheckReturn<TitleUpdate023Netplay>() == 0x823EE674);
  CHECK(GeneratedDefines("base", Name("base013_", Base013Netplay::kFramePump)));
  CHECK(GeneratedDefines("tu", Name("tu023_", TitleUpdate023Netplay::kFramePump)));
  CHECK(GeneratedContains("base", "ctx.lr = 0x82252BA4;\n\tbase013_sub_823EDB18(ctx, base);"));
  CHECK(GeneratedContains("tu", "ctx.lr = 0x82252BEC;\n\ttu023_sub_823EE208(ctx, base);"));
}

bool SourceHas(const std::string& text, const std::string& needle) { return text.find(needle) != std::string::npos; }

// The runtime steps every rollback frame through the game's step command, lets that command's own sim-step call
// through, owns the frame tail's net-active question in both images, and starts no fight before frame 0 agrees.
void TestHookRunsWholeGameFrames() {
  std::printf("  hook wiring\n");
  std::ifstream in(Root() / "src" / "netplay_hook.cpp");
  const std::string hook((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  CHECK(SourceHas(hook, "REX_HOOK_RAW(base013_sub_8241CC08)") && SourceHas(hook, "REX_EXTERN(__imp__base013_sub_8241CC08)"));
  CHECK(SourceHas(hook, "REX_HOOK_RAW(tu023_sub_8241D2E0)") && SourceHas(hook, "REX_EXTERN(__imp__tu023_sub_8241D2E0)"));
  CHECK(SourceHas(hook, "uint32_t(ctx.lr) == np::FrameTailNetCheckReturn<G>()"));
  CHECK(SourceHas(hook, "if (np::InRollbackFrame()) {"));
  CHECK(SourceHas(hook, "np::FrameKind::kRollbackFrame"));
  CHECK(SourceHas(hook, "config.verify_frame_zero = true;"));
  CHECK(SourceHas(hook, "live_sim_->NeutralInputs();"));
  CHECK(SourceHas(hook, "np::FrameZeroView(start)"));
  CHECK(SourceHas(hook, "DEBUG_SAULO netplay takeover refused reason={} generation={} fight={}"));
  CHECK(SourceHas(hook, "session_->OfferFrameZero(std::move(view), G::kGlobalsSnapshot, std::move(setup));"));
  CHECK(SourceHas(hook, "if (!adopted || !live_sim_->Load(blob)) return;"));
  CHECK(SourceHas(hook, "session_->ReofferFrameZero(std::move(view));"));
  CHECK(SourceHas(hook, "config.session_id = ssf2t::netplay::MatchSessionId(plan.generation, now_ms);"));
  CHECK(SourceHas(hook, "DEBUG_SAULO netplay setup local fight={} generation={}"));
  CHECK(SourceHas(hook, "DEBUG_SAULO post_fight state="));
  const size_t tu_guard = hook.find("#if SSF2T_TITLE_UPDATE_IMAGE\nREX_HOOK_RAW(tu023_");
  CHECK(tu_guard != std::string::npos && hook.find("REX_HOOK_RAW(tu023_sub_8241D2E0)") > tu_guard);
  CHECK(SourceHas(hook, "REX_HOOK_RAW(base013_sub_82252B90)") && SourceHas(hook, "REX_EXTERN(__imp__base013_sub_82252B90)"));
  CHECK(SourceHas(hook, "REX_EXTERN(__imp__tu023_sub_82252BD8)") &&
        hook.find("REX_HOOK_RAW(tu023_sub_82252BD8)") > tu_guard);
  CHECK(SourceHas(hook, "np::AnswerFrameTailNetActive<G>(uint32_t(ctx.lr), np::InFramePump(), owns)"));
  CHECK(SourceHas(hook, "++np::g_frame_pump_depth;\n  original(ctx, base);\n  --np::g_frame_pump_depth;"));
  CHECK(SourceHas(hook, "DEBUG_SAULO pre_fight presentation complete state="));
  CHECK(SourceHas(hook, "DEBUG_SAULO netplay takeover begin state="));
  CHECK(SourceHas(hook, "DEBUG_SAULO netplay frame_tail net_active_override="));
}

void TestWordsMatchOnAFakeGuest() {
  std::printf("  fingerprint matching\n");
  std::vector<uint8_t> memory(0x1000, 0);
  const auto store = [&](uint32_t offset, uint32_t word) {
    memory[offset] = uint8_t(word >> 24);
    memory[offset + 1] = uint8_t(word >> 16);
    memory[offset + 2] = uint8_t(word >> 8);
    memory[offset + 3] = uint8_t(word);
  };
  const uint32_t words[] = {0x7D8802A6, 0x9181FFF8, 0x9421FFA0};
  for (uint32_t i = 0; i < 3; ++i) store(0x100 + i * 4, words[i]);
  CHECK(WordsMatch(memory.data(), 0x100, words));
  store(0x108, 0x9421FFA1);
  CHECK(!WordsMatch(memory.data(), 0x100, words));
}

void TestBlobComparison() {
  std::printf("  blob comparison\n");
  std::vector<uint8_t> a(kGuestStateSize + 4, 7), b = a;
  CHECK(CompareBlobs(a, b).equal);
  b[kArenaSaveSize + 0x40] = 8;
  b[kArenaSaveSize + 0x41] = 9;
  b[3] = 1;
  const BlobDiff d = CompareBlobs(a, b);
  CHECK(!d.equal && d.first_offset == 3 && d.differing_bytes == 3 && d.hash_a != d.hash_b);
  const BlobDiff prefix = CompareBlobs(a, b, 3);
  CHECK(prefix.equal);
  std::vector<uint8_t> shorter(a.begin(), a.end() - 4);
  const BlobDiff s = CompareBlobs(a, shorter);
  CHECK(!s.equal && s.first_offset == shorter.size() && s.differing_bytes == 4);
  CHECK(CompareBlobs(a, shorter, shorter.size()).equal);
  CHECK(Fnv1a64(reinterpret_cast<const uint8_t*>("a"), 1) == 0xAF63DC4C8601EC8Cull);
  CHECK(Fnv1a64(nullptr, 0) == 14695981039346656037ull);
}

void TestOffsetNamesAndLogLines() {
  std::printf("  log lines\n");
  CHECK(DescribeOffset(0) == "arena+0x0");
  CHECK(DescribeOffset(kArenaSaveSize - 1) == "arena+0xFFFF");
  CHECK(DescribeOffset(kArenaSaveSize) == "globals+0x0");
  CHECK(DescribeOffset(kGuestStateSize - 1) == "globals+0x299FF");
  CHECK(DescribeOffset(kGuestStateSize) == "input_table+0x0");
  CHECK(DescribeOffset(kGuestStateSize + 8) == "input_table+0x8");
  CHECK(DescribeOffset(kGuestStateSize + kInputTableStateBytes - 1) == "input_table+0x26F");
  CHECK(DescribeOffset(kGuestStateSize + kInputTableStateBytes) == "input_ring+0x0");
  CHECK(DescribeOffset(kGuestStateSize + kInputTableStateBytes + kInputRingBytes - 1) == "input_ring+0x3F");
  CHECK(DescribeOffset(kGuestStateSize + kInputTableStateBytes + kInputRingBytes) == "rng+0x0");
  CHECK(DescribeOffset(kSimStateSize - kSideOrderBytes - 1) == "rng+0x83");
  CHECK(DescribeOffset(kSimStateSize - kSideOrderBytes) == "side_order+0x0");
  CHECK(DescribeOffset(kSimStateSize - 1) == "side_order+0xF");
  CHECK(DescribeOffset(kSimStateSize + 2) == "host+0x2");
  std::vector<uint8_t> blob(kSimBlobSize, 0);
  const std::string zero = DescribeStateHashes(blob);
  CHECK(zero.rfind("arena=", 0) == 0 && zero.find(" globals=") != std::string::npos &&
        zero.find(" table=") != std::string::npos && zero.find(" ring=") != std::string::npos &&
        zero.find(" rng=") != std::string::npos && zero.find(" side=") != std::string::npos &&
        zero.find(" prev=") != std::string::npos);
  blob[kSimStateSize - kSideOrderBytes - 1] = 1;
  const std::string rng = DescribeStateHashes(blob);
  CHECK(rng.substr(0, rng.find(" rng=")) == zero.substr(0, zero.find(" rng=")));
  CHECK(rng.substr(rng.find(" rng="), 13) != zero.substr(zero.find(" rng="), 13));
  CHECK(rng.substr(rng.find(" side=")) == zero.substr(zero.find(" side=")));
  CHECK(DescribeStateHashes(std::vector<uint8_t>(10)) == "unavailable");
  CHECK(kInputTableStateBytes == 624 && kInputRingBytes == 64);
  CHECK(kSimBlobSize == kSimStateSize + 4 && kSimStateSize == kGuestStateSize + 624 + 64 + 0x84 + 16);
  BlobDiff ok;
  CHECK(FormatCheck(123, "resim", ok) == "DEBUG_SAULO netplay frame=123 resim=match");
  BlobDiff bad;
  bad.equal = false;
  bad.first_offset = kArenaSaveSize + 0x10;
  bad.differing_bytes = 2;
  bad.hash_a = 0x1234;
  bad.hash_b = 0xABCDEF;
  CHECK(FormatCheck(7, "roundtrip", bad) ==
        "DEBUG_SAULO netplay frame=7 roundtrip=MISMATCH first=globals+0x10 bytes=2 hashA=0000000000001234 "
        "hashB=0000000000abcdef");
}

void TestRuns() {
  std::printf("  diff runs\n");
  std::vector<uint8_t> a(200, 0), b = a;
  CHECK(FindRuns(a, b, 16, 8).total == 0);
  b[4] = 1;
  b[10] = 1;
  b[100] = 1;
  b[199] = 1;
  const DiffRuns d = FindRuns(a, b, 16, 8);
  CHECK(d.total == 3 && d.runs.size() == 3);
  CHECK(d.runs[0].offset == 4 && d.runs[0].length == 7);
  CHECK(d.runs[1].offset == 100 && d.runs[1].length == 1);
  CHECK(d.runs[2].offset == 199 && d.runs[2].length == 1);
  const DiffRuns capped = FindRuns(a, b, 16, 2);
  CHECK(capped.total == 3 && capped.runs.size() == 2);
  CHECK(FormatRuns(9, "leak", 0x83C19FC0, d) ==
        "DEBUG_SAULO netplay frame=9 leak runs=3 83C19FC4+7 83C1A024+1 83C1A087+1");
  CHECK(FormatRuns(9, "post", 0x1000, capped) == "DEBUG_SAULO netplay frame=9 post runs=3 00001004+7 00001064+1 ...");
  CHECK(FormatRuns(9, "post", 0x1000, FindRuns(a, a, 16, 8)) == "DEBUG_SAULO netplay frame=9 post runs=0");
}

void TestSampleSchedule() {
  std::printf("  sample schedule\n");
  SampleSchedule s(120, 300, 3);
  CHECK(!s.ShouldSample(0));
  CHECK(!s.ShouldSample(119));
  CHECK(s.ShouldSample(120));
  s.Taken(120);
  CHECK(!s.ShouldSample(121));
  CHECK(!s.ShouldSample(419));
  CHECK(s.ShouldSample(420));
  s.Taken(420);
  s.Taken(900);
  CHECK(s.taken() == 3);
  CHECK(!s.ShouldSample(5000));
}

}  // namespace

int main() {
  std::printf("netplay guest tests\n");
  TestTraitsAndImages();
  TestGeneratedTreesDefineTheHookedFunctions();
  TestHookRunsWholeGameFrames();
  TestFrameTailAnswerScope();
  TestSetupWordsPacking();
  TestWordsMatchOnAFakeGuest();
  TestBlobComparison();
  TestOffsetNamesAndLogLines();
  TestRuns();
  TestSampleSchedule();
  std::printf("%d checks, %d failures\n", netplay_test::g_checks, netplay_test::g_failures);
  return netplay_test::g_failures == 0 ? 0 : 1;
}
