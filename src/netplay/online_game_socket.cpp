#include "netplay/online_game_socket.h"

#include <algorithm>
#include <cstring>

namespace ssf2t::netplay {
namespace {

constexpr size_t kMaxDatagram = 2048;
constexpr size_t kMaxWaiting = 64;
// About four seconds of a fight's traffic: a consumer this far behind has stalled.
constexpr size_t kMaxRelayQueued = 256;

void Sample(std::vector<size_t>& sizes, size_t size) {
  if (sizes.size() < kRelaySampledSizes) sizes.push_back(size);
}

}  // namespace

bool OnlineGameSocket::Open(std::initializer_list<uint16_t> ports, bool loopback_only) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (socket_.is_open()) return true;
  for (const uint16_t port : ports) {
    if (socket_.Open(port, loopback_only)) return true;
  }
  return false;
}

bool OnlineGameSocket::is_open() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return socket_.is_open();
}

uint16_t OnlineGameSocket::bound_port() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return socket_.bound_port();
}

void OnlineGameSocket::SetTap(DatagramTap* tap) {
  std::lock_guard<std::mutex> lock(mutex_);
  tap_ = tap;
}

void OnlineGameSocket::Use(const std::function<void(Transport&)>& use) {
  std::lock_guard<std::mutex> lock(mutex_);
  use(socket_);
}

void OnlineGameSocket::Pump() {
  std::lock_guard<std::mutex> lock(mutex_);
  uint8_t buffer[kMaxDatagram];
  Endpoint from;
  while (const size_t size = socket_.Receive(buffer, sizeof buffer, from)) {
    if (TakenByTap(from, buffer, size)) continue;
    if (waiting_.size() == kMaxWaiting) waiting_.pop_front();
    waiting_.emplace_back(from, std::vector<uint8_t>(buffer, buffer + size));
  }
}

void OnlineGameSocket::OpenRelay(uint32_t token, RelaySend send) {
  std::lock_guard<std::mutex> lock(mutex_);
  relay_token_ = token;
  relay_selected_ = false;
  relay_send_ = std::move(send);
  relay_seq_ = 0;
  relay_in_.clear();
  relay_failure_.clear();
}

void OnlineGameSocket::SelectRelay() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (relay_token_) relay_selected_ = true;
}

void OnlineGameSocket::CloseRelay() {
  std::lock_guard<std::mutex> lock(mutex_);
  relay_token_ = 0;
  relay_selected_ = false;
  relay_send_ = nullptr;
  relay_in_.clear();
  relay_failure_.clear();
}

bool OnlineGameSocket::relay_selected() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return relay_selected_;
}

bool OnlineGameSocket::DeliverRelay(const uint8_t* data, size_t size) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::optional<RelayFrame> frame = DecodeRelayFrame(data, size);
  if (!frame || !relay_token_ || frame->token != relay_token_ || !relay_failure_.empty()) {
    ++traffic_.relay_rejected;
    return false;
  }
  if (relay_in_.size() >= kMaxRelayQueued) {
    relay_failure_ = "receive_overflow";
    relay_in_.clear();
    return false;
  }
  ++traffic_.relay_rx_packets;
  traffic_.relay_rx_bytes += frame->payload.size();
  Sample(traffic_.relay_rx_sizes, frame->payload.size());
  relay_in_.push_back(std::move(frame->payload));
  return true;
}

std::string OnlineGameSocket::relay_failure() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return relay_failure_;
}

void OnlineGameSocket::ResetTraffic() {
  std::lock_guard<std::mutex> lock(mutex_);
  traffic_ = {};
}

MatchTraffic OnlineGameSocket::traffic() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return traffic_;
}

bool OnlineGameSocket::SendRelayLocked(const uint8_t* data, size_t size) {
  if (!relay_failure_.empty() || !relay_send_) return false;
  std::vector<uint8_t> frame = EncodeRelayFrame(relay_token_, ++relay_seq_, data, size);
  if (frame.empty()) return false;
  if (!relay_send_(std::move(frame))) {
    relay_failure_ = "send_refused";
    return false;
  }
  ++traffic_.relay_tx_packets;
  traffic_.relay_tx_bytes += size;
  Sample(traffic_.relay_tx_sizes, size);
  return true;
}

bool OnlineGameSocket::Send(const Endpoint& to, const uint8_t* data, size_t size) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (relay_selected_ && to == kRelayPeer) return SendRelayLocked(data, size);
  const bool sent = socket_.Send(to, data, size);
  if (sent) ++traffic_.udp_tx_packets;
  return sent;
}

size_t OnlineGameSocket::Receive(uint8_t* buffer, size_t capacity, Endpoint& from) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (relay_selected_ && !relay_in_.empty()) {
    const std::vector<uint8_t>& data = relay_in_.front();
    const size_t size = std::min(capacity, data.size());
    std::memcpy(buffer, data.data(), size);
    from = kRelayPeer;
    relay_in_.pop_front();
    return size;
  }
  if (!waiting_.empty()) {
    const auto& [sender, data] = waiting_.front();
    const size_t size = std::min(capacity, data.size());
    if (size) std::memcpy(buffer, data.data(), size);
    from = sender;
    waiting_.pop_front();
    ++traffic_.udp_rx_packets;
    return size;
  }
  for (;;) {
    const size_t size = socket_.Receive(buffer, capacity, from);
    if (size == 0) return 0;
    if (!TakenByTap(from, buffer, size)) {
      ++traffic_.udp_rx_packets;
      return size;
    }
  }
}

bool OnlineGameSocket::TakenByTap(const Endpoint& from, const uint8_t* data, size_t size) {
  return tap_ && tap_->Take(from, data, size, socket_);
}

}  // namespace ssf2t::netplay
