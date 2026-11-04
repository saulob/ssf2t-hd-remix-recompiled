#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#include "character/evil_ryu_results.h"
#include "character/fighter_identity.h"
#include "character/ultra_fighters.h"
#include "guest_memory.h"
#include "title_update_image.h"

namespace ssf2t::character {
namespace {

struct Base013Results {
  static constexpr uint32_t kMatchWinner = 0x83C133F4;
  static constexpr std::array<uint32_t, 2> kMatchCharacter = {0x83C133F0, 0x83C133EC};
  static constexpr uint32_t kLocalRecords = 0x83C235D0;
  static constexpr uint32_t kRecordBytes = 608;
};

#if SSF2T_TITLE_UPDATE_IMAGE
struct TitleUpdate023Results {
  static constexpr uint32_t kMatchWinner = 0x83C13474;
  static constexpr std::array<uint32_t, 2> kMatchCharacter = {0x83C13470, 0x83C1346C};
  static constexpr uint32_t kLocalRecords = 0x83C23690;
  static constexpr uint32_t kRecordBytes = 616;
};
#endif

constexpr uint32_t kRecordWins = 20;
constexpr uint32_t kRecordLosses = 88;
constexpr uint32_t kCellBytes = 12;
constexpr uint32_t kResultSlots = 8;
constexpr uint32_t kListEntries = kResultsRows * kResultsColumns;

struct GuestRow {
  uint32_t list = 0;
  uint32_t wins = 0;
  uint32_t losses = 0;
};

template <typename H>
void TakeBack(uint8_t* base, int side, uint32_t counter) {
  const uint32_t address = H::kLocalRecords + uint32_t(side) * H::kRecordBytes + counter;
  const uint32_t value = LoadBE32(GuestHost(base, address), 0);
  if (value > 0) GuestStoreBE32(base, address, value - 1);
}

// The match record the guest keeps for the results table. It raises Ryu's row for a carrier fight, so an Evil Ryu match
// is moved to the host book afterwards, by side and identity. Totals stay, since both are that player's wins.
template <typename H>
void RecordMatch(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  std::array<bool, 2> evil{};
  if (UltraFightersEnabled()) {
    for (int side = 0; side < 2; ++side) {
      evil[size_t(side)] = RequestedIdentity(side) == FighterIdentity::kEvilRyu &&
                           LoadBE32(GuestHost(base, H::kMatchCharacter[size_t(side)]), 0) == uint32_t(kRyuGuestCharacter);
    }
  }
  original(ctx, base);
  if (!evil[0] && !evil[1]) {
    GlobalEvilResults().Record(-1, evil);
    return;
  }
  const int winner = int(LoadBE32(GuestHost(base, H::kMatchWinner), 0));
  const ResultsCorrection correction = GlobalEvilResults().Record(winner, evil);
  if (correction.ryu_win_side >= 0) TakeBack<H>(base, correction.ryu_win_side, kRecordWins);
  if (correction.ryu_loss_side >= 0) TakeBack<H>(base, correction.ryu_loss_side, kRecordLosses);
  REXLOG_INFO("DEBUG_SAULO EVIL_RYU results winner={} evil={}{} p1={}-{} p2={}-{}", winner + 1, int(evil[0]),
              int(evil[1]), GlobalEvilResults().wins(0), GlobalEvilResults().losses(0), GlobalEvilResults().wins(1),
              GlobalEvilResults().losses(1));
}

void WriteCell(uint8_t* base, uint32_t cell, const char* text) {
  char* out = reinterpret_cast<char*>(GuestHost(base, cell));
  std::snprintf(out, kCellBytes, "%s", text);
}

GuestRow& RowFor(uint32_t slot) {
  static std::array<GuestRow, kResultSlots> rows{};
  return rows[slot];
}

bool EnsureRow(GuestRow& row) {
  if (row.list) return true;
  row.list = REX_KERNEL_MEMORY()->SystemHeapAlloc(kListEntries * 4);
  row.wins = REX_KERNEL_MEMORY()->SystemHeapAlloc(kCellBytes);
  row.losses = REX_KERNEL_MEMORY()->SystemHeapAlloc(kCellBytes);
  return row.list && row.wins && row.losses;
}

// The grid takes the list this native returns and reads rows x columns entries, so once the grid has grown every call
// must answer with the longer list, also in a lobby where the row stays blank and hidden.
void PlayerResults(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  const uint32_t slot = ctx.r3.u32;
  original(ctx, base);
  const uint32_t native = ctx.r3.u32;
  if (!ResultsRowReady() || native == 0 || slot >= kResultSlots) return;
  GuestRow& row = RowFor(slot);
  if (!EnsureRow(row)) return;
  char wins[kCellBytes] = "";
  char losses[kCellBytes] = "";
  if (slot < 2) {
    std::snprintf(wins, sizeof(wins), "%d", GlobalEvilResults().wins(int(slot)));
    std::snprintf(losses, sizeof(losses), "%d", GlobalEvilResults().losses(int(slot)));
  }
  WriteCell(base, row.wins, wins);
  WriteCell(base, row.losses, losses);
  for (int display = 0; display < kResultsRows; ++display) {
    const int source = ResultsNativeRow(display);
    const uint32_t entry = row.list + uint32_t(display) * kResultsColumns * 4;
    if (source < 0) {
      GuestStoreBE32(base, entry, row.wins);
      GuestStoreBE32(base, entry + 4, row.losses);
      continue;
    }
    const uint32_t from = native + uint32_t(source) * kResultsColumns * 4;
    GuestStoreBE32(base, entry, LoadBE32(GuestHost(base, from), 0));
    GuestStoreBE32(base, entry + 4, LoadBE32(GuestHost(base, from + 4), 0));
  }
  ctx.r3.u64 = row.list;
}

void PlayerResultsCharacter(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  const uint32_t slot = ctx.r3.u32;
  original(ctx, base);
  if (!ResultsRowReady() || slot > 1 || ctx.r3.u32 != uint32_t(kRyuGuestCharacter)) return;
  if (GlobalEvilResults().last_match_evil(int(slot))) ctx.r3.u64 = uint64_t(kEvilRyuResultsCharacter);
}

void ResetPlayerResults(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  const int slot = int(ctx.r3.u32);
  original(ctx, base);
  GlobalEvilResults().Reset(slot);
}

}  // namespace
}  // namespace ssf2t::character

