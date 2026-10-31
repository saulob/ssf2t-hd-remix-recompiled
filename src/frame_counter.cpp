#include "frame_counter.h"

#include <cfloat>
#include <string>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/keybinds.h>

#include "corner_overlay.h"
#include "settings.h"

namespace ssf2t {
namespace {

constexpr ImU32 kPanelColor = IM_COL32(12, 10, 8, 190);
constexpr ImU32 kBorderColor = IM_COL32(255, 133, 20, 255);
constexpr ImU32 kTextColor = IM_COL32(255, 255, 255, 255);

class FrameCounterOverlay final : public rex::ui::ImGuiDialog {
 public:
  explicit FrameCounterOverlay(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO&) override {
    const GuestFrameStats stats = GetGuestFrameStats();
    if (!FrameCounterVisible(REXCVAR_GET(ssf2t_show_frame_counter), stats)) {
      return;
    }
    DrawCornerText(FrameCounterText(stats), FrameCounterSlot());
  }
};

}  // namespace

// Drawn on the foreground list, so it stays above the cheat menu in the same corner.
void DrawCornerText(const std::string& text, int slot) {
  ImFont* font = ImGui::GetFont();
  const float size = ImGui::GetFontSize();
  const ImVec2 text_size = font->CalcTextSizeA(size, FLT_MAX, 0, text.c_str());
  const CornerPanel panel = CornerPanelAt(ImGui::GetIO().DisplaySize.x, size, text_size.x, text_size.y, slot);
  ImDrawList* draw = ImGui::GetForegroundDrawList();
  draw->AddRectFilled(ImVec2(panel.x0, panel.y0), ImVec2(panel.x1, panel.y1), kPanelColor, size * 0.5f);
  draw->AddRect(ImVec2(panel.x0, panel.y0), ImVec2(panel.x1, panel.y1), kBorderColor, size * 0.5f, 0, 1.0f);
  draw->AddText(font, size, ImVec2(panel.text_x, panel.text_y), kTextColor, text.c_str());
}

void CreateFrameCounter(rex::ui::ImGuiDrawer* drawer) {
  new FrameCounterOverlay(drawer);  // retained by the drawer for the session
  rex::ui::RegisterBind("bind_ssf2t_frame_counter", kFrameCounterKey, "Toggle frame counter", [] {
    rex::cvar::SetFlagByName("ssf2t_show_frame_counter", REXCVAR_GET(ssf2t_show_frame_counter) ? "false" : "true");
  });
}

}  // namespace ssf2t
