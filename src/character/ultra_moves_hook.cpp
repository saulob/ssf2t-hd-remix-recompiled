#include <array>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#include "character/evil_ryu_actions.h"
#include "character/evil_ryu_audio.h"
#include "character/evil_ryu_debug.h"
#include "character/evil_ryu_moves.h"
#include "character/ultra_audio.h"
#include "character/ultra_navigation.h"
#include "character/ultra_fight.h"
#include "guest_memory.h"
#include "netplay/fight_snapshot.h"
#include "netplay/guest_netplay.h"
#include "netplay_setting.h"
#include "player_match_guest.h"
#include "title_update_image.h"

REX_EXTERN(__imp__base013_sub_82265228);
REX_EXTERN(__imp__base013_sub_82265040);
REX_EXTERN(__imp__base013_sub_821C01F8);
REX_EXTERN(__imp__base013_sub_821C1E70);
REX_EXTERN(__imp__base013_sub_823A03A0);
REX_EXTERN(__imp__base013_sub_823A0660);
REX_EXTERN(__imp__base013_sub_823A2F58);
REX_EXTERN(__imp__base013_sub_823A4860);
REX_EXTERN(__imp__base013_sub_823A5DF8);
REX_EXTERN(base013_sub_823146C0);
REX_EXTERN(base013_sub_823782B0);
REX_EXTERN(base013_sub_82383400);
REX_EXTERN(base013_sub_82386E20);
REX_EXTERN(base013_sub_82391B10);
REX_EXTERN(base013_sub_82379AB0);
#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_82265698);
REX_EXTERN(__imp__tu023_sub_822654B0);
REX_EXTERN(__imp__tu023_sub_821C0318);
REX_EXTERN(__imp__tu023_sub_821C1F90);
REX_EXTERN(__imp__tu023_sub_823A0390);
REX_EXTERN(__imp__tu023_sub_823A0650);
REX_EXTERN(__imp__tu023_sub_823A2F48);
REX_EXTERN(__imp__tu023_sub_823A4850);
REX_EXTERN(__imp__tu023_sub_823A5DE8);
REX_EXTERN(tu023_sub_82314600);
REX_EXTERN(tu023_sub_82378380);
REX_EXTERN(tu023_sub_823834D0);
REX_EXTERN(tu023_sub_82386EF0);
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
};
#endif

constexpr uint32_t kActionD1Below = 0x11;
constexpr uint32_t kActionD0Below = 0x0D;
constexpr uint32_t kActionPointerAbove = 0x14;
constexpr uint32_t kActionTableIndexBelow = 0x26;
constexpr uint32_t kActionRecordCompared = 15;
constexpr uint32_t kEmittedObjectAbove = 0x10;
constexpr uint8_t kClassSpecial = 12;
constexpr uint8_t kRagingDemonMoveId = 10;
constexpr uint8_t kTeleportMoveId = 14;
constexpr uint8_t kSenpuMoveId = kTeleportMoveId;
constexpr uint8_t kAkumaSlot = 16;
constexpr uint8_t kClassicSlotOffset = 17;
constexpr uint8_t kDemonArmStep = 2;
constexpr uint8_t kDemonWaitStep = 4;
constexpr uint8_t kDemonDashStep = 6;
constexpr uint8_t kDemonCaughtStep = 8;
constexpr uint8_t kFullMeter = 0x30;
constexpr const char* kTeleportNames[] = {"forward-punch", "forward-kick", "back-punch", "back-kick"};

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
constexpr uint16_t kAnimationEnd = 0x8000;

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
  move = HostMove{};
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
  REXLOG_INFO("DEBUG_SAULO EVIL_RYU actions table={:08X} skipped={}", address, tables[slot].skipped());
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
  const int applied = BuildEvilPack(base, pack, memory[slot]);
  source[slot] = pack;
  GuestStoreBE32(base, fighter + kBoxPack, memory[slot]);
  REXLOG_INFO("DEBUG_SAULO EVIL_RYU box-pack source={:08X} patches={}", pack, applied);
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
  REXLOG_INFO("DEBUG_SAULO EVIL_RYU teleport side={} kind={}", side + 1, kTeleportNames[variant]);
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
bool StartRagingDemon(PPCContext& ctx, uint8_t* base, uint32_t fighter, int side) {
  if (!TableFor<H>(base, fighter)) return false;
  BeginMove<H>(ctx, base, fighter, kRagingDemonMoveId);
  GlobalHostMoves().For(side).kind = HostMoveKind::kRagingDemon;
  QueueRagingDemonVoice<H>(ctx, base, side);
  REXLOG_INFO("DEBUG_SAULO EVIL_RYU raging-demon side={} state=start", side + 1);
  return true;
}

