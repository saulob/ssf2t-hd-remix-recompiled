#include <array>
#include <cstdint>

#include <rex/hook.h>
#include <rex/system/kernel_state.h>

#include "character/evil_ryu_actions.h"
#include "character/evil_ryu_audio.h"
#include "character/evil_ryu_moves.h"
#include "character/ultra_audio.h"
#include "character/ultra_navigation.h"
#include "character/ultra_fight.h"
#include "character/violent_ken_actions.h"
#include "guest_memory.h"
#include "netplay/fight_snapshot.h"
#include "netplay/guest_netplay.h"
#include "title_update_image.h"

REX_EXTERN(__imp__base013_sub_82265228);
REX_EXTERN(__imp__base013_sub_82265040);
REX_EXTERN(__imp__base013_sub_8225E1F0);
REX_EXTERN(__imp__base013_sub_821C01F8);
REX_EXTERN(__imp__base013_sub_821C1E70);
REX_EXTERN(__imp__base013_sub_823A03A0);
REX_EXTERN(__imp__base013_sub_823A0660);
REX_EXTERN(__imp__base013_sub_823A2F58);
REX_EXTERN(__imp__base013_sub_823A4860);
REX_EXTERN(__imp__base013_sub_823A5DF8);
REX_EXTERN(__imp__base013_sub_8228E440);
REX_EXTERN(__imp__base013_sub_8228E488);
REX_EXTERN(__imp__base013_sub_8228D890);
REX_EXTERN(__imp__base013_sub_8228D8F8);
REX_EXTERN(__imp__base013_sub_8228D960);
REX_EXTERN(__imp__base013_sub_82195568);
REX_EXTERN(__imp__base013_sub_821B41F8);
REX_EXTERN(__imp__base013_sub_8218BB30);
REX_EXTERN(base013_sub_82377AC0);
REX_EXTERN(base013_sub_823146C0);
REX_EXTERN(base013_sub_823782B0);
REX_EXTERN(base013_sub_82383400);
REX_EXTERN(base013_sub_82386E20);
REX_EXTERN(__imp__base013_sub_82264F78);
REX_EXTERN(base013_sub_82391B10);
REX_EXTERN(base013_sub_82379AB0);
#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_82265698);
REX_EXTERN(__imp__tu023_sub_822654B0);
REX_EXTERN(__imp__tu023_sub_8225E228);
REX_EXTERN(__imp__tu023_sub_821C0318);
REX_EXTERN(__imp__tu023_sub_821C1F90);
REX_EXTERN(__imp__tu023_sub_823A0390);
REX_EXTERN(__imp__tu023_sub_823A0650);
REX_EXTERN(__imp__tu023_sub_823A2F48);
REX_EXTERN(__imp__tu023_sub_823A4850);
REX_EXTERN(__imp__tu023_sub_823A5DE8);
REX_EXTERN(__imp__tu023_sub_8228ED10);
REX_EXTERN(__imp__tu023_sub_8228ED58);
REX_EXTERN(__imp__tu023_sub_8228E160);
REX_EXTERN(__imp__tu023_sub_8228E1C8);
REX_EXTERN(__imp__tu023_sub_8228E230);
REX_EXTERN(__imp__tu023_sub_821953A0);
REX_EXTERN(__imp__tu023_sub_821B4318);
REX_EXTERN(__imp__tu023_sub_8218B998);
REX_EXTERN(tu023_sub_82377B90);
REX_EXTERN(tu023_sub_82314600);
REX_EXTERN(tu023_sub_82378380);
REX_EXTERN(tu023_sub_823834D0);
REX_EXTERN(tu023_sub_82386EF0);
REX_EXTERN(__imp__tu023_sub_822653E8);
REX_EXTERN(tu023_sub_82391118);
REX_EXTERN(tu023_sub_82379B80);
#endif

