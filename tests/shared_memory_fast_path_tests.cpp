// Shared memory lock-free valid-page fast path
// (patches/rexglue-sdk/0043-shared-memory-fast-path.patch): a request whose pages are all valid and
// allocated returns before taking global_critical_region_. The page test, the invalidation that
// clears a valid bit and the publication ordering are mirrored here; the request itself runs only
// with a GPU, so the patch text is inspected for the wiring.
// Run: ssf2t_shared_memory_fast_path_tests

#include <atomic>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

std::string ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

// The geometry used here (the real one is a 512 MB buffer, 4 KB pages and 16-bit blocks of 64
// pages).
constexpr uint32_t kPageLog2 = 12;
constexpr uint32_t kBufferLog2 = 22;
constexpr uint32_t kBufferSize = 1u << kBufferLog2;
constexpr uint32_t kPageCount = kBufferSize >> kPageLog2;
constexpr uint32_t kBlockCount = kPageCount / 64;
constexpr uint32_t kSparseGranularityLog2 = 20;
constexpr uint32_t kAllocationCount = kBufferSize >> kSparseGranularityLog2;

bool AnyUnset(const std::vector<uint64_t>& bits, uint32_t first, uint32_t count) {
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t bit = first + i;
    if (!((bits[bit >> 6] >> (bit & 63)) & 1)) {
      return true;
    }
  }
  return false;
}

void SetBits(std::vector<uint64_t>& bits, uint32_t first, uint32_t count, bool value) {
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t bit = first + i;
    const uint64_t mask = uint64_t(1) << (bit & 63);
    if (value) {
      bits[bit >> 6] |= mask;
    } else {
      bits[bit >> 6] &= ~mask;
    }
  }
}

// Mirrors SharedMemory::AreRangesValidLockFree.
bool AreRangesValidLockFree(const std::vector<uint64_t>& valid,
                            const std::vector<uint64_t>& allocated, bool sparse,
                            const std::pair<uint32_t, uint32_t>* ranges, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    const uint32_t start = ranges[i].first;
    const uint32_t length = ranges[i].second;
    if (!length) {
      continue;
    }
    if (start > kBufferSize || (kBufferSize - start) < length) {
      return false;
    }
    const uint32_t last = start + length - 1;
    if (sparse) {
      const uint32_t allocation_first = start >> kSparseGranularityLog2;
      const uint32_t allocation_last = last >> kSparseGranularityLog2;
      if (AnyUnset(allocated, allocation_first, allocation_last - allocation_first + 1)) {
        return false;
      }
    }
    const uint32_t page_first = start >> kPageLog2;
    const uint32_t page_last = last >> kPageLog2;
    const uint32_t block_first = page_first >> 6;
    const uint32_t block_last = page_last >> 6;
    for (uint32_t block = block_first; block <= block_last; ++block) {
      uint64_t needed = UINT64_MAX;
      if (block == block_first) {
        needed &= ~((uint64_t(1) << (page_first & 63)) - 1);
      }
      if (block == block_last && (page_last & 63) != 63) {
        needed &= (uint64_t(1) << ((page_last & 63) + 1)) - 1;
      }
      if ((valid[block] & needed) != needed) {
        return false;
      }
    }
  }
  return true;
}

std::vector<uint64_t> AllValidPages() { return std::vector<uint64_t>(kBlockCount, UINT64_MAX); }
std::vector<uint64_t> AllAllocated() {
  return std::vector<uint64_t>((kAllocationCount + 63) / 64, UINT64_MAX);
}

bool Request(const std::vector<uint64_t>& valid, const std::vector<uint64_t>& allocated,
             bool sparse, std::initializer_list<std::pair<uint32_t, uint32_t>> ranges) {
  std::vector<std::pair<uint32_t, uint32_t>> list(ranges);
  return AreRangesValidLockFree(valid, allocated, sparse, list.data(), list.size());
}

void TestAllPagesValid() {
  std::printf("all pages valid and allocated takes the fast path\n");
  const std::vector<uint64_t> valid = AllValidPages();
  // Empty ranges are skipped and an empty request is valid.
  CHECK(Request(valid, {}, false, {}));
  CHECK(Request(valid, {}, false, {{0, 0}, {1000, 0}}));
  // A single page, a whole non-adjacent page, and a range covering whole blocks.
  CHECK(Request(valid, {}, false, {{0, 1}}));
  CHECK(Request(valid, {}, false, {{kBufferSize - 1, 1}}));
  CHECK(Request(valid, {}, false, {{0, kBufferSize}}));
  CHECK(Request(valid, {}, false, {{4096, 40000}}));
}

