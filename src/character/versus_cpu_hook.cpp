#include "character/versus_cpu.h"

#include <cstring>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#include "guest_memory.h"
#include "localization/active_language.h"
#include "netplay/fight_snapshot.h"
#include "netplay_setting.h"
#include "title_update_image.h"

namespace ssf2t::character {

VersusCpuOpponent& GlobalVersusCpu() {
  static VersusCpuOpponent opponent;
  return opponent;
}

namespace {

constexpr uint32_t kCpuNameBytes = 32;

bool g_reading_empty_slot = false;
bool g_hud_label_logged = false;
bool g_select_pending = false;
bool g_select_shared = false;
uint16_t g_copy_logged = 0xFFFF;
uint16_t g_cursor_logged = 0;

template <typename G>
struct VersusCpuHooks {
  static uint8_t Mode(const uint8_t* base) { return base[G::kGameMode]; }
  static int CpuSide(const uint8_t* base) { return GlobalVersusCpu().SideIn(Mode(base)); }

  static int32_t Slot(const uint8_t* base, int side) {
    return int32_t(LoadBE32(base, G::kLobbySlots + uint32_t(side) * 4));
  }
  static void SetSlot(uint8_t* base, int side, int32_t value) {
    StoreBE32(base, G::kLobbySlots + uint32_t(side) * 4, uint32_t(value));
  }
  static int EmptySide(const uint8_t* base) { return EmptyLobbySide(Slot(base, 0), Slot(base, 1)); }

  static bool Fighters(const uint8_t* base, uint32_t& block) {
    block = LoadBE32(base, G::kFighterBlock);
    return block >= 0x10000 && block < 0xFFFF0000u;
  }

  static void InitLobby(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    GlobalVersusCpu().Reset();
    original(ctx, base);
  }

  static void CycleOpponent(uint8_t* base) {
    const int empty = EmptySide(base);
    const char* refused = Mode(base) != kLocalVersusGameMode                                    ? "not_local_match"
                          : rex::cvar::GetFlagByName(kNetplayRoleSetting) != kNetplayRoleOff ? "netplay_test"
                          : empty < 0                                                         ? "no_empty_side"
                                                                                              : nullptr;
    if (refused) {
      REXLOG_INFO("DEBUG_SAULO VS_CPU lobby-toggle refused={} slots={},{} guest={}", refused, Slot(base, 0),
                  Slot(base, 1), G::kName);
      return;
    }
    GlobalVersusCpu().Cycle(empty);
    REXLOG_INFO("DEBUG_SAULO VS_CPU lobby-toggle side={} cpu={} difficulty={} guest={}", empty + 1,
                GlobalVersusCpu().level() != 0, GlobalVersusCpu().level(), G::kName);
  }

  static void RemoveOpponent(uint8_t* base) {
    const int cpu = CpuSide(base);
    GlobalVersusCpu().Reset();
    REXLOG_INFO("DEBUG_SAULO VS_CPU lobby-remove side={} slots={},{} guest={}", cpu + 1, Slot(base, 0), Slot(base, 1),
                G::kName);
  }

  static void SetDifficulty(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    if (ctx.r3.u32 == kCycleCpuOpponentArgument) {
      CycleOpponent(base);
      return;
    }
    if (ctx.r3.u32 == kRemoveCpuOpponentArgument) {
      RemoveOpponent(base);
      return;
    }
    original(ctx, base);
  }

  // Only the lobby's own start and join rules see the CPU seat; the slot stays empty everywhere else.
  static void UpdateLobby(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    GlobalVersusCpu().FollowSeats(EmptySide(base));
    const int cpu = CpuSide(base);
    if (cpu < 0) {
      original(ctx, base);
      return;
    }
    SetSlot(base, cpu, kCpuLobbySlot);
    original(ctx, base);
    SetSlot(base, cpu, kEmptyLobbySlot);
    GlobalVersusCpu().FollowSeats(EmptySide(base));
  }

