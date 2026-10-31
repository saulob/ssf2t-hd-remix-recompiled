#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include "guest_memory.h"
#include "online_runtime.h"
#include "player_match_guest.h"
#include "player_match_lobby.h"
#include "title_update_image.h"

REXCVAR_DECLARE(bool, ssf2t_online_enabled);

// DEBUG_SAULO: telemetry of the game's own lobby code, to find why a joined Online Match lobby is left.
namespace {

using ssf2t::LoadBE32;
using ssf2t::LoadBE64;

constexpr uint32_t kPrintBufferBytes = 1024;
constexpr uint32_t kPacketsLogged = 16;

std::mutex g_print_mutex;
ssf2t::RepeatFilter g_print_repeats;
ssf2t::LineBudget g_print_budget(40);
std::atomic<uint32_t> g_packets_logged{0};

double NowSeconds() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

const uint8_t* GuestPtr(const uint8_t* base, uint32_t address) { return ssf2t::GuestHost(base, address); }

template <typename G>
const uint8_t* Table(const uint8_t* base) {
  return base + G::kNetManager + ssf2t::kPlayerTableOffset;
}

template <typename G>
std::string TableText(const uint8_t* base) {
  const ssf2t::LobbyTable t = ssf2t::ReadLobbyTable(Table<G>(base));
  return "players=" + std::to_string(t.players) + " public=" + std::to_string(t.public_used) + "/" +
         std::to_string(t.public_slots) + " private=" + std::to_string(t.private_used) + "/" +
         std::to_string(t.private_slots) +
         " op=" + ssf2t::OnlineOpName(LoadBE32(base, G::kNetManager + ssf2t::kCurrentOpOffset));
}

template <typename G>
struct LobbyHooks {
  // The game formats its network debug lines into one buffer and discards them.
  static void NetPrintf(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    original(ctx, base);
    if (!REXCVAR_GET(ssf2t_online_enabled)) return;
    const char* text = reinterpret_cast<const char*>(base + G::kNetPrintBuffer);
    std::string_view line(text, strnlen(text, kPrintBufferBytes));
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.remove_suffix(1);
    if (line.empty()) return;
    std::lock_guard<std::mutex> lock(g_print_mutex);
    const int repeats = g_print_repeats.Next(line);
    if (repeats < 0 || !g_print_budget.Allow(NowSeconds())) return;
    if (repeats > 0) REXLOG_INFO("DEBUG_SAULO guest net: (previous line repeated {} more times)", repeats);
    if (const uint32_t dropped = g_print_budget.TakeDropped()) {
      REXLOG_INFO("DEBUG_SAULO guest net: ({} lines dropped by the rate limit)", dropped);
    }
    REXLOG_INFO("DEBUG_SAULO guest net: {}", line);
  }

  // Shared by every op: only the wait for the host's player list is held to the network establishment's budget
  // instead of its own 10 s, which would end it while the transport is still being chosen.
  static void OpTimerExpired(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    original(ctx, base);
    const uint32_t op = LoadBE32(base, G::kNetManager + ssf2t::kCurrentOpOffset);
    if (op != ssf2t::online::kWaitForPlayerListOp || !REXCVAR_GET(ssf2t_online_enabled)) return;
    const bool expired = ssf2t::online::WaitTimerExpired(op, ssf2t::Online().GuestWaitState(), ctx.r3.u32 & 0xFF);
    ctx.r3.u64 = expired ? 1 : 0;
  }

  static void WaitForPlayerListDone(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const bool connected = base[G::kNetManager + ssf2t::kPlayerListReceivedOffset] != 0;
    if (REXCVAR_GET(ssf2t_online_enabled)) ssf2t::Online().NoteGuestWaitComplete(connected);
    original(ctx, base);
  }

  static void LobbyFatalError(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const int32_t code = ctx.r3.s32;
    const uint32_t site = uint32_t(ctx.lr) - 4;
    REXLOG_INFO("DEBUG_SAULO player_match leave requested reason={} error={} caller={:08X} guest={}",
                ssf2t::LobbyFatalReason(site, G::kTitleUpdate), code, site, G::kName);
    REXLOG_INFO("DEBUG_SAULO player_match leave state {}", TableText<G>(base));
    original(ctx, base);
  }

