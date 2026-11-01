#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xevent.h>

#include "guest_memory.h"
#include "netplay_runtime.h"
#include "online_runtime.h"
#include "player_match.h"
#include "player_match_guest.h"
#include "player_match_net.h"
#include "title_update_image.h"

REXCVAR_DECLARE(bool, ssf2t_online_enabled);

namespace {

using ssf2t::GuestHost;
using ssf2t::LoadBE16;
using ssf2t::LoadBE32;
using ssf2t::StoreBE16;
using ssf2t::StoreBE32;

uint8_t* GuestPtr(uint8_t* base, uint32_t address) { return const_cast<uint8_t*>(GuestHost(base, address)); }

bool LiveNow() { return REXCVAR_GET(ssf2t_online_enabled) && ssf2t::Online().Profile().signed_in; }

std::optional<ssf2t::PeerRoute> ActiveRoute() { return ssf2t::RouteFor(ssf2t::Online().Peer()); }

std::string Dotted(uint32_t a) {
  char text[16];
  std::snprintf(text, sizeof text, "%u.%u.%u.%u", a >> 24, (a >> 16) & 0xFF, (a >> 8) & 0xFF, a & 0xFF);
  return text;
}

std::string HexHead(const uint8_t* data, size_t size) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string text;
  for (size_t i = 0; i < size && i < 16; ++i) {
    text += kHex[data[i] >> 4];
    text += kHex[data[i] & 0xF];
  }
  return text;
}

bool ShouldLogPacket(uint32_t count) { return count <= 8 || count % 256 == 0; }

class MuxLink final : public ssf2t::LobbyLink {
 public:
  bool Send(const ssf2t::netplay::LobbyDatagram& d) override { return ssf2t::SendPlayerMatchLobby(d); }
  bool Take(ssf2t::netplay::LobbyDatagram& d) override { return ssf2t::TakePlayerMatchLobby(d); }
};

std::mutex g_shim_mutex;
ssf2t::VdpShim g_shim;
MuxLink g_link;
thread_local uint32_t g_pending_error = 0;
uint32_t g_sent = 0;
uint32_t g_received = 0;
uint32_t g_mapped_address = 0;

bool OwnsSocket(uint32_t handle) {
  std::lock_guard<std::mutex> lock(g_shim_mutex);
  return g_shim.Owns(handle);
}

void LogChange(const char* what, int64_t value) {
  struct Seen {
    const char* what;
    int64_t value;
  };
  static Seen seen[8] = {};
  for (Seen& s : seen) {
    if (s.what == what && s.value == value) return;
    if (s.what == what || !s.what) {
      s = {what, value};
      REXLOG_INFO("player_match {} value={}", what, value);
      return;
    }
  }
}

template <typename G>
struct NetHooks {
  static void XnAddrToInAddr(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const uint32_t xnaddr = ctx.r3.u32;
    const uint32_t out = ctx.r5.u32;
    const auto xuid = xnaddr && out && LiveNow() ? ssf2t::XnAddrXuid(GuestPtr(base, xnaddr)) : std::nullopt;
    if (!xuid) {
      original(ctx, base);
      REXLOG_INFO("XNetXnAddrToInAddr original result={}", ctx.r3.u32);
      return;
    }
    const uint32_t address = ssf2t::SyntheticAddress(*xuid);
    StoreBE32(GuestPtr(base, out), 0, address);
    ctx.r3.u64 = 0;
    if (address != g_mapped_address) {
      g_mapped_address = address;
      REXLOG_INFO("player_match xnet map created xuid={:016X} address={}", *xuid, Dotted(address));
    }
  }

  static void InAddrToXnAddr(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const uint32_t address = ctx.r3.u32;
    const uint32_t out = ctx.r4.u32;
    const auto route = ssf2t::IsSyntheticAddress(address) && out ? ActiveRoute() : std::nullopt;
    if (!route || route->address != address) {
      original(ctx, base);
      return;
    }
    ssf2t::WriteXnAddr(GuestPtr(base, out), route->xuid);
    ctx.r3.u64 = 0;
    LogChange("xnet reverse map", address);
  }

  static void ConnectStatus(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const uint32_t address = ctx.r3.u32;
    const bool synthetic = ssf2t::IsSyntheticAddress(address);
    const auto route = synthetic ? ActiveRoute() : std::nullopt;
    if (!route || route->address != address) {
      // A match that could not connect (neither directly nor through the relay) is lost now, not after a timeout.
      const auto failed = synthetic ? ssf2t::RouteFor(ssf2t::Online().FailedPeer()) : std::nullopt;
      if (failed && failed->address == address) {
        ctx.r3.u64 = ssf2t::kXnetLost;
        LogChange("xnet connect status", ssf2t::kXnetLost);
        return;
      }
      original(ctx, base);
      return;
    }
    ctx.r3.u64 = ssf2t::kXnetConnected;
    LogChange("xnet connect status", ssf2t::kXnetConnected);
  }