  static bool SeatsCpu(const uint8_t* base, int32_t side) {
    return !g_reading_empty_slot && ReadsAsCpu(side, CpuSide(base));
  }

  static void SlotOccupied(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    if (ctx.r3.s32 == kCpuActiveQuerySeat) {
      ctx.r3.u64 = CpuSide(base) >= 0 ? 1 : 0;
      return;
    }
    if (SeatsCpu(base, ctx.r3.s32)) {
      ctx.r3.u64 = 1;
      return;
    }
    original(ctx, base);
  }

  static uint32_t WriteCpuName(uint8_t* base, const std::string& text) {
    static uint32_t name = 0;
    if (name == 0) name = REX_KERNEL_MEMORY()->SystemHeapAlloc(kCpuNameBytes);
    if (name == 0) return 0;
    std::memcpy(GuestHost(base, name), text.c_str(), text.size() + 1);
    return name;
  }

  // The original also marks the script result as a string, so it runs, on the empty name, before the name is replaced.
  static void ReadCpuName(PPCContext& ctx, uint8_t* base, PPCFunc* original, const std::string& text) {
    g_reading_empty_slot = true;
    original(ctx, base);
    g_reading_empty_slot = false;
    const uint32_t name = WriteCpuName(base, text);
    if (name != 0) ctx.r3.u64 = name;
  }

  static void PlayerName(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    if (!SeatsCpu(base, ctx.r3.s32)) return original(ctx, base);
    ReadCpuName(ctx, base, original,
                CpuOpponentName(GlobalVersusCpu().level(), GlobalVersusCpu().in_lobby(),
                                localization::PortugueseBrazilSelected()));
  }

  // The fight HUD and Results read names through the text system. Its HUD variable has its own copy of GetPlayerName,
  // which indexes the name table by the seat, so an empty seat that reports occupied reads memory in front of the table.
  static void TextVariableName(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const int32_t side = ctx.r3.s32;
    if (!SeatsCpu(base, side)) return original(ctx, base);
    if (!g_hud_label_logged) {
      g_hud_label_logged = true;
      REXLOG_INFO("DEBUG_SAULO VS_CPU hud-label side={} path=text value={} in_lobby={} guest={}", side + 1, kCpuLabel,
                  GlobalVersusCpu().in_lobby(), G::kName);
    }
    ReadCpuName(ctx, base, original, kCpuLabel);
  }

  static SelectPads ReadPads(const uint8_t* base, uint32_t fighter) {
    return {LoadBE16(GuestHost(base, fighter + kFighterPadOffset), 0),
            LoadBE16(GuestHost(base, fighter + kFighterPadPreviousOffset), 0)};
  }

  static void WritePads(uint8_t* base, uint32_t fighter, const SelectPads& pads) {
    GuestStoreBE16(base, fighter + kFighterPadOffset, pads.current);
    GuestStoreBE16(base, fighter + kFighterPadPreviousOffset, pads.previous);
  }

  static bool Finished(const uint8_t* base, int side) {
    return PickFinished(base[G::kSelectDone + uint32_t(side)], base[G::kSelectMask], side);
  }

  static void EnterSelect(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    if (Mode(base) == kLocalVersusGameMode) {
      const int cpu = CpuSide(base);
      REXLOG_INFO("DEBUG_SAULO VS_CPU lobby-start side={} cpu={} difficulty={} slots={},{} guest={}", cpu + 1,
                  cpu >= 0, GlobalVersusCpu().level(), Slot(base, 0), Slot(base, 1), G::kName);
      g_select_pending = true;
      GlobalVersusCpu().LeaveLobby();
    }
    original(ctx, base);
  }

  static void LogSelect(int cpu) {
    g_select_pending = false;
    g_select_shared = false;
    g_copy_logged = 0xFFFF;
    g_cursor_logged = 0;
    REXLOG_INFO("DEBUG_SAULO VS_CPU select-init side={} owner={} shared_input={}", cpu + 1, cpu >= 0 ? "cpu" : "human",
                cpu >= 0);
  }

