#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "netplay/packet.h"

namespace ssf2t::netplay {

inline constexpr size_t kStateBlockBytes = 256;
inline constexpr size_t kMaxReportedBlocks = 8;
// More than this differing at frame 0 is not state a previous fight left behind: the peers met at different points.
inline constexpr size_t kMaxAdoptedBlocks = 64;

// The simulation blob as the peers compare it at frame 0. Of the input table only the fields the game's set-input
// command writes for the two fighters remain: the fight reads nothing else there, and the per-frame menu repeat
// counters change on every real frame, also while a peer waits.
std::vector<uint8_t> FrameZeroView(const std::vector<uint8_t>& blob);

std::vector<uint32_t> StateBlockHashes(const std::vector<uint8_t>& view);

// "arena+0x3F0 checked" (inside the game's own checksum), "arena+0x2000 unchecked", "globals+0x40 @83E0E820".
std::string DescribeStateOffset(size_t offset, uint32_t globals_address);

// "00000003,00020000,..."
std::string DescribeSetupWords(const std::vector<uint32_t>& words);

// kAgreeing: the setups match and the joiner is taking the host's frame-0 state.
enum class FrameZeroVerdict { kPending, kAgreeing, kMatch, kMismatch };
enum class FrameZeroRefusal { kNone, kSetup, kState, kTooDifferent };

const char* RefusalName(FrameZeroRefusal refusal);

struct FrameZeroAdoption {
  size_t blocks = 0;
  size_t bytes = 0;
};

// Agrees on the frame-0 state with the peer before the rollback core may start. The setup words must be equal. The
// state may differ only by what each peer's previous fight left behind: the joiner (`adopts`) then takes the host's
// bytes wherever the two differ and offers the result as round 1, which must match. Any other difference holds the
// fight; the peers then swap block hashes and bytes, and each differing byte range is reported with both values.
class FrameZeroCheck {
 public:
  FrameZeroCheck(uint32_t fight, uint8_t round, std::vector<uint8_t> view, uint32_t globals_address,
                 std::vector<uint32_t> setup, bool adopts);

  uint32_t fight() const { return fight_; }
  uint8_t round() const { return digest_.round; }
  uint32_t globals_address() const { return globals_address_; }
  FrameZeroVerdict verdict() const;
  FrameZeroRefusal refusal() const;
  const StateDigest& local_digest() const { return digest_; }
  const std::optional<StateDigest>& remote_digest() const { return remote_; }

  // The host's bytes of every differing block arrived.
  bool AdoptionReady() const;
  // Writes the host's bytes into `blob`, the simulation blob this check's view was made from, wherever the two views
  // differ. Bytes the view leaves out are never written.
  std::optional<FrameZeroAdoption> Adopt(std::vector<uint8_t>& blob) const;

  void OnDigest(const StateDigest& digest);
  void OnBlocks(const StateBlocks& blocks);
  void OnBytes(const StateBytes& bytes);

  // The packets due now: the digest every 100 ms and, once the digests disagree, the block hashes and then the bytes
  // of the differing blocks every 500 ms.
  std::vector<Body> Due(uint64_t now_ms);
  // Log lines produced since the last call.
  std::vector<std::string> TakeReport();

 private:
  bool SetupMatches() const { return remote_ && remote_->setup == digest_.setup; }
  bool TooDifferent() const { return differing_ && differing_->size() > kMaxAdoptedBlocks; }
  bool RemoteBlocksComplete() const { return remote_block_count_ == local_blocks_.size(); }
  bool BlockBytesKnown(size_t block) const;
  std::vector<uint8_t> RemoteBlock(size_t block) const;
  size_t BlockSize(size_t block) const;
  void ForgetRemoteDetail();
  void ReportSetup();
  void FindDifferingBlocks();
  void ReportBlock(size_t block);
  void ReportReadyBlocks();

  uint32_t fight_;
  std::vector<uint8_t> view_;
  uint32_t globals_address_;
  bool adopts_;
  StateDigest digest_;
  std::vector<uint32_t> local_blocks_;
  std::optional<StateDigest> remote_;
  std::vector<uint32_t> remote_blocks_;
  std::vector<bool> remote_block_known_;
  size_t remote_block_count_ = 0;
  std::optional<std::vector<size_t>> differing_;
  std::vector<bool> reported_;
  std::map<uint32_t, std::vector<uint8_t>> remote_bytes_;
  std::optional<uint64_t> last_digest_ms_;
  std::optional<uint64_t> last_detail_ms_;
  std::vector<std::string> report_;
};

}  // namespace ssf2t::netplay
