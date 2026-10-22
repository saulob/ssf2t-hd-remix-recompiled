#pragma once

#include <functional>

namespace rex::ui {
class ImGuiDrawer;
class Window;
}  // namespace rex::ui

namespace ssf2t {

void CreateInfiniteTimeNotice(rex::ui::ImGuiDrawer* drawer);
void ShowInfiniteTimeNotice(bool enabled, double seconds);
void QueueStartupInfiniteTimeNotice(bool enabled);

// F10 calls `toggle` once per press while the game runs. The key is left for the game and the other listeners.
void AttachInfiniteTimeKey(rex::ui::Window* window, std::function<void()> toggle);
void DetachInfiniteTimeKey();

}  // namespace ssf2t
