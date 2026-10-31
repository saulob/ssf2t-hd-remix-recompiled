#include "netplay/rollback_core.h"

#include <algorithm>

namespace ssf2t::netplay {
namespace {

constexpr size_t kMaxChecks = 64;

}  // namespace

RollbackCore::RollbackCore(Simulation& sim, const RollbackConfig& config)
    : sim_(sim),
      config_(config),
      frame_(config.start_frame),
      confirmed_(config.start_frame),
      local_next_(config.start_frame + config.input_delay),
      remote_contiguous_(config.start_frame + config.input_delay),
      peer_wanted_(config.start_frame + config.input_delay),
      local_(kInputHistory),
      remote_(kInputHistory),
      used_remote_(kInputHistory),
      snapshots_(std::max<uint32_t>(config.max_rollback, 1) + 2) {
  config_.max_rollback = std::max<uint32_t>(config.max_rollback, 1);
  for (uint32_t i = 0; i < config.input_delay; ++i) {
    const Frame f = config.start_frame + i;
    local_[Slot(f)] = {f, true, 0};
    remote_[Slot(f)] = {f, true, 0};
  }
}

bool RollbackCore::Start() { return SaveSnapshot(frame_); }

bool RollbackCore::SaveSnapshot(Frame f) {
  Snapshot& s = SnapshotFor(f);
  s.valid = false;
  if (!sim_.Save(s.state)) {
    fault_ = true;
    return false;
  }
  s.frame = f;
  s.valid = true;
  s.has_checksum = false;
  if (config_.checksum_interval != 0 && f != config_.start_frame && f % config_.checksum_interval == 0) {
    s.checksum = sim_.Checksum();
    s.has_checksum = true;
  }
  return true;
}

InputWord RollbackCore::RemoteFor(Frame f) const {
  const Word& real = remote_[Slot(f)];
  if (real.set && real.frame == f) return real.value;
  if (FrameDiff(remote_contiguous_, config_.start_frame) <= 0) return 0;
  const Frame last = remote_contiguous_ - 1;
  const Word& previous = remote_[Slot(last)];
  return previous.set && previous.frame == last ? previous.value : InputWord(0);
}

void RollbackCore::Inputs(Frame f, InputWord (&out)[kPlayers]) {
  const Word& local = local_[Slot(f)];
  const InputWord mine = local.set && local.frame == f ? local.value : InputWord(0);
  const InputWord theirs = RemoteFor(f);
  used_remote_[Slot(f)] = {f, true, theirs};
  out[config_.local_player] = mine;
  out[1 - config_.local_player] = theirs;
}

TickStatus RollbackCore::Tick(InputWord local_sample) {
  if (fault_) return TickStatus::kFault;
  if (rollback_from_) {
    const Frame to = *rollback_from_;
    rollback_from_.reset();
    if (!Rollback(to)) {
      fault_ = true;
      return TickStatus::kFault;
    }
  }
  if (FrameDiff(frame_, confirmed_) >= static_cast<int32_t>(config_.max_rollback) ||
      FrameDiff(local_next_, peer_wanted_) >= static_cast<int32_t>(kWindow)) {
    ++stats_.stalls;
    return TickStatus::kStalled;
  }
  local_[Slot(local_next_)] = {local_next_, true, local_sample};
  ++local_next_;

  InputWord inputs[kPlayers];
  Inputs(frame_, inputs);
  if (!sim_.Advance(inputs, false)) {
    fault_ = true;
    return TickStatus::kFault;
  }
  ++frame_;
  ++stats_.frames;
  if (!SaveSnapshot(frame_)) return TickStatus::kFault;
  UpdateConfirmed();
  return TickStatus::kAdvanced;
}

bool RollbackCore::Rollback(Frame to) {
  Snapshot& s = SnapshotFor(to);
  if (!s.valid || s.frame != to || !sim_.Load(s.state)) return false;
  const uint32_t depth = static_cast<uint32_t>(FrameDiff(frame_, to));
  ++stats_.rollbacks;
  stats_.resimulated += depth;
  stats_.max_depth = std::max(stats_.max_depth, depth);
  for (Frame g = to; g != frame_; ++g) {
    InputWord inputs[kPlayers];
    Inputs(g, inputs);
    if (!sim_.Advance(inputs, true) || !SaveSnapshot(g + 1)) return false;
  }
  UpdateConfirmed();
  return true;
}

void RollbackCore::OnRemoteInputs(Frame first, const InputWord* words, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    const Frame f = first + static_cast<Frame>(i);
    if (FrameDiff(f, remote_contiguous_) < 0) continue;
    if (FrameDiff(f, frame_) > static_cast<int32_t>(kMaxRemoteAhead)) break;
    Word& slot = remote_[Slot(f)];
    if (slot.set && slot.frame == f) continue;
    slot = {f, true, words[i]};
    const Word& used = used_remote_[Slot(f)];
    if (FrameDiff(f, frame_) < 0 && used.set && used.frame == f && used.value != words[i] &&
        (!rollback_from_ || FrameBefore(f, *rollback_from_))) {
      rollback_from_ = f;
    }
  }
  while (true) {
    const Word& w = remote_[Slot(remote_contiguous_)];
    if (!(w.set && w.frame == remote_contiguous_)) break;
    ++remote_contiguous_;
  }
  UpdateConfirmed();
}

