#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace ssf2t {

constexpr float kOverlayViewportFraction = 0.7f;
constexpr float kOverlayViewportMargin = 16.0f;
constexpr float kStickDeadzone = 0.25f;
constexpr double kReleaseTimeoutSeconds = 1.0;

// Window size along one axis: about 70% of the viewport, clamped to [min_size, max_size] and kept inside it.
inline float FitWindowExtent(float viewport, float min_size, float max_size) {
  const float wanted = std::clamp(viewport * kOverlayViewportFraction, min_size, max_size);
  return std::max(0.0f, std::min(wanted, viewport - 2.0f * kOverlayViewportMargin));
}

struct AchievementSummary {
  int unlocked = 0;
  int total = 0;
  int earned_gamerscore = 0;
  int total_gamerscore = 0;
};

// Totals over the achievements the store lists; `unlocked_flags[i]` says whether entry i is earned.
inline AchievementSummary Summarize(const std::vector<uint32_t>& gamerscores, const std::vector<bool>& unlocked_flags) {
  AchievementSummary summary;
  summary.total = static_cast<int>(gamerscores.size());
  for (size_t i = 0; i < gamerscores.size(); ++i) {
    summary.total_gamerscore += static_cast<int>(gamerscores[i]);
    if (i < unlocked_flags.size() && unlocked_flags[i]) {
      ++summary.unlocked;
      summary.earned_gamerscore += static_cast<int>(gamerscores[i]);
    }
  }
  return summary;
}

// Direction the list scrolls from the D-pad and the left stick: -1 up .. 1 down. The D-pad wins over the stick.
inline float ScrollDirection(bool dpad_up, bool dpad_down, float stick_y) {
  if (dpad_up != dpad_down) return dpad_up ? -1.0f : 1.0f;
  const float magnitude = stick_y < 0.0f ? -stick_y : stick_y;
  if (magnitude <= kStickDeadzone) return 0.0f;
  const float amount = std::min(1.0f, (magnitude - kStickDeadzone) / (1.0f - kStickDeadzone));
  return stick_y > 0.0f ? -amount : amount;
}

// Whether the modal may release the game's input: nothing it received is still held, or the wait ran out. A button
// that is never reported released (a controller unplugged as the dialog closes) cannot block the game forever.
inline bool InputReleased(bool anything_held, double waited_seconds, double timeout_seconds) {
  return !anything_held || waited_seconds >= timeout_seconds;
}

// Closing a modal: asking to close (B, Escape, F7) starts a wait, and the game gets its input back only after what
// was pressed in the dialog is released, so the closing press never also reaches the game's menu.
class CloseGate {
 public:
  void Request() { requested_ = true; }
  bool requested() const { return requested_; }

  // Once per frame. True when the dialog may go and the game may read its input again.
  bool Step(bool anything_held, double now) {
    if (!requested_) return false;
    if (since_ < 0.0) since_ = now;
    return InputReleased(anything_held, now - since_, kReleaseTimeoutSeconds);
  }

 private:
  bool requested_ = false;
  double since_ = -1.0;
};

// Whether the game may read the keyboard and controller. It may not while a modal overlay is open, which includes
// the wait for its closing press to be released; a pointer over any other overlay window keeps the click from the game.
constexpr bool GameInputActive(bool modal_open, bool overlay_windows, bool pointer_captured) {
  return !modal_open && !(overlay_windows && pointer_captured);
}

// F7 opens the overlay when it is closed and asks it to close when it is open. Nothing opens over another modal.
enum class OverlayToggle { kOpen, kClose, kNone };

constexpr OverlayToggle ToggleOverlay(bool overlay_open, bool other_modal_open) {
  if (overlay_open) return OverlayToggle::kClose;
  return other_modal_open ? OverlayToggle::kNone : OverlayToggle::kOpen;
}

}  // namespace ssf2t
