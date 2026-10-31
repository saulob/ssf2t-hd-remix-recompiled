#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "netplay/frame.h"
#include "netplay/packet.h"

namespace ssf2t::netplay {

uint64_t Fnv1a64(const uint8_t* data, size_t size);

struct BlobDiff {
  bool equal = true;
  size_t first_offset = 0;
  size_t differing_bytes = 0;
  uint64_t hash_a = 0;
  uint64_t hash_b = 0;
};

// Compares the first `length` bytes (all of the shorter blob when `length` is 0). Blobs of different size differ.
BlobDiff CompareBlobs(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, size_t length = 0);

// "arena+0x1A2C", "globals+0x40", "input_table+0x8", "rng+0x4" or "host+0x0" for an offset into a blob.
std::string DescribeOffset(size_t offset);

struct DiffRun {
  size_t offset = 0;
  size_t length = 0;
};

struct DiffRuns {
  std::vector<DiffRun> runs;
  size_t total = 0;
};

// Differing byte ranges of two equal-sized buffers, merged when closer than `merge_gap`, at most `max_runs` kept.
DiffRuns FindRuns(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, size_t merge_gap, size_t max_runs);

// "DEBUG_SAULO netplay frame=7 leak runs=2 83C19FC0+4 83E0A2D4+4", addresses from `base_address`.
std::string FormatRuns(Frame frame, const char* label, uint32_t base_address, const DiffRuns& diff);

// arena, globals, table, ring, rng, side, prev: the regions of a simulation blob.
const char* StateRegionName(size_t region);

// One hash per region of a simulation blob; all zero for a blob of the wrong size.
std::array<uint32_t, kStateRegions> StateRegionHashes(const std::vector<uint8_t>& blob);

// "arena=1A2B3C4D globals=... table=... ring=... rng=... side=...": one hash per region of a simulation blob, so two
// peers' logs show which part of their frame-0 state differs.
std::string DescribeStateHashes(const std::vector<uint8_t>& blob);

// One validation result as a log line: "DEBUG_SAULO netplay frame=123 resim=match".
std::string FormatCheck(Frame frame, const char* check, const BlobDiff& diff);

// Picks the fight frames the determinism test runs on: spaced apart, and only a few per session.
class SampleSchedule {
 public:
  SampleSchedule(uint32_t first_frame, uint32_t interval, uint32_t max_samples)
      : first_frame_(first_frame), interval_(interval), max_samples_(max_samples) {}

  bool ShouldSample(Frame frame) const;
  void Taken(Frame frame);
  uint32_t taken() const { return taken_; }

 private:
  uint32_t first_frame_;
  uint32_t interval_;
  uint32_t max_samples_;
  uint32_t taken_ = 0;
  bool any_ = false;
  Frame last_ = 0;
};

}  // namespace ssf2t::netplay
