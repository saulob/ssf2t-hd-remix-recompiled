#include "debug_saulo_f9.h"

#include <memory>

#include <rex/logging.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

#include "fight_area.h"
#include "infinite_time.h"

namespace ssf2t {
namespace {

constexpr size_t kInputZOrder = 1000;

std::unique_ptr<rex::ui::WindowInputListener> g_key;
rex::ui::Window* g_key_window = nullptr;

class DebugSauloF9Key final : public rex::ui::WindowInputListener {
 public:
  explicit DebugSauloF9Key(rex::ui::Window* window) : window_(window) {}

  void OnKeyDown(rex::ui::KeyEvent& e) override {
    if (e.virtual_key() != rex::ui::VirtualKey::kF9) {
      return;
    }
    const bool fresh = IsFreshKeyPress(e.prev_state());
    if (!fresh && repeat_logged_) {
      return;
    }
    repeat_logged_ |= !fresh;
    const bool target_matches = e.target() == window_;
    const bool focused = window_->HasFocus();
    REXLOG_WARN("DEBUG_SAULO F9_KEYDOWN fresh={} focus={} target_matches={} prev_state={}", fresh, focused,
                target_matches, e.prev_state());
    if (fresh && focused && target_matches) {
      DebugSauloArmVictoryCapture();
    }
  }

 private:
  rex::ui::Window* window_;
  bool repeat_logged_ = false;
};

}  // namespace

void AttachDebugSauloF9Key(rex::ui::Window* window, const char* guest) {
  DetachDebugSauloF9Key();
  g_key = std::make_unique<DebugSauloF9Key>(window);
  g_key_window = window;
  window->AddInputListener(g_key.get(), kInputZOrder);
  REXLOG_WARN("DEBUG_SAULO F9_MODULE_READY guest={} registered={} window={} listener={} z_order={}", guest,
              g_key != nullptr, static_cast<const void*>(window), static_cast<const void*>(g_key.get()), kInputZOrder);
}

void DetachDebugSauloF9Key() {
  if (!g_key) {
    return;
  }
  g_key_window->RemoveInputListener(g_key.get());
  g_key.reset();
  g_key_window = nullptr;
}

}  // namespace ssf2t