void TestInvalidPageFallsBack() {
  std::printf("one invalid page falls back\n");
  std::vector<uint64_t> valid = AllValidPages();
  const uint32_t first = 4096 >> kPageLog2;
  CHECK(Request(valid, {}, false, {{4096, 40000}}));
  SetBits(valid, first + 4, 1, false);
  CHECK(!Request(valid, {}, false, {{4096, 40000}}));
  // An invalid page outside the requested pages does not matter.
  SetBits(valid, first + 4, 1, true);
  SetBits(valid, first + 30, 1, false);
  CHECK(Request(valid, {}, false, {{4096, 4000}}));
}

void TestUnallocatedPageFallsBack() {
  std::printf("one unallocated page falls back\n");
  const std::vector<uint64_t> valid = AllValidPages();
  CHECK(Request(valid, AllAllocated(), true, {{4096, 40000}}));
  // The allocation covering the range is dropped.
  std::vector<uint64_t> allocated = AllAllocated();
  SetBits(allocated, (4096 >> kSparseGranularityLog2), 1, false);
  CHECK(!Request(valid, allocated, true, {{4096, 40000}}));
  // Another allocation being absent does not matter.
  allocated = AllAllocated();
  SetBits(allocated, (4096 >> kSparseGranularityLog2) + 2, 1, false);
  CHECK(Request(valid, allocated, true, {{4096, 40000}}));
  // The same valid pages are used without a sparse allocation when there is none.
  CHECK(Request(valid, {}, false, {{4096, 40000}}));
}

void TestInvalidation() {
  std::printf("invalidation makes the next request take the normal path\n");
  std::vector<uint64_t> valid = AllValidPages();
  CHECK(Request(valid, {}, false, {{8192, 20000}}));
  // The invalidation callback clears the written pages, so the same request now takes the lock.
  SetBits(valid, 8192 >> kPageLog2, 1, false);
  CHECK(!Request(valid, {}, false, {{8192, 20000}}));
  // Re-uploaded (valid again) the fast path is taken again.
  SetBits(valid, 8192 >> kPageLog2, 1, true);
  CHECK(Request(valid, {}, false, {{8192, 20000}}));
}

void TestPartialBlocks() {
  std::printf("pages outside the range in the same blocks do not matter\n");
  const std::vector<uint64_t> zero_blocks(kBlockCount, 0);
  const uint32_t start = (3 << 6) + 20;
  const uint32_t end = (5 << 6) + 40;
  // Only the requested pages are considered: everything else is invalid.
  std::vector<uint64_t> partial = zero_blocks;
  SetBits(partial, start, end - start + 1, true);
  CHECK(Request(partial, {}, false, {{start << kPageLog2, (end - start + 1) << kPageLog2}}));
  // One requested page within the range is invalid.
  SetBits(partial, (4 << 6) + 1, 1, false);
  CHECK(!Request(partial, {}, false, {{start << kPageLog2, (end - start + 1) << kPageLog2}}));
}

void TestSpanningGroups() {
  std::printf("a range spanning multiple groups behaves correctly\n");
  std::vector<uint64_t> valid = AllValidPages();
  const uint32_t start = (2 << 6) + 5;
  const uint32_t end = (7 << 6) + 9;
  const uint32_t length = (end - start + 1) << kPageLog2;
  CHECK(Request(valid, {}, false, {{start << kPageLog2, length}}));
  // A page in a middle block is invalid.
  SetBits(valid, (4 << 6) + 33, 1, false);
  CHECK(!Request(valid, {}, false, {{start << kPageLog2, length}}));
  // A range ending exactly at the end of the buffer, and two separate ranges at once.
  SetBits(valid, (4 << 6) + 33, 1, true);
  CHECK(Request(valid, {}, false, {{kBufferSize - 16384, 16384}}));
  CHECK(Request(valid, {}, false, {{0, 4096}, {kBufferSize - 4096, 4096}}));
  // Out of bounds.
  CHECK(!Request(valid, {}, false, {{kBufferSize, 1}}));
  CHECK(!Request(valid, {}, false, {{kBufferSize - 1, 2}}));
}

