#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "netplay/online_game_socket.h"
#include "online/protocol.h"

namespace ssf2t::online {

enum class CandidateType { kLan, kStun, kObserved, kPeer };

// A complete endpoint the peer may be reached at. kObserved is the signaling service's view of the peer's address
// with its local port (unproven guesswork); kPeer is where the peer's own punches came from.
struct Candidate {
  CandidateType type = CandidateType::kStun;
  netplay::Endpoint endpoint;
  friend bool operator==(const Candidate&, const Candidate&) = default;
};

const char* CandidateTypeName(CandidateType type);
std::string EndpointText(const netplay::Endpoint& endpoint);

// The peer's candidates, most direct first: its LAN endpoints only when both players share a network, its STUN-mapped
// endpoint, then the observed one as a fallback. `my_public` is this socket's own STUN endpoint.
std::vector<Candidate> PeerCandidates(const PeerInfo& peer, const std::vector<std::string>& my_lan,
                                      const netplay::Endpoint& my_public);

// Both peers derive it from the match id, so punches of any other match are ignored.
uint64_t PunchKey(const std::string& match);

enum class PunchKind : uint8_t { kPunch = 1, kAck = 2 };

struct PunchPacket {
  PunchKind kind = PunchKind::kPunch;
  uint8_t side = 0;  // 1 host, 2 join
  uint64_t key = 0;
};

std::vector<uint8_t> EncodePunch(const PunchPacket& packet);
std::optional<PunchPacket> DecodePunch(const uint8_t* data, size_t size);

// Match setup on the gameplay socket: learns this socket's public endpoint from STUN, then punches the peer's
// candidates until one answers. Only a punch acknowledgement from the peer selects a candidate (the round trip
// proves the path both ways), and it selects the address the acknowledgement actually came from.
// Not thread-safe: every call runs under the OnlineGameSocket's lock (inside Use, or as its tap).
class Rendezvous final : public netplay::DatagramTap {
 public:
  enum class Stage { kIdle, kRunning, kDone, kFailed };
  using Log = std::function<void(const std::string&)>;

  explicit Rendezvous(Log log = {}) : log_(std::move(log)) {}

  void StartStun(const std::vector<netplay::Endpoint>& servers, uint16_t local_port, uint64_t now_ms,
                 netplay::Transport& out);
  Stage stun_stage() const { return stun_stage_; }
  const netplay::Endpoint& mapped() const { return mapped_; }

  void StartPunch(uint64_t key, uint8_t side, const std::vector<Candidate>& candidates, uint64_t now_ms,
                  netplay::Transport& out);
  Stage punch_stage() const { return punch_stage_; }
  const Candidate& selected() const { return selected_; }
  // Where this match's peer punches came from, first heard first.
  const std::vector<netplay::Endpoint>& heard() const { return heard_; }

  // Forgets the match: from now on its STUN answers and punches are dropped unanswered.
  void Stop();
  void Tick(uint64_t now_ms, netplay::Transport& out);
  bool active() const { return stun_stage_ == Stage::kRunning || punch_stage_ == Stage::kRunning; }

  bool Take(const netplay::Endpoint& from, const uint8_t* data, size_t size, netplay::Transport& reply) override;

 private:
  struct StunQuery {
    netplay::Endpoint server;
    std::array<uint8_t, 12> id{};
    bool answered = false;
  };
  struct Target {
    Candidate candidate;
    bool sent = false;
  };

  void SendStun(netplay::Transport& out);
  void TakeStun(const uint8_t* data, size_t size);
  void TakePunch(const netplay::Endpoint& from, const PunchPacket& packet, netplay::Transport& reply);
  void SendPunches(netplay::Transport& out);
  bool Known(const netplay::Endpoint& endpoint) const;
  void Write(const std::string& line) const;

  Log log_;

  Stage stun_stage_ = Stage::kIdle;
  std::vector<StunQuery> stun_;
  uint16_t local_port_ = 0;
  uint64_t stun_deadline_ = 0;
  uint64_t stun_next_send_ = 0;
  int stun_attempt_ = 0;
  netplay::Endpoint mapped_;
  bool logged_second_ = false;

  Stage punch_stage_ = Stage::kIdle;
  uint64_t key_ = 0;
  uint8_t side_ = 0;
  std::vector<Target> targets_;
  std::vector<netplay::Endpoint> heard_;
  uint64_t punch_deadline_ = 0;
  uint64_t punch_next_send_ = 0;
  Candidate selected_;
};

}  // namespace ssf2t::online
