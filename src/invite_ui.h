#pragma once

#include <functional>

namespace rex::ui {
class ImGuiDrawer;
}  // namespace rex::ui

namespace ssf2t {

// The in-game Invite Player friend list and the prompt for a friend's invite. `post` runs work on the UI thread.
void CreateInviteUi(rex::ui::ImGuiDrawer* drawer, std::function<void(std::function<void()>)> post);

}  // namespace ssf2t
