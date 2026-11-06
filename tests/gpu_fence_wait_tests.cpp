// The Direct3D GPU waits: the sites and word arithmetic, the wait's state, and the host block. The generated code of
// each image is read to prove the sites, because a wrong return address would quietly bring the spin back.
// Run: ssf2t_gpu_fence_wait_tests

#include <atomic>
#include <chrono>
#include <climits>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#include "gpu_fence_wait.h"

namespace {

using namespace ssf2t;
using Clock = std::chrono::steady_clock;

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

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

std::string ReadSource(const char* relative) {
  return ReadFile(std::filesystem::path(SSF2T_SOURCE_DIR) / relative);
}

bool Has(const std::string& text, const std::string& needle) { return text.find(needle) != std::string::npos; }

double SecondsSince(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }

void WakeWord(uint32_t* word) {
#if defined(_WIN32)
  WakeByAddressAll(word);
#else
  syscall(SYS_futex, word, FUTEX_WAKE_PRIVATE, INT_MAX, nullptr, nullptr, 0);
#endif
}

double ProcessCpuSeconds() {
#if defined(_WIN32)
  FILETIME created, exited, kernel, user;
  GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
  const auto seconds = [](const FILETIME& t) {
    return (uint64_t(t.dwHighDateTime) << 32 | t.dwLowDateTime) / 1e7;
  };
  return seconds(kernel) + seconds(user);
#else
  timespec now{};
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now);
  return double(now.tv_sec) + double(now.tv_nsec) / 1e9;
#endif
}

void TestSites() {
  for (size_t i = 0; i < kGpuWaitSiteCount; ++i) {
    CHECK(kTitleUpdate023GpuWaits[i].poll_return - kBase013GpuWaits[i].poll_return == 0x760);
    CHECK(kTitleUpdate023GpuWaits[i].word_offset == kBase013GpuWaits[i].word_offset);
  }
  CHECK(kBase013GpuWaits[0].word_offset == 0);
  CHECK(kBase013GpuWaits[1].word_offset == 60);
  CHECK(kBase013GpuWaits[2].word_offset == 4);
}

// Each site's poll call and the word its caller checks next, in the generated code of one image.
void CheckImage(const char* variant, const char* poll, const GpuWaitSites& sites) {
  std::string code;
  for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(SSF2T_SOURCE_DIR) / "generated" / variant)) {
    if (entry.path().extension() == ".cpp") {
      code += ReadFile(entry.path());
    }
  }
  CHECK(!code.empty());
  for (const GpuWaitSite& site : sites) {
    char label[32];
    std::snprintf(label, sizeof(label), "ctx.lr = 0x%08X;", site.poll_return);
    const size_t at = code.find(label);
    CHECK(at != std::string::npos);
    if (at == std::string::npos) {
      continue;
    }
    CHECK(Has(code.substr(at - 64, 64), std::string("// bl ") + poll));
    const std::string after = code.substr(at, 800);
    CHECK(Has(after, "," + std::to_string(kGpuWritebackBlockOffset) + "(r"));
    CHECK(Has(after, "// lwz r11," + std::to_string(site.word_offset) + "(r11)"));
  }
}

void TestGeneratedCode() {
  CheckImage("base", "0x839a14b8", kBase013GpuWaits);
  CheckImage("tu", "0x839a1c10", kTitleUpdate023GpuWaits);
}

void TestHardwareAddress() {
  CHECK(GpuHardwareAddress(0xA0123000) == 0x00123000);
  CHECK(GpuHardwareAddress(0xC0123000) == 0x00123000);
  CHECK(GpuHardwareAddress(0xE0123000) == 0x00124000);
  CHECK(GpuHardwareAddress(0xE0123004) == 0x00124004);
  CHECK(GpuHardwareAddress(0xFFFFF000) == 0x20000000);
}

void TestWordWait() {
  GpuWordWait wait;
  CHECK(!wait.IsWait(0x7000, 5));
  wait.Begin(0x7000, 5, 10);
  CHECK(wait.IsWait(0x7000, 5));
  CHECK(!wait.IsWait(0x7000, 6));
  CHECK(!wait.IsWait(0x7010, 5));
  CHECK(wait.ShouldBlock(10));
  CHECK(!wait.ShouldBlock(11));
  CHECK(wait.seen() == 11);
  CHECK(wait.ShouldBlock(11));
  wait.End();
  CHECK(!wait.IsWait(0x7000, 5));
}

