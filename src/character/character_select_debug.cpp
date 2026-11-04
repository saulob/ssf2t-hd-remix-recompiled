// DEBUG_SAULO temporary Character Select tracing.
#include "character/character_select_debug.h"

#include <array>
#include <cstdint>

#include <rex/hook.h>
#include <rex/logging.h>

#include "character/ultra_fighters.h"
#include "guest_memory.h"
#include "netplay/guest_netplay.h"
#include "title_update_image.h"

namespace ssf2t::character::debug {
namespace {

enum FieldId {
  kGame08,
  kGame0C,
  kGame10,
  kGame12,
  kGame16,
  kGame18,
  kGame34,
  kGame36,
  kGame397,
  kGame398,
  kGame481,
  kGame496,
  kGame4C9,
  kGame667,
  kSelect08,
  kSelect0C,
  kSelect34,
  kSelect36,
  kBlock0F88,
  kBlock0F8C,
  kBlockA9D,
  kBlockE9D,
  kBlockAC1,
  kBlockEC1,
  kFieldCount,
};

enum class Base { kGame, kSelect, kBlock };

struct FieldSpec {
  Base base;
  uint32_t offset;
  uint32_t size;
  const char* name;
};

constexpr std::array<FieldSpec, kFieldCount> kFields = {{
    {Base::kGame, 0x08, 2, "game+0x08"},
    {Base::kGame, 0x0C, 2, "game+0x0C"},
    {Base::kGame, 0x10, 2, "game+0x10"},
    {Base::kGame, 0x12, 2, "game+0x12"},
    {Base::kGame, 0x16, 2, "game+0x16"},
    {Base::kGame, 0x18, 2, "game+0x18"},
    {Base::kGame, 0x34, 2, "game+0x34"},
    {Base::kGame, 0x36, 2, "game+0x36"},
    {Base::kGame, 0x397, 1, "game+0x397"},
    {Base::kGame, 0x398, 1, "game+0x398"},
    {Base::kGame, 0x481, 1, "game+0x481"},
    {Base::kGame, 0x496, 1, "game+0x496"},
    {Base::kGame, 0x4C9, 1, "game+0x4C9"},
    {Base::kGame, 0x667, 1, "game+0x667"},
    {Base::kSelect, 0x08, 2, "select+0x08"},
    {Base::kSelect, 0x0C, 2, "select+0x0C"},
    {Base::kSelect, 0x34, 2, "select+0x34"},
    {Base::kSelect, 0x36, 2, "select+0x36"},
    {Base::kBlock, 0x0F88, 2, "block+0x0F88"},
    {Base::kBlock, 0x0F8C, 2, "block+0x0F8C"},
    {Base::kBlock, 0x0A9D, 1, "block+0x0A9D"},
    {Base::kBlock, 0x0E9D, 1, "block+0x0E9D"},
    {Base::kBlock, 0x0AC1, 1, "block+0x0AC1"},
    {Base::kBlock, 0x0EC1, 1, "block+0x0EC1"},
}};

constexpr std::array<int, 6> kStateFields = {kSelect08, kSelect0C, kGame34, kGame36,
                                             kBlock0F88, kBlock0F8C};

struct Snapshot {
  uint32_t game = 0;
  uint32_t select = 0;
  uint32_t block = 0;
  std::array<uint32_t, kFieldCount> value{};
  std::array<bool, kFieldCount> valid{};
};

std::array<uint32_t, 2> g_block_char = {{0, 0}};
uint32_t g_game481 = 0;
uint32_t g_game496 = 0;
bool g_have_correlation = false;

bool Plausible(uint32_t address) { return address >= 0x10000 && address < 0xFFFF0000u; }

uint32_t ReadValue(uint8_t* base, uint32_t address, uint32_t size) {
  if (size == 1) return *GuestHost(base, address);
  if (size == 2) return LoadBE16(GuestHost(base, address), 0);
  return LoadBE32(GuestHost(base, address), 0);
}

template <typename G>
Snapshot Capture(uint8_t* base) {
  Snapshot snapshot;
  snapshot.game = LoadBE32(GuestHost(base, G::kFightWork), 0);
  snapshot.select = LoadBE32(GuestHost(base, G::kFightWork - 4), 0);
  snapshot.block = LoadBE32(GuestHost(base, G::kFighterBlockPointer), 0);
  const bool game_ok = Plausible(snapshot.game);
  const bool select_ok = Plausible(snapshot.select);
  const bool block_ok = Plausible(snapshot.block);
  for (size_t i = 0; i < kFields.size(); ++i) {
    const FieldSpec& field = kFields[i];
    uint32_t parent = 0;
    bool ok = false;
    if (field.base == Base::kGame) {
      parent = snapshot.game;
      ok = game_ok;
    } else if (field.base == Base::kSelect) {
      parent = snapshot.select;
      ok = select_ok;
    } else {
      parent = snapshot.block;
      ok = block_ok;
    }
    if (!ok) continue;
    const uint32_t address = parent + field.offset;
    if (!Plausible(address)) continue;
    snapshot.value[i] = ReadValue(base, address, field.size);
    snapshot.valid[i] = true;
  }
  return snapshot;
}

void UpdateCorrelation(const Snapshot& snapshot) {
  if (snapshot.valid[kBlockA9D]) g_block_char[0] = snapshot.value[kBlockA9D];
  if (snapshot.valid[kBlockE9D]) g_block_char[1] = snapshot.value[kBlockE9D];
  if (snapshot.valid[kGame481]) g_game481 = snapshot.value[kGame481];
  if (snapshot.valid[kGame496]) g_game496 = snapshot.value[kGame496];
  g_have_correlation = snapshot.valid[kBlockA9D] || snapshot.valid[kBlockE9D] || snapshot.valid[kGame481] ||
                       snapshot.valid[kGame496];
}

void LogPointers(const char* event, const Snapshot& snapshot, int port) {
  REXLOG_INFO("DEBUG_SAULO SELECT {} port={} game={:08X} select={:08X} block={:08X}", event, port, snapshot.game,
              snapshot.select, snapshot.block);
}

void LogField(const char* event, int port, const FieldSpec& field, uint32_t old_value, uint32_t new_value) {
  REXLOG_INFO("DEBUG_SAULO SELECT change event={} port={} field={} old={} new={}", event, port, field.name,
              old_value, new_value);
}

void LogDiff(const char* event, const Snapshot& before, const Snapshot& after, int port, bool state_only) {
  for (size_t i = 0; i < kFields.size(); ++i) {
    if (!after.valid[i]) continue;
    if (state_only) {
      bool watched = false;
      for (const int index : kStateFields) {
        if (static_cast<int>(i) == index) {
          watched = true;
          break;
        }
      }
      if (!watched) continue;
    }
    const uint32_t old_value = before.valid[i] ? before.value[i] : 0;
    if (before.valid[i] && old_value == after.value[i]) continue;
    LogField(event, port, kFields[i], old_value, after.value[i]);
  }
  UpdateCorrelation(after);
}

template <typename G>
void TraceInit(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (!UltraFightersEnabled()) {
    original(ctx, base);
    return;
  }
  original(ctx, base);
  const Snapshot snapshot = Capture<G>(base);
  LogPointers("init", snapshot, -1);
  LogDiff("init", Snapshot{}, snapshot, -1, false);
}

template <typename G>
void TraceDispatch(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (!UltraFightersEnabled()) {
    original(ctx, base);
    return;
  }
  const Snapshot before = Capture<G>(base);
  original(ctx, base);
  LogDiff("dispatch", before, Capture<G>(base), -1, true);
}

template <typename G>
void TraceInput(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (!UltraFightersEnabled()) {
    original(ctx, base);
    return;
  }
  const int port = int(ctx.r3.u32);
  const Snapshot before = Capture<G>(base);
  original(ctx, base);
  const Snapshot after = Capture<G>(base);
  LogDiff("input", before, after, port, false);
  if (before.game != after.game || before.select != after.select || before.block != after.block) {
    LogPointers("input", after, port);
  }
}

template <typename G>
void TraceConfirm(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (!UltraFightersEnabled()) {
    original(ctx, base);
    return;
  }
  const int port = int(ctx.r3.u32);
  const Snapshot before = Capture<G>(base);
  original(ctx, base);
  const Snapshot after = Capture<G>(base);
  REXLOG_INFO("DEBUG_SAULO SELECT confirm port={}", port);
  LogDiff("confirm", before, after, port, false);
}

template <typename G>
void TraceNested(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (!UltraFightersEnabled()) {
    original(ctx, base);
    return;
  }
  const Snapshot before = Capture<G>(base);
  original(ctx, base);
  LogDiff("state", before, Capture<G>(base), -1, false);
}

template <typename G>
void TraceCancel(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (!UltraFightersEnabled()) {
    original(ctx, base);
    return;
  }
  const Snapshot before = Capture<G>(base);
  original(ctx, base);
  const Snapshot after = Capture<G>(base);
  REXLOG_INFO("DEBUG_SAULO SELECT cancel");
  LogDiff("cancel", before, after, -1, false);
}

template <typename G>
void TraceMove(PPCContext& ctx, uint8_t* base, PPCFunc* original, const char* event) {
  if (!UltraFightersEnabled()) {
    original(ctx, base);
    return;
  }
  const Snapshot before = Capture<G>(base);
  original(ctx, base);
  LogDiff(event, before, Capture<G>(base), -1, false);
}

}  // namespace

void LogSetup(int side, int guest, bool hidden, FighterIdentity identity, FighterIdentity requested) {
  if (!UltraFightersEnabled()) return;
  const uint32_t block_char = side >= 0 && side < 2 ? g_block_char[side] : 0;
  REXLOG_INFO(
      "DEBUG_SAULO SELECT setup side={} guest={} hidden={} identity={} requested={} block_char={} game481={} "
      "game496={} correlated={}",
      side, guest, hidden ? 1 : 0, IdentityName(identity), IdentityName(requested), block_char, g_game481, g_game496,
      g_have_correlation ? 1 : 0);
}

}  // namespace ssf2t::character::debug

