#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <ctime>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace ssf2t {

// A Direct3D wait on a word the command processor writes and wakes (runtime patches 0009 and 0049). Swap waits at the
// fence for the flip of the frame before, which holds until the next vblank. Other waits keep the guest's polling.
struct GpuWaitSite {
  const char* name;
  uint32_t poll_return;  // where the site's call to the poll returns
  uint32_t word_offset;  // into the GPU writeback block
};

inline constexpr size_t kGpuWaitSiteCount = 3;
using GpuWaitSites = std::array<GpuWaitSite, kGpuWaitSiteCount>;

inline constexpr GpuWaitSites kBase013GpuWaits = {{
    {"fence", 0x839B18D8, 0},
    {"ring_space", 0x839B0BC4, 60},
    {"ring_progress", 0x839B0B20, 4},
}};
inline constexpr GpuWaitSites kTitleUpdate023GpuWaits = {{
    {"fence", 0x839B2038, 0},
    {"ring_space", 0x839B1324, 60},
    {"ring_progress", 0x839B1280, 4},
}};

inline constexpr uint32_t kGpuWritebackBlockOffset = 10896;  // in the device
inline constexpr uint32_t kGpuWaitStartOffset = 20;          // in the wait, the time base when it began
// Bounds one block for a writer that does not wake the waiter.
inline constexpr uint32_t kGpuWaitBlockMs = 2;
inline constexpr uint32_t kGpuWaitMissedWakeLimit = 8;

// The address the guest puts in its packets. The command processor stores to, and wakes, the physical view of it.
constexpr uint32_t GpuHardwareAddress(uint32_t guest_address) {
  return (guest_address & 0x1FFFFFFF) + (((guest_address >> 20) + 512) & 0x1000);
}

enum class GpuBlockResult { kReturned, kTimedOut, kFailed };

// Returns at once if the word no longer holds `seen`: the kernel compares and sleeps in one step, so a store after
// the caller read `seen` is never slept through. A signal ends the wait like a wake.
inline GpuBlockResult BlockWhileWordHolds(const uint32_t* word, uint32_t seen, uint32_t timeout_ms) {
#if defined(_WIN32)
  if (WaitOnAddress(const_cast<uint32_t*>(word), &seen, sizeof(seen), timeout_ms)) {
    return GpuBlockResult::kReturned;
  }
  return GetLastError() == ERROR_TIMEOUT ? GpuBlockResult::kTimedOut : GpuBlockResult::kFailed;
#else
  const timespec timeout = {time_t(timeout_ms / 1000), long(timeout_ms % 1000) * 1000000L};
  if (syscall(SYS_futex, word, FUTEX_WAIT_PRIVATE, seen, &timeout, nullptr, 0) == 0) {
    return GpuBlockResult::kReturned;
  }
  switch (errno) {
    case EAGAIN:
    case EINTR:
      return GpuBlockResult::kReturned;
    case ETIMEDOUT:
      return GpuBlockResult::kTimedOut;
    default:
      return GpuBlockResult::kFailed;
  }
#endif
}

// One guest wait on one thread. The first poll only reads the word, so a block always follows a guest check of it.
class GpuWordWait {
 public:
  bool IsWait(uint32_t object, uint32_t start) const { return armed_ && object_ == object && start_ == start; }

  void Begin(uint32_t object, uint32_t start, uint32_t word) {
    object_ = object;
    start_ = start;
    seen_ = word;
    armed_ = true;
  }

  void End() { armed_ = false; }

  uint32_t seen() const { return seen_; }

  // False when the word changed since the guest last checked it, which the guest must see before any block.
  bool ShouldBlock(uint32_t word) {
    if (word == seen_) {
      return true;
    }
    seen_ = word;
    return false;
  }

 private:
  uint32_t object_ = 0;
  uint32_t start_ = 0;
  uint32_t seen_ = 0;
  bool armed_ = false;
};

// Falls back to the guest's polling for good if the word proves not to be the one the command processor wakes.
class GpuWaitHealth {
 public:
  bool blocking() const { return blocking_.load(std::memory_order_relaxed); }

  // Returns why blocking just ended, or nullptr if it goes on.
  const char* Record(GpuBlockResult result, bool word_changed, bool views_agree) {
    if (result == GpuBlockResult::kFailed) {
      return Disable("the wait failed");
    }
    if (!views_agree) {
      return Disable("the guest reads another address");
    }
    if (result == GpuBlockResult::kTimedOut) {
      if (word_changed && missed_wakes_.fetch_add(1, std::memory_order_relaxed) + 1 >= kGpuWaitMissedWakeLimit) {
        return Disable("its writer does not wake it");
      }
    } else if (word_changed) {
      missed_wakes_.store(0, std::memory_order_relaxed);
    }
    return nullptr;
  }

 private:
  const char* Disable(const char* reason) {
    return blocking_.exchange(false, std::memory_order_relaxed) ? reason : nullptr;
  }

  std::atomic<bool> blocking_{true};
  std::atomic<uint32_t> missed_wakes_{0};
};

}  // namespace ssf2t