namespace ssf2t::character {
namespace {

struct Base013Moves {
  using Netplay = netplay::Base013Netplay;
  static constexpr uint32_t kActionTables = 0x83BEDDC0;
  static void RagingDemonStep(PPCContext& ctx, uint8_t* base) { base013_sub_823146C0(ctx, base); }
  static void QueueSound(PPCContext& ctx, uint8_t* base) { base013_sub_823782B0(ctx, base); }
  static void ClampToWalls(PPCContext& ctx, uint8_t* base) { base013_sub_82383400(ctx, base); }
  static void ReturnToIdle(PPCContext& ctx, uint8_t* base) { base013_sub_82386E20(ctx, base); }
  static void MoveStarted(PPCContext& ctx, uint8_t* base) { base013_sub_82391B10(ctx, base); }
  static void AdvanceAnimation(PPCContext& ctx, uint8_t* base) { base013_sub_82379AB0(ctx, base); }
  static void SuperFreeze(PPCContext& ctx, uint8_t* base) { base013_sub_82377AC0(ctx, base); }
};

#if SSF2T_TITLE_UPDATE_IMAGE
struct TitleUpdate023Moves {
  using Netplay = netplay::TitleUpdate023Netplay;
  static constexpr uint32_t kActionTables = 0x83BEDE40;
  static void RagingDemonStep(PPCContext& ctx, uint8_t* base) { tu023_sub_82314600(ctx, base); }
  static void QueueSound(PPCContext& ctx, uint8_t* base) { tu023_sub_82378380(ctx, base); }
  static void ClampToWalls(PPCContext& ctx, uint8_t* base) { tu023_sub_823834D0(ctx, base); }
  static void ReturnToIdle(PPCContext& ctx, uint8_t* base) { tu023_sub_82386EF0(ctx, base); }
  static void MoveStarted(PPCContext& ctx, uint8_t* base) { tu023_sub_82391118(ctx, base); }
  static void AdvanceAnimation(PPCContext& ctx, uint8_t* base) { tu023_sub_82379B80(ctx, base); }
  static void SuperFreeze(PPCContext& ctx, uint8_t* base) { tu023_sub_82377B90(ctx, base); }
};
#endif

constexpr uint32_t kActionD1Below = 0x11;
constexpr uint32_t kActionD0Below = 0x0D;
constexpr uint32_t kActionPointerAbove = 0x14;
constexpr uint32_t kActionTableIndexBelow = 0x26;
constexpr uint32_t kEmittedObjectAbove = 0x10;
constexpr uint8_t kClassSpecial = 12;
constexpr uint8_t kRagingDemonMoveId = 10;
constexpr uint8_t kTeleportMoveId = 14;
constexpr uint8_t kSenpuMoveId = kTeleportMoveId;
constexpr uint8_t kShakunetsuMoveId = 6;
constexpr uint8_t kFireballWindupStep = 2;
constexpr uint8_t kFireballRecoveryStep = 4;
constexpr uint8_t kUniqueHeavyPunch = 8;
constexpr uint8_t kCloseHeavyPunch = 4;
constexpr uint8_t kFarHeavyPunch = 5;
constexpr uint16_t kCloseHeavyPunchRange = 0x29;
constexpr uint8_t kAkumaSlot = 16;
constexpr uint8_t kClassicSlotOffset = 17;
constexpr uint8_t kDemonArmStep = 2;
constexpr uint8_t kDemonWaitStep = 4;
constexpr uint8_t kDemonDashStep = 6;
constexpr uint8_t kDemonCaughtStep = 8;
constexpr uint8_t kFullMeter = 0x30;
constexpr uint8_t kStandAction = 0x00;
constexpr uint8_t kCrouchAction = 0x02;
constexpr uint8_t kStandSubs = 4;

constexpr uint32_t kState = 3;
constexpr uint32_t kStep = 4;
constexpr uint32_t kSubStep = 5;
constexpr uint32_t kFacing = 0x12;
constexpr uint32_t kAnimationFlags = 0x14;
constexpr uint32_t kAnimationEvent = 0x15;
constexpr uint32_t kStepTimer = 0x1A;
constexpr uint32_t kDemonTimeout = 0x1B;
constexpr uint32_t kAnimationRecord = 0x1C;
constexpr uint32_t kBoxPack = 0x34;
constexpr uint32_t kWallFlag = 0x48;
constexpr uint32_t kSlide = 0x52;
constexpr uint32_t kMoveId = 0x88;
constexpr uint32_t kPositionX = 8;
constexpr uint32_t kPositionY = 0xC;
constexpr uint32_t kVelocity = 0x3C;
constexpr uint32_t kVelocityY = 0x3E;
constexpr uint32_t kDeceleration = 0x40;
constexpr uint32_t kDecelerationY = 0x42;
constexpr uint32_t kHealth = 0x2C;
constexpr uint32_t kHealthMax = 0x2E;
constexpr uint32_t kUntargetable = 0x181;
constexpr uint32_t kMoveStart = 0x18B;
constexpr uint32_t kMoveLatch = 0x16D;
constexpr uint32_t kMeter = 0x2B4;
constexpr uint32_t kCountdown = 0x47;
constexpr uint32_t kCharacterSlot = 0x39D;
constexpr uint32_t kClassicFlag = 0x3B6;
constexpr uint32_t kRuleset = 0x382;
constexpr uint32_t kBusy = 0x49;
constexpr uint32_t kHitConfirmed = 0x195;
constexpr uint32_t kOpponentFacing = 0x1BE;
constexpr uint32_t kInputRelative = 0x3A4;
constexpr uint32_t kInputRelativePrevious = 0x3A6;
constexpr uint32_t kPadCurrent = 0x390;
constexpr uint32_t kPadPrevious = 0x392;
constexpr uint32_t kTatsuStrength = 0x93;
constexpr uint32_t kShakunetsuStrength = 0xE3;
constexpr uint32_t kWalkRow = 0x30;
constexpr uint32_t kAttackSelection = 0x180;
constexpr uint32_t kOpponentRange = 0x182;
constexpr uint16_t kAnimationEnd = 0x8000;
constexpr uint32_t kFrameDuration = 0;
constexpr uint32_t kSuperField62 = 0x62;
constexpr uint32_t kSuperField70 = 0x70;
constexpr uint32_t kOpponentPause = 0x1FA;
constexpr uint32_t kSuperFreezing = 0x2AD;
constexpr uint32_t kSuperActive = 0x2B5;
constexpr uint32_t kCpuSpecialArmed = 0x148;
constexpr uint32_t kCpuSpecialId = 0x186;
constexpr uint32_t kCpuScriptByte = 0x205;
constexpr uint32_t kCpuScriptFallback = 0x206;
constexpr uint32_t kCpuScriptArgument = 0x207;
constexpr uint8_t kCpuSpecialScripted = 2;
constexpr int kCpuRetries = 4;

uint8_t U8(const uint8_t* base, uint32_t address) { return *GuestHost(base, address); }
void SetU8(uint8_t* base, uint32_t address, uint8_t value) { GuestStore8(base, address, value); }
uint16_t U16(const uint8_t* base, uint32_t address) { return LoadBE16(GuestHost(base, address), 0); }

template <typename H>
uint32_t ActiveFighter(const uint8_t* base) {
  return LoadBE32(GuestHost(base, H::Netplay::kFightWork - 4), 0);
}

template <typename H>
uint32_t GameState(const uint8_t* base) {
  return LoadBE32(GuestHost(base, H::Netplay::kFightWork), 0);
}

template <typename H>
int EvilSide(const uint8_t* base, uint32_t fighter) {
  return EvilRyuSide<typename H::Netplay>(base, fighter);
}

template <typename H>
bool CanStartMove(const uint8_t* base, uint32_t fighter, bool super, bool teleport) {
  const uint32_t game = GameState<H>(base);
  const uint8_t state = U8(base, fighter + kState);
  if (state == kClassSpecial || state == 14 || state == 20) return false;
  if (U8(base, fighter + kRuleset) == 0 || U8(base, fighter + kBusy) != 0) return false;
  if (U8(base, fighter + kUntargetable) != 0 || U8(base, fighter + kClassicFlag) != 0) return false;
  if (U16(base, fighter + kHealth) != U16(base, fighter + kHealthMax)) return false;
  if (U8(base, game + 0x3A5) != 0 || U8(base, game + 0x584) != 0 || U8(base, game + 0x590) != 0) return false;
  if (super && U8(base, fighter + kMeter) != kFullMeter) return false;
  if (teleport && (state == 10 || (state == 4 && U8(base, fighter + kStep) == 6)) &&
      U8(base, fighter + kHitConfirmed) == 0) {
    return false;
  }
  return true;
}

template <typename H>
void BeginMove(PPCContext& ctx, uint8_t* base, uint32_t fighter, uint8_t move_id) {
  SetU8(base, fighter + kState, kClassSpecial);
  SetU8(base, fighter + kStep, 0);
  SetU8(base, fighter + kSubStep, 0);
  SetU8(base, fighter + kMoveId, move_id);
  SetU8(base, fighter + kMoveStart, 1);
  SetU8(base, fighter + kMoveLatch, 1);
  const uint32_t game = GameState<H>(base);
  GuestStoreBE16(base, game + 0x594, uint16_t(U16(base, game + 0x594) + 1));
  const PPCContext saved = ctx;
  H::MoveStarted(ctx, base);
  ctx = saved;
}

void ClearVelocity(uint8_t* base, uint32_t fighter) {
  GuestStoreBE16(base, fighter + kVelocity, 0);
  GuestStoreBE16(base, fighter + kVelocityY, 0);
  GuestStoreBE16(base, fighter + kDeceleration, 0);
  GuestStoreBE16(base, fighter + kDecelerationY, 0);
}

void EndHostMove(uint8_t* base, uint32_t fighter, int side) {
  HostMove& move = GlobalHostMoves().For(side);
  if (move.kind == HostMoveKind::kTeleport) {
    SetU8(base, fighter + kUntargetable, 0);
    GuestStoreBE16(base, fighter + kSlide, 0);
    ClearVelocity(base, fighter);
  }
  if (move.kind == HostMoveKind::kShinbu) SetU8(base, fighter + kOpponentPause, 0);
  const GroundHeight ground = move.ground;
  move = HostMove{};
  move.ground = ground;
}

template <typename H>
uint32_t ActionTableAddress(const uint8_t* base, uint32_t fighter) {
  uint8_t slot = U8(base, fighter + kCharacterSlot);
  if (U8(base, fighter + kRuleset) != 0 && U8(base, fighter + kClassicFlag) != 0) slot += kClassicSlotOffset;
  return LoadBE32(GuestHost(base, H::kActionTables + uint32_t(slot) * 4), 0);
}

uint32_t GuestAlloc(uint32_t bytes) { return REX_KERNEL_MEMORY()->SystemHeapAlloc(bytes); }

template <typename H>
const EvilActionTable* TableFor(uint8_t* base, uint32_t fighter) {
  const uint32_t address = ActionTableAddress<H>(base, fighter);
  if (address < 0x10000 || address >= 0xFFFF0000u) return nullptr;
  auto& tables = GlobalEvilActionTables();
  for (const EvilActionTable& table : tables) {
    if (table.table() == address) return &table;
  }
  static std::array<uint32_t, 2> memory{};
  static size_t next = 0;
  const size_t slot = next++ % tables.size();
  if (memory[slot] == 0) memory[slot] = GuestAlloc(kEvilActionBytes);
  if (memory[slot] == 0 || !tables[slot].Build(base, address, memory[slot], kEvilActionBytes)) return nullptr;
  return &tables[slot];
}

// The fighter reads its boxes through its own pack pointer, so swapping it changes Evil Ryu without touching Ryu.
void UseEvilPack(uint8_t* base, uint32_t fighter) {
  static std::array<uint32_t, 2> source{};
  static std::array<uint32_t, 2> memory{};
  static size_t next = 0;
  const uint32_t pack = LoadBE32(GuestHost(base, fighter + kBoxPack), 0);
  if (pack < 0x10000 || pack >= 0xFFFF0000u || pack == memory[0] || pack == memory[1]) return;
  for (size_t i = 0; i < source.size(); ++i) {
    if (source[i] == pack) return GuestStoreBE32(base, fighter + kBoxPack, memory[i]);
  }
  const size_t slot = next++ % source.size();
  if (memory[slot] == 0) memory[slot] = GuestAlloc(kEvilPackBytes);
  if (memory[slot] == 0) return;
  BuildEvilPack(base, pack, memory[slot]);
  source[slot] = pack;
  GuestStoreBE32(base, fighter + kBoxPack, memory[slot]);
}

struct WalkRows {
  std::array<uint32_t, 2> source{};
  std::array<uint32_t, 2> memory{};
  size_t next = 0;
};

// Walk speeds are read through the fighter's own row pointer, so the carrier keeps the shared table.
void UseWalkRow(uint8_t* base, uint32_t fighter, WalkRows& rows, int16_t forward, int16_t back) {
  const uint32_t row = LoadBE32(GuestHost(base, fighter + kWalkRow), 0);
  if (row < 0x10000 || row >= 0xFFFF0000u || row == rows.memory[0] || row == rows.memory[1]) return;
  for (size_t i = 0; i < rows.source.size(); ++i) {
    if (rows.source[i] == row) return GuestStoreBE32(base, fighter + kWalkRow, rows.memory[i]);
  }
  const size_t slot = rows.next++ % rows.source.size();
  if (rows.memory[slot] == 0) rows.memory[slot] = GuestAlloc(kEvilWalkBytes);
  if (rows.memory[slot] == 0 || !BuildWalkRow(base, row, rows.memory[slot], forward, back)) return;
  rows.source[slot] = row;
  GuestStoreBE32(base, fighter + kWalkRow, rows.memory[slot]);
}

void UseEvilWalk(uint8_t* base, uint32_t fighter) {
  static WalkRows rows;
  UseWalkRow(base, fighter, rows, kEvilWalkForward, kEvilWalkBack);
}

void UseViolentWalk(uint8_t* base, uint32_t fighter) {
  static WalkRows rows;
  UseWalkRow(base, fighter, rows, kViolentWalkForward, kViolentWalkBack);
}

template <typename H>
bool StartTeleport(PPCContext& ctx, uint8_t* base, uint32_t fighter, int side, int variant) {
  const EvilActionTable* table = TableFor<H>(base, fighter);
  if (!table || table->Teleport() == 0) return false;
  HostMove& move = GlobalHostMoves().For(side);
  BeginMove<H>(ctx, base, fighter, kTeleportMoveId);
  ClearVelocity(base, fighter);
  SetFighterAnimation(base, fighter, table->Teleport());
  move.kind = HostMoveKind::kTeleport;
  move.phase = HostMovePhase::kStartup;
  move.variant = int8_t(variant);
  return true;
}

// Queued as that side's Hadoken so it takes the voice channel, volume and pan of a real voice; the sound hook then swaps
// the clip.
template <typename H>
void QueueRagingDemonVoice(PPCContext& ctx, uint8_t* base, int side) {
  if (!MarkRagingDemonVoice(side)) return;
  const PPCContext saved = ctx;
  ctx.r3.u64 = 0;
  ctx.r4.u64 = side == 0 ? kRyuHadokenSound : kRyuHadokenSound + kPlayerTwoVoiceOffset;
  ctx.r5.u64 = 0;
  ctx.r6.u64 = 0;
  H::QueueSound(ctx, base);
  ctx = saved;
}

template <typename H>
void QueueViolentMoveVoice(PPCContext& ctx, uint8_t* base, int side, ViolentMoveVoice voice) {
  if (!MarkViolentMoveVoice(side, voice)) return;
  const PPCContext saved = ctx;
  ctx.r3.u64 = 0;
  ctx.r4.u64 = side == 0 ? kKenHadokenSound : kKenHadokenSound + kPlayerTwoVoiceOffset;
  ctx.r5.u64 = 0;
  ctx.r6.u64 = 0;
  H::QueueSound(ctx, base);
  ctx = saved;
}

template <typename H>
bool StartRagingDemon(PPCContext& ctx, uint8_t* base, uint32_t fighter, int side) {
  if (!TableFor<H>(base, fighter)) return false;
  BeginMove<H>(ctx, base, fighter, kRagingDemonMoveId);
  GlobalHostMoves().For(side).kind = HostMoveKind::kRagingDemon;
  QueueRagingDemonVoice<H>(ctx, base, side);
  return true;
}

template <typename H>
const ViolentActionTable* ViolentTableFor(uint8_t* base, uint32_t fighter) {
  const uint32_t address = ActionTableAddress<H>(base, fighter);
  const ViolentKenGameplay* gameplay = LoadedViolentKenGameplay();
  if (!gameplay || address < 0x10000 || address >= 0xFFFF0000u) return nullptr;
  auto& tables = GlobalViolentActionTables();
  for (const ViolentActionTable& table : tables) {
    if (table.table() == address) return &table;
  }
  static std::array<uint32_t, 2> memory{};
  static size_t next = 0;
  const size_t slot = next++ % tables.size();
  if (memory[slot] == 0) memory[slot] = GuestAlloc(kViolentActionBytes);
  if (memory[slot] == 0 || !tables[slot].Build(base, address, memory[slot], kViolentActionBytes, *gameplay)) {
    return nullptr;
  }
  return &tables[slot];
}

// The fighter reads its boxes through its own pack pointer, so swapping it changes Violent Ken without touching Ken.
void UseViolentPack(uint8_t* base, uint32_t fighter) {
  static std::array<uint32_t, 2> source{};
  static std::array<uint32_t, 2> memory{};
  static size_t next = 0;
  const ViolentKenGameplay* gameplay = LoadedViolentKenGameplay();
  const uint32_t pack = LoadBE32(GuestHost(base, fighter + kBoxPack), 0);
  if (!gameplay || pack < 0x10000 || pack >= 0xFFFF0000u || pack == memory[0] || pack == memory[1]) return;
  for (size_t i = 0; i < source.size(); ++i) {
    if (source[i] == pack) return GuestStoreBE32(base, fighter + kBoxPack, memory[i]);
  }
  const size_t slot = next++ % source.size();
  if (memory[slot] == 0) memory[slot] = GuestAlloc(kViolentPackBytes);
  if (memory[slot] == 0) return;
  BuildViolentPack(base, pack, memory[slot], *gameplay);
  source[slot] = pack;
  GuestStoreBE32(base, fighter + kBoxPack, memory[slot]);
}

int32_t FacingSign(const uint8_t* base, uint32_t fighter) { return U8(base, fighter + kFacing) != 0 ? 1 : -1; }

template <typename H>
bool StartViolentMove(PPCContext& ctx, uint8_t* base, uint32_t fighter, int side, HostMoveKind kind, uint8_t action,
                      uint8_t sub) {
  const ViolentActionTable* table = ViolentTableFor<H>(base, fighter);
  const uint32_t record = table ? table->Find(action, sub) : 0;
  if (record == 0) return false;
  BeginMove<H>(ctx, base, fighter, kTeleportMoveId);
  ClearVelocity(base, fighter);
  SetFighterAnimation(base, fighter, record);
  HostMove& move = GlobalHostMoves().For(side);
  move.kind = kind;
  move.phase = HostMovePhase::kDash;
  move.ticks = 0;
  move.ground_y = int32_t(LoadBE32(GuestHost(base, fighter + kPositionY), 0));
  return true;
}

template <typename H>
bool StartRasetsu(PPCContext& ctx, uint8_t* base, uint32_t fighter, int side, int strength) {
  if (!StartViolentMove<H>(ctx, base, fighter, side, HostMoveKind::kRasetsu, kRasetsuAction, uint8_t(strength))) {
    return false;
  }
  const RasetsuDash dash = RasetsuDashFor(strength, U8(base, fighter + kFacing) != 0);
  GlobalHostMoves().For(side).dash = {dash.velocity, dash.deceleration, dash.threshold};
  QueueViolentMoveVoice<H>(ctx, base, side, ViolentMoveVoice::kRasetsu);
  return true;
}

template <typename H>
bool StartShinbu(PPCContext& ctx, uint8_t* base, uint32_t fighter, int side) {
  if (!StartViolentMove<H>(ctx, base, fighter, side, HostMoveKind::kShinbu, kShinbuAction, kShinbuRushSub)) return false;
  // Ken's Shoryureppa start: the guest's own freeze, flash and meter reset.
  SetU8(base, fighter + kSuperField62, 8);
  SetU8(base, fighter + kSuperField70, 0xA);
  SetU8(base, fighter + kOpponentPause, 1);
  const PPCContext saved = ctx;
  H::SuperFreeze(ctx, base);
  ctx = saved;
  HostMove& move = GlobalHostMoves().For(side);
  move.phase = HostMovePhase::kFreeze;
  move.dash.velocity = kShinbuRushVelocity;
  move.tail = 0;
  QueueViolentMoveVoice<H>(ctx, base, side, ViolentMoveVoice::kShinbu);
  return true;
}

template <typename H>
bool OnNeutralRecord(const uint8_t* base, uint32_t fighter, const ViolentActionTable& table) {
  const uint32_t record = LoadBE32(GuestHost(base, fighter + kAnimationRecord), 0);
  const uint32_t guest = table.SourceFrame(record) ? table.SourceFrame(record) : record;
  for (uint8_t action : {kStandAction, kCrouchAction}) {
    const uint32_t subs = table.table() + U16(base, table.table() + action);
    for (uint8_t sub = 0; sub < kStandSubs; ++sub) {
      const uint32_t start = subs + U16(base, subs + uint32_t(sub) * 2);
      const uint32_t offset = guest - start;
      if (guest >= start && offset < 8 * kActionFrameBytes && offset % kActionFrameBytes == 0) return true;
    }
  }
  return false;
}

// The guest's ground line is not a constant the port knows. Heights are not sampled while the guest holds the frame.
template <typename H>
void TrackGround(const uint8_t* base, uint32_t fighter, HostMove& move) {
  const uint32_t game = GameState<H>(base);
  const bool frozen = U8(base, game + 0x3A5) != 0 || U8(base, game + 0x584) != 0 || U8(base, game + 0x590) != 0;
  ObserveGround(move.ground, int32_t(LoadBE32(GuestHost(base, fighter + kPositionY), 0)), frozen);
}

template <typename H>
void DetectViolentMoves(PPCContext& ctx, uint8_t* base, uint32_t fighter, int side) {
  HostMove& move = GlobalHostMoves().For(side);
  const uint8_t nibble = uint8_t(U16(base, fighter + kInputRelative) & 0xF);
  const uint16_t current = U16(base, fighter + kPadCurrent);
  const uint16_t previous = U16(base, fighter + kPadPrevious);
  const int strength = move.rasetsu.Update(nibble, current, previous);
  const bool shinbu = move.shinbu.Update(nibble, current, previous);
  const bool inazuma = InazumaInput(nibble, current, previous);
  const int32_t y = int32_t(LoadBE32(GuestHost(base, fighter + kPositionY), 0));
  if (AboveGround(move.ground, y)) return;
  if (shinbu && CanStartMove<H>(base, fighter, true, false)) {
    StartShinbu<H>(ctx, base, fighter, side);
  } else if (strength >= 0 && CanStartMove<H>(base, fighter, false, true)) {
    StartRasetsu<H>(ctx, base, fighter, side, strength);
  } else if (inazuma && CanStartMove<H>(base, fighter, false, true)) {
    const ViolentActionTable* table = ViolentTableFor<H>(base, fighter);
    if (table && OnNeutralRecord<H>(base, fighter, *table)) {
      StartViolentMove<H>(ctx, base, fighter, side, HostMoveKind::kInazuma, kInazumaAction, 0);
    }
  }
}

template <typename H>
bool OnEvilNeutralRecord(const uint8_t* base, uint32_t fighter, const EvilActionTable& table) {
  const uint32_t record = LoadBE32(GuestHost(base, fighter + kAnimationRecord), 0);
  const uint32_t guest = table.SourceFrame(record) ? table.SourceFrame(record) : record;
  for (uint8_t action : {kStandAction, kCrouchAction}) {
    const uint32_t subs = table.table() + U16(base, table.table() + action);
    for (uint8_t sub = 0; sub < kStandSubs; ++sub) {
      const uint32_t start = subs + U16(base, subs + uint32_t(sub) * 2);
      const uint32_t offset = guest - start;
      if (guest >= start && offset < 8 * kActionFrameBytes && offset % kActionFrameBytes == 0) return true;
    }
  }
  return false;
}

template <typename H>
bool StartSenpuKyaku(PPCContext& ctx, uint8_t* base, uint32_t fighter, int side) {
  const EvilActionTable* table = TableFor<H>(base, fighter);
  const uint32_t record = table ? table->Find(kUniqueAction, kSenpuSub, 0) : 0;
  if (record == 0) return false;
  BeginMove<H>(ctx, base, fighter, kSenpuMoveId);
  ClearVelocity(base, fighter);
  SetFighterAnimation(base, fighter, record);
  HostMove& move = GlobalHostMoves().For(side);
  move.kind = HostMoveKind::kSenpuKyaku;
  move.phase = HostMovePhase::kStartup;
  move.ground_y = int32_t(LoadBE32(GuestHost(base, fighter + kPositionY), 0));
  return true;
}

template <typename H>
void DetectMoves(PPCContext& ctx, uint8_t* base, uint32_t fighter, int side) {
  HostMove& move = GlobalHostMoves().For(side);
  const uint16_t relative = U16(base, fighter + kInputRelative);
  const int variant =
      move.teleport.Update(uint8_t(relative & 0xF), U16(base, fighter + kPadCurrent), U16(base, fighter + kPadPrevious));
  const bool demon = move.raging_demon.Update(relative, U16(base, fighter + kInputRelativePrevious));
  const bool senpu =
      SenpuInput(uint8_t(relative & 0xF), U16(base, fighter + kPadCurrent), U16(base, fighter + kPadPrevious));
  if (demon && CanStartMove<H>(base, fighter, true, false)) {
    StartRagingDemon<H>(ctx, base, fighter, side);
  } else if (variant >= 0 && CanStartMove<H>(base, fighter, false, true)) {
    StartTeleport<H>(ctx, base, fighter, side, variant);
  } else if (senpu && CanStartMove<H>(base, fighter, false, true)) {
    const EvilActionTable* table = TableFor<H>(base, fighter);
    if (table && OnEvilNeutralRecord<H>(base, fighter, *table)) StartSenpuKyaku<H>(ctx, base, fighter, side);
  }
}

// Runs before the guest's own special trackers so the three-button teleports win over the single-button moves.
template <typename H>
void TrackSpecials(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (!UltraFightersEnabled()) return original(ctx, base);
  const uint32_t fighter = ActiveFighter<H>(base);
  const int side = EvilSide<H>(base, fighter);
  if (side < 0) return original(ctx, base);
  UseEvilPack(base, fighter);
  UseEvilWalk(base, fighter);
  TrackGround<H>(base, fighter, GlobalHostMoves().For(side));
  if (GlobalHostMoves().Active(side) && U8(base, fighter + kState) != kClassSpecial) {
    EndHostMove(base, fighter, side);
  }
  if (!GlobalHostMoves().Active(side)) DetectMoves<H>(ctx, base, fighter, side);
  original(ctx, base);
}

// Ken has his own tracker and special runner in the per-character tables 83A29498 and 83A291D8; Ryu's never see him.
template <typename H>
void TrackKenSpecials(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (!UltraFightersEnabled()) return original(ctx, base);
  const uint32_t fighter = ActiveFighter<H>(base);
  const int violent = ViolentKenSide<typename H::Netplay>(base, fighter);
  if (violent < 0) return original(ctx, base);
  UseViolentPack(base, fighter);
  UseViolentWalk(base, fighter);
  TrackGround<H>(base, fighter, GlobalHostMoves().For(violent));
  if (GlobalHostMoves().Active(violent) && U8(base, fighter + kState) != kClassSpecial) {
    EndHostMove(base, fighter, violent);
  }
  if (!GlobalHostMoves().Active(violent)) DetectViolentMoves<H>(ctx, base, fighter, violent);
  original(ctx, base);
}

// Evil Ryu's half circle is USF2's 63214; everyone else keeps the guest's. Only this one tracker sees him mirrored.
template <typename H>
void TrackHalfCircle(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (!UltraFightersEnabled()) return original(ctx, base);
  const uint32_t fighter = ActiveFighter<H>(base);
  if (EvilSide<H>(base, fighter) < 0) return original(ctx, base);
  const uint16_t input = U16(base, fighter + kInputRelative);
  GuestStoreBE16(base, fighter + kInputRelative, MirrorDirections(input));
  original(ctx, base);
  GuestStoreBE16(base, fighter + kInputRelative, input);
}

// The three kick commands are the only writers of move ids 8, 10, 12 and of their deferred form at +0x16A.
template <typename H>
void TrackKenKickCommand(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (UltraFightersEnabled() && ViolentKenSide<typename H::Netplay>(base, ActiveFighter<H>(base)) >= 0) return;
  original(ctx, base);
}

// A scripted CPU special the character does not have takes the guest's own refusal: the script moves to its next byte.
template <typename H>
void SelectCpuMove(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  original(ctx, base);
  if (!UltraFightersEnabled()) return;
  const uint32_t fighter = ActiveFighter<H>(base);
  if (ViolentKenSide<typename H::Netplay>(base, fighter) < 0) return;
  for (int retry = 0; retry < kCpuRetries; ++retry) {
    if (U8(base, fighter + kCpuSpecialArmed) != kCpuSpecialScripted) return;
    if (!ViolentCpuSpecialRemoved(U8(base, fighter + kCpuSpecialId))) return;
    SetU8(base, fighter + kCpuSpecialId, 0);
    SetU8(base, fighter + kCpuScriptByte, U8(base, fighter + kCpuScriptFallback));
    SetU8(base, fighter + kCpuScriptArgument, 0);
    original(ctx, base);
  }
  SetU8(base, fighter + kCpuSpecialArmed, 0);
  SetU8(base, fighter + kCpuSpecialId, 0);
}

// The guest's jump table is one runtime block for all fighters, so the row is replaced after the guest has stored it.
// The human and CPU jump starts share this.
template <typename H>
void StartJump(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  original(ctx, base);
  if (!UltraFightersEnabled()) return;
  const uint32_t fighter = ActiveFighter<H>(base);
  if (ViolentKenSide<typename H::Netplay>(base, fighter) < 0) return;
  const int32_t sign = U8(base, fighter + kFacing) != 0 ? 1 : -1;
  const JumpRow row = ViolentJumpRow(sign * int16_t(U16(base, fighter + kVelocity)));
  GuestStoreBE16(base, fighter + kVelocity, uint16_t(sign * row.vx));
  GuestStoreBE16(base, fighter + kDeceleration, uint16_t(sign * row.ax));
  GuestStoreBE16(base, fighter + kVelocityY, uint16_t(row.vy));
  GuestStoreBE16(base, fighter + kDecelerationY, uint16_t(row.ay));
}

template <typename H>
void AdvanceAnimation(PPCContext& ctx, uint8_t* base) {
  const PPCContext saved = ctx;
  H::AdvanceAnimation(ctx, base);
  ctx = saved;
}

template <typename H>
void StepTeleportDash(PPCContext& ctx, uint8_t* base, uint32_t fighter, HostMove& move) {
  int32_t position = int32_t(LoadBE32(GuestHost(base, fighter + kPositionX), 0));
  const bool finished = move.dash.Step(position, U8(base, fighter + kWallFlag));
  GuestStoreBE32(base, fighter + kPositionX, uint32_t(position));
  const PPCContext saved = ctx;
  H::ClampToWalls(ctx, base);
  ctx = saved;
  SetU8(base, fighter + kUntargetable, 1);
  SetU8(base, fighter + kFacing, U8(base, fighter + kOpponentFacing));
  if (!finished) return;
  move.phase = HostMovePhase::kTail;
  move.tail = kTeleportTailFrames;
}

template <typename H>
void FinishHostMove(PPCContext& ctx, uint8_t* base, uint32_t fighter, int side) {
  EndHostMove(base, fighter, side);
  const PPCContext saved = ctx;
  H::ReturnToIdle(ctx, base);
  ctx = saved;
}

template <typename H>
void StepTeleport(PPCContext& ctx, uint8_t* base, uint32_t fighter, int side) {
  HostMove& move = GlobalHostMoves().For(side);
  ++move.ticks;
  switch (move.phase) {
    case HostMovePhase::kStartup: {
      const uint8_t event = U8(base, fighter + kAnimationEvent);
      if (event < 0x80) {
        GuestStoreBE16(base, fighter + kSlide, uint16_t(TeleportSlide(event)));
        return AdvanceAnimation<H>(ctx, base);
      }
      const DashParameters dash = DashFor(move.variant, U8(base, fighter + kFacing) != 0);
      GuestStoreBE16(base, fighter + kSlide, 0);
      SetU8(base, fighter + kUntargetable, 1);
      move.dash = {dash.velocity, dash.deceleration, dash.threshold};
      move.phase = HostMovePhase::kDash;
      return;
    }
    case HostMovePhase::kDash:
      return StepTeleportDash<H>(ctx, base, fighter, move);
    case HostMovePhase::kTail:
      SetU8(base, fighter + kFacing, U8(base, fighter + kOpponentFacing));
      if (--move.tail > 0) return;
      SetU8(base, fighter + kUntargetable, 0);
      move.phase = HostMovePhase::kRecovery;
      return AdvanceAnimation<H>(ctx, base);
    case HostMovePhase::kRecovery:
      if ((U16(base, fighter + kAnimationFlags) & kAnimationEnd) == 0) return AdvanceAnimation<H>(ctx, base);
      return FinishHostMove<H>(ctx, base, fighter, side);
    default:
      return;
  }
}

void MoveFighter(uint8_t* base, uint32_t fighter, uint32_t axis, int32_t delta) {
  const int32_t position = int32_t(LoadBE32(GuestHost(base, fighter + axis), 0));
  GuestStoreBE32(base, fighter + axis, uint32_t(position + delta));
}

template <typename H>
void ClampFighter(PPCContext& ctx, uint8_t* base) {
  const PPCContext saved = ctx;
  H::ClampToWalls(ctx, base);
  ctx = saved;
}

bool AnimationEnded(const uint8_t* base, uint32_t fighter) {
  return (U16(base, fighter + kAnimationFlags) & kAnimationEnd) != 0;
}

void LandSenpuKyaku(uint8_t* base, uint32_t fighter, const HostMove& move) {
  GuestStoreBE32(base, fighter + kPositionY, uint32_t(move.ground_y));
  SetU8(base, fighter + kUntargetable, 0);
  ClearVelocity(base, fighter);
}

// USF2 cActionControl18 category 57: the frame event starts the hop, and the hold frame waits for the landing.
template <typename H>
void StepSenpuKyaku(PPCContext& ctx, uint8_t* base, uint32_t fighter, int side) {
  HostMove& move = GlobalHostMoves().For(side);
  if (++move.ticks >= kSenpuTimeoutTicks) {
    LandSenpuKyaku(base, fighter, move);
    return FinishHostMove<H>(ctx, base, fighter, side);
  }
  switch (move.phase) {
    case HostMovePhase::kStartup:
      AdvanceAnimation<H>(ctx, base);
      if (U8(base, fighter + kAnimationEvent) == 0) return;
      move.phase = HostMovePhase::kDash;
      move.dash.velocity = kSenpuHopVelocityX;
      move.rise_velocity = kSenpuHopVelocityY;
      SetU8(base, fighter + kUntargetable, 1);
      return;
    case HostMovePhase::kDash: {
      const EvilActionTable* table = TableFor<H>(base, fighter);
      const uint32_t start = table ? table->Find(kUniqueAction, kSenpuSub, 0) : 0;
      if (start == 0) {
        LandSenpuKyaku(base, fighter, move);
        return FinishHostMove<H>(ctx, base, fighter, side);
      }
      move.dash.velocity -= kSenpuHopDecelerationX;
      move.rise_velocity -= kSenpuHopDecelerationY;
      MoveFighter(base, fighter, kPositionX, FacingSign(base, fighter) * move.dash.velocity * 256);
      MoveFighter(base, fighter, kPositionY, move.rise_velocity * 256);
      ClampFighter<H>(ctx, base);
      const uint32_t hold = start + kSenpuHoldFrame * kActionFrameBytes;
      const bool landed = int32_t(LoadBE32(GuestHost(base, fighter + kPositionY), 0)) <= move.ground_y;
      if (landed) GuestStoreBE32(base, fighter + kPositionY, uint32_t(move.ground_y));
      if (LoadBE32(GuestHost(base, fighter + kAnimationRecord), 0) != hold) return AdvanceAnimation<H>(ctx, base);
      if (!landed) return;
      LandSenpuKyaku(base, fighter, move);
      move.phase = HostMovePhase::kRecovery;
      SetFighterAnimation(base, fighter, hold + kActionFrameBytes);
      return;
    }
    default:
      if (AnimationEnded(base, fighter)) return FinishHostMove<H>(ctx, base, fighter, side);
      return AdvanceAnimation<H>(ctx, base);
  }
}

template <typename H>
void StepInazuma(PPCContext& ctx, uint8_t* base, uint32_t fighter, int side) {
  HostMove& move = GlobalHostMoves().For(side);
  if (AnimationEnded(base, fighter) || ++move.ticks >= kUltraMoveTimeoutTicks) {
    return FinishHostMove<H>(ctx, base, fighter, side);
  }
  AdvanceAnimation<H>(ctx, base);
}

// USF2 category 73: the first frame holds while the speed falls to the threshold; the mover is never stopped, so the
// remaining speed carries through the recovery.
template <typename H>
void StepRasetsu(PPCContext& ctx, uint8_t* base, uint32_t fighter, int side) {
  HostMove& move = GlobalHostMoves().For(side);
  if (++move.ticks >= kUltraMoveTimeoutTicks) return FinishHostMove<H>(ctx, base, fighter, side);
  if (move.phase == HostMovePhase::kDash) {
    const int32_t carried = int16_t(move.dash.velocity - move.dash.deceleration);
    int32_t position = int32_t(LoadBE32(GuestHost(base, fighter + kPositionX), 0));
    const bool finished = move.dash.Step(position, U8(base, fighter + kWallFlag));
    GuestStoreBE32(base, fighter + kPositionX, uint32_t(position));
    ClampFighter<H>(ctx, base);
    if (finished) {
      move.phase = HostMovePhase::kRecovery;
      move.rise_velocity = carried;
    }
    return;
  }
  if (AnimationEnded(base, fighter)) return FinishHostMove<H>(ctx, base, fighter, side);
  const int32_t sign = move.rise_velocity < 0 ? -1 : 1;
  move.rise_velocity = sign * DecayTowardZero(sign * move.rise_velocity, kRasetsuDecelerationMagnitude);
  MoveFighter(base, fighter, kPositionX, move.rise_velocity * 256);
  ClampFighter<H>(ctx, base);
  AdvanceAnimation<H>(ctx, base);
}

template <typename H>
void ShowShinbuAction(uint8_t* base, uint32_t fighter, uint8_t sub) {
  if (const ViolentActionTable* table = ViolentTableFor<H>(base, fighter)) {
    if (const uint32_t record = table->Find(kShinbuAction, sub)) SetFighterAnimation(base, fighter, record);
  }
}

// True once the last frame's duration has run out, which is when USF2 raises its animation-finished flag at +0x6D.
bool ShinbuFrameRunOut(const uint8_t* base, uint32_t fighter, HostMove& move) {
  if (!AnimationEnded(base, fighter)) return false;
  if (move.tail == 0) {
    const uint32_t record = LoadBE32(GuestHost(base, fighter + kAnimationRecord), 0);
    move.tail = uint8_t(U16(base, record + kFrameDuration));
  }
  return move.tail == 0 || --move.tail == 0;
}

// USF2 cActionControl19 category 78, steps 0, 1, 2, 5, 6, 7, 8 of table 0xb9c3cc.
template <typename H>
void StepShinbu(PPCContext& ctx, uint8_t* base, uint32_t fighter, int side) {
  HostMove& move = GlobalHostMoves().For(side);
  if (move.phase == HostMovePhase::kFreeze) {
    if (U8(base, fighter + kSuperFreezing) != 0) return;
    SetU8(base, fighter + kOpponentPause, 0);
    SetU8(base, fighter + kSuperActive, 1);
    move.phase = HostMovePhase::kDash;
    return;
  }
  if (++move.ticks >= kUltraMoveTimeoutTicks) return FinishHostMove<H>(ctx, base, fighter, side);
  const int32_t sign = FacingSign(base, fighter);
  switch (move.phase) {
    case HostMovePhase::kDash:
      if (ShinbuFrameRunOut(base, fighter, move)) {
        move.phase = HostMovePhase::kRiseStartup;
        move.dash.velocity = kShinbuRiseVelocityX;
        move.rise_velocity = kShinbuRiseVelocityY;
        ShowShinbuAction<H>(base, fighter, kShinbuRiseSub);
        return;
      }
      MoveFighter(base, fighter, kPositionX, sign * move.dash.velocity * 256);
      ClampFighter<H>(ctx, base);
      move.dash.velocity = DecayTowardZero(move.dash.velocity, kShinbuRushDeceleration);
      break;
    case HostMovePhase::kRiseStartup:
      if (U8(base, fighter + kAnimationEvent) != 0) break;
      move.phase = HostMovePhase::kRise;
      SetU8(base, fighter + kUntargetable, 1);
      [[fallthrough]];
    case HostMovePhase::kRise: {
      MoveFighter(base, fighter, kPositionX, sign * move.dash.velocity * 256);
      MoveFighter(base, fighter, kPositionY, move.rise_velocity * 256);
      ClampFighter<H>(ctx, base);
      move.dash.velocity = DecayTowardZero(move.dash.velocity, kShinbuRiseDecelerationX);
      move.rise_velocity -= kShinbuRiseDecelerationY;
      const int32_t y = int32_t(LoadBE32(GuestHost(base, fighter + kPositionY), 0));
      if (move.rise_velocity < 0 && y <= move.ground_y) {
        GuestStoreBE32(base, fighter + kPositionY, uint32_t(move.ground_y));
        SetU8(base, fighter + kUntargetable, 0);
        move.phase = HostMovePhase::kLand;
        ShowShinbuAction<H>(base, fighter, kShinbuLandSub);
        return;
      }
      break;
    }
    case HostMovePhase::kLand:
      if (AnimationEnded(base, fighter)) {
        move.phase = HostMovePhase::kRecovery;
        ShowShinbuAction<H>(base, fighter, kShinbuRecoverySub);
        return;
      }
      break;
    default:
      if (AnimationEnded(base, fighter)) return FinishHostMove<H>(ctx, base, fighter, side);
      break;
  }
  if (!AnimationEnded(base, fighter)) AdvanceAnimation<H>(ctx, base);
}

template <typename H>
void StepViolentMove(PPCContext& ctx, uint8_t* base, uint32_t fighter, int side) {
  const uint8_t countdown = U8(base, fighter + kCountdown);
  if (countdown != 0) return SetU8(base, fighter + kCountdown, uint8_t(countdown - 1));
  switch (GlobalHostMoves().For(side).kind) {
    case HostMoveKind::kRasetsu: return StepRasetsu<H>(ctx, base, fighter, side);
    case HostMoveKind::kInazuma: return StepInazuma<H>(ctx, base, fighter, side);
    case HostMoveKind::kShinbu: return StepShinbu<H>(ctx, base, fighter, side);
    default: return FinishHostMove<H>(ctx, base, fighter, side);
  }
}

void ArmDemonDash(uint8_t* base, uint32_t fighter) {
  const bool forward = U8(base, fighter + kFacing) != 0;
  const int16_t velocity = forward ? kDemonDashVelocity : int16_t(-kDemonDashVelocity);
  const int16_t deceleration = forward ? kDemonDashDeceleration : int16_t(-kDemonDashDeceleration);
  GuestStoreBE16(base, fighter + kVelocity, uint16_t(velocity));
  GuestStoreBE16(base, fighter + kDeceleration, uint16_t(deceleration));
  SetU8(base, fighter + kStepTimer, kDemonStartupFrames);
  SetU8(base, fighter + kDemonTimeout, kDemonDashTimeout);
}

// The guest's own raging demon runs the freeze, grab, blackout and damage; USF2's dash replaces its slower one.
template <typename H>
void StepRagingDemon(PPCContext& ctx, uint8_t* base, uint32_t fighter) {
  const uint8_t before = U8(base, fighter + kStep);
  H::RagingDemonStep(ctx, base);
  const uint8_t after = U8(base, fighter + kStep);
  if (before == kDemonArmStep && after == kDemonWaitStep) {
    ArmDemonDash(base, fighter);
  } else if (before == kDemonWaitStep) {
    AdvanceAnimation<H>(ctx, base);
  } else if (before == kDemonDashStep && after == kDemonDashStep) {
    if (DemonDashSpent(int16_t(U16(base, fighter + kVelocity)))) SetU8(base, fighter + kDemonTimeout, 1);
  } else if (before == kDemonDashStep && after == kDemonCaughtStep) {
    if (const EvilActionTable* table = TableFor<H>(base, fighter)) {
      const uint32_t start = table->Find(kRagingDemonAction, 0, 0);
      if (start != 0) SetFighterAnimation(base, fighter, start + kDemonCaughtFrame * kActionFrameBytes);
    }
  }
}

template <typename H>
void RunSpecialClass(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (!UltraFightersEnabled()) return original(ctx, base);
  const uint32_t fighter = ActiveFighter<H>(base);
  const int side = EvilSide<H>(base, fighter);
  if (side < 0) return original(ctx, base);
  if (!GlobalHostMoves().Active(side)) {
    const uint8_t step = U8(base, fighter + kStep);
    original(ctx, base);
    // The guest has just picked its own Hadoken recovery; USF2 gives the half circle fireball a longer one.
    if (U8(base, fighter + kMoveId) == kShakunetsuMoveId && step == kFireballWindupStep &&
        U8(base, fighter + kStep) == kFireballRecoveryStep) {
      SetU8(base, fighter + kStepTimer, ShakunetsuRecovery(U8(base, fighter + kShakunetsuStrength)));
    }
    return;
  }
  if (GlobalHostMoves().For(side).kind == HostMoveKind::kTeleport) return StepTeleport<H>(ctx, base, fighter, side);
  const uint8_t countdown = U8(base, fighter + kCountdown);
  if (countdown != 0) return SetU8(base, fighter + kCountdown, uint8_t(countdown - 1));
  if (GlobalHostMoves().For(side).kind == HostMoveKind::kSenpuKyaku) return StepSenpuKyaku<H>(ctx, base, fighter, side);
  StepRagingDemon<H>(ctx, base, fighter);
}

template <typename H>
void RunKenSpecialClass(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (!UltraFightersEnabled()) return original(ctx, base);
  const uint32_t fighter = ActiveFighter<H>(base);
  const int violent = ViolentKenSide<typename H::Netplay>(base, fighter);
  if (violent < 0) return original(ctx, base);
  if (GlobalHostMoves().Active(violent)) return StepViolentMove<H>(ctx, base, fighter, violent);
  if (!ViolentMoveRemoved(U8(base, fighter + kMoveId))) return original(ctx, base);
  FinishHostMove<H>(ctx, base, fighter, violent);
}

template <typename H>
bool LookupViolentAction(PPCContext& ctx, uint8_t* base, PPCFunc* original, uint32_t fighter) {
  const ViolentActionTable* table = ViolentTableFor<H>(base, fighter);
  if (!table) return false;
  const uint32_t work = H::Netplay::kFightWork;
  const uint32_t guest_table = LoadBE32(GuestHost(base, H::kActionTables + U16(base, work - kActionTableIndexBelow)), 0);
  if (guest_table != table->table()) return false;
  const uint8_t action = U8(base, work - kActionD1Below);
  const uint32_t record = table->Find(action, U8(base, work - kActionD0Below));
  if (record == 0) return false;
  if (GuestHasAction(base, table->table(), action)) original(ctx, base);
  GuestStoreBE32(base, work + kActionPointerAbove, record);
  SetFighterAnimation(base, fighter, record);
  return true;
}

// Both run where the guest selects the action, the one point the human and the CPU paths of each move share. The Shoryuken
// selection follows the guest's velocity store; the air Tatsumaki one follows a first integration with Ken's values.
void ApplyViolentMovement(uint8_t* base, uint32_t fighter, uint32_t work) {
  const uint8_t action = U8(base, work - kActionD1Below);
  const uint8_t sub = U8(base, work - kActionD0Below);
  if (action == kShoryukenAction && sub < kShoryukenSubs) {
    GuestStoreBE16(base, fighter + kVelocity, uint16_t(kViolentShoryukenVelocity[sub]));
  } else if (IsViolentAirTatsu(action, sub)) {
    const int32_t speed = FacingSign(base, fighter) * kViolentAirTatsuVelocity[sub - kAirTatsuFirstSub];
    MoveFighter(base, fighter, kPositionX, (speed - int16_t(U16(base, fighter + kVelocity))) * 256);
    MoveFighter(base, fighter, kPositionY, -int32_t(int16_t(U16(base, fighter + kVelocityY))) * 256);
    GuestStoreBE16(base, fighter + kVelocity, uint16_t(speed));
    GuestStoreBE16(base, fighter + kVelocityY, 0);
    GuestStoreBE16(base, fighter + kDeceleration, 0);
    GuestStoreBE16(base, fighter + kDecelerationY, 0);
  }
}

// Evil Ryu's changed and added actions resolve to his own copy. The fighter's first selection comes from its init, so
// this is where the identity takes effect.
template <typename H>
void LookupAction(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (!UltraFightersEnabled()) return original(ctx, base);
  const uint32_t fighter = ActiveFighter<H>(base);
  if (ViolentKenSide<typename H::Netplay>(base, fighter) >= 0) {
    if (!LookupViolentAction<H>(ctx, base, original, fighter)) original(ctx, base);
    ApplyViolentMovement(base, fighter, H::Netplay::kFightWork);
    return;
  }
  const int side = EvilSide<H>(base, fighter);
  if (side < 0) return original(ctx, base);
  const EvilActionTable* table = TableFor<H>(base, fighter);
  if (!table) return original(ctx, base);
  const uint32_t work = H::Netplay::kFightWork;
  const uint32_t guest_table = LoadBE32(GuestHost(base, H::kActionTables + U16(base, work - kActionTableIndexBelow)), 0);
  if (guest_table != table->table()) return original(ctx, base);
  const uint8_t action = U8(base, work - kActionD1Below);
  const uint32_t record =
      table->Find(action, U8(base, work - kActionD0Below), U8(base, fighter + kTatsuStrength));
  if (record == 0) return original(ctx, base);
  // The guest also leaves results in its registers, so it runs whenever it has an entry to read.
  if (GuestHasAction(base, table->table(), action)) original(ctx, base);
  GuestStoreBE32(base, work + kActionPointerAbove, record);
  SetFighterAnimation(base, fighter, record);
  // The guest set Ryu's spin count just before selecting this action.
  if (action == kTatsuAction) SetU8(base, fighter + kStepTimer, TatsuSpins(U8(base, fighter + kTatsuStrength)));
}

// USF2 Evil Ryu has no forward + heavy punch unique: the guest's choice falls back to the plain heavy punch.
template <typename H>
void HeavyPunch(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  original(ctx, base);
  if (!UltraFightersEnabled()) return;
  const uint32_t fighter = ActiveFighter<H>(base);
  if (EvilSide<H>(base, fighter) < 0 || U8(base, fighter + kAttackSelection) != kUniqueHeavyPunch) return;
  const bool close = U16(base, fighter + kOpponentRange) < kCloseHeavyPunchRange;
  SetU8(base, fighter + kAttackSelection, close ? kCloseHeavyPunch : kFarHeavyPunch);
}

// The sprite emitters derive the HD art key from the record's offset in the character's action data, which a copied
// record does not have. They only read the fighter, so it shows them the guest frame for the length of the call.
template <typename H>
void EmitSprites(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (!UltraFightersEnabled()) return original(ctx, base);
  const uint32_t object = LoadBE32(GuestHost(base, H::Netplay::kFightWork + kEmittedObjectAbove), 0);
  const bool violent = ViolentKenSide<typename H::Netplay>(base, object) >= 0;
  const int side = violent ? 0 : EvilSide<H>(base, object);
  if (side < 0) return original(ctx, base);
  const uint32_t record = LoadBE32(GuestHost(base, object + kAnimationRecord), 0);
  const uint32_t source = violent ? ViolentSourceFrame(record) : EvilSourceFrame(record);
  if (source == 0) return original(ctx, base);
  GuestStoreBE32(base, object + kAnimationRecord, source);
  original(ctx, base);
  GuestStoreBE32(base, object + kAnimationRecord, record);
}

// The grab range is read per character slot; the raging demon uses the rows the guest keeps for Akuma.
template <typename H>
void LoadGrabRange(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  const uint32_t fighter = ActiveFighter<H>(base);
  const int side = UltraFightersEnabled() ? EvilSide<H>(base, fighter) : -1;
  if (side < 0 || GlobalHostMoves().For(side).kind != HostMoveKind::kRagingDemon) return original(ctx, base);
  const uint8_t slot = U8(base, fighter + kCharacterSlot);
  SetU8(base, fighter + kCharacterSlot, kAkumaSlot);
  original(ctx, base);
  SetU8(base, fighter + kCharacterSlot, slot);
}

}  // namespace
}  // namespace ssf2t::character

