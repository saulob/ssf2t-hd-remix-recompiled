#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "netplay/frame.h"

namespace ssf2t::netplay {

using InputWord = uint16_t;
inline constexpr int kPlayers = 2;
inline constexpr size_t kInputHistory = 256;
inline constexpr uint32_t kMaxRemoteAhead = 64;
inline constexpr size_t kWindow = 32;

// The deterministic fight simulation the core drives. All calls come from the one thread that owns the core.
class Simulation {
 public:
  virtual ~Simulation() = default;
  virtual bool Save(std::vector<uint8_t>& out) = 0;
  virtual bool Load(const std::vector<uint8_t>& in) = 0;
  // `replay` marks a frame run again for a rollback, which must produce no audiovisual effects.
  virtual bool Advance(const InputWord (&inputs)[kPlayers], bool replay) = 0;
  // Of the live state; compared with the peer's at the same frame.
  virtual uint32_t Checksum() = 0;
};

struct RollbackConfig {
  int local_player = 0;
  uint32_t input_delay = 1;
  uint32_t max_rollback = 8;
  uint32_t checksum_interval = 30;
  Frame start_frame = 0;
};

enum class TickStatus { kAdvanced, kStalled, kFault };

struct Desync {
  Frame frame = 0;
  uint32_t local = 0;
  uint32_t remote = 0;
};

struct CoreStats {
  uint32_t frames = 0;
  uint32_t rollbacks = 0;
  uint32_t resimulated = 0;
  uint32_t max_depth = 0;
  uint32_t stalls = 0;
};

struct ChecksumPoint {
  Frame frame = 0;
  uint32_t value = 0;
};

class RollbackCore {
 public:
  RollbackCore(Simulation& sim, const RollbackConfig& config);

  // Saves the first snapshot; false when the simulation cannot save.
  bool Start();

  // Runs the next frame with the local sample, predicting the remote input when it is missing. Stalls rather than
  // predicting past the rollback window or leaving the peer more than a packet of inputs behind.
  TickStatus Tick(InputWord local_sample);

  // Stores the peer's inputs for [first, first + count). Frames already known, too old or too far ahead are ignored.
  void OnRemoteInputs(Frame first, const InputWord* words, size_t count);
  void OnPeerWanted(Frame next_wanted);
  void OnRemoteChecksum(Frame frame, uint32_t value);

  // The peer lacks the local inputs from `first` on; copies up to `max` of them.
  size_t CollectLocalInputs(Frame& first, InputWord* out, size_t max) const;
  // Local checkpoints that became final since the last call.
  std::vector<ChecksumPoint> TakeLocalChecksums();

  Frame current_frame() const { return frame_; }
  Frame confirmed_frame() const { return confirmed_; }
  Frame remote_wanted() const { return remote_contiguous_; }
  Frame local_next() const { return local_next_; }
  const RollbackConfig& config() const { return config_; }
  const std::optional<Desync>& desync() const { return desync_; }
  const CoreStats& stats() const { return stats_; }
  bool faulted() const { return fault_; }

 private:
  struct Word {
    Frame frame = 0;
    bool set = false;
    InputWord value = 0;
  };
  struct Snapshot {
    Frame frame = 0;
    bool valid = false;
    bool has_checksum = false;
    uint32_t checksum = 0;
    std::vector<uint8_t> state;
  };

  static size_t Slot(Frame f) { return f % kInputHistory; }
  Snapshot& SnapshotFor(Frame f) { return snapshots_[f % snapshots_.size()]; }
  bool SaveSnapshot(Frame f);
  InputWord RemoteFor(Frame f) const;
  void Inputs(Frame f, InputWord (&out)[kPlayers]);
  bool Rollback(Frame to);
  void UpdateConfirmed();
  void CompareChecksums();

  Simulation& sim_;
  RollbackConfig config_;
  Frame frame_;
  Frame confirmed_;
  Frame local_next_;
  Frame remote_contiguous_;
  Frame peer_wanted_;
  std::optional<Frame> rollback_from_;
  std::vector<Word> local_;
  std::vector<Word> remote_;
  std::vector<Word> used_remote_;
  std::vector<Snapshot> snapshots_;
  std::vector<ChecksumPoint> outgoing_;
  std::vector<ChecksumPoint> local_checks_;
  std::vector<ChecksumPoint> remote_checks_;
  std::optional<Desync> desync_;
  CoreStats stats_;
  bool fault_ = false;
};

}  // namespace ssf2t::netplay