  static void LogSelectState(const uint8_t* base, uint32_t block, uint32_t fighter, int cpu, bool shared) {
    REXLOG_INFO(
        "DEBUG_SAULO VS_CPU select-state side={} state={} side_state={} active={} final={},{} mask={} locked={} "
        "joined={} delay={} guest={}",
        cpu + 1, *GuestHost(base, block + kBlockSelectStateOffset), base[G::kSideState + uint32_t(cpu)], shared,
        base[G::kSelectDone], base[G::kSelectDone + 1], base[G::kSelectMask],
        *GuestHost(base, fighter + kFighterLockedOffset), *GuestHost(base, fighter + kFighterJoinedOffset),
        *GuestHost(base, block + kBlockSelectDelayOffset), G::kName);
  }

  // The confirmed fighter keeps its pose and sound; only the wait before the lock goes, so the guest's own lock, prompts
  // and states still run in order.
  static void ShortenConfirmWindow(uint8_t* base, uint32_t block, uint32_t fighter, int player) {
    const uint8_t window = *GuestHost(base, fighter + kFighterConfirmWindowOffset);
    if (!ShortensConfirmWindow(window, *GuestHost(base, fighter + kFighterCommitOffset))) return;
    REXLOG_INFO("DEBUG_SAULO VS_CPU human-confirm side={} state={} side_state={} mask={} locked={} window={} guest={}",
                player + 1, *GuestHost(base, block + kBlockSelectStateOffset), base[G::kSideState + uint32_t(player)],
                base[G::kSelectMask], *GuestHost(base, fighter + kFighterLockedOffset), window, G::kName);
    GuestStore8(base, fighter + kFighterConfirmWindowOffset, 1);
  }

  static bool HandedOver(const uint8_t* base, uint32_t fighter, int player) {
    return PlayerHandedOver(*GuestHost(base, fighter + kFighterLockedOffset), base[G::kSideState + uint32_t(player)],
                            base[G::kColourPrompt], Finished(base, player));
  }

  // Runs before the select update: that function turns the pad words into press edges and then runs the side steps,
  // so a copy made after it is never seen, and the guest refreshes both sides' words at the start of every frame.
  static void ShareInput(uint8_t* base, uint32_t block, int cpu) {
    SetSlot(base, cpu, kEmptyLobbySlot);
    const int player = 1 - cpu;
    const uint32_t cpu_fighter = block + netplay::kFighterOffsets[cpu];
    const uint32_t player_fighter = block + netplay::kFighterOffsets[player];
    ShortenConfirmWindow(base, block, player_fighter, player);
    const bool shared = HandedOver(base, player_fighter, player);
    if (shared != g_select_shared) {
      g_select_shared = shared;
      g_copy_logged = 0xFFFF;
      REXLOG_INFO("DEBUG_SAULO VS_CPU select-shared side={} active={} reason={} guest={}", cpu + 1, shared,
                  Finished(base, player) ? "finished" : "confirmed", G::kName);
      LogSelectState(base, block, cpu_fighter, cpu, shared);
    }
    const SelectPads source = ReadPads(base, player_fighter);
    WritePads(base, cpu_fighter, CpuSidePads(shared, source));
    if (shared && source.current != g_copy_logged) {
      g_copy_logged = source.current;
      const SelectPads copy = ReadPads(base, cpu_fighter);
      REXLOG_INFO("DEBUG_SAULO VS_CPU input-copy src_cur={:04X} src_prev={:04X} dst_cur={:04X} dst_prev={:04X} side={}",
                  source.current, source.previous, copy.current, copy.previous, cpu + 1);
    }
  }

  static void SelectInput(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const int cpu = CpuSide(base);
    if (g_select_pending) LogSelect(cpu);
    uint32_t block = 0;
    if (cpu >= 0 && Fighters(base, block) && !Finished(base, cpu)) ShareInput(base, block, cpu);
    original(ctx, base);
  }

