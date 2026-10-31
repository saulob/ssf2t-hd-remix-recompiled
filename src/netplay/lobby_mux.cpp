#include "netplay/lobby_mux.h"

#include <algorithm>
#include <cstring>
#include <iterator>

namespace ssf2t::netplay {
namespace {

constexpr uint8_t kMagic[4] = {'S', 'F', 'L', 'B'};
constexpr size_t kMaxQueued = 256;
constexpr size_t kScratchBytes = 2048;

void Put16(uint8_t* at, uint16_t v) {
  at[0] = uint8_t(v >> 8);
  at[1] = uint8_t(v);
}
void Put32(uint8_t* at, uint32_t v) {
  Put16(at, uint16_t(v >> 16));
  Put16(at + 2, uint16_t(v));
}
uint16_t Get16(const uint8_t* at) { return uint16_t(at[0] << 8 | at[1]); }
uint32_t Get32(const uint8_t* at) { return uint32_t(Get16(at)) << 16 | Get16(at + 2); }

}  // namespace

std::vector<uint8_t> EncodeLobby(uint32_t tag, const LobbyDatagram& datagram) {
  std::vector<uint8_t> out(kLobbyHeaderBytes + datagram.data.size());
  std::memcpy(out.data(), kMagic, sizeof(kMagic));
  Put32(out.data() + 4, tag);
  Put16(out.data() + 8, datagram.from_port);
  Put16(out.data() + 10, datagram.to_port);
  if (!datagram.data.empty()) std::memcpy(out.data() + kLobbyHeaderBytes, datagram.data.data(), datagram.data.size());
  return out;
}

std::optional<std::pair<uint32_t, LobbyDatagram>> DecodeLobby(const uint8_t* data, size_t size) {
  if (size < kLobbyHeaderBytes || size > kLobbyHeaderBytes + kMaxLobbyPayload) return std::nullopt;
  if (std::memcmp(data, kMagic, sizeof(kMagic)) != 0) return std::nullopt;
  LobbyDatagram datagram;
  datagram.from_port = Get16(data + 8);
  datagram.to_port = Get16(data + 10);
  datagram.data.assign(data + kLobbyHeaderBytes, data + size);
  return std::make_pair(Get32(data + 4), std::move(datagram));
}

bool LobbyHold::Hold(uint32_t tag, const LobbyDatagram& datagram) {
  if (tag == 0 || datagram.data.size() > kMaxLobbyPayload) return false;
  if (tag != tag_) Clear();
  if (held_.size() >= kMaxHeldLobby) return false;
  tag_ = tag;
  held_.push_back(datagram);
  return true;
}

std::vector<LobbyDatagram> LobbyHold::Take(uint32_t tag) {
  std::vector<LobbyDatagram> out;
  if (tag != 0 && tag == tag_) out.assign(std::make_move_iterator(held_.begin()), std::make_move_iterator(held_.end()));
  Clear();
  return out;
}

void LobbyHold::Clear() {
  tag_ = 0;
  held_.clear();
}

void LobbyMux::SetRoute(uint32_t tag, const Endpoint& peer) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (tag != tag_) lobby_.clear();
  tag_ = tag;
  peer_ = peer;
}

bool LobbyMux::SendLobby(const LobbyDatagram& datagram) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (tag_ == 0 || peer_.port == 0 || datagram.data.size() > kMaxLobbyPayload) return false;
  const std::vector<uint8_t> bytes = EncodeLobby(tag_, datagram);
  if (!inner_.Send(peer_, bytes.data(), bytes.size())) return false;
  ++sent_;
  return true;
}

bool LobbyMux::TakeLobby(LobbyDatagram& out) {
  std::lock_guard<std::mutex> lock(mutex_);
  DrainLocked();
  if (lobby_.empty()) return false;
  out = std::move(lobby_.front());
  lobby_.pop_front();
  return true;
}

bool LobbyMux::Send(const Endpoint& to, const uint8_t* data, size_t size) {
  std::lock_guard<std::mutex> lock(mutex_);
  return inner_.Send(to, data, size);
}

size_t LobbyMux::Receive(uint8_t* buffer, size_t capacity, Endpoint& from) {
  std::lock_guard<std::mutex> lock(mutex_);
  DrainLocked();
  if (game_.empty()) return 0;
  auto [sender, bytes] = std::move(game_.front());
  game_.pop_front();
  const size_t n = std::min(capacity, bytes.size());
  std::memcpy(buffer, bytes.data(), n);
  from = sender;
  return n;
}

void LobbyMux::DrainLocked() {
  uint8_t scratch[kScratchBytes];
  Endpoint from;
  while (const size_t size = inner_.Receive(scratch, sizeof(scratch), from)) {
    auto lobby = DecodeLobby(scratch, size);
    if (!lobby) {
      if (game_.size() < kMaxQueued) game_.emplace_back(from, std::vector<uint8_t>(scratch, scratch + size));
      continue;
    }
    if (tag_ == 0 || lobby->first != tag_ || lobby_.size() >= kMaxQueued) {
      ++rejected_;
      continue;
    }
    peer_ = from;
    ++received_;
    lobby_.push_back(std::move(lobby->second));
  }
}

uint32_t LobbyMux::lobby_sent() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return sent_;
}
uint32_t LobbyMux::lobby_received() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return received_;
}
uint32_t LobbyMux::lobby_rejected() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return rejected_;
}

}  // namespace ssf2t::netplay
