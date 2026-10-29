#include "netplay/session.h"

#include <algorithm>

namespace ssf2t::netplay {
namespace {

constexpr uint64_t kHelloIntervalMs = 100;
constexpr uint64_t kKeepaliveMs = 100;
constexpr int kReceiveBudget = 64;
constexpr int kChecksumRepeats = 3;
constexpr uint32_t kMaxInputDelay = 8;

}  // namespace

Session::Session(const SessionConfig& config, Transport& transport, Simulation& sim)
    : config_(config), transport_(transport), sim_(sim) {
  if (config_.session_id == 0) config_.session_id = 1;
}

void Session::Start(uint64_t now_ms) {
  now_ms_ = now_ms;
  started_ms_ = now_ms;
  last_recv_ms_ = now_ms;
  if (config_.role == Role::kHost) {
    state_ = SessionState::kWaiting;
    return;
  }
  peer_ = config_.peer;
  state_ = SessionState::kConnecting;
  last_hello_ms_ = now_ms - kHelloIntervalMs;
  SendHelloIfDue(now_ms);
}

void Session::Poll(uint64_t now_ms) {
  now_ms_ = now_ms;
  if (state_ != SessionState::kWaiting && state_ != SessionState::kConnecting && state_ != SessionState::kRunning) {
    return;
  }
  Receive(now_ms);
  switch (state_) {
    case SessionState::kConnecting:
      if (now_ms - started_ms_ > config_.handshake_timeout_ms) {
        state_ = SessionState::kTimedOut;
      } else {
        SendHelloIfDue(now_ms);
      }
      break;
    case SessionState::kRunning:
      if (now_ms - last_recv_ms_ > config_.disconnect_timeout_ms) {
        state_ = SessionState::kTimedOut;
      } else if (now_ms - last_send_ms_ >= kKeepaliveMs) {
        SendInputs();
      }
      break;
    default:
      break;
  }
}

TickStatus Session::RunFrame(InputWord local_sample, uint64_t now_ms) {
  Poll(now_ms);
  if (state_ == SessionState::kFaulted) return TickStatus::kFault;
  if (state_ != SessionState::kRunning || !core_) return TickStatus::kStalled;
  const TickStatus status = core_->Tick(local_sample);
  if (status == TickStatus::kFault) {
    state_ = SessionState::kFaulted;
    return status;
  }
  QueueChecksums();
  SendInputs();
  SendPendingChecksums();
  return status;
}

void Session::Stop(uint64_t now_ms) {
  now_ms_ = now_ms;
  if (state_ == SessionState::kRunning) {
    Send(Bye{ByeReason::kQuit}, session_id_);
    Send(Bye{ByeReason::kQuit}, session_id_);
  }
  state_ = SessionState::kClosed;
}

void Session::Receive(uint64_t now_ms) {
  uint8_t buffer[kMaxPacketSize + 1];
  for (int i = 0; i < kReceiveBudget; ++i) {
    Endpoint from;
    const size_t size = transport_.Receive(buffer, sizeof buffer, from);
    if (size == 0) return;
    const std::optional<Packet> packet = Decode(buffer, size);
    if (!packet) {
      ++packets_dropped_;
      continue;
    }
    ++packets_received_;
    Handle(*packet, from, now_ms);
  }
}

void Session::Handle(const Packet& packet, const Endpoint& from, uint64_t now_ms) {
  if (const Hello* hello = std::get_if<Hello>(&packet.body)) {
    HandleHello(packet, *hello, from, now_ms);
    return;
  }
  if (const HelloAck* ack = std::get_if<HelloAck>(&packet.body)) {
    HandleHelloAck(packet, *ack, from, now_ms);
    return;
  }
  if (state_ != SessionState::kRunning || !(from == peer_) || packet.header.session != session_id_) {
    ++packets_dropped_;
    return;
  }
  last_recv_ms_ = now_ms;
  if (SeqDiff(packet.header.seq, last_recv_seq_) > 0) last_recv_seq_ = packet.header.seq;
  if (const Input* input = std::get_if<Input>(&packet.body)) {
    core_->OnPeerWanted(input->next_wanted);
    core_->OnRemoteInputs(input->first, input->words.data(), input->words.size());
  } else if (const Checksum* checksum = std::get_if<Checksum>(&packet.body)) {
    core_->OnRemoteChecksum(checksum->frame, checksum->value);
  } else if (const Bye* bye = std::get_if<Bye>(&packet.body)) {
    peer_bye_ = bye->reason;
    state_ = SessionState::kClosed;
  }
}

void Session::HandleHello(const Packet&, const Hello& hello, const Endpoint& from, uint64_t now_ms) {
  if (config_.role != Role::kHost) {
    ++packets_dropped_;
    return;
  }
  HelloAck ack;
  ack.nonce = hello.nonce;
  ack.input_delay = static_cast<uint8_t>(std::min(config_.input_delay, kMaxInputDelay));
  if (hello.guest != config_.guest) {
    ack.reject = RejectReason::kGuestMismatch;
    SendTo(from, ack, 0);
    return;
  }
  if (hello.build != kNetplayBuild) {
    ack.reject = RejectReason::kBuildMismatch;
    SendTo(from, ack, 0);
    return;
  }
  if (state_ == SessionState::kWaiting) {
    peer_ = from;
    session_id_ = config_.session_id;
    joiner_nonce_ = hello.nonce;
    local_player_ = 0;
    if (!CreateCore(0, ack.input_delay, 0)) return;
    last_recv_ms_ = now_ms;
    state_ = SessionState::kRunning;
    SendTo(from, ack, session_id_);
  } else if (state_ == SessionState::kRunning && from == peer_ && hello.nonce == joiner_nonce_) {
    SendTo(from, ack, session_id_);
  } else {
    ack.reject = RejectReason::kBusy;
    SendTo(from, ack, 0);
  }
}

void Session::HandleHelloAck(const Packet& packet, const HelloAck& ack, const Endpoint& from, uint64_t now_ms) {
  if (config_.role != Role::kJoin || state_ != SessionState::kConnecting || !(from == peer_) ||
      ack.nonce != config_.nonce) {
    ++packets_dropped_;
    return;
  }
  if (ack.reject != RejectReason::kNone) {
    reject_reason_ = ack.reject;
    state_ = SessionState::kRejected;
    return;
  }
  if (packet.header.session == 0 || ack.input_delay > kMaxInputDelay || ack.joiner_slot != 1) {
    ++packets_dropped_;
    return;
  }
  session_id_ = packet.header.session;
  local_player_ = ack.joiner_slot;
  if (!CreateCore(local_player_, ack.input_delay, ack.start_frame)) return;
  last_recv_ms_ = now_ms;
  state_ = SessionState::kRunning;
}

bool Session::CreateCore(int local_player, uint32_t input_delay, Frame start_frame) {
  RollbackConfig rc;
  rc.local_player = local_player;
  rc.input_delay = input_delay;
  rc.max_rollback = config_.max_rollback;
  rc.checksum_interval = config_.checksum_interval;
  rc.start_frame = start_frame;
  core_ = std::make_unique<RollbackCore>(sim_, rc);
  if (core_->Start()) return true;
  core_.reset();
  state_ = SessionState::kFaulted;
  return false;
}

void Session::SendTo(const Endpoint& to, const Body& body, uint32_t session) {
  uint8_t buffer[kMaxPacketSize];
  const Header header{session, send_seq_++, last_recv_seq_};
  const size_t size = Encode(header, body, buffer, sizeof buffer);
  if (size == 0) return;
  if (transport_.Send(to, buffer, size)) {
    ++packets_sent_;
    last_send_ms_ = now_ms_;
  }
}

void Session::Send(const Body& body, uint32_t session) { SendTo(peer_, body, session); }

void Session::SendInputs() {
  if (!core_) return;
  Input input;
  InputWord words[kMaxInputsPerPacket];
  const size_t count = core_->CollectLocalInputs(input.first, words, kMaxInputsPerPacket);
  if (count == 0) input.first = core_->local_next();
  input.words.assign(words, words + count);
  input.next_wanted = core_->remote_wanted();
  Send(input, session_id_);
}

void Session::QueueChecksums() {
  for (const ChecksumPoint& p : core_->TakeLocalChecksums()) pending_checks_.push_back({p, kChecksumRepeats});
}

void Session::SendPendingChecksums() {
  for (PendingCheck& pending : pending_checks_) {
    Send(Checksum{pending.point.frame, pending.point.value}, session_id_);
    --pending.remaining;
  }
  std::erase_if(pending_checks_, [](const PendingCheck& p) { return p.remaining <= 0; });
}

void Session::SendHelloIfDue(uint64_t now_ms) {
  if (now_ms - last_hello_ms_ < kHelloIntervalMs) return;
  last_hello_ms_ = now_ms;
  Hello hello;
  hello.guest = config_.guest;
  hello.nonce = config_.nonce;
  SendTo(peer_, hello, 0);
}

}  // namespace ssf2t::netplay
