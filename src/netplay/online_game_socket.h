#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "netplay/relay_frame.h"
#include "netplay/udp_transport.h"

namespace ssf2t::netplay {

// Sees every datagram the socket receives before the game does; true keeps it from the game. `reply` sends from the
// same socket and may be used during the call.
class DatagramTap {
 public:
  virtual ~DatagramTap() = default;
  virtual bool Take(const Endpoint& from, const uint8_t* data, size_t size, Transport& reply) = 0;
};

// Hands one encoded relay frame to the online service's connection; false when it cannot take it now.
using RelaySend = std::function<bool(std::vector<uint8_t>&&)>;

inline constexpr size_t kRelaySampledSizes = 4;

// The current match's gameplay datagrams, whichever way they went.
struct MatchTraffic {
  uint64_t udp_tx_packets = 0;
  uint64_t udp_rx_packets = 0;
  uint64_t relay_tx_packets = 0;
  uint64_t relay_tx_bytes = 0;
  uint64_t relay_rx_packets = 0;
  uint64_t relay_rx_bytes = 0;
  uint64_t relay_rejected = 0;
  std::vector<size_t> relay_tx_sizes;  // the first few payload sizes each way
  std::vector<size_t> relay_rx_sizes;
};

// The one UDP socket online play uses from its early bind (so Windows can prompt for network access before any match)
// through every match: STUN, punching, the Player Match lobby and the rollback session all share it, because the NAT
// mapping the peer was told about belongs to this socket alone. When a match has no direct path, the same Transport
// carries it over the online service's relay instead: the session and the lobby then talk to kRelayPeer and never
// know the difference. Thread-safe.
class OnlineGameSocket final : public Transport {
 public:
  // Binds the first free port of `ports`. Once open it stays on that port: later calls never rebind.
  bool Open(std::initializer_list<uint16_t> ports, bool loopback_only = false);
  bool is_open() const;
  uint16_t bound_port() const;

  void SetTap(DatagramTap* tap);
  // Runs `use` with the socket held, so the tap's own state and sends stay consistent with Take.
  void Use(const std::function<void(Transport&)>& use);
  // Lets the tap see what has arrived when no session may be reading; game datagrams wait for Receive.
  void Pump();

  // The match's relay: from now on its frames (those carrying `token`) are queued, but only once it is selected do
  // sends to kRelayPeer go through it and do its datagrams come out of Receive. Replaces any earlier relay.
  void OpenRelay(uint32_t token, RelaySend send);
  void SelectRelay();
  // Forgets the relay and everything it queued.
  void CloseRelay();
  bool relay_selected() const;
  // One binary message from the online service; false when it is not a frame of the open relay.
  bool DeliverRelay(const uint8_t* data, size_t size);
  // Why the relay can no longer carry the match ("receive_overflow", "send_refused"); empty while it can.
  std::string relay_failure() const;

  // Counting starts again for each match.
  void ResetTraffic();
  MatchTraffic traffic() const;

  bool Send(const Endpoint& to, const uint8_t* data, size_t size) override;
  size_t Receive(uint8_t* buffer, size_t capacity, Endpoint& from) override;

 private:
  bool TakenByTap(const Endpoint& from, const uint8_t* data, size_t size);
  bool SendRelayLocked(const uint8_t* data, size_t size);

  mutable std::mutex mutex_;
  UdpTransport socket_;
  DatagramTap* tap_ = nullptr;
  std::deque<std::pair<Endpoint, std::vector<uint8_t>>> waiting_;

  uint32_t relay_token_ = 0;
  bool relay_selected_ = false;
  RelaySend relay_send_;
  uint32_t relay_seq_ = 0;
  std::deque<std::vector<uint8_t>> relay_in_;
  std::string relay_failure_;
  MatchTraffic traffic_;
};

}  // namespace ssf2t::netplay