REX_EXTERN(__imp__base013_sub_823C33C8);
REX_HOOK_RAW(base013_sub_823C33C8) {
  ssf2t::character::RecordMatch<ssf2t::character::Base013Results>(ctx, base, __imp__base013_sub_823C33C8);
}
REX_EXTERN(__imp__base013_sub_823C8468);
REX_HOOK_RAW(base013_sub_823C8468) { ssf2t::character::PlayerResults(ctx, base, __imp__base013_sub_823C8468); }
REX_EXTERN(__imp__base013_sub_823C85E0);
REX_HOOK_RAW(base013_sub_823C85E0) { ssf2t::character::PlayerResultsCharacter(ctx, base, __imp__base013_sub_823C85E0); }
REX_EXTERN(__imp__base013_sub_823C8540);
REX_HOOK_RAW(base013_sub_823C8540) { ssf2t::character::ResetPlayerResults(ctx, base, __imp__base013_sub_823C8540); }

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_823C3970);
REX_HOOK_RAW(tu023_sub_823C3970) {
  ssf2t::character::RecordMatch<ssf2t::character::TitleUpdate023Results>(ctx, base, __imp__tu023_sub_823C3970);
}
REX_EXTERN(__imp__tu023_sub_823C8A60);
REX_HOOK_RAW(tu023_sub_823C8A60) { ssf2t::character::PlayerResults(ctx, base, __imp__tu023_sub_823C8A60); }
REX_EXTERN(__imp__tu023_sub_823C8BD8);
REX_HOOK_RAW(tu023_sub_823C8BD8) { ssf2t::character::PlayerResultsCharacter(ctx, base, __imp__tu023_sub_823C8BD8); }
REX_EXTERN(__imp__tu023_sub_823C8B38);
REX_HOOK_RAW(tu023_sub_823C8B38) { ssf2t::character::ResetPlayerResults(ctx, base, __imp__tu023_sub_823C8B38); }
#endif