REX_HOOK_RAW(base013_sub_82264F78) {
  ssf2t::character::TrackHalfCircle<ssf2t::character::Base013Moves>(ctx, base, __imp__base013_sub_82264F78);
}
REX_HOOK_RAW(base013_sub_82265228) {
  ssf2t::character::TrackSpecials<ssf2t::character::Base013Moves>(ctx, base, __imp__base013_sub_82265228);
}
REX_HOOK_RAW(base013_sub_82265040) {
  ssf2t::character::RunSpecialClass<ssf2t::character::Base013Moves>(ctx, base, __imp__base013_sub_82265040);
}
REX_HOOK_RAW(base013_sub_8225E1F0) {
  ssf2t::character::HeavyPunch<ssf2t::character::Base013Moves>(ctx, base, __imp__base013_sub_8225E1F0);
}
REX_HOOK_RAW(base013_sub_821C01F8) {
  ssf2t::character::LookupAction<ssf2t::character::Base013Moves>(ctx, base, __imp__base013_sub_821C01F8);
}
REX_HOOK_RAW(base013_sub_821C1E70) {
  ssf2t::character::LoadGrabRange<ssf2t::character::Base013Moves>(ctx, base, __imp__base013_sub_821C1E70);
}
REX_HOOK_RAW(base013_sub_823A03A0) {
  ssf2t::character::EmitSprites<ssf2t::character::Base013Moves>(ctx, base, __imp__base013_sub_823A03A0);
}
REX_HOOK_RAW(base013_sub_823A0660) {
  ssf2t::character::EmitSprites<ssf2t::character::Base013Moves>(ctx, base, __imp__base013_sub_823A0660);
}
REX_HOOK_RAW(base013_sub_823A2F58) {
  ssf2t::character::EmitSprites<ssf2t::character::Base013Moves>(ctx, base, __imp__base013_sub_823A2F58);
}
REX_HOOK_RAW(base013_sub_823A4860) {
  ssf2t::character::EmitSprites<ssf2t::character::Base013Moves>(ctx, base, __imp__base013_sub_823A4860);
}
REX_HOOK_RAW(base013_sub_823A5DF8) {
  ssf2t::character::EmitSprites<ssf2t::character::Base013Moves>(ctx, base, __imp__base013_sub_823A5DF8);
}

