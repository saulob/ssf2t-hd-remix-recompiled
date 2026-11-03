#pragma once

// DEBUG_SAULO: temporary F9 trigger for the victory-screen capture.

namespace rex::ui {
class Window;
}  // namespace rex::ui

namespace ssf2t {

void AttachDebugSauloF9Key(rex::ui::Window* window, const char* guest);
void DetachDebugSauloF9Key();

}  // namespace ssf2t