void TestPublicationOrdering() {
  std::printf("validity publication ordering\n");
  // The invalidation callback publishes a cleared bit with a release store; the fast path reads it
  // with an acquire load. This mirrors the clear/read pair and checks that a reader never sees a
  // page become valid again once a clear has been observed.
  std::atomic<uint64_t> valid{UINT64_MAX};
  std::atomic<bool> done{false};
  std::atomic<int> resurrected{0};
  std::thread writer([&] {
    for (uint32_t bit = 0; bit < 64; ++bit) {
      valid.fetch_and(~(uint64_t(1) << bit), std::memory_order_release);
      std::this_thread::yield();
    }
    done.store(true, std::memory_order_release);
  });
  std::vector<std::thread> readers;
  for (int r = 0; r < 4; ++r) {
    readers.emplace_back([&] {
      uint64_t saw_invalid = 0;
      while (!done.load(std::memory_order_acquire)) {
        const uint64_t page = valid.load(std::memory_order_acquire);
        const uint64_t newly_invalid = saw_invalid & page;
        if (newly_invalid) {
          resurrected.fetch_add(1, std::memory_order_relaxed);
        }
        saw_invalid |= ~page;
        std::this_thread::yield();
      }
    });
  }
  writer.join();
  for (std::thread& reader : readers) {
    reader.join();
  }
  CHECK(resurrected.load() == 0);
}

void TestPatchContent() {
  std::printf("patch fast path and wiring\n");
  const std::string patch =
      ReadFile(std::string(SSF2T_SOURCE_DIR) +
               "/patches/rexglue-sdk/0043-shared-memory-fast-path.patch");
  CHECK(!patch.empty());

  // The helper, its declaration and the request-side guard.
  CHECK(Has(patch, "bool SharedMemory::AreRangesValidLockFree("));
  CHECK(Has(patch, "bool AreRangesValidLockFree(const std::pair<uint32_t, uint32_t>* ranges, "
                   "size_t count) const;"));
  CHECK(Has(patch, "if (AreRangesValidLockFree(ranges, count)) {"));
  // The allocation test guards against a full-buffer RangeWrittenByGpu.
  CHECK(Has(patch, "host_gpu_memory_sparse_granularity_log2_ != UINT32_MAX"));
  CHECK(Has(patch, "host_gpu_memory_sparse_allocated_.data()"));
  // The fast path returns true (nothing to upload) without taking the lock.
  CHECK(Has(patch, "if (AreRangesValidLockFree(ranges, count)) {"));
  CHECK(!Has(patch, "global_critical_region_.Acquire()"));

  // The acquire load of the valid bits and the release store that publishes a clear.
  CHECK(Has(patch, "std::atomic_ref<uint64_t>(const_cast<uint64_t&>(system_page_flags_valid_"
                   "[block]))"));
  CHECK(Has(patch, ".load(std::memory_order_acquire);"));
  CHECK(Has(patch, "valid.store(valid.load(std::memory_order_relaxed) & ~invalidate_bits,"));
  CHECK(Has(patch, "std::memory_order_release);"));
  // The release store publishes the clear in the invalidation callback.
  const size_t clear =
      patch.find("std::pair<uint32_t, uint32_t> SharedMemory::MemoryInvalidationCallback(");
  const size_t release = patch.find("std::memory_order_release);");
  CHECK(clear != std::string::npos);
  CHECK(release != std::string::npos);
  CHECK(clear < release);

  // The existing persistent request vector is kept.
  CHECK(Has(patch, "std::vector<std::pair<uint32_t, uint32_t>>& merged_ranges = "
                   "request_ranges_merged_;"));

  // The smaller invalidation granularity of Crackdown 0078 is deferred, so it is not here.
  CHECK(!Has(patch, "gpu_invalidation_expand_pages"));
  CHECK(!Has(patch, "bit_floor"));
}

}  // namespace

int main() {
  TestAllPagesValid();
  TestInvalidPageFallsBack();
  TestUnallocatedPageFallsBack();
  TestInvalidation();
  TestPartialBlocks();
  TestSpanningGroups();
  TestPublicationOrdering();
  TestPatchContent();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