  static void QosListen(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    constexpr uint32_t kSetData = 0x4;
    const uint32_t data = ctx.r4.u32;
    const uint32_t size = ctx.r5.u32;
    const uint32_t flags = ctx.r7.u32;
    if ((flags & kSetData) && data && size && size <= 512 && LiveNow()) {
      const uint8_t* bytes = GuestPtr(base, data);
      ssf2t::Online().SetPlayerMatchQos(std::vector<uint8_t>(bytes, bytes + size));
    }
    original(ctx, base);
  }

  static void QosLookup(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const uint32_t count = ctx.r3.u32;
    const uint32_t xnaddrs = ctx.r4.u32;
    const uint32_t stack = ctx.r1.u32;
    const uint32_t event = LoadBE32(base, stack + 100);
    const uint32_t result_ptr = LoadBE32(base, stack + 108);
    if (!LiveNow() || !xnaddrs || !result_ptr || count == 0 || count > 64) {
      original(ctx, base);
      return;
    }
    REXLOG_INFO("player_match qos begin targets={}", count);
    const std::vector<ssf2t::online::PlayerMatchAd> ads = ssf2t::Online().PlayerMatchResults();
    std::vector<ssf2t::QosTarget> targets;
    bool ours = false;
    for (uint32_t i = 0; i < count; ++i) {
      const uint32_t xnaddr = LoadBE32(GuestPtr(base, xnaddrs), i * 4);
      if (!xnaddr) {
        targets.emplace_back();
        continue;
      }
      ours = ours || ssf2t::XnAddrXuid(GuestPtr(base, xnaddr)).has_value();
      targets.push_back(ssf2t::QosTargetFor(GuestPtr(base, xnaddr), ads));
      const auto xuid = ssf2t::XnAddrXuid(GuestPtr(base, xnaddr));
      REXLOG_INFO("player_match qos lookup peer={:016X}", xuid.value_or(0));
      REXLOG_INFO("player_match qos result reachable={} rtt={} data={}", targets.back().reachable,
                  targets.back().rtt_ms, targets.back().data.size());
    }
    if (!ours) {
      original(ctx, base);
      return;
    }
    const uint32_t bytes = ssf2t::QosResultBytes(targets);
    const uint32_t address = REX_KERNEL_MEMORY()->SystemHeapAlloc(bytes);
    if (!address) {
      original(ctx, base);
      return;
    }
    ssf2t::WriteQosResult(GuestPtr(base, address), address, targets);
    StoreBE32(GuestPtr(base, result_ptr), 0, address);
    if (event) {
      if (auto ev = REX_KERNEL_OBJECTS()->LookupObject<rex::system::XEvent>(event)) ev->Set(0, false);
    }
    ctx.r3.u64 = 0;
    REXLOG_INFO("player_match qos complete targets={}", count);
  }

  static void Socket(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const uint32_t type = ctx.r4.u32;
    const uint32_t protocol = ctx.r5.u32;
    if (protocol != ssf2t::kVdpProtocol || !LiveNow()) {
      original(ctx, base);
      return;
    }
    std::lock_guard<std::mutex> lock(g_shim_mutex);
    ctx.r3.u64 = g_shim.Create();
    REXLOG_INFO("player_match socket create handle={:08X} type={} protocol={}", ctx.r3.u32, type,
                protocol);
  }

  static void Bind(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const uint32_t handle = ctx.r3.u32;
    if (!OwnsSocket(handle)) {
      original(ctx, base);
      return;
    }
    const uint16_t port = ctx.r4.u32 ? LoadBE16(GuestPtr(base, ctx.r4.u32), 2) : 0;
    std::lock_guard<std::mutex> lock(g_shim_mutex);
    g_shim.Bind(handle, port);
    ctx.r3.u64 = 0;
    REXLOG_INFO("player_match socket bind handle={:08X} port={}", handle, port);
  }

  static void Connect(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const uint32_t handle = ctx.r3.u32;
    if (!OwnsSocket(handle)) {
      original(ctx, base);
      return;
    }
    const uint32_t to = ctx.r4.u32;
    REXLOG_INFO("player_match socket connect handle={:08X} address={}", handle,
                to ? Dotted(LoadBE32(GuestPtr(base, to), 4)) : std::string("-"));
    ctx.r3.u64 = 0;
  }