void RollbackCore::OnPeerWanted(Frame next_wanted) {
  if (FrameDiff(next_wanted, local_next_) > 0) return;
  if (FrameDiff(next_wanted, peer_wanted_) > 0) peer_wanted_ = next_wanted;
}

void RollbackCore::OnRemoteChecksum(Frame frame, uint32_t value) {
  for (const ChecksumPoint& p : remote_checks_) {
    if (p.frame == frame) return;
  }
  if (remote_checks_.size() >= kMaxChecks) remote_checks_.erase(remote_checks_.begin());
  remote_checks_.push_back({frame, value});
  CompareChecksums();
}

void RollbackCore::UpdateConfirmed() {
  const Frame old = confirmed_;
  while (FrameDiff(confirmed_, frame_) < 0) {
    const Word& real = remote_[Slot(confirmed_)];
    const Word& used = used_remote_[Slot(confirmed_)];
    if (!(real.set && real.frame == confirmed_ && used.set && used.frame == confirmed_ && used.value == real.value)) {
      break;
    }
    ++confirmed_;
  }
  if (config_.checksum_interval != 0) {
    for (Frame c = old + 1; FrameDiff(c, confirmed_) <= 0; ++c) {
      if (c % config_.checksum_interval != 0) continue;
      const Snapshot& s = snapshots_[c % snapshots_.size()];
      if (!s.valid || s.frame != c || !s.has_checksum) continue;
      if (local_checks_.size() >= kMaxChecks) local_checks_.erase(local_checks_.begin());
      local_checks_.push_back({c, s.checksum});
      outgoing_.push_back({c, s.checksum});
    }
  }
  CompareChecksums();
}

void RollbackCore::CompareChecksums() {
  for (auto local = local_checks_.begin(); local != local_checks_.end();) {
    const auto remote = std::find_if(remote_checks_.begin(), remote_checks_.end(),
                                     [&](const ChecksumPoint& p) { return p.frame == local->frame; });
    if (remote == remote_checks_.end()) {
      ++local;
      continue;
    }
    ++stats_.checksums;
    if (local->value != remote->value) {
      ++stats_.mismatches;
      if (!desync_) desync_ = Desync{local->frame, local->value, remote->value};
    }
    remote_checks_.erase(remote);
    local = local_checks_.erase(local);
  }
}

size_t RollbackCore::CollectLocalInputs(Frame& first, InputWord* out, size_t max) const {
  first = peer_wanted_;
  const int32_t pending = FrameDiff(local_next_, peer_wanted_);
  if (pending <= 0) return 0;
  const size_t n = std::min<size_t>({static_cast<size_t>(pending), max, kWindow});
  for (size_t i = 0; i < n; ++i) {
    const Frame f = peer_wanted_ + static_cast<Frame>(i);
    const Word& w = local_[Slot(f)];
    out[i] = w.set && w.frame == f ? w.value : InputWord(0);
  }
  return n;
}

std::vector<ChecksumPoint> RollbackCore::TakeLocalChecksums() {
  std::vector<ChecksumPoint> taken;
  taken.swap(outgoing_);
  return taken;
}

}  // namespace ssf2t::netplay
