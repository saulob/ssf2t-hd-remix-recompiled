// Runtime patches 0049 to 0052 (patches/rexglue-sdk): progress wakes after the read pointer and scratch writes,
// bulk register writes everywhere except registers with write side effects, the signed scissor compare and the
// Windows host thread affinity. The register range rule is mirrored here; the patch text is checked for the wiring.
// Run: ssf2t_gpu_register_wake_tests

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

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

std::string ReadPatch(const char* name) {
  std::ifstream in(std::string(SSF2T_SOURCE_DIR) + "/patches/rexglue-sdk/" + name, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

struct Span {
  uint32_t first;
  uint32_t last;
};

constexpr uint32_t kRegisterCount = 0x5003;
constexpr Span kShaderConstants{0x4000, 0x4927};
constexpr Span kScratch{0x0578, 0x057F};
constexpr Span kCoherStatus{0x0A31, 0x0A31};
constexpr Span kLut{0x1922, 0x1925};

bool Overlaps(uint32_t first, uint32_t last, Span span) {
  return first <= span.last && last >= span.first;
}

bool SideEffectFree(uint32_t first, uint32_t last) {
  return first <= last && last < kRegisterCount && !Overlaps(first, last, kShaderConstants) &&
         !Overlaps(first, last, kScratch) && !Overlaps(first, last, kCoherStatus) && !Overlaps(first, last, kLut);
}

void TestRegisterRanges() {
  CHECK(SideEffectFree(0x2000, 0x2010));
  CHECK(SideEffectFree(0x0100, 0x0200));
  CHECK(!SideEffectFree(0x0570, 0x0580));
  CHECK(!SideEffectFree(kScratch.first, kScratch.first));
  CHECK(!SideEffectFree(0x0A00, 0x0A31));
  CHECK(SideEffectFree(0x0A00, 0x0A30));
  CHECK(!SideEffectFree(0x1925, 0x1930));
  CHECK(!SideEffectFree(0x3FFF, 0x4000));
  CHECK(!SideEffectFree(0x4927, 0x4930));
  CHECK(SideEffectFree(0x4928, 0x4930));
  CHECK(!SideEffectFree(5, 4));
  CHECK(!SideEffectFree(kRegisterCount - 1, kRegisterCount));
}

void TestPatchContent() {
  const std::string wake = ReadPatch("0049-command-processor-progress-wake.patch");
  CHECK(Has(wake, "+      WakeGuestWordWaiters(memory_->TranslatePhysical(read_ptr_writeback_ptr_));"));
  CHECK(Has(wake, "+      WakeGuestWordWaiters(memory_->TranslatePhysical(mem_addr));"));

  const std::string ranges = ReadPatch("0050-gpu-side-effect-free-register-ranges.patch");
  CHECK(Has(ranges, "bool IsSideEffectFreeRegisterRange(uint32_t first_index, uint32_t last_index)"));
  CHECK(Has(ranges, "!overlaps(XE_GPU_REG_SCRATCH_REG0, XE_GPU_REG_SCRATCH_REG7) &&"));
  CHECK(Has(ranges, "!overlaps(XE_GPU_REG_COHER_STATUS_HOST, XE_GPU_REG_COHER_STATUS_HOST) &&"));
  CHECK(Has(ranges, "!overlaps(XE_GPU_REG_DC_LUT_RW_INDEX, XE_GPU_REG_DC_LUT_30_COLOR);"));
  CHECK(Has(ranges, "-bool IsContextRegisterRange("));

  const std::string scissor = ReadPatch("0051-draw-extent-scissor-signed-compare.patch");
  CHECK(Has(scissor, "+        if (scissor_bottom >= int32_t(xenos::kTexture2DCubeMaxWidthHeight)) {"));
  CHECK(Has(scissor, "+          if (scissor_right >= int32_t(xenos::kTexture2DCubeMaxWidthHeight)) {"));

  const std::string affinity = ReadPatch("0052-d3d12-host-threads-avoid-cpu0.windows.patch");
  CHECK(Has(affinity, "REXCVAR_DEFINE_BOOL(gpu_host_threads_avoid_cpu0, true"));
  CHECK(Has(affinity, "std::popcount(uint64_t(process_mask)) < 8"));
  CHECK(Has(affinity, "return uint64_t(process_mask) & ~uint64_t(0b11);"));
  CHECK(Has(affinity, "async_submission_thread_->set_affinity_mask(host_thread_affinity_mask);"));
}

}  // namespace

int main() {
  TestRegisterRanges();
  TestPatchContent();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
