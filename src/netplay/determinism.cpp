#include "netplay/determinism.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>

#include "netplay/guest_netplay.h"

namespace ssf2t::netplay {

uint64_t Fnv1a64(const uint8_t* data, size_t size) {
  uint64_t hash = 14695981039346656037ull;
  for (size_t i = 0; i < size; ++i) {
    hash ^= data[i];
    hash *= 1099511628211ull;
  }
  return hash;
}

BlobDiff CompareBlobs(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, size_t length) {
  BlobDiff diff;
  const size_t common = std::min(a.size(), b.size());
  const size_t n = length == 0 ? common : std::min(length, common);
  diff.hash_a = Fnv1a64(a.data(), length == 0 ? a.size() : std::min(length, a.size()));
  diff.hash_b = Fnv1a64(b.data(), length == 0 ? b.size() : std::min(length, b.size()));
  bool found = false;
  for (size_t i = 0; i < n; ++i) {
    if (a[i] == b[i]) continue;
    if (!found) {
      diff.first_offset = i;
      found = true;
    }
    ++diff.differing_bytes;
  }
  if (length == 0 && a.size() != b.size()) {
    if (!found) diff.first_offset = n;
    found = true;
    diff.differing_bytes += std::max(a.size(), b.size()) - n;
  }
  diff.equal = !found;
  return diff;
}

std::string DescribeOffset(size_t offset) {
  struct Region {
    const char* name;
    size_t begin;
    size_t end;
  };
  constexpr size_t table_end = kGuestStateSize + kInputTableStateBytes;
  constexpr size_t ring_end = table_end + kInputRingBytes;
  constexpr size_t rng_end = ring_end + kRngStateBytes;
  constexpr Region regions[] = {
      {"arena", 0, kArenaSaveSize},
      {"globals", kArenaSaveSize, kGuestStateSize},
      {"input_table", kGuestStateSize, table_end},
      {"input_ring", table_end, ring_end},
      {"rng", ring_end, rng_end},
      {"side_order", rng_end, kSimStateSize},
  };
  char text[48];
  for (const Region& r : regions) {
    if (offset >= r.begin && offset < r.end) {
      std::snprintf(text, sizeof text, "%s+0x%zX", r.name, offset - r.begin);
      return text;
    }
  }
  std::snprintf(text, sizeof text, "host+0x%zX", offset - kSimStateSize);
  return text;
}

DiffRuns FindRuns(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, size_t merge_gap, size_t max_runs) {
  DiffRuns diff;
  const size_t n = std::min(a.size(), b.size());
  size_t i = 0;
  while (i < n) {
    if (a[i] == b[i]) {
      ++i;
      continue;
    }
    size_t end = i + 1;
    for (size_t j = end; j < n && j < end + merge_gap; ++j) {
      if (a[j] != b[j]) end = j + 1;
    }
    ++diff.total;
    if (diff.runs.size() < max_runs) diff.runs.push_back({i, end - i});
    i = end;
  }
  return diff;
}

std::string FormatRuns(Frame frame, const char* label, uint32_t base_address, const DiffRuns& diff) {
  std::string text = "DEBUG_SAULO netplay frame=" + std::to_string(frame) + " " + label + " runs=" +
                     std::to_string(diff.total);
  char run[40];
  for (const DiffRun& r : diff.runs) {
    std::snprintf(run, sizeof run, " %08X+%zu", static_cast<uint32_t>(base_address + r.offset), r.length);
    text += run;
  }
  if (diff.total > diff.runs.size()) text += " ...";
  return text;
}

std::string FormatCheck(Frame frame, const char* check, const BlobDiff& diff) {
  char text[200];
  if (diff.equal) {
    std::snprintf(text, sizeof text, "DEBUG_SAULO netplay frame=%u %s=%s", frame, check, "match");
  } else {
    std::snprintf(text, sizeof text,
                  "DEBUG_SAULO netplay frame=%u %s=MISMATCH first=%s bytes=%zu hashA=%016" PRIx64 " hashB=%016" PRIx64,
                  frame, check, DescribeOffset(diff.first_offset).c_str(), diff.differing_bytes, diff.hash_a,
                  diff.hash_b);
  }
  return text;
}

bool SampleSchedule::ShouldSample(Frame frame) const {
  if (taken_ >= max_samples_ || FrameDiff(frame, first_frame_) < 0) return false;
  return !any_ || FrameDiff(frame, last_) >= static_cast<int32_t>(interval_);
}

void SampleSchedule::Taken(Frame frame) {
  ++taken_;
  any_ = true;
  last_ = frame;
}

}  // namespace ssf2t::netplay
