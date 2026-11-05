#include "character/evil_ryu_debug_keys.h"

#include <memory>

#include <rex/logging.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

#include "character/akuma_overlay.h"
#include "character/evil_ryu_debug.h"
#include "character/ultra_fighters.h"

namespace ssf2t::character {
namespace {

// One layer above the port's other key listeners so the debug keys are offered the event first.
constexpr size_t kInputZOrder = 1001;

DebugKey KeyFor(rex::ui::VirtualKey key) {
  switch (key) {
    case rex::ui::VirtualKey::kF4: return DebugKey::kF4;
    case rex::ui::VirtualKey::kF5: return DebugKey::kF5;
    case rex::ui::VirtualKey::kF6: return DebugKey::kF6;
    case rex::ui::VirtualKey::kF7: return DebugKey::kF7;
    case rex::ui::VirtualKey::kF8: return DebugKey::kF8;
    default: return DebugKey::kNone;
  }
}

class DebugKeys final : public rex::ui::WindowInputListener {
 public:
  explicit DebugKeys(rex::ui::Window* window) : window_(window) {}

  void OnKeyDown(rex::ui::KeyEvent& e) override {
    const DebugKey key = KeyFor(e.virtual_key());
    if (key == DebugKey::kNone) return;
    const bool focused = window_->HasFocus();
    if (!e.prev_state()) {
      REXLOG_INFO("DEBUG_SAULO ULTRA debug-key key=F{} shift={} ctrl={} alt={} focus={}", 3 + int(key),
                  e.is_shift_pressed(), e.is_ctrl_pressed(), e.is_alt_pressed(), focused);
    }
    if (e.target() != window_ || !focused || e.is_alt_pressed()) return;
    const bool shift = e.is_shift_pressed();
    const bool ctrl = e.is_ctrl_pressed();
    const int64_t now = DebugNowMs();
    const DebugFightStatus status = GlobalDebugBridge().Status(now);
    const DebugReject reason = HostGate(UltraFightersEnabled(), CharacterSelectActive(), status);
    if (reason != DebugReject::kNone) {
      if (!e.prev_state()) LogRejected(key, reason);
      return;
    }
    if (DebugMoveFor(status.p1, key, shift, ctrl) == DebugMove::kNone) return;
    const DebugMove move = DebugMoveForPress(status.p1, key, shift, ctrl, e.prev_state());
    e.set_handled(true);
    if (move != DebugMove::kNone) GlobalDebugBridge().Post(move, now);
  }

 private:
  static void LogRejected(DebugKey key, DebugReject reason) {
    REXLOG_INFO("DEBUG_SAULO ULTRA debug-hotkey key=F{} side=1 accepted=0 reason={}", 3 + int(key),
                DebugRejectName(reason));
  }

  rex::ui::Window* window_;
};

std::unique_ptr<DebugKeys> g_keys;
rex::ui::Window* g_window = nullptr;

}  // namespace

void AttachEvilRyuDebugKeys(rex::ui::Window* window) {
  DetachEvilRyuDebugKeys();
  g_keys = std::make_unique<DebugKeys>(window);
  g_window = window;
  window->AddInputListener(g_keys.get(), kInputZOrder);
}

void DetachEvilRyuDebugKeys() {
  if (!g_keys) return;
  g_window->RemoveInputListener(g_keys.get());
  g_keys.reset();
  g_window = nullptr;
}

}  // namespace ssf2t::character