  static void Ioctl(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    if (!OwnsSocket(ctx.r3.u32)) {
      original(ctx, base);
      return;
    }
    ctx.r3.u64 = 0;
  }

  static void Close(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const uint32_t handle = ctx.r3.u32;
    if (!OwnsSocket(handle)) {
      original(ctx, base);
      return;
    }
    std::lock_guard<std::mutex> lock(g_shim_mutex);
    g_shim.Close(handle);
    ctx.r3.u64 = 0;
    REXLOG_INFO("player_match socket close handle={:08X}", handle);
  }

  static void SendTo(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const uint32_t handle = ctx.r3.u32;
    if (!OwnsSocket(handle)) {
      original(ctx, base);
      return;
    }
    const uint32_t size = ctx.r5.u32;
    const uint8_t* data = GuestPtr(base, ctx.r4.u32);
    const uint32_t to = ctx.r7.u32;
    const uint32_t address = to ? LoadBE32(GuestPtr(base, to), 4) : 0;
    const uint16_t port = to ? LoadBE16(GuestPtr(base, to), 2) : 0;
    const auto route = ActiveRoute();
    bool routed = false;
    {
      std::lock_guard<std::mutex> lock(g_shim_mutex);
      routed = g_shim.SendTo(handle, address, port, data, size, route, g_link);
    }
    if (ShouldLogPacket(++g_sent)) {
      REXLOG_INFO("player_match lobby packet tx n={} handle={:08X} to={}:{} bytes={} routed={} head={}",
                  g_sent, handle, Dotted(address), port, size, routed, HexHead(data, size));
    }
    ctx.r3.u64 = size;
  }

  static void RecvFrom(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const uint32_t handle = ctx.r3.u32;
    if (!OwnsSocket(handle)) {
      original(ctx, base);
      return;
    }
    const uint32_t buffer = ctx.r4.u32;
    const uint32_t capacity = ctx.r5.u32;
    const uint32_t from = ctx.r7.u32;
    const uint32_t from_size = ctx.r8.u32;
    std::optional<ssf2t::VdpShim::Received> received;
    {
      const auto route = ActiveRoute();
      std::lock_guard<std::mutex> lock(g_shim_mutex);
      received = g_shim.RecvFrom(handle, route, g_link);
    }
    if (!received) {
      g_pending_error = ssf2t::kWouldBlock;
      ctx.r3.u64 = 0xFFFFFFFFu;
      return;
    }
    const uint32_t size = std::min<uint32_t>(capacity, uint32_t(received->data.size()));
    std::memcpy(GuestPtr(base, buffer), received->data.data(), size);
    if (from) {
      uint8_t* sockaddr = GuestPtr(base, from);
      std::memset(sockaddr, 0, 16);
      StoreBE16(sockaddr, 0, 2);
      StoreBE16(sockaddr, 2, received->from_port);
      StoreBE32(sockaddr, 4, received->from_address);
    }
    if (from_size) StoreBE32(GuestPtr(base, from_size), 0, 16);
    if (ShouldLogPacket(++g_received)) {
      REXLOG_INFO("player_match lobby packet rx n={} handle={:08X} from={}:{} bytes={} head={}",
                  g_received, handle, Dotted(received->from_address), received->from_port, size,
                  HexHead(received->data.data(), size));
    }
    ctx.r3.u64 = size;
  }

  static void LastError(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    if (g_pending_error) {
      ctx.r3.u64 = g_pending_error;
      g_pending_error = 0;
      return;
    }
    original(ctx, base);
  }

  static void GetName(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const uint32_t user = ctx.r3.u32;
    const uint32_t buffer = ctx.r4.u32;
    const uint32_t size = ctx.r5.u32;
    const ssf2t::online::OnlineProfile profile =
        user == ssf2t::LiveUserIndex(LoadBE32(base, G::kUserIndex)) && buffer && size &&
                REXCVAR_GET(ssf2t_online_enabled)
            ? ssf2t::Online().Profile()
            : ssf2t::online::OnlineProfile{};
    if (!profile.signed_in || profile.name.empty()) {
      original(ctx, base);
      return;
    }
    const size_t n = std::min<size_t>(profile.name.size(), std::min<uint32_t>(size, 16) - 1);
    std::memset(GuestPtr(base, buffer), 0, std::min<uint32_t>(size, 16));
    std::memcpy(GuestPtr(base, buffer), profile.name.data(), n);
    ctx.r3.u64 = 0;
  }

