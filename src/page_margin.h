#pragma once

#include <imgui.h>
#include <imgui_internal.h>

namespace ssf2t {

// The window's rectangles are rebuilt every frame, so nothing has to be restored. The window comes from the context
// pointer: ImGui::GetCurrentWindow() would read GImGui, which the runtime does not export to the program.
inline ImGuiWindow* CurrentWindow() { return ImGui::GetCurrentContext()->CurrentWindow; }

// Keeps a page's content a margin short of the page's scrollbar.
inline void KeepClearOfScrollbar(float margin) {
  ImGuiWindow* window = CurrentWindow();
  window->WorkRect.Max.x -= margin;
  window->ContentRegionRect.Max.x -= margin;
}

}  // namespace ssf2t