  // (handler, connection, packet, size, ...): the first packets of each lobby, with the table they leave behind.
  static void LobbyPacket(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const uint32_t connection = ctx.r4.u32;
    const uint32_t packet = ctx.r5.u32;
    const uint32_t size = ctx.r6.u32;
    const uint32_t n = g_packets_logged.load();
    const bool logged = n < kPacketsLogged && packet && size;
    if (logged) {
      g_packets_logged = n + 1;
      const uint8_t* data = GuestPtr(base, packet);
      REXLOG_INFO("DEBUG_SAULO player_match lobby packet n={} conn={} type={}({}) size={} head={} before {}", n + 1,
                  int32_t(connection), ssf2t::LobbyPacketName(data[0]), data[0], size,
                  ssf2t::HexHead(data, size), TableText<G>(base));
    }
    original(ctx, base);
    if (logged) REXLOG_INFO("DEBUG_SAULO player_match lobby packet n={} after {}", n + 1, TableText<G>(base));
  }

  // (table, player, slot): every player the lobby takes in, local or remote.
  static void PlayerInsert(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const uint32_t player = ctx.r4.u32;
    const int32_t slot = ctx.r5.s32;
    const uint32_t before = ssf2t::ReadLobbyTable(Table<G>(base)).players;
    if (player) {
      const uint8_t* entry = GuestPtr(base, player);
      REXLOG_INFO("DEBUG_SAULO player_match remote insert begin slot={} xuid={:016X} private={} {}", slot,
                  LoadBE64(entry, 8), entry[54], TableText<G>(base));
    }
    original(ctx, base);
    const uint32_t after = ssf2t::ReadLobbyTable(Table<G>(base)).players;
    REXLOG_INFO("DEBUG_SAULO player_match remote insert result={} slot={}", ctx.r3.s32, slot);
    REXLOG_INFO("DEBUG_SAULO player_match GameLobby players before={} after={} guest={}", before, after, G::kName);
  }
};

struct Base013 : ssf2t::Base013PlayerMatch {
  static constexpr bool kTitleUpdate = false;
};
struct TitleUpdate023 : ssf2t::TitleUpdate023PlayerMatch {
  static constexpr bool kTitleUpdate = true;
};

using Base = LobbyHooks<Base013>;
using Update = LobbyHooks<TitleUpdate023>;

}  // namespace

namespace ssf2t {

void ResetLobbyTelemetry() { g_packets_logged = 0; }

}  // namespace ssf2t

REX_EXTERN(__imp__base013_sub_8242AD00);
REX_EXTERN(__imp__base013_sub_82426B28);
REX_EXTERN(__imp__base013_sub_82428C20);
REX_EXTERN(__imp__base013_sub_824246D0);
REX_EXTERN(__imp__base013_sub_8241E8E8);
REX_EXTERN(__imp__base013_sub_824282C8);
REX_HOOK_RAW(base013_sub_8242AD00) { Base::NetPrintf(ctx, base, __imp__base013_sub_8242AD00); }
REX_HOOK_RAW(base013_sub_82426B28) { Base::LobbyFatalError(ctx, base, __imp__base013_sub_82426B28); }
REX_HOOK_RAW(base013_sub_82428C20) { Base::LobbyPacket(ctx, base, __imp__base013_sub_82428C20); }
REX_HOOK_RAW(base013_sub_824246D0) { Base::PlayerInsert(ctx, base, __imp__base013_sub_824246D0); }
REX_HOOK_RAW(base013_sub_8241E8E8) { Base::OpTimerExpired(ctx, base, __imp__base013_sub_8241E8E8); }
REX_HOOK_RAW(base013_sub_824282C8) { Base::WaitForPlayerListDone(ctx, base, __imp__base013_sub_824282C8); }

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_8242B460);
REX_EXTERN(__imp__tu023_sub_82427288);
REX_EXTERN(__imp__tu023_sub_82429380);
REX_EXTERN(__imp__tu023_sub_82424E30);
REX_EXTERN(__imp__tu023_sub_8241F0A8);
REX_EXTERN(__imp__tu023_sub_82428A28);
REX_HOOK_RAW(tu023_sub_8242B460) { Update::NetPrintf(ctx, base, __imp__tu023_sub_8242B460); }
REX_HOOK_RAW(tu023_sub_82427288) { Update::LobbyFatalError(ctx, base, __imp__tu023_sub_82427288); }
REX_HOOK_RAW(tu023_sub_82429380) { Update::LobbyPacket(ctx, base, __imp__tu023_sub_82429380); }
REX_HOOK_RAW(tu023_sub_82424E30) { Update::PlayerInsert(ctx, base, __imp__tu023_sub_82424E30); }
REX_HOOK_RAW(tu023_sub_8241F0A8) { Update::OpTimerExpired(ctx, base, __imp__tu023_sub_8241F0A8); }
REX_HOOK_RAW(tu023_sub_82428A28) { Update::WaitForPlayerListDone(ctx, base, __imp__tu023_sub_82428A28); }
#endif
