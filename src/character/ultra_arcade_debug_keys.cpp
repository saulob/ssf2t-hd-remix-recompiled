#include "character/ultra_arcade_debug_keys.h"

#include <memory>

#include <rex/logging.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

#include "character/ultra_arcade_debug.h"

namespace ssf2t::character {
namespace {

constexpr size_t kInputZOrder = 1000;

class ArcadeKeys final : public rex::ui::WindowInputListener {
 public:
  explicit ArcadeKeys(rex::ui::Window* window) : window_(window) {}

  void OnKeyDown(rex::ui::KeyEvent& e) override {
    if (e.virtual_key() != rex::ui::VirtualKey::kF1 || e.target() != window_ || !window_->HasFocus() ||
        e.is_alt_pressed() || e.is_ctrl_pressed() || e.is_shift_pressed()) {
      return;
    }
    const bool first = IsFirstPress(e.prev_state());
    const int64_t now = ArcadeNowMs();
    const ArcadeReject reason = HostGate(GlobalArcadeBridge().Status(now));
    if (reason != ArcadeReject::kNone) {
      if (first) REXLOG_INFO("DEBUG_SAULO ARCADE final-boss-request rejected reason={}", ArcadeRejectName(reason));
      return;
    }
    e.set_handled(true);
    if (first) GlobalArcadeBridge().Post(now);
  }

 private:
  rex::ui::Window* window_;
};

std::unique_ptr<ArcadeKeys> g_keys;
rex::ui::Window* g_window = nullptr;

}  // namespace

void AttachArcadeDebugKeys(rex::ui::Window* window) {
  DetachArcadeDebugKeys();
  g_keys = std::make_unique<ArcadeKeys>(window);
  g_window = window;
  window->AddInputListener(g_keys.get(), kInputZOrder);
}

void DetachArcadeDebugKeys() {
  if (!g_keys) return;
  g_window->RemoveInputListener(g_keys.get());
  g_keys.reset();
  g_window = nullptr;
}

}  // namespace ssf2t::character
