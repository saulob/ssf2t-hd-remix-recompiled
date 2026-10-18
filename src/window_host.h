#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>

inline constexpr char kWindowTitle[] = "Super Street Fighter II Turbo HD Remix";

// The latest one-second sample of the game's swaps; all zero until the first one is measured.
struct GuestFrameStats {
  double fps = 0;
  double frame_time_ms = 0;
  uint64_t frame_count = 0;
};

namespace rex::ui {
class ImGuiDrawer;
class Window;
class WindowedAppContext;
}  // namespace rex::ui

inline constexpr std::chrono::milliseconds kCursorHideDelay{5000};

// Hides the cursor in fullscreen gameplay with focus after kCursorHideDelay without mouse motion.
class CursorHider {
 public:
  using Clock = std::chrono::steady_clock;

  bool visible() const { return visible_; }

  void MouseMoved(Clock::time_point now) { last_motion_ = now; }

  // Returns the new visibility when it changes, otherwise nullopt.
  std::optional<bool> Update(bool gameplay, bool fullscreen, bool focused, Clock::time_point now) {
    const bool active = gameplay && fullscreen && focused;
    if (!active || !active_) {
      last_motion_ = now;
    }
    active_ = active;
    const bool visible = !active || now - last_motion_ < kCursorHideDelay;
    if (visible == visible_) {
      return std::nullopt;
    }
    visible_ = visible;
    return visible;
  }

 private:
  Clock::time_point last_motion_{};
  bool active_ = false;
  bool visible_ = true;
};

// The cursor auto-hides only while `gameplay_active` returns true, i.e. no mouse-driven overlay is open.
void AttachWindowHost(rex::ui::Window* window, rex::ui::WindowedAppContext& app_context,
                      std::function<bool()> gameplay_active);
void DetachWindowHost();

// Checks the cursor's idle time on every UI frame.
void CreateCursorTick(rex::ui::ImGuiDrawer* drawer);
GuestFrameStats GetGuestFrameStats();
