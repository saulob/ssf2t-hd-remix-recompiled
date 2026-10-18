#include "window_host.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>
#include <rex/ui/windowed_app_context.h>

#include "aspect_ratio.h"
#include "settings.h"
#include "window_title.h"

namespace {

constexpr double kSampleSeconds = 1.0;
// Above the app's own input listener (z-order 0), so F11 never reaches the game.
constexpr size_t kWindowHostZOrder = 1000;

using Clock = std::chrono::steady_clock;

std::atomic<bool> g_active{false};
rex::ui::Window* g_window = nullptr;
rex::ui::WindowedAppContext* g_app_context = nullptr;
std::function<bool()> g_gameplay_active;
// UI thread only.
CursorHider g_cursor;
std::optional<double> g_title_fps;
std::string g_shown_title;

std::mutex g_sample_mutex;
std::optional<Clock::time_point> g_sample_start;
uint64_t g_sample_frames = 0;
uint64_t g_total_frames = 0;
GuestFrameStats g_stats;

std::optional<std::string> ShownRenderResolution() {
  if (!REXCVAR_GET(hcu_show_render_resolution)) {
    return std::nullopt;
  }
  const int scale = std::max(1, std::atoi(rex::cvar::GetFlagByName("resolution_scale").c_str()));
  return ssf2t::RenderResolutionText(scale, ssf2t::ParseAspectRatio(rex::cvar::GetFlagByName("hcu_aspect_ratio")));
}

// Composes all title parts together, so one update never drops another.
void RefreshTitle() {
  const std::optional<double> fps = REXCVAR_GET(hcu_show_fps) ? g_title_fps : std::nullopt;
  std::string title = ssf2t::WindowTitleText(ShownRenderResolution(), fps);
  if (title != g_shown_title) {
    g_shown_title = title;
    g_window->SetTitle(title);
  }
}

void ShowFps(double fps) {
  if (g_active.load(std::memory_order_relaxed)) {
    g_title_fps = fps;
    RefreshTitle();
  }
}

void CountGuestFrame() {
  if (!g_active.load(std::memory_order_relaxed)) {
    return;
  }
  std::lock_guard lock(g_sample_mutex);
  const auto now = Clock::now();
  if (!g_sample_start) {
    g_sample_start = now;
    return;
  }
  ++g_sample_frames;
  ++g_total_frames;
  const double elapsed = std::chrono::duration<double>(now - *g_sample_start).count();
  if (elapsed < kSampleSeconds) {
    return;
  }
  const double fps = g_sample_frames / elapsed;
  g_stats = {fps, elapsed * 1000.0 / g_sample_frames, g_total_frames};
  g_sample_start = now;
  g_sample_frames = 0;
  g_app_context->CallInUIThread([fps] { ShowFps(fps); });
}

void ApplyCursor(rex::ui::Window* window, bool gameplay) {
  if (const auto visible = g_cursor.Update(gameplay, window->IsFullscreen(), window->HasFocus(),
                                           CursorHider::Clock::now())) {
    window->SetCursorVisibility(*visible ? rex::ui::Window::CursorVisibility::kVisible
                                         : rex::ui::Window::CursorVisibility::kHidden);
  }
}

void UpdateCursor() {
  if (g_window && g_active.load(std::memory_order_relaxed)) {
    ApplyCursor(g_window, g_gameplay_active && g_gameplay_active());
  }
}

void RestoreCursor() {
  if (g_window && g_active.load(std::memory_order_relaxed)) {
    ApplyCursor(g_window, false);
  }
}

class WindowHost final : public rex::ui::WindowListener, public rex::ui::WindowInputListener {
 public:
  void OnClosing(rex::ui::UIEvent&) override {
    RestoreCursor();
    g_active.store(false, std::memory_order_relaxed);
  }

  void OnGotFocus(rex::ui::UISetupEvent&) override { UpdateCursor(); }
  void OnLostFocus(rex::ui::UISetupEvent&) override { UpdateCursor(); }

  void OnMouseMove(rex::ui::MouseEvent& e) override {
    if (e.target() != g_window) {
      return;
    }
    g_cursor.MouseMoved(CursorHider::Clock::now());
    UpdateCursor();
  }

  void OnKeyDown(rex::ui::KeyEvent& e) override {
    if (e.virtual_key() != rex::ui::VirtualKey::kF11 || e.target() != g_window) {
      return;
    }
    e.set_handled(true);
    if (e.prev_state()) {
      return;
    }
    // SetFullscreen(true) is SDL's default fullscreen mode, which is borderless desktop.
    const bool fullscreen = !g_window->IsFullscreen();
    g_window->SetFullscreen(fullscreen);
    REXLOG_INFO("Window mode: {}", fullscreen ? "borderless fullscreen" : "windowed");
    UpdateCursor();
  }
};

WindowHost window_host;

// Dialogs are drawn on the UI thread every frame, which gives the idle timer a periodic check.
class CursorTick final : public rex::ui::ImGuiDialog {
 public:
  explicit CursorTick(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO&) override { UpdateCursor(); }
};

}  // namespace

void AttachWindowHost(rex::ui::Window* window, rex::ui::WindowedAppContext& app_context,
                      std::function<bool()> gameplay_active) {
  g_window = window;
  g_app_context = &app_context;
  g_gameplay_active = std::move(gameplay_active);
  RefreshTitle();
  g_active.store(true, std::memory_order_relaxed);
  window->AddListener(&window_host);
  window->AddInputListener(&window_host, kWindowHostZOrder);
}

void CreateCursorTick(rex::ui::ImGuiDrawer* drawer) {
  new CursorTick(drawer);  // retained by the drawer for the session
}

// The guest can end without the window receiving OnClosing.
void DetachWindowHost() {
  RestoreCursor();
  g_active.store(false, std::memory_order_relaxed);
  g_gameplay_active = nullptr;
}

GuestFrameStats GetGuestFrameStats() {
  std::lock_guard lock(g_sample_mutex);
  return g_stats;
}

// The SDK's VdSwap body, exported by rexruntime.dll. The SDK has no header for it.
namespace rex::kernel::xboxkrnl {

void VdSwap_entry(mapped_void buffer_ptr, mapped_void fetch_ptr, mapped_void unk2, mapped_void unk3, mapped_void unk4,
                  mapped_u32 frontbuffer_ptr, mapped_u32 texture_format_ptr, mapped_u32 color_space_ptr,
                  mapped_u32 width, mapped_u32 height);

}  // namespace rex::kernel::xboxkrnl

// Named like the SDK's export, so REX_EXTERN would call this hook again: the original is called by its entry point.
REX_HOOK_RAW(__imp__VdSwap) {
  rex::ppc::HostToGuestFunction<rex::kernel::xboxkrnl::VdSwap_entry>(ctx, base);
  CountGuestFrame();
}
