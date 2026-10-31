#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

#include "netplay/lobby_mux.h"
#include "online/online_service.h"

namespace ssf2t {

// The guest addresses a Player Match peer by a synthetic IN_ADDR in 10.83.0.0/16 derived from its virtual XUID; it
// never sees a real address. Host byte order.
inline constexpr uint32_t kSyntheticNet = 0x0A530000;
uint32_t SyntheticAddress(uint64_t xuid);
constexpr bool IsSyntheticAddress(uint32_t address) { return (address & 0xFFFF0000u) == kSyntheticNet; }

// XNADDR: ina (4), inaOnline (4), wPortOnline (2), abEnet (6), abOnline (20).
inline constexpr uint32_t kXnAddrBytes = 36;
void WriteXnAddr(uint8_t* xnaddr, uint64_t xuid);
// The XUID of an XNADDR written by WriteXnAddr.
std::optional<uint64_t> XnAddrXuid(const uint8_t* xnaddr);

uint32_t SessionTag(std::string_view match);

// The other player of the active Player Match, as the guest and the mux see it.
struct PeerRoute {
  uint32_t address = 0;
  uint64_t xuid = 0;
  uint32_t tag = 0;
  bool host = false;
};
std::optional<PeerRoute> RouteFor(const std::optional<online::ActivePeer>& peer);

// XNQOS: cxnqos, cxnqosPending, then one XNQOSINFO (24 bytes) per probed address, then the probe data.
inline constexpr uint32_t kQosHeaderBytes = 8;
inline constexpr uint32_t kQosInfoBytes = 24;
inline constexpr uint8_t kQosComplete = 0x01;
inline constexpr uint8_t kQosTargetContacted = 0x02;
inline constexpr uint8_t kQosTargetDisabled = 0x04;
inline constexpr uint8_t kQosDataReceived = 0x08;
// No probe has run before the join; the game only needs a plausible value.
inline constexpr uint16_t kUnmeasuredRttMs = 50;

struct QosTarget {
  bool reachable = false;
  std::vector<uint8_t> data;
  uint16_t rtt_ms = 0;
};
uint32_t QosResultBytes(const std::vector<QosTarget>& targets);
// `out` is the host view of guest address `address`, at least QosResultBytes() long.
void WriteQosResult(uint8_t* out, uint32_t address, const std::vector<QosTarget>& targets);
// The probe target for an XNADDR: a friend's advertised Player Match with its host's QoS listen data.
QosTarget QosTargetFor(const uint8_t* xnaddr, const std::vector<online::PlayerMatchAd>& ads);

// Where the shim sends and takes lobby datagrams.
class LobbyLink {
 public:
  virtual ~LobbyLink() = default;
  virtual bool Send(const netplay::LobbyDatagram& datagram) = 0;
  virtual bool Take(netplay::LobbyDatagram& out) = 0;
};

// The title's VDP sockets while Player Match is online: guest handles that never reach a host socket. A datagram to
// the peer's synthetic address goes through the LobbyLink; received ones reach the socket bound to their port.
class VdpShim {
 public:
  struct Received {
    uint32_t from_address = 0;
    uint16_t from_port = 0;
    std::vector<uint8_t> data;
  };

  uint32_t Create();
  bool Owns(uint32_t handle) const { return sockets_.count(handle) != 0; }
  void Bind(uint32_t handle, uint16_t port);
  void Close(uint32_t handle);
  // False when the datagram was dropped (no active match, or not addressed to the peer).
  bool SendTo(uint32_t handle, uint32_t to_address, uint16_t to_port, const uint8_t* data, size_t size,
              const std::optional<PeerRoute>& route, LobbyLink& link);
  std::optional<Received> RecvFrom(uint32_t handle, const std::optional<PeerRoute>& route, LobbyLink& link);
  uint16_t bound_port(uint32_t handle) const;
  size_t queued(uint32_t handle) const;

 private:
  struct Socket {
    uint16_t port = 0;
    std::deque<netplay::LobbyDatagram> queue;
  };
  void Follow(const std::optional<PeerRoute>& route);

  static constexpr uint32_t kFirstHandle = 0x5D500001;
  uint32_t next_ = kFirstHandle;
  uint32_t tag_ = 0;
  std::map<uint32_t, Socket> sockets_;
};

}  // namespace ssf2t