REX_HOOK_RAW(base013_sub_8228E440) {
  ssf2t::character::TrackKenSpecials<ssf2t::character::Base013Moves>(ctx, base, __imp__base013_sub_8228E440);
}
REX_HOOK_RAW(base013_sub_8228E488) {
  ssf2t::character::RunKenSpecialClass<ssf2t::character::Base013Moves>(ctx, base, __imp__base013_sub_8228E488);
}
REX_HOOK_RAW(base013_sub_8228D890) {
  ssf2t::character::TrackKenKickCommand<ssf2t::character::Base013Moves>(ctx, base, __imp__base013_sub_8228D890);
}
REX_HOOK_RAW(base013_sub_8228D8F8) {
  ssf2t::character::TrackKenKickCommand<ssf2t::character::Base013Moves>(ctx, base, __imp__base013_sub_8228D8F8);
}
REX_HOOK_RAW(base013_sub_8228D960) {
  ssf2t::character::TrackKenKickCommand<ssf2t::character::Base013Moves>(ctx, base, __imp__base013_sub_8228D960);
}
REX_HOOK_RAW(base013_sub_821B41F8) {
  ssf2t::character::StartJump<ssf2t::character::Base013Moves>(ctx, base, __imp__base013_sub_821B41F8);
}
REX_HOOK_RAW(base013_sub_8218BB30) {
  ssf2t::character::StartJump<ssf2t::character::Base013Moves>(ctx, base, __imp__base013_sub_8218BB30);
}
REX_HOOK_RAW(base013_sub_82195568) {
  ssf2t::character::SelectCpuMove<ssf2t::character::Base013Moves>(ctx, base, __imp__base013_sub_82195568);
}