  static void CursorInput(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const int cpu = CpuSide(base);
    uint32_t block = 0;
    if (cpu >= 0 && g_select_shared && Fighters(base, block) &&
        LoadBE32(base, G::kActiveFighter) == block + netplay::kFighterOffsets[cpu]) {
      const SelectPads pads = ReadPads(base, block + netplay::kFighterOffsets[cpu]);
      const uint16_t pressed = pads.Pressed();
      if (pressed != g_cursor_logged) {
        g_cursor_logged = pressed;
        if (pressed != 0) {
          REXLOG_INFO("DEBUG_SAULO VS_CPU cursor-read side={} cur={:04X} prev={:04X} move={:X}", cpu + 1, pads.current,
                      pads.previous, pressed & 0xF);
        }
      }
    }
    original(ctx, base);
  }

  // Before the load state, not at the guest's later copy of the side table: the fighter assets depend on the owner.
  static void LoadMatch(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const int cpu = CpuSide(base);
    uint32_t block = 0;
    if (cpu >= 0 && Fighters(base, block)) {
      const uint32_t fighter = block + netplay::kFighterOffsets[cpu];
      const uint32_t game = LoadBE32(base, G::kGameState);
      const int level = GlobalVersusCpu().level();
      g_hud_label_logged = false;
      SetSlot(base, cpu, kEmptyLobbySlot);
      base[G::kSideHuman + uint32_t(cpu)] = 0;
      GuestStore8(base, fighter + kFighterHumanOffset, 0);
      GuestStore8(base, fighter + kFighterOldVersionOffset, 0);
      GuestStore8(base, game + kGameDifficultyOffset, CpuGuestDifficulty(level));
      GuestStoreBE16(base, game + kGameLadderOffset, CpuLadderStep(level));
      REXLOG_INFO("DEBUG_SAULO VS_CPU match-init p1={} p2={} difficulty={} mode={} guest={}",
                  cpu == 0 ? "cpu" : "human", cpu == 1 ? "cpu" : "human", level, GameModeName(Mode(base)), G::kName);
    }
    original(ctx, base);
  }
};

using Base = VersusCpuHooks<Base013VersusCpu>;

}  // namespace
}  // namespace ssf2t::character

REX_EXTERN(__imp__base013_sub_823C9530);
REX_EXTERN(__imp__base013_sub_823CCDD8);
REX_EXTERN(__imp__base013_sub_823C7FC0);
REX_EXTERN(__imp__base013_sub_823C80A8);
REX_EXTERN(__imp__base013_sub_823CD580);
REX_EXTERN(__imp__base013_sub_823D4078);
REX_EXTERN(__imp__base013_sub_823D40A0);
REX_EXTERN(__imp__base013_sub_823C7618);
REX_EXTERN(__imp__base013_sub_823E9B98);
REX_EXTERN(__imp__base013_sub_82247E90);
REX_EXTERN(__imp__base013_sub_82245748);
REX_EXTERN(__imp__base013_sub_82248188);
REX_HOOK_RAW(base013_sub_823C9530) { ssf2t::character::Base::InitLobby(ctx, base, __imp__base013_sub_823C9530); }
REX_HOOK_RAW(base013_sub_823CCDD8) { ssf2t::character::Base::UpdateLobby(ctx, base, __imp__base013_sub_823CCDD8); }
REX_HOOK_RAW(base013_sub_823C7FC0) { ssf2t::character::Base::SlotOccupied(ctx, base, __imp__base013_sub_823C7FC0); }
REX_HOOK_RAW(base013_sub_823C80A8) { ssf2t::character::Base::SlotOccupied(ctx, base, __imp__base013_sub_823C80A8); }
REX_HOOK_RAW(base013_sub_823CD580) { ssf2t::character::Base::PlayerName(ctx, base, __imp__base013_sub_823CD580); }
REX_HOOK_RAW(base013_sub_823D4078) { ssf2t::character::Base::TextVariableName(ctx, base, __imp__base013_sub_823D4078); }
REX_HOOK_RAW(base013_sub_823D40A0) { ssf2t::character::Base::TextVariableName(ctx, base, __imp__base013_sub_823D40A0); }
REX_HOOK_RAW(base013_sub_823C7618) { ssf2t::character::Base::SetDifficulty(ctx, base, __imp__base013_sub_823C7618); }
REX_HOOK_RAW(base013_sub_823E9B98) { ssf2t::character::Base::EnterSelect(ctx, base, __imp__base013_sub_823E9B98); }
REX_HOOK_RAW(base013_sub_82245748) { ssf2t::character::Base::CursorInput(ctx, base, __imp__base013_sub_82245748); }
REX_HOOK_RAW(base013_sub_82247E90) { ssf2t::character::Base::SelectInput(ctx, base, __imp__base013_sub_82247E90); }
REX_HOOK_RAW(base013_sub_82248188) { ssf2t::character::Base::LoadMatch(ctx, base, __imp__base013_sub_82248188); }

