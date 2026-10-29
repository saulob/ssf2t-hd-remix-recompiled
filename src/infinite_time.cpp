#include "infinite_time.h"

#include <atomic>
#include <string>

#include <rex/hook.h>

#include "game_files.h"
#include "title_update_image.h"
#include "updater/sha256.h"

namespace {

std::atomic<bool> g_enabled{ssf2t::kDefaultInfiniteTime};
std::atomic<bool> g_xex_supported{false};

template <typename G>
bool TimerCodeMatches(const uint8_t* base) {
  static const bool matches = ssf2t::TimerTickMatches<G>(base);
  return matches;
}

template <typename G>
void TimerTick(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  const bool enabled = g_enabled.load(std::memory_order_relaxed);
  const bool supported = g_xex_supported.load(std::memory_order_relaxed);
  if (ssf2t::SkipTimerTick(enabled, supported, TimerCodeMatches<G>(base))) {
    return;
  }
  original(ctx, base);
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
  const GameCheck check = CheckGameFiles(game_dir);
  if (check.state != GameFiles::kReady) {
    return false;
  }
  std::string error;
  if (trg::update::Sha256FileHex(game_dir / "default.xex", error) != kXexSha256) {
    return false;
  }
  return check.guest == GuestVersion::kBase013 ||
         trg::update::Sha256FileHex(game_dir / kTitleUpdateFile, error) == kXexpSha256;
}

}  // namespace ssf2t

REX_EXTERN(__imp__base013_sub_8224ED58);
REX_HOOK_RAW(base013_sub_8224ED58) { TimerTick<ssf2t::Base013Timer>(ctx, base, __imp__base013_sub_8224ED58); }

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_8224EDA0);
REX_HOOK_RAW(tu023_sub_8224EDA0) { TimerTick<ssf2t::TitleUpdate023Timer>(ctx, base, __imp__tu023_sub_8224EDA0); }
#endif
