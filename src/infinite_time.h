#pragma once

#include <cstdint>
#include <filesystem>

#include "guest_memory.h"

namespace ssf2t {

inline constexpr char kInfiniteTimeSetting[] = "ssf2t_infinite_time";
inline constexpr bool kDefaultInfiniteTime = false;
inline constexpr char kInfiniteTimeLabel[] = "Infinite Time";
inline constexpr char kInfiniteTimeHelp[] = "Prevents the fight timer from decreasing. F10 toggles it while playing.";
inline constexpr double kInfiniteTimeToggleNoticeSeconds = 2.0;
inline constexpr double kInfiniteTimeStartupNoticeSeconds = 5.0;

// The round clock tick. The displayed digits advance inside it, so skipping the whole call freezes them.
inline constexpr uint32_t kTimerTick = 0x8224EDA0;
inline constexpr uint32_t kTimerTickWords[] = {
    0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x39400000, 0x914B57B8, 0x3D6083CD, 0x816B57F4,
    0xA16B0580, 0x7D6B0734, 0x3D4083CD, 0x916A57C0, 0x3D6083CD, 0x816B57C0, 0x2F0B0000, 0x409A000C,
};

// `base` is the guest memory. The words cover the prologue and the read of the round clock.
inline bool TimerTickMatches(const uint8_t* base) {
  uint32_t offset = 0;
  for (const uint32_t word : kTimerTickWords) {
    if (LoadBE32(base, kTimerTick + offset) != word) {
      return false;
    }
    offset += 4;
  }
  return true;
}

// The stock tick runs unless the setting is on and both fingerprints match.
constexpr bool SkipTimerTick(bool enabled, bool build_supported, bool code_matches) {
  return enabled && build_supported && code_matches;
}

constexpr bool ToggledInfiniteTime(bool enabled) {
  return !enabled;
}

// A held key reports repeats, which must not toggle again.
constexpr bool IsFreshKeyPress(bool repeat) {
  return !repeat;
}

constexpr const char* InfiniteTimeNoticeText(bool enabled) {
  return enabled ? "Infinite Time: On" : "Infinite Time: Off";
}

// A startup notice waits for the FPS counter when the counter is shown, so both appear on the same frame.
constexpr bool StartupNoticeReady(bool frame_counter_shown, bool frame_counter_visible) {
  return !frame_counter_shown || frame_counter_visible;
}

class InfiniteTimeNotice {
 public:
  void Show(bool enabled, double seconds, double now) {
    Begin(enabled, seconds);
    shown_at_ = now;
    state_ = State::kShown;
  }

  // The clock starts at the first frame where the notice is ready.
  void Queue(bool enabled, double seconds) {
    Begin(enabled, seconds);
    state_ = State::kQueued;
  }

  // Once per launch. Off queues nothing.
  void QueueStartup(bool enabled, double seconds) {
    if (startup_queued_) {
      return;
    }
    startup_queued_ = true;
    if (enabled) {
      Queue(enabled, seconds);
    }
  }

  bool Visible(double now, bool ready = true) {
    if (state_ == State::kQueued && ready) {
      shown_at_ = now;
      state_ = State::kShown;
    }
    return state_ == State::kShown && now >= shown_at_ && now - shown_at_ < seconds_;
  }

  const char* text() const { return text_; }

 private:
  enum class State { kHidden, kQueued, kShown };

  void Begin(bool enabled, double seconds) {
    text_ = InfiniteTimeNoticeText(enabled);
    seconds_ = seconds;
  }

  State state_ = State::kHidden;
  const char* text_ = "";
  double seconds_ = 0.0;
  double shown_at_ = 0.0;
  bool startup_queued_ = false;
};

void SetInfiniteTimeEnabled(bool enabled);
void SetInfiniteTimeXexSupported(bool supported);
// default.xex and its Title Update in game_dir are the supported build: sizes, CRC32s and SHA-256s all match.
bool InfiniteTimeXexSupported(const std::filesystem::path& game_dir);

}  // namespace ssf2t
