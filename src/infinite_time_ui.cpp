#include "infinite_time_ui.h"

#include <cfloat>
#include <chrono>
#include <memory>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

#include "frame_counter.h"
#include "infinite_time.h"
#include "settings.h"
#include "window_host.h"

namespace ssf2t {
namespace {

constexpr size_t kInputZOrder = 1000;
constexpr ImU32 kPanelColor = IM_COL32(12, 10, 8, 190);
constexpr ImU32 kBorderColor = IM_COL32(255, 133, 20, 255);
constexpr ImU32 kTextColor = IM_COL32(255, 255, 255, 255);

InfiniteTimeNotice g_notice;
std::unique_ptr<rex::ui::WindowInputListener> g_key;
rex::ui::Window* g_key_window = nullptr;

double NowSeconds() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

class InfiniteTimeNoticeOverlay final : public rex::ui::ImGuiDialog {
 public:
  explicit InfiniteTimeNoticeOverlay(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    const bool frame_counter_shown = REXCVAR_GET(ssf2t_show_frame_counter);
    const bool frame_counter_visible = FrameCounterVisible(frame_counter_shown, GetGuestFrameStats());
    if (!g_notice.Visible(NowSeconds(), StartupNoticeReady(frame_counter_shown, frame_counter_visible))) {
      return;
    }
    const char* text = g_notice.text();
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize();
    const ImVec2 text_size = font->CalcTextSizeA(size, FLT_MAX, 0, text);
    const ImVec2 pad(size * 0.6f, size * 0.3f);
    const float top = size * 3.0f;
    const ImVec2 p1(io.DisplaySize.x - size, top + text_size.y + pad.y * 2);
    const ImVec2 p0(p1.x - text_size.x - pad.x * 2, top);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled(p0, p1, kPanelColor, size * 0.5f);
    draw->AddRect(p0, p1, kBorderColor, size * 0.5f, 0, 1.0f);
    draw->AddText(font, size, ImVec2(p0.x + pad.x, p0.y + pad.y), kTextColor, text);
  }
};

class InfiniteTimeKey final : public rex::ui::WindowInputListener {
 public:
  InfiniteTimeKey(rex::ui::Window* window, std::function<void()> toggle)
      : window_(window), toggle_(std::move(toggle)) {}

  void OnKeyDown(rex::ui::KeyEvent& e) override {
    if (e.target() != window_ || !window_->HasFocus() || e.virtual_key() != rex::ui::VirtualKey::kF10) {
      return;
    }
    if (IsFreshKeyPress(e.prev_state())) {
      toggle_();
    }
  }

 private:
  rex::ui::Window* window_;
  std::function<void()> toggle_;
};

}  // namespace

void CreateInfiniteTimeNotice(rex::ui::ImGuiDrawer* drawer) {
  new InfiniteTimeNoticeOverlay(drawer);  // retained by the drawer for the session
}

void ShowInfiniteTimeNotice(bool enabled, double seconds) {
  g_notice.Show(enabled, seconds, NowSeconds());
}

void QueueStartupInfiniteTimeNotice(bool enabled) {
  g_notice.QueueStartup(enabled, kInfiniteTimeStartupNoticeSeconds);
}

void AttachInfiniteTimeKey(rex::ui::Window* window, std::function<void()> toggle) {
  DetachInfiniteTimeKey();
  g_key = std::make_unique<InfiniteTimeKey>(window, std::move(toggle));
  g_key_window = window;
  window->AddInputListener(g_key.get(), kInputZOrder);
}

void DetachInfiniteTimeKey() {
  if (!g_key) {
    return;
  }
  g_key_window->RemoveInputListener(g_key.get());
  g_key.reset();
  g_key_window = nullptr;
}

}  // namespace ssf2t
