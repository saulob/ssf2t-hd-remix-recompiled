#include "online/rendezvous.h"

#include <algorithm>
#include <cstring>

#include "online/net_addr.h"
#include "online/random.h"
#include "online/stun.h"

namespace ssf2t::online {
namespace {

constexpr uint64_t kStunMs = 2500;
constexpr uint64_t kPunchMs = 10000;
constexpr uint64_t kPunchIntervalMs = 100;
constexpr uint8_t kPunchMagic[4] = {'S', 'F', 'P', 'N'};
constexpr size_t kPunchSize = 14;
constexpr uint32_t kLoopbackAddress = 0x7F000001;

bool LooksLikeStun(const uint8_t* data, size_t size) {
  return size >= 20 && (data[0] & 0xc0) == 0 && data[4] == 0x21 && data[5] == 0x12 && data[6] == 0xa4 &&
         data[7] == 0x42;
}

bool IsLoopbackText(const std::string& ip) { return ip.rfind("127.", 0) == 0; }

}  // namespace

const char* CandidateTypeName(CandidateType type) {
  switch (type) {
    case CandidateType::kLan: return "lan";
    case CandidateType::kStun: return "stun";
    case CandidateType::kObserved: return "observed";
    default: return "peer";
  }
}

std::string EndpointText(const netplay::Endpoint& endpoint) {
  return net::EndpointText({endpoint.address, endpoint.port});
}

std::vector<Candidate> PeerCandidates(const PeerInfo& peer, const std::vector<std::string>& my_lan,
                                      const netplay::Endpoint& my_public) {
  std::vector<Candidate> out;
  const auto add = [&out](CandidateType type, uint32_t address, uint16_t port) {
    const netplay::Endpoint endpoint{address, port};
    if (!address || !port) return;
    if (std::any_of(out.begin(), out.end(), [&](const Candidate& c) { return c.endpoint == endpoint; })) return;
    out.push_back({type, endpoint});
  };
  uint32_t peer_public = 0;
  const bool has_public =
      peer.public_port && netplay::UsableAddress(netplay::ParsePeerAddress(peer.public_ip, peer_public));
  if (peer.same_network || (has_public && my_public.address == peer_public)) {
    std::vector<std::string> remote;
    for (const std::string& ip : peer.lan) {
      if (!IsLoopbackText(ip)) remote.push_back(ip);
    }
    const bool same_pc = remote.empty() || std::any_of(remote.begin(), remote.end(), [&](const std::string& ip) {
      return std::find(my_lan.begin(), my_lan.end(), ip) != my_lan.end();
    });
    if (same_pc) add(CandidateType::kLan, kLoopbackAddress, peer.port);
    for (const std::string& ip : remote) add(CandidateType::kLan, net::ParseIp(ip), peer.port);
  }
  if (has_public) add(CandidateType::kStun, peer_public, peer.public_port);
  if (uint32_t observed = 0; netplay::UsableAddress(netplay::ParsePeerAddress(peer.ip, observed))) {
    add(CandidateType::kObserved, observed, peer.port);
  }
  return out;
}

uint64_t PunchKey(const std::string& match) {
  uint64_t hash = 0xcbf29ce484222325ull;
  for (const char c : match) hash = (hash ^ uint8_t(c)) * 0x100000001b3ull;
  return hash ? hash : 1;
}

std::vector<uint8_t> EncodePunch(const PunchPacket& packet) {
  std::vector<uint8_t> out(kPunchSize);
  std::memcpy(out.data(), kPunchMagic, sizeof kPunchMagic);
  out[4] = uint8_t(packet.kind);
  out[5] = packet.side;
  for (int i = 0; i < 8; ++i) out[6 + i] = uint8_t(packet.key >> (56 - 8 * i));
  return out;
}

std::optional<PunchPacket> DecodePunch(const uint8_t* data, size_t size) {
  if (size != kPunchSize || std::memcmp(data, kPunchMagic, sizeof kPunchMagic) != 0) return std::nullopt;
  if (data[4] != uint8_t(PunchKind::kPunch) && data[4] != uint8_t(PunchKind::kAck)) return std::nullopt;
  PunchPacket packet;
  packet.kind = PunchKind(data[4]);
  packet.side = data[5];
  for (int i = 0; i < 8; ++i) packet.key = packet.key << 8 | data[6 + i];
  return packet;
}

void Rendezvous::StartStun(const std::vector<netplay::Endpoint>& servers, uint16_t local_port, uint64_t now_ms,
                           netplay::Transport& out) {
  stun_.clear();
  mapped_ = {};
  logged_second_ = false;
  local_port_ = local_port;
  for (const netplay::Endpoint& server : servers) {
    StunQuery query;
    query.server = server;
    if (server.address && server.port && RandomBytes(query.id.data(), query.id.size())) stun_.push_back(query);
  }
  if (stun_.empty()) {
    stun_stage_ = Stage::kFailed;
    Write("DEBUG_SAULO stun mapped endpoint=none reason=no_server local_port=" + std::to_string(local_port_));
    return;
  }
  stun_stage_ = Stage::kRunning;
  stun_deadline_ = now_ms + kStunMs;
  stun_attempt_ = 0;
  stun_next_send_ = now_ms;
  Tick(now_ms, out);
}

void Rendezvous::StartPunch(uint64_t key, uint8_t side, const std::vector<Candidate>& candidates, uint64_t now_ms,
                            netplay::Transport& out) {
  key_ = key;
  side_ = side;
  targets_.clear();
  heard_.clear();
  selected_ = {};
  for (const Candidate& candidate : candidates) {
    if (!Known(candidate.endpoint)) targets_.push_back({candidate});
  }
  if (targets_.empty()) {
    punch_stage_ = Stage::kFailed;
    Write("DEBUG_SAULO udp rendezvous failed reason=no_candidates");
    return;
  }
  punch_stage_ = Stage::kRunning;
  punch_deadline_ = now_ms + kPunchMs;
  punch_next_send_ = now_ms;
  Tick(now_ms, out);
}

void Rendezvous::Stop() {
  stun_stage_ = Stage::kIdle;
  stun_.clear();
  mapped_ = {};
  punch_stage_ = Stage::kIdle;
  key_ = 0;
  targets_.clear();
  heard_.clear();
  selected_ = {};
}

void Rendezvous::Tick(uint64_t now_ms, netplay::Transport& out) {
  if (stun_stage_ == Stage::kRunning) {
    if (now_ms >= stun_deadline_) {
      stun_stage_ = Stage::kFailed;
      Write("DEBUG_SAULO stun mapped endpoint=none reason=no_answer local_port=" + std::to_string(local_port_));
    } else if (now_ms >= stun_next_send_) {
      SendStun(out);
      stun_next_send_ = now_ms + (250u << std::min(stun_attempt_++, 3));
    }
  }
  if (punch_stage_ == Stage::kRunning) {
    if (now_ms >= punch_deadline_) {
      punch_stage_ = Stage::kFailed;
      Write("DEBUG_SAULO udp rendezvous failed reason=punch_timeout candidates=" + std::to_string(targets_.size()) +
            " peer_heard=" + (heard_.empty() ? "false" : "true"));
    } else if (now_ms >= punch_next_send_) {
      SendPunches(out);
      punch_next_send_ = now_ms + kPunchIntervalMs;
    }
  }
}

bool Rendezvous::Take(const netplay::Endpoint& from, const uint8_t* data, size_t size, netplay::Transport& reply) {
  if (LooksLikeStun(data, size)) {
    TakeStun(data, size);
    return true;
  }
  if (size != kPunchSize || std::memcmp(data, kPunchMagic, sizeof kPunchMagic) != 0) return false;
  if (const std::optional<PunchPacket> packet = DecodePunch(data, size);
      packet && key_ && packet->key == key_ && packet->side != side_) {
    TakePunch(from, *packet, reply);
  }
  return true;
}

void Rendezvous::SendStun(netplay::Transport& out) {
  for (const StunQuery& query : stun_) {
    if (query.answered) continue;
    uint8_t request[net::stun::kRequestSize];
    net::stun::MakeRequest(query.id, request);
    out.Send(query.server, request, sizeof request);
  }
}

void Rendezvous::TakeStun(const uint8_t* data, size_t size) {
  for (StunQuery& query : stun_) {
    net::Endpoint mapped;
    if (query.answered || !net::stun::ParseResponse(data, size, query.id, mapped)) continue;
    query.answered = true;
    const netplay::Endpoint endpoint{mapped.ip, mapped.port};
    if (stun_stage_ == Stage::kRunning) {
      stun_stage_ = Stage::kDone;
      mapped_ = endpoint;
      Write("DEBUG_SAULO stun mapped endpoint=" + EndpointText(endpoint) + " local_port=" +
            std::to_string(local_port_));
    } else if (stun_stage_ == Stage::kDone && !logged_second_) {
      logged_second_ = true;
      Write("DEBUG_SAULO stun second endpoint=" + EndpointText(endpoint) +
            " agrees=" + (endpoint == mapped_ ? "true" : "false"));
    }
    return;
  }
}

void Rendezvous::TakePunch(const netplay::Endpoint& from, const PunchPacket& packet, netplay::Transport& reply) {
  if (std::find(heard_.begin(), heard_.end(), from) == heard_.end()) {
    heard_.push_back(from);
    Write("DEBUG_SAULO udp punch rx endpoint=" + EndpointText(from));
  }
  if (packet.kind == PunchKind::kPunch) {
    const std::vector<uint8_t> ack = EncodePunch({PunchKind::kAck, side_, key_});
    reply.Send(from, ack.data(), ack.size());
    if (punch_stage_ == Stage::kRunning && !Known(from)) {
      targets_.push_back({{CandidateType::kPeer, from}});
      Write("DEBUG_SAULO peer candidate received type=peer endpoint=" + EndpointText(from));
    }
    return;
  }
  if (punch_stage_ != Stage::kRunning) return;
  selected_ = {CandidateType::kPeer, from};
  for (const Target& target : targets_) {
    if (target.candidate.endpoint == from) selected_.type = target.candidate.type;
  }
  punch_stage_ = Stage::kDone;
  Write(std::string("DEBUG_SAULO udp candidate selected endpoint=") + EndpointText(from) +
        " type=" + CandidateTypeName(selected_.type));
}

void Rendezvous::SendPunches(netplay::Transport& out) {
  const std::vector<uint8_t> punch = EncodePunch({PunchKind::kPunch, side_, key_});
  for (Target& target : targets_) {
    out.Send(target.candidate.endpoint, punch.data(), punch.size());
    if (target.sent) continue;
    target.sent = true;
    Write(std::string("DEBUG_SAULO udp punch tx endpoint=") + EndpointText(target.candidate.endpoint) +
          " type=" + CandidateTypeName(target.candidate.type));
  }
}

bool Rendezvous::Known(const netplay::Endpoint& endpoint) const {
  return std::any_of(targets_.begin(), targets_.end(),
                     [&](const Target& target) { return target.candidate.endpoint == endpoint; });
}

void Rendezvous::Write(const std::string& line) const {
  if (log_) log_(line);
}

}  // namespace ssf2t::online
