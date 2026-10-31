#include "netplay/session.h"

#include <algorithm>

namespace ssf2t::netplay {
namespace {

constexpr uint64_t kHelloIntervalMs = 100;
constexpr uint64_t kReadyIntervalMs = 100;
constexpr uint64_t kKeepaliveMs = 100;
constexpr int kReceiveBudget = 64;
constexpr int kChecksumRepeats = 3;
constexpr uint32_t kMaxInputDelay = 8;
constexpr uint64_t kRttWindowMs = 2000;

}  // namespace

Session::Session(const SessionConfig& config, Transport& transport) : config_(config), transport_(transport) {
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
  if (state_ != SessionState::kWaiting && state_ != SessionState::kConnecting &&
      state_ != SessionState::kConnected && state_ != SessionState::kRunning) {
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
    case SessionState::kConnected:
      if (now_ms - last_recv_ms_ > config_.disconnect_timeout_ms) {
        state_ = SessionState::kTimedOut;
      } else {
        SendReadyIfDue(now_ms);
        SendFrameZeroIfDue(now_ms);
      }
      break;
    case SessionState::kRunning:
      if (now_ms - last_recv_ms_ > config_.disconnect_timeout_ms) {
        state_ = SessionState::kTimedOut;
      } else {
        SendReadyIfDue(now_ms);
        SendFrameZeroIfDue(now_ms);
        if (now_ms - last_send_ms_ >= kKeepaliveMs) SendInputs();
      }
      break;
    default:
      break;
  }
}

void Session::SetLocalReady(bool ready, uint64_t now_ms) {
  const bool left = local_ready_ && !ready;
  local_ready_ = ready;
  if (!ready && state_ == SessionState::kRunning) {
    EndFight(now_ms);
    return;
  }
  if (left) ResetFrameZero();
  CheckBothReady();
}

void Session::CheckBothReady() {
  if (config_.role == Role::kHost && state_ == SessionState::kConnected && !agreed_start_ && local_ready_ &&
      remote_ready_) {
    agreed_start_ = config_.start_frame;
  }
}

bool Session::ReadyToBeginFight() const { return state_ == SessionState::kConnected && agreed_start_.has_value(); }

void Session::OfferFrameZero(std::vector<uint8_t> view, uint32_t globals_address, std::vector<uint32_t> setup) {
  frame_zero_ = std::make_unique<FrameZeroCheck>(fights_begun_, 0, std::move(view), globals_address, std::move(setup),
                                                 config_.role == Role::kJoin);
  if (early_digest_ && early_digest_->fight == fights_begun_) frame_zero_->OnDigest(*early_digest_);
  early_digest_.reset();
}

FrameZeroVerdict Session::frame_zero() const {
  return frame_zero_ ? frame_zero_->verdict() : FrameZeroVerdict::kPending;
}

FrameZeroRefusal Session::frame_zero_refusal() const {
  return frame_zero_ ? frame_zero_->refusal() : FrameZeroRefusal::kNone;
}

bool Session::FrameZeroAdoptionReady() const { return frame_zero_ && frame_zero_->AdoptionReady(); }

std::optional<FrameZeroAdoption> Session::AdoptFrameZero(std::vector<uint8_t>& blob) const {
  if (!frame_zero_) return std::nullopt;
  return frame_zero_->Adopt(blob);
}

// The host's digest of round 0 stays the one to match: the host never changes its state.
void Session::ReofferFrameZero(std::vector<uint8_t> view) {
  if (!frame_zero_ || frame_zero_->round() != 0) return;
  const FrameZeroCheck& first = *frame_zero_;
  const std::optional<StateDigest> host = first.remote_digest();
  auto again = std::make_unique<FrameZeroCheck>(first.fight(), 1, std::move(view), first.globals_address(),
                                                first.local_digest().setup, false);
  if (host) again->OnDigest(*host);
  frame_zero_ = std::move(again);
}

std::vector<std::string> Session::TakeFrameZeroReport() {
  return frame_zero_ ? frame_zero_->TakeReport() : std::vector<std::string>{};
}

bool Session::BeginFight(Simulation& sim, uint64_t now_ms) {
  now_ms_ = now_ms;
  if (!ReadyToBeginFight()) return false;
  if (config_.verify_frame_zero && frame_zero() != FrameZeroVerdict::kMatch) return false;
  if (!CreateCore(sim, local_player_, negotiated_input_delay_, *agreed_start_)) return false;
  ++fights_begun_;
  state_ = SessionState::kRunning;
  return true;
}

void Session::EndFight(uint64_t) {
  core_.reset();
  pending_checks_.clear();
  remote_ready_ = false;
  agreed_start_.reset();
  ResetFrameZero();
  if (state_ == SessionState::kRunning) state_ = SessionState::kConnected;
}

void Session::ResetFrameZero() {
  frame_zero_.reset();
  early_digest_.reset();
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
  if (state_ == SessionState::kRunning || state_ == SessionState::kConnected) {
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
  if ((state_ != SessionState::kConnected && state_ != SessionState::kRunning) || !(from == peer_) ||
      packet.header.session != session_id_) {
    ++packets_dropped_;
    return;
  }
  last_recv_ms_ = now_ms;
  if (SeqDiff(packet.header.seq, last_recv_seq_) > 0) last_recv_seq_ = packet.header.seq;
  NoteAck(packet.header.ack, now_ms);
  if (const Ready* ready = std::get_if<Ready>(&packet.body)) {
    HandleReady(*ready, now_ms);
  } else if (const Bye* bye = std::get_if<Bye>(&packet.body)) {
    peer_bye_ = bye->reason;
    state_ = SessionState::kClosed;
  } else if (std::holds_alternative<StateDigest>(packet.body) || std::holds_alternative<StateBlocks>(packet.body) ||
             std::holds_alternative<StateBytes>(packet.body)) {
    HandleFrameZero(packet.body);
  } else if (state_ != SessionState::kRunning || packet.header.fight != uint16_t(fights_begun_)) {
    ++packets_dropped_;
  } else if (const Input* input = std::get_if<Input>(&packet.body)) {
    core_->OnPeerWanted(input->next_wanted);
    core_->OnRemoteInputs(input->first, input->words.data(), input->words.size());
  } else if (const Checksum* checksum = std::get_if<Checksum>(&packet.body)) {
    core_->OnRemoteChecksum(checksum->frame, checksum->value);
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
    negotiated_input_delay_ = ack.input_delay;
    last_recv_ms_ = now_ms;
    state_ = SessionState::kConnected;
    SendTo(from, ack, session_id_);
  } else if (state_ != SessionState::kWaiting && from == peer_ && hello.nonce == joiner_nonce_) {
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
  negotiated_input_delay_ = ack.input_delay;
  last_recv_ms_ = now_ms;
  state_ = SessionState::kConnected;
}

void Session::HandleReady(const Ready& ready, uint64_t now_ms) {
  const bool left = remote_ready_ && !ready.ready;
  remote_ready_ = ready.ready;
  if (!remote_ready_ && state_ == SessionState::kRunning) {
    EndFight(now_ms);
    return;
  }
  if (left) ResetFrameZero();
  if (config_.role == Role::kJoin && ready.has_start_frame && !agreed_start_) {
    agreed_start_ = ready.start_frame;
  }
  CheckBothReady();
}

// A digest may arrive before the local fight reached frame 0; it is kept for that fight only.
void Session::HandleFrameZero(const Body& body) {
  if (const StateDigest* digest = std::get_if<StateDigest>(&body)) {
    if (frame_zero_) {
      frame_zero_->OnDigest(*digest);
    } else if (digest->fight == fights_begun_ && (!early_digest_ || digest->round >= early_digest_->round)) {
      early_digest_ = *digest;
    }
    return;
  }
  if (!frame_zero_) return;
  if (const StateBlocks* blocks = std::get_if<StateBlocks>(&body)) frame_zero_->OnBlocks(*blocks);
  if (const StateBytes* bytes = std::get_if<StateBytes>(&body)) frame_zero_->OnBytes(*bytes);
}

// Once the peer's inputs arrive it has begun too, so it holds the local digest.
void Session::SendFrameZeroIfDue(uint64_t now_ms) {
  if (!frame_zero_) return;
  if (core_ && FrameDiff(core_->remote_wanted(), core_->config().start_frame + core_->config().input_delay) > 0) {
    return;
  }
  for (const Body& body : frame_zero_->Due(now_ms)) Send(body, session_id_);
}

bool Session::CreateCore(Simulation& sim, int local_player, uint32_t input_delay, Frame start_frame) {
  RollbackConfig rc;
  rc.local_player = local_player;
  rc.input_delay = input_delay;
  rc.max_rollback = config_.max_rollback;
  rc.checksum_interval = config_.checksum_interval;
  rc.start_frame = start_frame;
  core_ = std::make_unique<RollbackCore>(sim, rc);
  if (core_->Start()) return true;
  core_.reset();
  state_ = SessionState::kFaulted;
  return false;
}

void Session::SendTo(const Endpoint& to, const Body& body, uint32_t session) {
  uint8_t buffer[kMaxPacketSize];
  const Header header{session, send_seq_, last_recv_seq_, uint16_t(fights_begun_)};
  const size_t slot = send_seq_ % kSentRing;
  sent_seq_[slot] = send_seq_;
  sent_at_[slot] = now_ms_;
  sent_valid_[slot] = true;
  ++send_seq_;
  const size_t size = Encode(header, body, buffer, sizeof buffer);
  if (size == 0) return;
  if (transport_.Send(to, buffer, size)) {
    ++packets_sent_;
    last_send_ms_ = now_ms_;
  }
}

void Session::NoteAck(uint16_t ack, uint64_t now_ms) {
  const bool first = !has_acked_;
  if (!first && SeqDiff(ack, last_acked_) <= 0) return;
  has_acked_ = true;
  last_acked_ = ack;
  const size_t slot = ack % kSentRing;
  if (first || !sent_valid_[slot] || sent_seq_[slot] != ack || now_ms < sent_at_[slot]) return;
  rtt_samples_.push_back({now_ms, uint32_t(now_ms - sent_at_[slot])});
  std::erase_if(rtt_samples_, [now_ms](const RttSample& s) { return now_ms - s.at > kRttWindowMs; });
}

std::optional<uint32_t> Session::rtt_ms() const {
  std::optional<uint32_t> lowest;
  for (const RttSample& s : rtt_samples_) {
    if (now_ms_ - s.at > kRttWindowMs) continue;
    if (!lowest || s.ms < *lowest) lowest = s.ms;
  }
  return lowest;
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

void Session::SendReadyIfDue(uint64_t now_ms) {
  if (now_ms - last_ready_ms_ < kReadyIntervalMs) return;
  last_ready_ms_ = now_ms;
  Ready ready;
  ready.ready = local_ready_;
  if (config_.role == Role::kHost && agreed_start_) {
    ready.has_start_frame = true;
    ready.start_frame = *agreed_start_;
  }
  Send(ready, session_id_);
}

}  // namespace ssf2t::netplay
