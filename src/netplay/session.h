#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "netplay/frame_zero.h"
#include "netplay/packet.h"
#include "netplay/rollback_core.h"
#include "netplay/udp_transport.h"

namespace ssf2t::netplay {

enum class Role { kHost, kJoin };

enum class SessionState {
  kIdle,
  kWaiting,     // host, no peer yet
  kConnecting,  // joiner, Hello sent
  kConnected,   // handshake done; waiting for both sides to be fight-ready
  kRunning,     // fight-ready barrier passed; the rollback core is ticking
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
  Frame start_frame = 0;  // host: the rollback core's first frame number, once both sides are fight-ready
  bool verify_frame_zero = false;  // BeginFight only after both peers offered the same frame-0 state
};

// The previous match used the same ports: its late packets must not reach the next one.
inline uint32_t MatchSessionId(uint32_t generation, uint64_t now_ms) {
  const uint32_t id = 0x53465254u ^ (generation * 0x9E3779B9u) ^ uint32_t(now_ms);
  return id != 0 ? id : 1;
}

// One peer connection, for one match: a new match takes a new Session. Connecting and agreeing that a fight may
// begin (kConnected) is independent of actually running one (kRunning): the rollback core is created only once
// BeginFight() supplies the real Simulation, which the caller can only build from inside its own fight-frame hook.
// Not thread-safe: every call comes from the thread that runs the simulation, so no socket call happens elsewhere.
class Session {
 public:
  Session(const SessionConfig& config, Transport& transport);

  void Start(uint64_t now_ms);
  // Drains the socket, resends the handshake, the ready barrier and keepalives. Never blocks. Call every tick,
  // whether or not a fight is active.
  void Poll(uint64_t now_ms);
  // Reports whether the local side is currently in a fight it wants netplay to drive. Toggling it off while
  // kRunning ends the takeover (EndFight), and the peer is told so it does the same.
  void SetLocalReady(bool ready, uint64_t now_ms);
  // True once connected and both sides are known ready: the caller should build its Simulation and call
  // BeginFight(). Only meaningful in kConnected.
  bool ReadyToBeginFight() const;
  // The local frame-0 state (FrameZeroView) of the fight about to begin and its setup words, swapped with the peer's
  // until both agree.
  void OfferFrameZero(std::vector<uint8_t> view, uint32_t globals_address, std::vector<uint32_t> setup = {});
  FrameZeroVerdict frame_zero() const;
  FrameZeroRefusal frame_zero_refusal() const;
  // Joiner: the host's differing frame-0 bytes arrived. AdoptFrameZero writes them into the simulation blob the offered
  // view was made from; the caller loads it into the game and offers the reloaded state with ReofferFrameZero.
  bool FrameZeroAdoptionReady() const;
  std::optional<FrameZeroAdoption> AdoptFrameZero(std::vector<uint8_t>& blob) const;
  void ReofferFrameZero(std::vector<uint8_t> view);
  std::vector<std::string> TakeFrameZeroReport();
  // Creates the rollback core bound to `sim` and starts it at the agreed frame; moves to kRunning. False when the
  // frame-0 states must agree and do not yet, and (kFaulted) when the simulation cannot save its first snapshot.
  bool BeginFight(Simulation& sim, uint64_t now_ms);
  // Tears the rollback core down and returns to kConnected, so a later fight can renegotiate readiness.
  void EndFight(uint64_t now_ms);
  // Poll, one core Tick with the local sample, then the redundant inputs out. kStalled outside kRunning.
  TickStatus RunFrame(InputWord local_sample, uint64_t now_ms);
  void Stop(uint64_t now_ms);

  SessionState state() const { return state_; }
  RejectReason reject_reason() const { return reject_reason_; }
  std::optional<ByeReason> peer_bye() const { return peer_bye_; }
  RollbackCore* core() { return core_.get(); }
  const RollbackCore* core() const { return core_.get(); }
  uint32_t session_id() const { return session_id_; }
  int local_player() const { return local_player_; }
  // The fight running now, or the one being set up: counted from 0 within this session.
  uint32_t fight_index() const { return state_ == SessionState::kRunning ? fights_begun_ - 1 : fights_begun_; }
  bool local_ready() const { return local_ready_; }
  bool remote_ready() const { return remote_ready_; }
  uint32_t packets_sent() const { return packets_sent_; }
  uint32_t packets_received() const { return packets_received_; }
  uint32_t packets_dropped() const { return packets_dropped_; }
  // Round trip of the gameplay connection, from the packet sequence numbers the peer acknowledges: the lowest of the
  // last two seconds. Empty until the peer has acknowledged something and when it went quiet.
  std::optional<uint32_t> rtt_ms() const;

 private:
  struct PendingCheck {
    ChecksumPoint point;
    int remaining = 0;
  };

  void Receive(uint64_t now_ms);
  void Handle(const Packet& packet, const Endpoint& from, uint64_t now_ms);
  void HandleHello(const Packet& packet, const Hello& hello, const Endpoint& from, uint64_t now_ms);
  void HandleHelloAck(const Packet& packet, const HelloAck& ack, const Endpoint& from, uint64_t now_ms);
  void HandleReady(const Ready& ready, uint64_t now_ms);
  void HandleFrameZero(const Body& body);
  void ResetFrameZero();
  void SendFrameZeroIfDue(uint64_t now_ms);
  void CheckBothReady();
  bool CreateCore(Simulation& sim, int local_player, uint32_t input_delay, Frame start_frame);
  void Send(const Body& body, uint32_t session);
  void SendTo(const Endpoint& to, const Body& body, uint32_t session);
  void SendInputs();
  void QueueChecksums();
  void SendPendingChecksums();
  void SendHelloIfDue(uint64_t now_ms);
  void SendReadyIfDue(uint64_t now_ms);
  void NoteAck(uint16_t ack, uint64_t now_ms);

  SessionConfig config_;
  Transport& transport_;
  SessionState state_ = SessionState::kIdle;
  RejectReason reject_reason_ = RejectReason::kNone;
  std::optional<ByeReason> peer_bye_;
  Endpoint peer_;
  uint32_t session_id_ = 0;
  uint32_t joiner_nonce_ = 0;
  int local_player_ = 0;
  uint32_t negotiated_input_delay_ = 0;
  bool local_ready_ = false;
  bool remote_ready_ = false;
  std::optional<Frame> agreed_start_;
  uint16_t send_seq_ = 0;
  uint16_t last_recv_seq_ = 0;
  uint64_t now_ms_ = 0;
  uint64_t started_ms_ = 0;
  uint64_t last_recv_ms_ = 0;
  uint64_t last_send_ms_ = 0;
  uint64_t last_hello_ms_ = 0;
  uint64_t last_ready_ms_ = 0;
  uint32_t packets_sent_ = 0;
  uint32_t packets_received_ = 0;
  uint32_t packets_dropped_ = 0;
  std::unique_ptr<RollbackCore> core_;
  std::vector<PendingCheck> pending_checks_;
  uint32_t fights_begun_ = 0;
  std::unique_ptr<FrameZeroCheck> frame_zero_;
  std::optional<StateDigest> early_digest_;

  static constexpr size_t kSentRing = 64;
  struct RttSample {
    uint64_t at = 0;
    uint32_t ms = 0;
  };
  uint16_t sent_seq_[kSentRing] = {};
  uint64_t sent_at_[kSentRing] = {};
  bool sent_valid_[kSentRing] = {};
  bool has_acked_ = false;
  uint16_t last_acked_ = 0;
  std::vector<RttSample> rtt_samples_;
};

}  // namespace ssf2t::netplay
