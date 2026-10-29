#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "netplay/packet.h"
#include "netplay/rollback_core.h"
#include "netplay/udp_transport.h"

namespace ssf2t::netplay {

enum class Role { kHost, kJoin };

enum class SessionState {
  kIdle,
  kWaiting,     // host, no peer yet
  kConnecting,  // joiner, Hello sent
  kRunning,
  kRejected,
  kTimedOut,
  kClosed,
  kFaulted,
};

struct SessionConfig {
  Role role = Role::kHost;
  GuestId guest = GuestId::kBase013;
  Endpoint peer;                // joiner: the host
  uint32_t session_id = 1;      // host: nonzero, identifies the match
  uint32_t nonce = 1;           // joiner: identifies this attempt
  uint32_t input_delay = 1;     // host: sent to the joiner
  uint32_t max_rollback = 8;
  uint32_t checksum_interval = 30;
  uint64_t handshake_timeout_ms = 15000;
  uint64_t disconnect_timeout_ms = 5000;
};

// One peer connection and its rollback core, for one match: a new match takes a new Session. Not thread-safe: every
// call comes from the thread that runs the simulation, so no socket call happens on any other thread.
class Session {
 public:
  Session(const SessionConfig& config, Transport& transport, Simulation& sim);

  void Start(uint64_t now_ms);
  // Drains the socket, resends the handshake and keepalives. Never blocks.
  void Poll(uint64_t now_ms);
  // Poll, one core Tick with the local sample, then the redundant inputs out. kStalled until Running.
  TickStatus RunFrame(InputWord local_sample, uint64_t now_ms);
  void Stop(uint64_t now_ms);

  SessionState state() const { return state_; }
  RejectReason reject_reason() const { return reject_reason_; }
  std::optional<ByeReason> peer_bye() const { return peer_bye_; }
  RollbackCore* core() { return core_.get(); }
  const RollbackCore* core() const { return core_.get(); }
  uint32_t session_id() const { return session_id_; }
  int local_player() const { return local_player_; }
  uint32_t packets_sent() const { return packets_sent_; }
  uint32_t packets_received() const { return packets_received_; }
  uint32_t packets_dropped() const { return packets_dropped_; }

 private:
  struct PendingCheck {
    ChecksumPoint point;
    int remaining = 0;
  };

  void Receive(uint64_t now_ms);
  void Handle(const Packet& packet, const Endpoint& from, uint64_t now_ms);
  void HandleHello(const Packet& packet, const Hello& hello, const Endpoint& from, uint64_t now_ms);
  void HandleHelloAck(const Packet& packet, const HelloAck& ack, const Endpoint& from, uint64_t now_ms);
  bool CreateCore(int local_player, uint32_t input_delay, Frame start_frame);
  void Send(const Body& body, uint32_t session);
  void SendTo(const Endpoint& to, const Body& body, uint32_t session);
  void SendInputs();
  void QueueChecksums();
  void SendPendingChecksums();
  void SendHelloIfDue(uint64_t now_ms);

  SessionConfig config_;
  Transport& transport_;
  Simulation& sim_;
  SessionState state_ = SessionState::kIdle;
  RejectReason reject_reason_ = RejectReason::kNone;
  std::optional<ByeReason> peer_bye_;
  Endpoint peer_;
  uint32_t session_id_ = 0;
  uint32_t joiner_nonce_ = 0;
  int local_player_ = 0;
  uint16_t send_seq_ = 0;
  uint16_t last_recv_seq_ = 0;
  uint64_t now_ms_ = 0;
  uint64_t started_ms_ = 0;
  uint64_t last_recv_ms_ = 0;
  uint64_t last_send_ms_ = 0;
  uint64_t last_hello_ms_ = 0;
  uint32_t packets_sent_ = 0;
  uint32_t packets_received_ = 0;
  uint32_t packets_dropped_ = 0;
  std::unique_ptr<RollbackCore> core_;
  std::vector<PendingCheck> pending_checks_;
};

}  // namespace ssf2t::netplay
