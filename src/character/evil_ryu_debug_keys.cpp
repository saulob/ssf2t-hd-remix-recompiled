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

constexpr size_t kInputZOrder = 1000;

DebugKey KeyFor(rex::ui::VirtualKey key) {
  switch (key) {
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
    if (e.target() != window_ || !window_->HasFocus() || key == DebugKey::kNone) return;
    const bool shift = e.is_shift_pressed();
    if (DebugMoveFor(key, shift) == DebugMove::kNone) return;
    const int64_t now = DebugNowMs();
    const DebugReject reason =
        HostGate(UltraFightersEnabled(), CharacterSelectActive(), GlobalDebugBridge().Status(now));
    const DebugMove move = DebugMoveForPress(key, shift, e.prev_state());
    if (reason != DebugReject::kNone) {
      if (move != DebugMove::kNone) LogRejected(move, reason);
      return;
    }
    e.set_handled(true);
    if (move != DebugMove::kNone) GlobalDebugBridge().Post(move, now);
  }

 private:
  static void LogRejected(DebugMove move, DebugReject reason) {
    REXLOG_INFO("DEBUG_SAULO EVIL_RYU debug-hotkey key={} move={} side=1 accepted=0 reason={}",
                DebugKeyLabel(move), DebugMoveName(move), DebugRejectName(reason));
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
