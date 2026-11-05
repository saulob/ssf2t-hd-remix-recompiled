#pragma once

// DEBUG_SAULO: temporary Arcade final-boss key, see ultra_arcade_debug.h.

namespace rex::ui {
class Window;
}

namespace ssf2t::character {

// F1 asks the running Arcade run to go to its final boss, M. Bison. The key is claimed only while an Arcade run is
// live; in menus, Local Match and online play it behaves as before.
void AttachArcadeDebugKeys(rex::ui::Window* window);
void DetachArcadeDebugKeys();

}  // namespace ssf2t::character