// Evil Ryu has no recognised command for this action yet, so it is only entered from the debug hotkey.
template <typename H>
bool StartSenpuKyaku(PPCContext& ctx, uint8_t* base, uint32_t fighter, int side) {
  const EvilActionTable* table = TableFor<H>(base, fighter);
  const uint32_t record = table ? table->Find(kUniqueAction, kSenpuSub, 0) : 0;
  if (record == 0) return false;
  BeginMove<H>(ctx, base, fighter, kSenpuMoveId);
  ClearVelocity(base, fighter);
  SetFighterAnimation(base, fighter, record);
  GlobalHostMoves().For(side).kind = HostMoveKind::kSenpuKyaku;
  REXLOG_INFO("DEBUG_SAULO EVIL_RYU senpu-kyaku side={} state=start", side + 1);
  return true;
}

template <typename H>
void DetectMoves(PPCContext& ctx, uint8_t* base, uint32_t fighter, int side) {
  HostMove& move = GlobalHostMoves().For(side);
  const uint16_t relative = U16(base, fighter + kInputRelative);
  const int variant =
      move.teleport.Update(uint8_t(relative & 0xF), U16(base, fighter + kPadCurrent), U16(base, fighter + kPadPrevious));
  const bool demon = move.raging_demon.Update(relative, U16(base, fighter + kInputRelativePrevious));
  if (demon && CanStartMove<H>(base, fighter, true, false)) {
    StartRagingDemon<H>(ctx, base, fighter, side);
  } else if (variant >= 0 && CanStartMove<H>(base, fighter, false, true)) {
    StartTeleport<H>(ctx, base, fighter, side, variant);
  }
}

void LogFightStart(int side) {
  static std::array<uint32_t, 2> logged{};
  const uint32_t session = GlobalSelectSession().id();
  if (logged[size_t(side)] == session) return;
  logged[size_t(side)] = session;
  REXLOG_INFO("DEBUG_SAULO EVIL_RYU fight-setup side={} identity=EvilRyu carrier=Ryu", side + 1);
}

// DEBUG_SAULO: hotkey entry into the move start functions above; the key handler only posts a request.
template <typename H>
void PublishDebugFight(const uint8_t* base) {
  const bool online = U8(base, H::Netplay::kGameMode) == kOnlineGameMode ||
                      rex::cvar::GetFlagByName(kNetplayRoleSetting) != kNetplayRoleOff;
  GlobalDebugBridge().PublishFight(DebugNowMs(), online, EvilRyuOnSide<typename H::Netplay>(base, 0));
}

void LogDebugRejected(DebugMove move, DebugReject reason) {
  REXLOG_INFO("DEBUG_SAULO EVIL_RYU debug-hotkey key={} move={} side=1 accepted=0 reason={}", DebugKeyLabel(move),
              DebugMoveName(move), DebugRejectName(reason));
}

bool DebugMoveReady(const EvilActionTable* table, DebugMove move) {
  if (!table) return false;
  if (IsTeleportMove(move)) return table->Teleport() != 0;
  if (move == DebugMove::kSenpuKyaku) return table->Find(kUniqueAction, kSenpuSub, 0) != 0;
  return true;
}

template <typename H>
void RunDebugMove(PPCContext& ctx, uint8_t* base, uint32_t fighter, int side, DebugMove move) {
  const EvilActionTable* table = TableFor<H>(base, fighter);
  if (!DebugMoveReady(table, move)) return LogDebugRejected(move, DebugReject::kFighterNotReady);
  const bool teleport = IsTeleportMove(move);
  if (GlobalHostMoves().Active(side) || !CanStartMove<H>(base, fighter, false, teleport)) {
    return LogDebugRejected(move, DebugReject::kIncompatibleState);
  }
  if (move == DebugMove::kRagingDemon) {
    const uint8_t meter_before = U8(base, fighter + kMeter);
    REXLOG_INFO("DEBUG_SAULO EVIL_RYU debug-hotkey key={} move={} side=1 accepted=1 meter_before={:02X}",
                DebugKeyLabel(move), DebugMoveName(move), meter_before);
    SetU8(base, fighter + kMeter, kFullMeter);
    StartRagingDemon<H>(ctx, base, fighter, side);
    return;
  }
  REXLOG_INFO("DEBUG_SAULO EVIL_RYU debug-hotkey key={} move={} side=1 accepted=1", DebugKeyLabel(move),
              DebugMoveName(move));
  if (teleport) {
    StartTeleport<H>(ctx, base, fighter, side, TeleportVariantFor(move));
  } else {
    StartSenpuKyaku<H>(ctx, base, fighter, side);
  }
}

template <typename H>
void RunDebugRequest(PPCContext& ctx, uint8_t* base, uint32_t fighter, int side) {
  const DebugMove move = GlobalDebugBridge().Take(DebugNowMs());
  if (move != DebugMove::kNone) RunDebugMove<H>(ctx, base, fighter, side, move);
}

