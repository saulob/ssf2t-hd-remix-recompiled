#pragma once

// DEBUG_SAULO: temporary Evil Ryu move hotkeys, see evil_ryu_debug.h.

namespace rex::ui {
class Window;
}

namespace ssf2t::character {

// F5/F6/F7/F8 (and Shift+F5/F6) ask the fight to start an Evil Ryu move for P1. The keys are claimed only while P1 is
// Evil Ryu in a local fight, so everywhere else they behave as before.
void AttachEvilRyuDebugKeys(rex::ui::Window* window);
void DetachEvilRyuDebugKeys();

}  // namespace ssf2t::character