void TestHealth() {
  GpuWaitHealth timeouts;
  CHECK(timeouts.Record(GpuBlockResult::kTimedOut, false, true) == nullptr);
  CHECK(timeouts.blocking());

  GpuWaitHealth missed;
  for (uint32_t i = 0; i + 1 < kGpuWaitMissedWakeLimit; ++i) {
    CHECK(missed.Record(GpuBlockResult::kTimedOut, true, true) == nullptr);
  }
  CHECK(missed.blocking());
  CHECK(missed.Record(GpuBlockResult::kReturned, true, true) == nullptr);
  for (uint32_t i = 0; i + 1 < kGpuWaitMissedWakeLimit; ++i) {
    CHECK(missed.Record(GpuBlockResult::kTimedOut, true, true) == nullptr);
  }
  CHECK(missed.blocking());
  CHECK(missed.Record(GpuBlockResult::kTimedOut, true, true) != nullptr);
  CHECK(!missed.blocking());
  CHECK(missed.Record(GpuBlockResult::kTimedOut, true, true) == nullptr);

  GpuWaitHealth failed;
  CHECK(failed.Record(GpuBlockResult::kFailed, false, true) != nullptr);
  CHECK(!failed.blocking());

  GpuWaitHealth views;
  CHECK(views.Record(GpuBlockResult::kReturned, false, false) != nullptr);
  CHECK(!views.blocking());
}

void TestBlock() {
  alignas(4) uint32_t word = 1;

  auto start = Clock::now();
  CHECK(BlockWhileWordHolds(&word, 2, 2000) == GpuBlockResult::kReturned);
  CHECK(SecondsSince(start) < 1.0);

  CHECK(BlockWhileWordHolds(&word, 1, 20) == GpuBlockResult::kTimedOut);

  start = Clock::now();
  std::thread writer([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    __atomic_store_n(&word, 7u, __ATOMIC_RELEASE);
    WakeWord(&word);
  });
  CHECK(BlockWhileWordHolds(&word, 1, 5000) == GpuBlockResult::kReturned);
  CHECK(__atomic_load_n(&word, __ATOMIC_ACQUIRE) == 7);
  CHECK(SecondsSince(start) < 2.0);
  writer.join();
}

// The port's wait must sleep through the frame: the spin it replaces burns the whole interval.
void TestBlockIsNotBusy() {
  alignas(4) uint32_t word = 1;
  const double cpu_start = ProcessCpuSeconds();
  const auto start = Clock::now();
  while (SecondsSince(start) < 0.4) {
    BlockWhileWordHolds(&word, 1, kGpuWaitBlockMs);
  }
  CHECK(ProcessCpuSeconds() - cpu_start < 0.2);
}

void TestWiring() {
  const std::string hook = ReadSource("src/gpu_fence_wait.cpp");
  const size_t guard = hook.find("#if SSF2T_TITLE_UPDATE_IMAGE");
  CHECK(hook.find("REX_HOOK_RAW(base013_sub_839A14B8)") < guard);
  CHECK(guard < hook.find("REX_HOOK_RAW(tu023_sub_839A1C10)"));
  CHECK(Has(hook, "kBase013GpuWaits"));
  CHECK(Has(hook, "kTitleUpdate023GpuWaits"));
  CHECK(!Has(hook, "StoreBE"));
  CHECK(!Has(hook, "GuestStore"));

  CHECK(Has(ReadSource("CMakeLists.txt"), "src/gpu_fence_wait.cpp"));
  CHECK(Has(ReadSource("patches/rexglue-sdk/0009-guest-fence-wake.patch"), "ExecutePacketType3_EVENT_WRITE_SHD"));
  CHECK(Has(ReadSource("patches/rexglue-sdk/0049-command-processor-progress-wake.patch"), "read_ptr_writeback_ptr_"));
}

}  // namespace

int main() {
  TestSites();
  TestGeneratedCode();
  TestHardwareAddress();
  TestWordWait();
  TestHealth();
  TestBlock();
  TestBlockIsNotBusy();
  TestWiring();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