REX_EXTERN(__imp__base013_sub_82246180);
REX_HOOK_RAW(base013_sub_82246180) {
  ssf2t::character::debug::TraceInit<ssf2t::netplay::Base013Netplay>(ctx, base, __imp__base013_sub_82246180);
}
REX_EXTERN(__imp__base013_sub_82246738);
REX_HOOK_RAW(base013_sub_82246738) {
  ssf2t::character::debug::TraceDispatch<ssf2t::netplay::Base013Netplay>(ctx, base, __imp__base013_sub_82246738);
}
REX_EXTERN(__imp__base013_sub_82246798);
REX_HOOK_RAW(base013_sub_82246798) {
  ssf2t::character::debug::TraceConfirm<ssf2t::netplay::Base013Netplay>(ctx, base, __imp__base013_sub_82246798);
}
REX_EXTERN(__imp__base013_sub_82246820);
REX_HOOK_RAW(base013_sub_82246820) {
  ssf2t::character::debug::TraceInput<ssf2t::netplay::Base013Netplay>(ctx, base, __imp__base013_sub_82246820);
}
REX_EXTERN(__imp__base013_sub_822456D8);
REX_HOOK_RAW(base013_sub_822456D8) {
  ssf2t::character::debug::TraceNested<ssf2t::netplay::Base013Netplay>(ctx, base, __imp__base013_sub_822456D8);
}
REX_EXTERN(__imp__base013_sub_82243880);
REX_HOOK_RAW(base013_sub_82243880) {
  ssf2t::character::debug::TraceMove<ssf2t::netplay::Base013Netplay>(ctx, base, __imp__base013_sub_82243880, "move_a");
}
REX_EXTERN(__imp__base013_sub_82243A38);
REX_HOOK_RAW(base013_sub_82243A38) {
  ssf2t::character::debug::TraceMove<ssf2t::netplay::Base013Netplay>(ctx, base, __imp__base013_sub_82243A38, "move_b");
}
REX_EXTERN(__imp__base013_sub_82243AC8);
REX_HOOK_RAW(base013_sub_82243AC8) {
  ssf2t::character::debug::TraceMove<ssf2t::netplay::Base013Netplay>(ctx, base, __imp__base013_sub_82243AC8, "move_c");
}
REX_EXTERN(__imp__base013_sub_82243BA8);
REX_HOOK_RAW(base013_sub_82243BA8) {
  ssf2t::character::debug::TraceMove<ssf2t::netplay::Base013Netplay>(ctx, base, __imp__base013_sub_82243BA8, "move_d");
}
REX_EXTERN(__imp__base013_sub_82243C20);
REX_HOOK_RAW(base013_sub_82243C20) {
  ssf2t::character::debug::TraceCancel<ssf2t::netplay::Base013Netplay>(ctx, base, __imp__base013_sub_82243C20);
}

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_822461E8);
REX_HOOK_RAW(tu023_sub_822461E8) {
  ssf2t::character::debug::TraceInit<ssf2t::netplay::TitleUpdate023Netplay>(ctx, base, __imp__tu023_sub_822461E8);
}
REX_EXTERN(__imp__tu023_sub_82246780);
REX_HOOK_RAW(tu023_sub_82246780) {
  ssf2t::character::debug::TraceDispatch<ssf2t::netplay::TitleUpdate023Netplay>(ctx, base,
                                                                               __imp__tu023_sub_82246780);
}
REX_EXTERN(__imp__tu023_sub_822467E0);
REX_HOOK_RAW(tu023_sub_822467E0) {
  ssf2t::character::debug::TraceConfirm<ssf2t::netplay::TitleUpdate023Netplay>(ctx, base, __imp__tu023_sub_822467E0);
}
REX_EXTERN(__imp__tu023_sub_82246868);
REX_HOOK_RAW(tu023_sub_82246868) {
  ssf2t::character::debug::TraceInput<ssf2t::netplay::TitleUpdate023Netplay>(ctx, base, __imp__tu023_sub_82246868);
}
REX_EXTERN(__imp__tu023_sub_82245740);
REX_HOOK_RAW(tu023_sub_82245740) {
  ssf2t::character::debug::TraceNested<ssf2t::netplay::TitleUpdate023Netplay>(ctx, base, __imp__tu023_sub_82245740);
}
REX_EXTERN(__imp__tu023_sub_822438E8);
REX_HOOK_RAW(tu023_sub_822438E8) {
  ssf2t::character::debug::TraceMove<ssf2t::netplay::TitleUpdate023Netplay>(ctx, base, __imp__tu023_sub_822438E8,
                                                                           "move_a");
}
REX_EXTERN(__imp__tu023_sub_82243AA0);
REX_HOOK_RAW(tu023_sub_82243AA0) {
  ssf2t::character::debug::TraceMove<ssf2t::netplay::TitleUpdate023Netplay>(ctx, base, __imp__tu023_sub_82243AA0,
                                                                           "move_b");
}
REX_EXTERN(__imp__tu023_sub_82243B30);
REX_HOOK_RAW(tu023_sub_82243B30) {
  ssf2t::character::debug::TraceMove<ssf2t::netplay::TitleUpdate023Netplay>(ctx, base, __imp__tu023_sub_82243B30,
                                                                           "move_c");
}
REX_EXTERN(__imp__tu023_sub_82243C10);
REX_HOOK_RAW(tu023_sub_82243C10) {
  ssf2t::character::debug::TraceMove<ssf2t::netplay::TitleUpdate023Netplay>(ctx, base, __imp__tu023_sub_82243C10,
                                                                           "move_d");
}
REX_EXTERN(__imp__tu023_sub_82243C88);
REX_HOOK_RAW(tu023_sub_82243C88) {
  ssf2t::character::debug::TraceCancel<ssf2t::netplay::TitleUpdate023Netplay>(ctx, base,
                                                                              __imp__tu023_sub_82243C88);
}
#endif
