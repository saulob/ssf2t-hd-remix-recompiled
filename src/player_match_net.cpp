#include "player_match_net.h"

#include <algorithm>
#include <cstring>

#include "guest_memory.h"
#include "player_match.h"

namespace ssf2t {
namespace {

constexpr uint32_t kInaOffset = 0;
constexpr uint32_t kInaOnlineOffset = 4;
constexpr uint32_t kEnetOffset = 10;
constexpr uint32_t kOnlineIdOffset = 16;
constexpr uint8_t kXnAddrTag[4] = {'S', 'F', 'H', 'D'};
constexpr size_t kMaxQueued = 128;

}  // namespace

uint32_t SyntheticAddress(uint64_t xuid) {
  const uint32_t hi = 1 + uint32_t((xuid >> 8) & 0xFF) % 254;
  const uint32_t lo = 1 + uint32_t(xuid & 0xFF) % 254;
  return kSyntheticNet | hi << 8 | lo;
}

void WriteXnAddr(uint8_t* xnaddr, uint64_t xuid) {
  std::memset(xnaddr, 0, kXnAddrBytes);
  const uint32_t address = SyntheticAddress(xuid);
  StoreBE32(xnaddr, kInaOffset, address);
  StoreBE32(xnaddr, kInaOnlineOffset, address);
  for (uint32_t i = 0; i < 6; ++i) xnaddr[kEnetOffset + i] = uint8_t(xuid >> (40 - 8 * i));
  StoreBE64(xnaddr, kOnlineIdOffset, xuid);
  std::memcpy(xnaddr + kOnlineIdOffset + 8, kXnAddrTag, sizeof(kXnAddrTag));
}

std::optional<uint64_t> XnAddrXuid(const uint8_t* xnaddr) {
  if (std::memcmp(xnaddr + kOnlineIdOffset + 8, kXnAddrTag, sizeof(kXnAddrTag)) != 0) return std::nullopt;
  const uint64_t xuid = LoadBE64(xnaddr, kOnlineIdOffset);
  if (LoadBE32(xnaddr, kInaOffset) != SyntheticAddress(xuid)) return std::nullopt;
  return xuid;
}

uint32_t SessionTag(std::string_view match) {
  uint32_t hash = 0x811C9DC5u;
  for (const char c : match) {
    hash ^= static_cast<unsigned char>(c);
    hash *= 0x01000193u;
  }
  return hash ? hash : 1;
}

std::optional<PeerRoute> RouteFor(const std::optional<online::ActivePeer>& peer) {
  if (!peer || peer->match.empty() || peer->peer_player.empty()) return std::nullopt;
  PeerRoute route;
  route.xuid = VirtualXuid(peer->peer_player);
  route.address = SyntheticAddress(route.xuid);
  route.tag = SessionTag(peer->match);
  route.host = peer->host;
  return route;
}

uint32_t QosResultBytes(const std::vector<QosTarget>& targets) {
  uint32_t bytes = kQosHeaderBytes + uint32_t(std::max<size_t>(targets.size(), 1)) * kQosInfoBytes;
  for (const QosTarget& t : targets) bytes += uint32_t(t.data.size());
  return bytes;
}

void WriteQosResult(uint8_t* out, uint32_t address, const std::vector<QosTarget>& targets) {
  std::memset(out, 0, QosResultBytes(targets));
  StoreBE32(out, 0, uint32_t(targets.size()));
  uint32_t data_at = kQosHeaderBytes + uint32_t(std::max<size_t>(targets.size(), 1)) * kQosInfoBytes;
  for (size_t i = 0; i < targets.size(); ++i) {
    const QosTarget& t = targets[i];
    uint8_t* info = out + kQosHeaderBytes + i * kQosInfoBytes;
    if (!t.reachable) {
      info[0] = kQosComplete | kQosTargetDisabled;
      continue;
    }
    info[0] = kQosComplete | kQosTargetContacted | (t.data.empty() ? 0 : kQosDataReceived);
    StoreBE16(info, 2, 1);
    StoreBE16(info, 4, 1);
    StoreBE16(info, 6, uint16_t(t.data.size()));
    StoreBE32(info, 8, t.data.empty() ? 0 : address + data_at);
    StoreBE16(info, 12, t.rtt_ms);
    StoreBE16(info, 14, t.rtt_ms);
    if (!t.data.empty()) std::memcpy(out + data_at, t.data.data(), t.data.size());
    data_at += uint32_t(t.data.size());
  }
}

QosTarget QosTargetFor(const uint8_t* xnaddr, const std::vector<online::PlayerMatchAd>& ads) {
  QosTarget target;
  const std::optional<uint64_t> xuid = XnAddrXuid(xnaddr);
  if (!xuid) return target;
  for (const online::PlayerMatchAd& ad : ads) {
    if (VirtualXuid(ad.player) != *xuid) continue;
    target.data = online::BytesOfHex(ad.qos);
    target.reachable = !target.data.empty();
    target.rtt_ms = kUnmeasuredRttMs;
    break;
  }
  return target;
}

uint32_t VdpShim::Create() {
  const uint32_t handle = next_++;
  sockets_[handle] = {};
  return handle;
}

void VdpShim::Bind(uint32_t handle, uint16_t port) {
  if (auto it = sockets_.find(handle); it != sockets_.end()) it->second.port = port;
}

void VdpShim::Close(uint32_t handle) { sockets_.erase(handle); }

void VdpShim::Follow(const std::optional<PeerRoute>& route) {
  const uint32_t tag = route ? route->tag : 0;
  if (tag == tag_) return;
  tag_ = tag;
  for (auto& [handle, socket] : sockets_) socket.queue.clear();
}

bool VdpShim::SendTo(uint32_t handle, uint32_t to_address, uint16_t to_port, const uint8_t* data, size_t size,
                     const std::optional<PeerRoute>& route, LobbyLink& link) {
  Follow(route);
  const auto it = sockets_.find(handle);
  if (it == sockets_.end() || !route || to_address != route->address) return false;
  netplay::LobbyDatagram datagram;
  datagram.from_port = it->second.port;
  datagram.to_port = to_port;
  datagram.data.assign(data, data + size);
  return link.Send(datagram);
}

std::optional<VdpShim::Received> VdpShim::RecvFrom(uint32_t handle, const std::optional<PeerRoute>& route,
                                                   LobbyLink& link) {
  Follow(route);
  if (!route || !Owns(handle)) return std::nullopt;
  netplay::LobbyDatagram datagram;
  while (link.Take(datagram)) {
    for (auto& [h, socket] : sockets_) {
      if (socket.port == datagram.to_port && socket.queue.size() < kMaxQueued) {
        socket.queue.push_back(std::move(datagram));
        break;
      }
    }
  }
  Socket& socket = sockets_[handle];
  if (socket.queue.empty()) return std::nullopt;
  Received received{route->address, socket.queue.front().from_port, std::move(socket.queue.front().data)};
  socket.queue.pop_front();
  return received;
}

uint16_t VdpShim::bound_port(uint32_t handle) const {
  const auto it = sockets_.find(handle);
  return it == sockets_.end() ? 0 : it->second.port;
}

size_t VdpShim::queued(uint32_t handle) const {
  const auto it = sockets_.find(handle);
  return it == sockets_.end() ? 0 : it->second.queue.size();
}

}  // namespace ssf2t