#if SSF2T_TITLE_UPDATE_IMAGE
REX_HOOK_RAW(tu023_sub_8228ED10) {
  ssf2t::character::TrackKenSpecials<ssf2t::character::TitleUpdate023Moves>(ctx, base, __imp__tu023_sub_8228ED10);
}
REX_HOOK_RAW(tu023_sub_8228ED58) {
  ssf2t::character::RunKenSpecialClass<ssf2t::character::TitleUpdate023Moves>(ctx, base, __imp__tu023_sub_8228ED58);
}
REX_HOOK_RAW(tu023_sub_8228E160) {
  ssf2t::character::TrackKenKickCommand<ssf2t::character::TitleUpdate023Moves>(ctx, base, __imp__tu023_sub_8228E160);
}
REX_HOOK_RAW(tu023_sub_8228E1C8) {
  ssf2t::character::TrackKenKickCommand<ssf2t::character::TitleUpdate023Moves>(ctx, base, __imp__tu023_sub_8228E1C8);
}
REX_HOOK_RAW(tu023_sub_8228E230) {
  ssf2t::character::TrackKenKickCommand<ssf2t::character::TitleUpdate023Moves>(ctx, base, __imp__tu023_sub_8228E230);
}
REX_HOOK_RAW(tu023_sub_821B4318) {
  ssf2t::character::StartJump<ssf2t::character::TitleUpdate023Moves>(ctx, base, __imp__tu023_sub_821B4318);
}
REX_HOOK_RAW(tu023_sub_8218B998) {
  ssf2t::character::StartJump<ssf2t::character::TitleUpdate023Moves>(ctx, base, __imp__tu023_sub_8218B998);
}
REX_HOOK_RAW(tu023_sub_821953A0) {
  ssf2t::character::SelectCpuMove<ssf2t::character::TitleUpdate023Moves>(ctx, base, __imp__tu023_sub_821953A0);
}
REX_HOOK_RAW(tu023_sub_822653E8) {
  ssf2t::character::TrackHalfCircle<ssf2t::character::TitleUpdate023Moves>(ctx, base, __imp__tu023_sub_822653E8);
}
REX_HOOK_RAW(tu023_sub_82265698) {
  ssf2t::character::TrackSpecials<ssf2t::character::TitleUpdate023Moves>(ctx, base, __imp__tu023_sub_82265698);
}
REX_HOOK_RAW(tu023_sub_822654B0) {
  ssf2t::character::RunSpecialClass<ssf2t::character::TitleUpdate023Moves>(ctx, base, __imp__tu023_sub_822654B0);
}
REX_HOOK_RAW(tu023_sub_8225E228) {
  ssf2t::character::HeavyPunch<ssf2t::character::TitleUpdate023Moves>(ctx, base, __imp__tu023_sub_8225E228);
}
REX_HOOK_RAW(tu023_sub_821C0318) {
  ssf2t::character::LookupAction<ssf2t::character::TitleUpdate023Moves>(ctx, base, __imp__tu023_sub_821C0318);
}
REX_HOOK_RAW(tu023_sub_821C1F90) {
  ssf2t::character::LoadGrabRange<ssf2t::character::TitleUpdate023Moves>(ctx, base, __imp__tu023_sub_821C1F90);
}
REX_HOOK_RAW(tu023_sub_823A0390) {
  ssf2t::character::EmitSprites<ssf2t::character::TitleUpdate023Moves>(ctx, base, __imp__tu023_sub_823A0390);
}
REX_HOOK_RAW(tu023_sub_823A0650) {
  ssf2t::character::EmitSprites<ssf2t::character::TitleUpdate023Moves>(ctx, base, __imp__tu023_sub_823A0650);
}
REX_HOOK_RAW(tu023_sub_823A2F48) {
  ssf2t::character::EmitSprites<ssf2t::character::TitleUpdate023Moves>(ctx, base, __imp__tu023_sub_823A2F48);
}
REX_HOOK_RAW(tu023_sub_823A4850) {
  ssf2t::character::EmitSprites<ssf2t::character::TitleUpdate023Moves>(ctx, base, __imp__tu023_sub_823A4850);
}
REX_HOOK_RAW(tu023_sub_823A5DE8) {
  ssf2t::character::EmitSprites<ssf2t::character::TitleUpdate023Moves>(ctx, base, __imp__tu023_sub_823A5DE8);
}
#endif