// Runs before the guest's own special trackers so the three-button teleports win over the single-button moves.
template <typename H>
void TrackSpecials(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (!UltraFightersEnabled()) return original(ctx, base);
  PublishDebugFight<H>(base);
  const uint32_t fighter = ActiveFighter<H>(base);
  const int side = EvilSide<H>(base, fighter);
  if (side < 0) return original(ctx, base);
  UseEvilPack(base, fighter);
  if (GlobalHostMoves().Active(side)) {
    if (U8(base, fighter + kState) != kClassSpecial) EndHostMove(base, fighter, side);
  } else {
    if (side == 0) RunDebugRequest<H>(ctx, base, fighter, side);
    if (!GlobalHostMoves().Active(side)) DetectMoves<H>(ctx, base, fighter, side);
  }
  original(ctx, base);
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
  }
}

template <typename H>
void StepSenpuKyaku(PPCContext& ctx, uint8_t* base, uint32_t fighter, int side) {
  HostMove& move = GlobalHostMoves().For(side);
  const bool ended = (U16(base, fighter + kAnimationFlags) & kAnimationEnd) != 0 || ++move.ticks >= kSenpuTimeoutTicks;
  if (!ended) return AdvanceAnimation<H>(ctx, base);
  FinishHostMove<H>(ctx, base, fighter, side);
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
  if (side < 0 || !GlobalHostMoves().Active(side)) return original(ctx, base);
  if (GlobalHostMoves().For(side).kind == HostMoveKind::kTeleport) return StepTeleport<H>(ctx, base, fighter, side);
  if (GlobalHostMoves().For(side).kind == HostMoveKind::kSenpuKyaku) return StepSenpuKyaku<H>(ctx, base, fighter, side);
  const uint8_t countdown = U8(base, fighter + kCountdown);
  if (countdown != 0) return SetU8(base, fighter + kCountdown, uint8_t(countdown - 1));
  StepRagingDemon<H>(ctx, base, fighter);
}

void LogInstalledRecord(const uint8_t* base, uint32_t fighter, uint8_t action, uint32_t table, uint32_t record) {
  static uint32_t logged = 0;
  const uint32_t session = GlobalSelectSession().id();
  if (logged == session) return;
  logged = session;
  REXLOG_INFO("DEBUG_SAULO EVIL_RYU crash-trace fighter={:08X} action={:02X} table={:08X} record={:08X} field0={:08X} "
              "field1={:02X}",
              fighter, action, table, record, LoadBE32(GuestHost(base, fighter + kAnimationRecord), 0),
              U8(base, record + kActionRecordCompared));
}

// Evil Ryu's changed and added actions resolve to his own copy. The fighter's first selection comes from its init, so
// this is where the identity takes effect.
template <typename H>
void LookupAction(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (!UltraFightersEnabled()) return original(ctx, base);
  const uint32_t fighter = ActiveFighter<H>(base);
  const int side = EvilSide<H>(base, fighter);
  if (side < 0) return original(ctx, base);
  LogFightStart(side);
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
  LogInstalledRecord(base, fighter, action, table->table(), record);
}

void LogEmittedFrame(int side, uint32_t record, uint32_t source) {
  static std::array<uint32_t, 2> logged{};
  const uint32_t session = GlobalSelectSession().id();
  if (logged[size_t(side)] == session) return;
  logged[size_t(side)] = session;
  REXLOG_INFO("DEBUG_SAULO EVIL_RYU sprite-key side={} record={:08X} source={:08X}", side + 1, record, source);
}

// The sprite emitters derive the HD art key from the record's offset in the character's action data, which a copied
// record does not have. They only read the fighter, so it shows them the guest frame for the length of the call.
template <typename H>
void EmitSprites(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (!UltraFightersEnabled()) return original(ctx, base);
  const uint32_t object = LoadBE32(GuestHost(base, H::Netplay::kFightWork + kEmittedObjectAbove), 0);
  const int side = EvilSide<H>(base, object);
  if (side < 0) return original(ctx, base);
  const uint32_t record = LoadBE32(GuestHost(base, object + kAnimationRecord), 0);
  const uint32_t source = EvilSourceFrame(record);
  if (source == 0) return original(ctx, base);
  LogEmittedFrame(side, record, source);
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

REX_HOOK_RAW(base013_sub_82265228) {
  ssf2t::character::TrackSpecials<ssf2t::character::Base013Moves>(ctx, base, __imp__base013_sub_82265228);
}
REX_HOOK_RAW(base013_sub_82265040) {
  ssf2t::character::RunSpecialClass<ssf2t::character::Base013Moves>(ctx, base, __imp__base013_sub_82265040);
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

#if SSF2T_TITLE_UPDATE_IMAGE
REX_HOOK_RAW(tu023_sub_82265698) {
  ssf2t::character::TrackSpecials<ssf2t::character::TitleUpdate023Moves>(ctx, base, __imp__tu023_sub_82265698);
}
REX_HOOK_RAW(tu023_sub_822654B0) {
  ssf2t::character::RunSpecialClass<ssf2t::character::TitleUpdate023Moves>(ctx, base, __imp__tu023_sub_822654B0);
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