#if SSF2T_TITLE_UPDATE_IMAGE
namespace ssf2t::character {
namespace {
using Update = VersusCpuHooks<TitleUpdate023VersusCpu>;
}  // namespace
}  // namespace ssf2t::character

REX_EXTERN(__imp__tu023_sub_823C9B28);
REX_EXTERN(__imp__tu023_sub_823CD3B8);
REX_EXTERN(__imp__tu023_sub_823C85B8);
REX_EXTERN(__imp__tu023_sub_823C86A0);
REX_EXTERN(__imp__tu023_sub_823CDB60);
REX_EXTERN(__imp__tu023_sub_823D4740);
REX_EXTERN(__imp__tu023_sub_823D4768);
REX_EXTERN(__imp__tu023_sub_823C7C10);
REX_EXTERN(__imp__tu023_sub_823EA2D0);
REX_EXTERN(__imp__tu023_sub_82247ED8);
REX_EXTERN(__imp__tu023_sub_822457B0);
REX_EXTERN(__imp__tu023_sub_822481D0);
REX_HOOK_RAW(tu023_sub_823C9B28) { ssf2t::character::Update::InitLobby(ctx, base, __imp__tu023_sub_823C9B28); }
REX_HOOK_RAW(tu023_sub_823CD3B8) { ssf2t::character::Update::UpdateLobby(ctx, base, __imp__tu023_sub_823CD3B8); }
REX_HOOK_RAW(tu023_sub_823C85B8) { ssf2t::character::Update::SlotOccupied(ctx, base, __imp__tu023_sub_823C85B8); }
REX_HOOK_RAW(tu023_sub_823C86A0) { ssf2t::character::Update::SlotOccupied(ctx, base, __imp__tu023_sub_823C86A0); }
REX_HOOK_RAW(tu023_sub_823CDB60) { ssf2t::character::Update::PlayerName(ctx, base, __imp__tu023_sub_823CDB60); }
REX_HOOK_RAW(tu023_sub_823D4740) { ssf2t::character::Update::TextVariableName(ctx, base, __imp__tu023_sub_823D4740); }
REX_HOOK_RAW(tu023_sub_823D4768) { ssf2t::character::Update::TextVariableName(ctx, base, __imp__tu023_sub_823D4768); }
REX_HOOK_RAW(tu023_sub_823C7C10) { ssf2t::character::Update::SetDifficulty(ctx, base, __imp__tu023_sub_823C7C10); }
REX_HOOK_RAW(tu023_sub_823EA2D0) { ssf2t::character::Update::EnterSelect(ctx, base, __imp__tu023_sub_823EA2D0); }
REX_HOOK_RAW(tu023_sub_822457B0) { ssf2t::character::Update::CursorInput(ctx, base, __imp__tu023_sub_822457B0); }
REX_HOOK_RAW(tu023_sub_82247ED8) { ssf2t::character::Update::SelectInput(ctx, base, __imp__tu023_sub_82247ED8); }
REX_HOOK_RAW(tu023_sub_822481D0) { ssf2t::character::Update::LoadMatch(ctx, base, __imp__tu023_sub_822481D0); }
#endif
