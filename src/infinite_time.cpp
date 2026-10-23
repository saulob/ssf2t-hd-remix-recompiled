#include "infinite_time.h"

#include <atomic>
#include <string>

#include <rex/hook.h>

#include "game_files.h"
#include "updater/sha256.h"

namespace {

std::atomic<bool> g_enabled{ssf2t::kDefaultInfiniteTime};
std::atomic<bool> g_xex_supported{false};

bool TimerCodeMatches(const uint8_t* base) {
  static const bool matches = ssf2t::TimerTickMatches(base);
  return matches;
}

}  // namespace

namespace ssf2t {

void SetInfiniteTimeEnabled(bool enabled) {
  g_enabled.store(enabled, std::memory_order_relaxed);
}

void SetInfiniteTimeXexSupported(bool supported) {
  g_xex_supported.store(supported, std::memory_order_relaxed);
}

bool InfiniteTimeXexSupported(const std::filesystem::path& game_dir) {
  if (CheckGameFiles(game_dir).state != GameFiles::kReady) {
    return false;
  }
  std::string error;
  return trg::update::Sha256FileHex(game_dir / "default.xex", error) == kXexSha256;
}

}  // namespace ssf2t

REX_EXTERN(__imp__sub_8224ED58);
REX_HOOK_RAW(sub_8224ED58) {
  const bool enabled = g_enabled.load(std::memory_order_relaxed);
  const bool supported = g_xex_supported.load(std::memory_order_relaxed);
  if (ssf2t::SkipTimerTick(enabled, supported, TimerCodeMatches(base))) {
    return;
  }
  __imp__sub_8224ED58(ctx, base);
}