  static void InLobby(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    original(ctx, base);
    if (LiveNow()) LogChange("GameLobby in_lobby", ctx.r3.u32 & 0xFF);
  }

  static void NumPlayersInLobby(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    original(ctx, base);
    if (!LiveNow()) return;
    LogChange("GameLobby players", ctx.r3.s32);
    if (ctx.r3.s32 >= 2) LogChange("remote player visible", 1);
  }

  static void GetIsReady(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const int32_t slot = ctx.r3.s32;
    original(ctx, base);
    if (!LiveNow() || slot < 0 || slot > 1) return;
    LogChange(slot == 0 ? "ready slot0" : "ready slot1", ctx.r3.u32 & 0xFF);
  }

  static void SetGameStarted(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const uint32_t started = ctx.r3.u32;
    original(ctx, base);
    if (started && LiveNow()) REXLOG_INFO("player_match game start guest={}", G::kName);
  }
};

using Base = NetHooks<ssf2t::Base013PlayerMatch>;
using Update = NetHooks<ssf2t::TitleUpdate023PlayerMatch>;

}  // namespace

#define SSF2T_NET_HOOK(prefix, address, Traits, handler) \
  REX_EXTERN(__imp__##prefix##_sub_##address);              \
  REX_HOOK_RAW(prefix##_sub_##address) { Traits::handler(ctx, base, __imp__##prefix##_sub_##address); }

SSF2T_NET_HOOK(base013, 839C1D48, Base, XnAddrToInAddr)
SSF2T_NET_HOOK(base013, 839C1D60, Base, InAddrToXnAddr)
SSF2T_NET_HOOK(base013, 839C1D78, Base, ConnectStatus)
SSF2T_NET_HOOK(base013, 839C1D88, Base, QosListen)
SSF2T_NET_HOOK(base013, 839C1DA8, Base, QosLookup)
SSF2T_NET_HOOK(base013, 839C1EE8, Base, Socket)
SSF2T_NET_HOOK(base013, 839C1F00, Base, Close)
SSF2T_NET_HOOK(base013, 839C1F10, Base, Ioctl)
SSF2T_NET_HOOK(base013, 839C1F28, Base, Bind)
SSF2T_NET_HOOK(base013, 839C1F40, Base, Connect)
SSF2T_NET_HOOK(base013, 839C1F80, Base, RecvFrom)
SSF2T_NET_HOOK(base013, 839C1FB8, Base, SendTo)
SSF2T_NET_HOOK(base013, 839C1FE0, Base, LastError)
SSF2T_NET_HOOK(base013, 839C34C8, Base, GetName)
SSF2T_NET_HOOK(base013, 823C2200, Base, InLobby)
SSF2T_NET_HOOK(base013, 823BDA08, Base, NumPlayersInLobby)
SSF2T_NET_HOOK(base013, 823C7BB0, Base, GetIsReady)
SSF2T_NET_HOOK(base013, 823C7C28, Base, SetGameStarted)

#if SSF2T_TITLE_UPDATE_IMAGE
SSF2T_NET_HOOK(tu023, 839C24A8, Update, XnAddrToInAddr)
SSF2T_NET_HOOK(tu023, 839C24C0, Update, InAddrToXnAddr)
SSF2T_NET_HOOK(tu023, 839C24D8, Update, ConnectStatus)
SSF2T_NET_HOOK(tu023, 839C24E8, Update, QosListen)
SSF2T_NET_HOOK(tu023, 839C2508, Update, QosLookup)
SSF2T_NET_HOOK(tu023, 839C2648, Update, Socket)
SSF2T_NET_HOOK(tu023, 839C2660, Update, Close)
SSF2T_NET_HOOK(tu023, 839C2670, Update, Ioctl)
SSF2T_NET_HOOK(tu023, 839C2688, Update, Bind)
SSF2T_NET_HOOK(tu023, 839C26A0, Update, Connect)
SSF2T_NET_HOOK(tu023, 839C26E0, Update, RecvFrom)
SSF2T_NET_HOOK(tu023, 839C2718, Update, SendTo)
SSF2T_NET_HOOK(tu023, 839C2740, Update, LastError)
SSF2T_NET_HOOK(tu023, 839C3C28, Update, GetName)
SSF2T_NET_HOOK(tu023, 823C27A8, Update, InLobby)
SSF2T_NET_HOOK(tu023, 823BD9C0, Update, NumPlayersInLobby)
SSF2T_NET_HOOK(tu023, 823C81A8, Update, GetIsReady)
SSF2T_NET_HOOK(tu023, 823C8220, Update, SetGameStarted)
#endif
