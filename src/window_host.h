#pragma once

#include <cstdint>
#include <functional>

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

// `ui_wants_mouse` reports whether ImGui currently captures the mouse, so
// runtime menus force the cursor visible like the project's own overlays.
void AttachWindowHost(rex::ui::Window* window, rex::ui::WindowedAppContext& app_context,
                      std::function<bool()> ui_wants_mouse);
void DetachWindowHost();

struct DrawableSize {
  int width = 0;
  int height = 0;
};

// The current native client area in physical pixels: the extent the presenter draws into now, not the
// last size event cached by the window.
DrawableSize CurrentDrawableSize(rex::ui::Window* window);

// Called by ModalDialog while an in-game overlay is open.
void SetCursorOverlayOpen(bool open);

// Keeps the cursor in step with ImGui overlays that do not use ModalDialog.
void CreateCursorSync(rex::ui::ImGuiDrawer* drawer);
GuestFrameStats GetGuestFrameStats();
