#pragma once

#include <functional>

namespace rex::ui {
class ImGuiDrawer;
}  // namespace rex::ui

namespace ssf2t {

// The game's own message boxes as an overlay like the achievements list: centred, the focused button marked, A or
// Enter chooses, B or Escape cancels, the mouse still clicks. Opens when no other overlay is up. `post` runs work on
// the UI thread.
void CreateMessageBoxUi(rex::ui::ImGuiDrawer* drawer, std::function<void(std::function<void()>)> post);

}  // namespace ssf2t
