#include "netplay/frame_zero.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <utility>

#include "netplay/determinism.h"
#include "netplay/guest_netplay.h"
#include "netplay/rollback_core.h"

namespace ssf2t::netplay {
namespace {

constexpr uint64_t kDigestIntervalMs = 100;
constexpr uint64_t kDetailIntervalMs = 500;
constexpr size_t kMaxRunsPerBlock = 8;
constexpr size_t kMaxValueBytes = 16;
constexpr size_t kMaxListedBlocks = 16;
constexpr size_t kMaxStoredChunks = 2 * kMaxAdoptedBlocks;

// Arena bytes the game's own rollback checksum (callback command 4) covers.
constexpr size_t kChecked[][2] = {{0, 1638}, {1792, 3328}};

// What the set-input command writes in each fighter's entry: state, current, previous, pressed, released.
constexpr uint32_t kInputCommandFields[] = {0, kInputCurrentOffset, kInputPreviousOffset, 16, 20};

std::string Hex(const uint8_t* data, size_t size) {
  std::string text;
  char byte[3];
  for (size_t i = 0; i < std::min(size, kMaxValueBytes); ++i) {
    std::snprintf(byte, sizeof byte, "%02X", data[i]);
    text += byte;
  }
  if (size > kMaxValueBytes) text += "..";
  return text;
}

}  // namespace

std::vector<uint8_t> FrameZeroView(const std::vector<uint8_t>& blob) {
  if (blob.size() < kSimBlobSize) return blob;
  std::vector<uint8_t> view(blob.begin(), blob.begin() + kSimBlobSize);
  std::fill(view.begin() + kGuestStateSize, view.begin() + kGuestStateSize + kInputTableStateBytes, 0);
  for (uint32_t player = 0; player < kPlayers; ++player) {
    for (const uint32_t field : kInputCommandFields) {
      const size_t at = kGuestStateSize + player * kInputEntryStride + field;
      std::memcpy(view.data() + at, blob.data() + at, 4);
    }
  }
  return view;
}

std::vector<uint32_t> StateBlockHashes(const std::vector<uint8_t>& view) {
  std::vector<uint32_t> hashes;
  for (size_t at = 0; at < view.size(); at += kStateBlockBytes) {
    const uint64_t hash = Fnv1a64(view.data() + at, std::min(kStateBlockBytes, view.size() - at));
    hashes.push_back(uint32_t(hash ^ (hash >> 32)));
  }
  return hashes;
}

std::string DescribeStateOffset(size_t offset, uint32_t globals_address) {
  std::string text = DescribeOffset(offset);
  if (offset < kArenaSaveSize) {
    const bool checked = std::any_of(std::begin(kChecked), std::end(kChecked),
                                     [offset](const size_t (&r)[2]) { return offset >= r[0] && offset < r[1]; });
    text += checked ? " checked" : " unchecked";
  } else if (offset < kGuestStateSize && globals_address != 0) {
    char address[16];
    std::snprintf(address, sizeof address, " @%08X", uint32_t(globals_address + offset - kArenaSaveSize));
    text += address;
  }
  return text;
}

std::string DescribeSetupWords(const std::vector<uint32_t>& words) {
  std::string text;
  char word[16];
  for (const uint32_t value : words) {
    std::snprintf(word, sizeof word, "%s%08X", text.empty() ? "" : ",", value);
    text += word;
  }
  return text;
}

const char* RefusalName(FrameZeroRefusal refusal) {
  switch (refusal) {
    case FrameZeroRefusal::kSetup: return "setup_mismatch";
    case FrameZeroRefusal::kState: return "frame0_mismatch";
    case FrameZeroRefusal::kTooDifferent: return "frame0_too_different";
    default: return "none";
  }
}

FrameZeroCheck::FrameZeroCheck(uint32_t fight, uint8_t round, std::vector<uint8_t> view, uint32_t globals_address,
                               std::vector<uint32_t> setup, bool adopts)
    : fight_(fight), view_(std::move(view)), globals_address_(globals_address), adopts_(adopts) {
  digest_.fight = fight_;
  digest_.round = round;
  digest_.regions = StateRegionHashes(view_);
  digest_.setup = std::move(setup);
  local_blocks_ = StateBlockHashes(view_);
  remote_blocks_.assign(local_blocks_.size(), 0);
  remote_block_known_.assign(local_blocks_.size(), false);
}

FrameZeroVerdict FrameZeroCheck::verdict() const {
  if (!remote_) return FrameZeroVerdict::kPending;
  if (!SetupMatches()) return FrameZeroVerdict::kMismatch;
  if (remote_->regions == digest_.regions) return FrameZeroVerdict::kMatch;
  if (TooDifferent()) return FrameZeroVerdict::kMismatch;
  return digest_.round == 0 && remote_->round == 0 ? FrameZeroVerdict::kAgreeing : FrameZeroVerdict::kMismatch;
}

FrameZeroRefusal FrameZeroCheck::refusal() const {
  if (verdict() != FrameZeroVerdict::kMismatch) return FrameZeroRefusal::kNone;
  if (!SetupMatches()) return FrameZeroRefusal::kSetup;
  return TooDifferent() ? FrameZeroRefusal::kTooDifferent : FrameZeroRefusal::kState;
}

// The highest round wins: a joiner's state after it took the host's replaces what it offered before.
void FrameZeroCheck::OnDigest(const StateDigest& digest) {
  if (digest.fight != fight_ || (remote_ && digest.round <= remote_->round)) return;
  if (remote_) ForgetRemoteDetail();
  remote_ = digest;
  ReportSetup();
  if (!SetupMatches()) return;
  const char* round = digest_.round != 0 || remote_->round != 0 ? " round=1" : "";
  char line[192];
  if (remote_->regions == digest_.regions) {
    std::snprintf(line, sizeof line, "netplay frame0 state match fight=%u%s", fight_, round);
    report_.push_back(line);
    return;
  }
  std::string regions;
  for (size_t i = 0; i < kStateRegions; ++i) {
    if (digest_.regions[i] == remote_->regions[i]) continue;
    regions += regions.empty() ? "" : ",";
    regions += StateRegionName(i);
  }
  std::snprintf(line, sizeof line, "netplay frame0 state MISMATCH fight=%u%s regions=%s", fight_, round,
                regions.c_str());
  report_.push_back(line);
  for (size_t i = 0; i < kStateRegions; ++i) {
    if (digest_.regions[i] == remote_->regions[i]) continue;
    std::snprintf(line, sizeof line, "netplay frame0 region=%s local=%08X remote=%08X", StateRegionName(i),
                  digest_.regions[i], remote_->regions[i]);
    report_.push_back(line);
  }
}

void FrameZeroCheck::ReportSetup() {
  char line[112];
  std::snprintf(line, sizeof line, "netplay setup remote fight=%u round=%u words=", fight_,
                unsigned(remote_->round));
  report_.push_back(line + DescribeSetupWords(remote_->setup));
  if (SetupMatches()) return;
  const size_t count = std::max(digest_.setup.size(), remote_->setup.size());
  for (size_t i = 0; i < count; ++i) {
    const bool both = i < digest_.setup.size() && i < remote_->setup.size();
    if (both && digest_.setup[i] == remote_->setup[i]) continue;
    std::snprintf(line, sizeof line, "netplay setup MISMATCH fight=%u word=%zu local=%08X remote=%08X",
                  fight_, i, i < digest_.setup.size() ? digest_.setup[i] : 0,
                  i < remote_->setup.size() ? remote_->setup[i] : 0);
    report_.push_back(line);
  }
}

void FrameZeroCheck::ForgetRemoteDetail() {
  remote_blocks_.assign(local_blocks_.size(), 0);
  remote_block_known_.assign(local_blocks_.size(), false);
  remote_block_count_ = 0;
  differing_.reset();
  reported_.clear();
  remote_bytes_.clear();
  last_detail_ms_.reset();
}

void FrameZeroCheck::OnBlocks(const StateBlocks& blocks) {
  if (!remote_ || blocks.fight != fight_ || blocks.round != remote_->round ||
      size_t(blocks.first) + blocks.hashes.size() > local_blocks_.size()) {
    return;
  }
  for (size_t i = 0; i < blocks.hashes.size(); ++i) {
    const size_t block = blocks.first + i;
    if (remote_block_known_[block]) continue;
    remote_block_known_[block] = true;
    remote_blocks_[block] = blocks.hashes[i];
    ++remote_block_count_;
  }
  if (RemoteBlocksComplete() && !differing_) FindDifferingBlocks();
  ReportReadyBlocks();
}

void FrameZeroCheck::OnBytes(const StateBytes& bytes) {
  if (!remote_ || bytes.fight != fight_ || bytes.round != remote_->round ||
      size_t(bytes.offset) + bytes.bytes.size() > view_.size()) {
    return;
  }
  if (remote_bytes_.size() >= kMaxStoredChunks && !remote_bytes_.count(bytes.offset)) return;
  remote_bytes_[bytes.offset] = bytes.bytes;
  ReportReadyBlocks();
}

size_t FrameZeroCheck::BlockSize(size_t block) const {
  return std::min(kStateBlockBytes, view_.size() - block * kStateBlockBytes);
}

bool FrameZeroCheck::BlockBytesKnown(size_t block) const {
  const size_t begin = block * kStateBlockBytes;
  for (size_t at = begin; at < begin + BlockSize(block); at += kMaxStateBytesPerPacket) {
    const auto chunk = remote_bytes_.find(uint32_t(at));
    const size_t want = std::min(kMaxStateBytesPerPacket, begin + BlockSize(block) - at);
    if (chunk == remote_bytes_.end() || chunk->second.size() != want) return false;
  }
  return true;
}

std::vector<uint8_t> FrameZeroCheck::RemoteBlock(size_t block) const {
  const size_t begin = block * kStateBlockBytes;
  std::vector<uint8_t> remote(BlockSize(block));
  for (size_t at = 0; at < remote.size(); at += kMaxStateBytesPerPacket) {
    const std::vector<uint8_t>& chunk = remote_bytes_.at(uint32_t(begin + at));
    std::memcpy(remote.data() + at, chunk.data(), chunk.size());
  }
  return remote;
}

bool FrameZeroCheck::AdoptionReady() const {
  if (!adopts_ || verdict() != FrameZeroVerdict::kAgreeing || !differing_) return false;
  return std::all_of(differing_->begin(), differing_->end(), [this](size_t block) { return BlockBytesKnown(block); });
}

std::optional<FrameZeroAdoption> FrameZeroCheck::Adopt(std::vector<uint8_t>& blob) const {
  if (!AdoptionReady() || blob.size() < view_.size()) return std::nullopt;
  FrameZeroAdoption adoption;
  for (const size_t block : *differing_) {
    const size_t begin = block * kStateBlockBytes;
    const std::vector<uint8_t> remote = RemoteBlock(block);
    const size_t before = adoption.bytes;
    for (size_t i = 0; i < remote.size(); ++i) {
      if (remote[i] == view_[begin + i]) continue;
      blob[begin + i] = remote[i];
      ++adoption.bytes;
    }
    if (adoption.bytes != before) ++adoption.blocks;
  }
  return adoption;
}

void FrameZeroCheck::FindDifferingBlocks() {
  differing_.emplace();
  for (size_t block = 0; block < local_blocks_.size(); ++block) {
    if (local_blocks_[block] != remote_blocks_[block]) differing_->push_back(block);
  }
  reported_.assign(differing_->size(), false);
  std::string list;
  for (size_t i = 0; i < differing_->size() && i < kMaxListedBlocks; ++i) {
    list += list.empty() ? "" : ",";
    list += DescribeOffset((*differing_)[i] * kStateBlockBytes);
  }
  if (differing_->size() > kMaxListedBlocks) list += ",..";
  char line[96];
  std::snprintf(line, sizeof line, "netplay frame0 blocks block_bytes=%zu differing=%zu", kStateBlockBytes,
                differing_->size());
  report_.push_back(std::string(line) + " list=" + list);
}

void FrameZeroCheck::ReportReadyBlocks() {
  if (!differing_) return;
  for (size_t i = 0; i < differing_->size() && i < kMaxReportedBlocks; ++i) {
    if (reported_[i] || !BlockBytesKnown((*differing_)[i])) continue;
    reported_[i] = true;
    ReportBlock((*differing_)[i]);
  }
}

void FrameZeroCheck::ReportBlock(size_t block) {
  const size_t begin = block * kStateBlockBytes;
  const std::vector<uint8_t> remote = RemoteBlock(block);
  const std::vector<uint8_t> local(view_.begin() + begin, view_.begin() + begin + remote.size());
  const DiffRuns runs = FindRuns(local, remote, 4, kMaxRunsPerBlock);
  for (const DiffRun& run : runs.runs) {
    const size_t at = begin + run.offset;
    report_.push_back("netplay frame0 diff at=" + DescribeStateOffset(at, globals_address_) +
                      " size=" + std::to_string(run.length) + " local=" + Hex(local.data() + run.offset, run.length) +
                      " remote=" + Hex(remote.data() + run.offset, run.length));
  }
  if (runs.total > runs.runs.size()) {
    report_.push_back("netplay frame0 diff block=" + DescribeOffset(begin) +
                      " more_runs=" + std::to_string(runs.total - runs.runs.size()));
  }
}

// While agreeing the host sends every differing block for the joiner to take; otherwise the first few, for the report.
std::vector<Body> FrameZeroCheck::Due(uint64_t now_ms) {
  std::vector<Body> out;
  if (!last_digest_ms_ || now_ms - *last_digest_ms_ >= kDigestIntervalMs) {
    last_digest_ms_ = now_ms;
    out.push_back(digest_);
  }
  const FrameZeroVerdict now = verdict();
  if ((now != FrameZeroVerdict::kAgreeing && now != FrameZeroVerdict::kMismatch) || !SetupMatches()) return out;
  if (last_detail_ms_ && now_ms - *last_detail_ms_ < kDetailIntervalMs) return out;
  last_detail_ms_ = now_ms;
  for (size_t first = 0; first < local_blocks_.size(); first += kMaxBlockHashesPerPacket) {
    StateBlocks blocks;
    blocks.fight = fight_;
    blocks.round = digest_.round;
    blocks.first = uint16_t(first);
    const size_t count = std::min(kMaxBlockHashesPerPacket, local_blocks_.size() - first);
    blocks.hashes.assign(local_blocks_.begin() + first, local_blocks_.begin() + first + count);
    out.push_back(std::move(blocks));
  }
  if (!differing_) return out;
  const size_t limit = now == FrameZeroVerdict::kAgreeing && !adopts_ ? kMaxAdoptedBlocks : kMaxReportedBlocks;
  for (size_t i = 0; i < differing_->size() && i < limit; ++i) {
    const size_t block = (*differing_)[i];
    const size_t begin = block * kStateBlockBytes;
    for (size_t at = begin; at < begin + BlockSize(block); at += kMaxStateBytesPerPacket) {
      StateBytes bytes;
      bytes.fight = fight_;
      bytes.round = digest_.round;
      bytes.offset = uint32_t(at);
      const size_t count = std::min(kMaxStateBytesPerPacket, begin + BlockSize(block) - at);
      bytes.bytes.assign(view_.begin() + at, view_.begin() + at + count);
      out.push_back(std::move(bytes));
    }
  }
  return out;
}

std::vector<std::string> FrameZeroCheck::TakeReport() {
  std::vector<std::string> out;
  out.swap(report_);
  return out;
}

}  // namespace ssf2t::netplay
