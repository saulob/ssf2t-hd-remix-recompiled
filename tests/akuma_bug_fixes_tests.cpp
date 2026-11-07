// The Akuma bug fixes option against the real frame data of both builds. The guest builds these tables in its startup
// code, so the bytes below were read after running that code; everything else is checked from production source.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "character/akuma_bug_fixes.h"

namespace {

using namespace ssf2t::character;

int failures = 0;

void Check(bool condition, const std::string& what) {
  if (condition) return;
  std::printf("FAIL %s\n", what.c_str());
  ++failures;
}

struct SparseMemory {
  std::map<uint32_t, uint8_t> bytes;
  std::vector<uint32_t> writes;

  uint8_t U8(uint32_t address) const {
    const auto it = bytes.find(address);
    return it == bytes.end() ? 0 : it->second;
  }
  uint16_t U16(uint32_t address) const { return uint16_t(U8(address) << 8 | U8(address + 1)); }
  uint32_t U32(uint32_t address) const { return uint32_t(U16(address)) << 16 | U16(address + 2); }
  void Put8(uint32_t address, uint8_t value) { bytes[address] = value; }
  void Put16(uint32_t address, uint16_t value) {
    Put8(address, uint8_t(value >> 8));
    Put8(address + 1, uint8_t(value));
  }
  void Put32(uint32_t address, uint32_t value) {
    Put16(address, uint16_t(value >> 16));
    Put16(address + 2, uint16_t(value));
  }
  void SetU8(uint32_t address, uint8_t value) {
    writes.push_back(address);
    Put8(address, value);
  }
};

struct Frame {
  uint16_t duration;
  uint8_t head, body, legs;
  uint8_t attack;
};

struct Build {
  const char* name;
  uint32_t remix_fighter_data;
  uint32_t remix_block;
  uint32_t remix_actions;
  uint32_t classic_block;
  uint32_t classic_actions;
};

constexpr uint32_t kFireballSubs = 0x194;
constexpr uint32_t kDragonPunchSubs = 0x1A0;
constexpr uint16_t kActionCount = 0x60;
constexpr uint32_t kFirstActionsInBlock = 0x10;
constexpr uint32_t kFourthWordInBlock = 0xC;
constexpr uint32_t kFrameAttackByte = 12;
constexpr uint16_t kUpBitOfTheGuestJumpTest = 8;
constexpr uint32_t kRyuRecords = 0x10000;
constexpr uint32_t kKenRecords = 0x20000;
constexpr uint16_t kFireballOffsets[] = {0x1354, 0x13B8, 0x141C, 0x1480, 0x1544, 0x1608};
constexpr uint16_t kDragonPunchOffsets[] = {0x17B4, 0x1878, 0x193C};

constexpr Build kBase013 = {"Base013", 0x83A37250, 0x8485C180, 0x8485FDBC, 0x8496C500, 0x8497013C};
constexpr Build kTu023 = {"TU023", 0x83A37250, 0x8485C6A0, 0x848602DC, 0x8496CA20, 0x8497065C};

const std::vector<Frame> kBlueFireball = {{4, 0, 0, 0, 0}, {4, 1, 1, 1, 0}, {4, 1, 1, 1, 0}, {20, 35, 26, 22, 0}};
const std::vector<Frame> kRedFireball = {{2, 0, 0, 0, 0}, {2, 0, 0, 0, 0}, {2, 1, 1, 1, 0}, {2, 1, 1, 1, 0},
                                         {2, 1, 1, 1, 0}, {4, 1, 1, 1, 0}, {4, 1, 1, 1, 0}, {20, 35, 26, 22, 0}};
const std::vector<std::vector<Frame>> kDragonPunches = {
    {{1, 0, 0, 0, 0},
     {3, 0, 0, 0, 54},
     {4, 0, 0, 0, 31},
     {14, 0, 0, 0, 32},
     {6, 37, 28, 24, 0},
     {2, 38, 28, 24, 0},
     {3, 39, 1, 1, 0},
     {8, 39, 1, 1, 0}},
    {{1, 0, 0, 0, 0},
     {3, 0, 0, 0, 55},
     {4, 0, 0, 0, 41},
     {22, 0, 0, 0, 42},
     {6, 37, 28, 24, 0},
     {2, 38, 28, 24, 0},
     {3, 39, 1, 1, 0},
     {8, 39, 1, 1, 0}},
    {{1, 0, 0, 0, 0},
     {4, 0, 0, 0, 56},
     {4, 0, 0, 0, 43},
     {26, 0, 0, 0, 44},
     {6, 37, 28, 24, 0},
     {2, 38, 28, 24, 0},
     {3, 39, 1, 1, 0},
     {8, 39, 1, 1, 0}},
};
const std::vector<std::vector<Frame>> kRyuDragonPunches = {
    {{4, 0, 0, 0, 0}, {4, 0, 0, 0, 31}, {10, 37, 0, 0, 32}},
    {{4, 0, 0, 0, 0}, {4, 0, 0, 0, 41}, {14, 37, 28, 0, 42}},
    {{4, 0, 0, 0, 0}, {4, 0, 0, 0, 43}, {18, 37, 28, 0, 44}},
};
const std::vector<std::vector<Frame>> kKenDragonPunches = {
    {{2, 0, 0, 0, 0}, {4, 0, 0, 0, 31}, {14, 37, 0, 0, 32}},
    {{2, 0, 0, 0, 0}, {4, 0, 0, 0, 41}, {22, 0, 0, 0, 42}},
};

void PutFrames(SparseMemory& memory, uint32_t record, const std::vector<Frame>& frames) {
  for (size_t i = 0; i < frames.size(); ++i) {
    const uint32_t at = record + uint32_t(i) * kAkumaFrameBytes;
    memory.Put16(at, frames[i].duration);
    memory.Put8(at + kAkumaHurtOffset, frames[i].head);
    memory.Put8(at + kAkumaHurtOffset + 1, frames[i].body);
    memory.Put8(at + kAkumaHurtOffset + 2, frames[i].legs);
    memory.Put8(at + kFrameAttackByte, frames[i].attack);
  }
}

void PutActions(SparseMemory& memory, uint32_t table) {
  memory.Put16(table, kActionCount);
  memory.Put16(table + kAkumaFireballAction, uint16_t(kFireballSubs));
  memory.Put16(table + kAkumaDragonPunchAction, uint16_t(kDragonPunchSubs));
  for (int sub = 0; sub < 6; ++sub) {
    memory.Put16(table + kFireballSubs + uint32_t(sub) * 2, kFireballOffsets[sub]);
    PutFrames(memory, table + kFireballSubs + kFireballOffsets[sub], sub < 3 ? kBlueFireball : kRedFireball);
  }
  for (int sub = 0; sub < 3; ++sub) {
    memory.Put16(table + kDragonPunchSubs + uint32_t(sub) * 2, kDragonPunchOffsets[sub]);
    PutFrames(memory, table + kDragonPunchSubs + kDragonPunchOffsets[sub], kDragonPunches[sub]);
  }
}

SparseMemory Guest(const Build& build) {
  SparseMemory memory;
  memory.Put32(build.remix_fighter_data + kAkumaFighterSlot * 4, build.remix_block);
  memory.Put32(build.remix_block, build.remix_block + kFirstActionsInBlock);
  memory.Put32(build.remix_block + kFourthWordInBlock, build.remix_actions);
  memory.Put32(build.classic_block + kFourthWordInBlock, build.classic_actions);
  PutActions(memory, build.remix_actions);
  PutActions(memory, build.classic_actions);
  for (uint32_t sub = 0; sub < kRyuDragonPunches.size(); ++sub) {
    PutFrames(memory, build.remix_block - kRyuRecords + sub * 0x100, kRyuDragonPunches[sub]);
  }
  for (uint32_t sub = 0; sub < kKenDragonPunches.size(); ++sub) {
    PutFrames(memory, build.remix_block - kKenRecords + sub * 0x100, kKenDragonPunches[sub]);
  }
  return memory;
}

Frame Read(const SparseMemory& memory, uint32_t table, uint8_t action, uint8_t sub, uint8_t index) {
  const uint32_t at = AkumaFrameAddress(memory, table, action, sub, index);
  return {memory.U16(at), memory.U8(at + kAkumaHurtOffset), memory.U8(at + kAkumaHurtOffset + 1),
          memory.U8(at + kAkumaHurtOffset + 2), memory.U8(at + kFrameAttackByte)};
}

bool Same(const Frame& a, const Frame& b) {
  return a.duration == b.duration && a.head == b.head && a.body == b.body && a.legs == b.legs && a.attack == b.attack;
}

bool Unhittable(const Frame& frame) { return frame.head == 0 && frame.body == 0 && frame.legs == 0; }

int TicksBeforeAttack(const std::vector<Frame>& frames) {
  int ticks = 0;
  for (const Frame& frame : frames) {
    if (frame.attack != 0) break;
    ticks += frame.duration;
  }
  return ticks;
}

int TotalTicks(const std::vector<Frame>& frames) {
  int ticks = 0;
  for (const Frame& frame : frames) ticks += frame.duration;
  return ticks;
}

void TestRedFireball(const Build& build) {
  const std::string name = build.name;
  SparseMemory memory = Guest(build);
  const uint32_t table = build.remix_actions;
  for (uint8_t sub = kAkumaRedFireballSub; sub < 6; ++sub) {
    Check(Unhittable(Read(memory, table, kAkumaFireballAction, sub, 0)) &&
              Unhittable(Read(memory, table, kAkumaFireballAction, sub, 1)),
          name + " the shipped red fireball has no hurt ids on its first two frames");
  }
  Check(SetAkumaFrames(memory, table, true), name + " the fix installs");
  for (uint8_t sub = kAkumaRedFireballSub; sub < 6; ++sub) {
    for (uint8_t i = 0; i < kRedFireball.size(); ++i) {
      const Frame frame = Read(memory, table, kAkumaFireballAction, sub, i);
      const Frame shipped = kRedFireball[i];
      Check(!Unhittable(frame), name + " no red fireball frame stays unhittable");
      Check(frame.duration == shipped.duration, name + " red fireball timing is unchanged");
      Check(frame.attack == shipped.attack, name + " red fireball attack ids are unchanged");
      if (i >= 2) Check(Same(frame, shipped), name + " red fireball frames after the startup are unchanged");
    }
    for (uint8_t i = 0; i < 2; ++i) {
      const Frame startup = Read(memory, table, kAkumaFireballAction, sub, i);
      Check(startup.head == 1 && startup.body == 1 && startup.legs == 1, name + " the startup becomes hurt ids 1,1,1");
    }
  }
  for (uint8_t sub = 0; sub < kAkumaRedFireballSub; ++sub) {
    for (uint8_t i = 0; i < kBlueFireball.size(); ++i) {
      Check(Same(Read(memory, table, kAkumaFireballAction, sub, i), kBlueFireball[i]),
            name + " the blue fireball is not touched");
    }
  }
}

void TestDragonPunch(const Build& build) {
  const std::string name = build.name;
  SparseMemory memory = Guest(build);
  const uint32_t table = build.remix_actions;
  for (uint8_t sub = 0; sub < 3; ++sub) {
    const Frame frame = Read(memory, table, kAkumaDragonPunchAction, sub, kAkumaEarlyDragonPunchFrame);
    Check(frame.attack == kAkumaEarlyDragonPunchAttacks[sub], name + " the shipped early hit is an Akuma-only id");
    Check(TicksBeforeAttack(kDragonPunches[sub]) == 1, name + " the shipped hit comes one tick in");
  }
  const std::vector<int> startup_after_fix = {4, 4, 5};
  Check(SetAkumaFrames(memory, table, true), name + " the fix installs");
  for (uint8_t sub = 0; sub < 3; ++sub) {
    std::vector<Frame> frames;
    for (uint8_t i = 0; i < kDragonPunches[sub].size(); ++i) {
      frames.push_back(Read(memory, table, kAkumaDragonPunchAction, sub, i));
    }
    Check(TicksBeforeAttack(frames) == startup_after_fix[sub], name + " the first hit comes after the startup pose");
    Check(TotalTicks(frames) == TotalTicks(kDragonPunches[sub]), name + " the move's length is unchanged");
    Check(frames[2].attack == kRyuDragonPunches[sub][1].attack, name + " the first hit is the id Ryu's move starts with");
    Check(frames[3].attack == kRyuDragonPunches[sub][2].attack, name + " the second hit is the id Ryu's move follows with");
    Check(Same(frames[0], kDragonPunches[sub][0]), name + " the first frame is unchanged");
    Check(frames[1].duration == kDragonPunches[sub][1].duration, name + " the startup pose keeps its duration");
    Check(Unhittable(frames[1]), name + " the Dragon Punch stays invulnerable");
    for (uint8_t i = 2; i < frames.size(); ++i) {
      Check(Same(frames[i], kDragonPunches[sub][i]), name + " the rest of the Dragon Punch is unchanged");
    }
  }
  Check(TicksBeforeAttack(kRyuDragonPunches[0]) == 4 && TicksBeforeAttack(kRyuDragonPunches[1]) == 4,
        "jab and strong match Ryu's startup pose");
}

void TestScope(const Build& build) {
  const std::string name = build.name;
  SparseMemory memory = Guest(build);
  const SparseMemory shipped = memory;
  const uint32_t table = RemixAkumaActions(memory, build.remix_fighter_data);
  Check(table == build.remix_actions, name + " the selected Remix Akuma table is found from the fighter data");
  Check(SetAkumaFrames(memory, table, true), name + " the fix installs");
  Check(memory.writes.size() == std::size(kAkumaFramePatches), name + " only the listed frames are written");
  for (const uint32_t address : memory.writes) {
    Check(address >= build.remix_actions && address < build.classic_block,
          name + " every write lands in the Remix Akuma table");
  }
  Check(build.classic_block > build.remix_actions, name + " the Classic block is separate");
  size_t changed = 0;
  for (const auto& [address, value] : shipped.bytes) {
    changed += memory.U8(address) != value ? 1 : 0;
    if (address >= build.classic_block) Check(memory.U8(address) == value, name + " Classic Akuma bytes are unchanged");
    if (address < build.remix_block) Check(memory.U8(address) == value, name + " Ryu and Ken bytes are unchanged");
  }
  Check(changed == std::size(kAkumaFramePatches), name + " nothing else in the guest bytes changes");
}

void TestSafety(const Build& build) {
  const std::string name = build.name;
  SparseMemory memory = Guest(build);
  const SparseMemory shipped = memory;
  const uint32_t table = build.remix_actions;
  Check(ReadAkumaFrameState(memory, table) == AkumaFrameState::kOriginal, name + " reads as original");
  Check(SetAkumaFrames(memory, table, false) && memory.writes.empty(), name + " Off writes nothing to original frames");
  Check(SetAkumaFrames(memory, table, true), name + " On installs");
  Check(ReadAkumaFrameState(memory, table) == AkumaFrameState::kFixed, name + " reads as fixed");
  const size_t writes = memory.writes.size();
  Check(SetAkumaFrames(memory, table, true) && memory.writes.size() == writes, name + " installing twice writes nothing");
  Check(SetAkumaFrames(memory, table, false), name + " the fix reverts");
  Check(memory.bytes == shipped.bytes, name + " reverting restores every byte");

  for (const AkumaFramePatch& patch : kAkumaFramePatches) {
    SparseMemory wrong = shipped;
    wrong.Put8(AkumaPatchAddress(wrong, table, patch), 0x7E);
    const SparseMemory before = wrong;
    Check(ReadAkumaFrameState(wrong, table) == AkumaFrameState::kUnknown, name + " one wrong byte reads as unknown");
    Check(!SetAkumaFrames(wrong, table, true), name + " one wrong byte rejects the whole fix");
    Check(!SetAkumaFrames(wrong, table, false), name + " one wrong byte rejects the revert");
    Check(wrong.writes.empty() && wrong.bytes == before.bytes, name + " a rejected table is left alone");
  }

  SparseMemory empty;
  Check(RemixAkumaActions(empty, build.remix_fighter_data) == 0, name + " an unset block has no table");
  Check(!SetAkumaFrames(empty, 0, true) && empty.writes.empty(), name + " a missing table is rejected");
  SparseMemory short_table = shipped;
  short_table.Put16(table, kAkumaDragonPunchAction);
  Check(!SetAkumaFrames(short_table, table, true), name + " a table without these actions is rejected");
}

constexpr DemonThrower kDashingAkuma = {true, kFighterStateSpecial, kAkumaDemonDashStep, kAkumaRagingDemonMove};

constexpr uint8_t kStateHit = 0xE;
constexpr uint8_t kStateJump = 4;

enum class Outcome { kEscaped, kCaught };

// Two consecutive frames around the end of blockstun, using the guest's rules: the opponent leaves blockstun in frame
// one and starts the jump in frame two, the throw test refuses state 0xE, the dash skips state 4, and each frame's
// coin flip decides who ticks first.
Outcome EndOfBlockstun(bool akuma_first_frame_one, bool akuma_first_frame_two, bool holding_up, bool fixed) {
  uint8_t state = kStateHit;
  const uint16_t input = holding_up ? kUpBitOfTheGuestJumpTest : 0;
  const auto catches = [&] {
    if (state == kStateHit || state == kStateJump) return false;
    return !(fixed && DemonWaitsForJump(kDashingAkuma, {state, 0, input}));
  };
  if (akuma_first_frame_one && catches()) return Outcome::kCaught;
  state = kFighterStateBlockStance;
  if (!akuma_first_frame_one && catches()) return Outcome::kCaught;
  if (akuma_first_frame_two && catches()) return Outcome::kCaught;
  if (holding_up) state = kStateJump;
  if (!akuma_first_frame_two && catches()) return Outcome::kCaught;
  return Outcome::kEscaped;
}

int Escapes(bool holding_up, bool fixed) {
  int escapes = 0;
  for (const bool first : {false, true}) {
    for (const bool second : {false, true}) {
      escapes += EndOfBlockstun(first, second, holding_up, fixed) == Outcome::kEscaped ? 1 : 0;
    }
  }
  return escapes;
}

void TestRagingDemon() {
  Check(Escapes(true, false) == 1, "the shipped order lets one of four coin flip pairs escape: 25%");
  Check(EndOfBlockstun(true, false, true, false) == Outcome::kEscaped, "the escaping pair is Akuma first, then last");
  Check(Escapes(true, true) == 4, "with the fix every order escapes");
  Check(Escapes(false, false) == 0 && Escapes(false, true) == 0, "without holding up the catch is unchanged");

  const DemonTarget jumping = {kFighterStateBlockStance, 0, kUpBitOfTheGuestJumpTest};
  Check(DemonWaitsForJump(kDashingAkuma, jumping), "the dash waits for a stance holding up");
  Check(!DemonWaitsForJump(kDashingAkuma, {kFighterStateBlockStance, 0, uint16_t(~kUpBitOfTheGuestJumpTest)}),
        "no wait without up");
  Check(!DemonWaitsForJump(kDashingAkuma, {kFighterStateBlockStance, 1, kUpBitOfTheGuestJumpTest}),
        "no wait when the jump input is locked out");
  Check(!DemonWaitsForJump(kDashingAkuma, {kStateHit, 0, kUpBitOfTheGuestJumpTest}), "hitstun handling is not touched");
  for (const uint8_t state : {uint8_t(2), uint8_t(4), uint8_t(6), uint8_t(0xA), uint8_t(0xC), uint8_t(0xE), uint8_t(0x14)}) {
    Check(!DemonWaitsForJump(kDashingAkuma, {state, 0, kUpBitOfTheGuestJumpTest}), "only block stance is affected");
  }
  Check(!DemonWaitsForJump({false, kFighterStateSpecial, kAkumaDemonDashStep, kAkumaRagingDemonMove}, jumping),
        "Evil Ryu's dash, on Ryu, is not touched");
  Check(!DemonWaitsForJump({true, 0xA, kAkumaDemonDashStep, kAkumaRagingDemonMove}, jumping), "normal throws are not touched");
  Check(!DemonWaitsForJump({true, kFighterStateSpecial, 4, kAkumaRagingDemonMove}, jumping), "only the dash step waits");
  Check(!DemonWaitsForJump({true, kFighterStateSpecial, kAkumaDemonDashStep, 8}, jumping), "other moves do not wait");
}

void TestPatchList() {
  SparseMemory memory = Guest(kBase013);
  std::map<uint32_t, int> seen;
  size_t red = 0;
  size_t dragon_punch = 0;
  for (const AkumaFramePatch& patch : kAkumaFramePatches) {
    ++seen[AkumaPatchAddress(memory, kBase013.remix_actions, patch)];
    Check(patch.original != patch.fixed, "every patch changes a value");
    if (patch.action == kAkumaFireballAction) ++red;
    if (patch.action == kAkumaDragonPunchAction) ++dragon_punch;
    Check(patch.action == kAkumaFireballAction || patch.action == kAkumaDragonPunchAction, "no other move is listed");
  }
  Check(seen.size() == std::size(kAkumaFramePatches), "no two patches overlap");
  Check(red == 18 && dragon_punch == 3, "6 red fireball frames x 3 hurt ids and 3 Dragon Punch frames");
  Check((red != 0) == kAkumaFixesWhenApplied.red_fireball, "the report matches the red fireball patches");
  Check((dragon_punch != 0) == kAkumaFixesWhenApplied.dragon_punch, "the report matches the Dragon Punch patches");
}

void TestPolicy() {
  Check(std::string(kAkumaBugFixesSetting) == "ssf2t_akuma_bug_fixes", "setting key");
  Check(std::string(kAkumaBugFixesLabel) == "Akuma bug fixes", "launcher label");
  Check(!kDefaultAkumaBugFixes, "default Off");
  for (const uint8_t mode : {kArcadeGameMode, kLocalVersusGameMode, kTrainingGameMode}) {
    Check(!AkumaBugFixesActive(false, mode, false), "Off stays off in every offline mode");
    Check(AkumaBugFixesActive(true, mode, false), "On applies in every offline mode");
    Check(!AkumaBugFixesActive(true, mode, true), "the developer Netplay Test plays the original game");
  }
  Check(!AkumaBugFixesActive(true, kOnlineSelectMode, false), "online plays the original game");
  Check(kAkumaFixesWhenApplied.raging_demon, "the Raging Demon gate is reported");
  Check(!kAkumaFixesWhenApplied.air_fireball, "the air fireball hit box is not claimed");
}

std::string Source(const std::string& path) {
  std::ifstream file(std::string(SSF2T_SOURCE_DIR) + "/" + path, std::ios::binary);
  Check(file.good(), "readable " + path);
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const std::string& what) { return text.find(what) != std::string::npos; }

void TestSources() {
  const std::string settings = Source("src/settings.cpp");
  Check(Has(settings, "kAkumaBugFixesSetting"), "the launcher captures and saves the setting");

  const std::string launcher = Source("src/launcher.cpp");
  const size_t row = launcher.find("character::kAkumaBugFixesLabel");
  Check(row != std::string::npos, "the Gameplay page has the row");
  Check(row > launcher.find("static void PageGameplay"), "the row is on the Gameplay page");
  Check(Has(launcher.substr(row, 200), "\"Off\", \"On\""), "the row offers Off and On");
  Check(Has(launcher.substr(row, 200), "character::kDefaultAkumaBugFixes"), "the row uses the default");

  const std::string pt_br = Source("src/localization/pt_br_ui_text.cpp");
  Check(Has(pt_br, std::string(kAkumaBugFixesLabel) + "\t"), "the label is translated");
  Check(Has(pt_br, std::string(kAkumaBugFixesHelp) + "\t"), "the help is translated");

  const std::string hooks = Source("src/character/akuma_bug_fixes.cpp");
  Check(Has(hooks, "REXCVAR_DEFINE_BOOL(ssf2t_akuma_bug_fixes, false"), "the cvar defaults to false");
  for (const char* hook : {"base013_sub_82389310", "tu023_sub_823893E0", "base013_sub_8238D028", "tu023_sub_8238D018"}) {
    Check(Has(hooks, std::string("REX_HOOK_RAW(") + hook), std::string("hooked ") + hook);
  }
  Check(Has(hooks, "if (!option && !g_fixes_active) return Report(Reported::kOff);"),
        "Off with untouched frames returns before reading the guest tables");
  Check(Has(hooks, "if (!g_fixes_active || ClassicStyle(*GuestHost(base, H::kHdStyle))) return original(ctx, base);"),
        "the catch gate is off when the option is, and in Classic");
  Check(!Has(hooks, "AkumaEnabled"), "Enable Akuma and this option stay independent");
  Check(Has(hooks, "raging_demon={} red_fireball={} dragon_punch={} air_fireball={}"), "the install is reported");
  for (const char* projectile : {"83B574D4", "84AF94D8", "83A4F6D4", "0xF9", "kAirFireball"}) {
    Check(!Has(hooks, projectile) && !Has(Source("src/character/akuma_bug_fixes.h"), projectile),
          std::string("the unresolved air fireball data is not touched: ") + projectile);
  }

  Check(!Has(Source("src/character/akuma.cpp"), "kAkumaBugFixes"), "Enable Akuma does not read this option");
  Check(!Has(Source("src/online_runtime.cpp"), "kAkumaBugFixes"), "online negotiation is unchanged");
  Check(!Has(Source("src/character/secret_select.h"), "AkumaBugFixes"), "character selection is unchanged");

  for (const char* other : {"src/character/ultra_moves_hook.cpp", "src/character/ultra_hook_table.h",
                            "src/character/versus_cpu_hook.cpp", "src/netplay_hook.cpp"}) {
    const std::string text = Source(other);
    for (const char* address : {"82389310", "823893E0", "8238D028", "8238D018"}) {
      Check(!Has(text, address), std::string("no second hook in ") + other);
    }
  }

  const std::string readme = Source("README.md");
  Check(Has(readme, "### Akuma bug fixes"), "the README has the section");
  Check(Has(readme, "https://www.sirlin.net/sf-hdr/akuma"), "the README links the article");
  Check(Has(readme, "https://github.com/saulob/ssf2t-hd-remix-recompiled/issues/13"), "the README links the issue");
  Check(Has(readme, "| Air fireball: oversized hit box behind the projectile | Not corrected yet |"),
        "the README does not claim the air fireball");
}

}  // namespace

int main() {
  TestPolicy();
  TestPatchList();
  TestRagingDemon();
  for (const Build& build : {kBase013, kTu023}) {
    TestRedFireball(build);
    TestDragonPunch(build);
    TestScope(build);
    TestSafety(build);
  }
  TestSources();
  if (failures != 0) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("akuma bug fixes tests passed\n");
  return 0;
}
