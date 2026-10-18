#include "achievements_overlay.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <rex/runtime.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/immediate_drawer.h>

#include "launcher.h"
#include "overlay_rules.h"

namespace ssf2t {
namespace {

constexpr float kMinWidth = 420.0f;
constexpr float kMaxWidth = 560.0f;
constexpr float kMinHeight = 320.0f;
constexpr float kMaxHeight = 460.0f;
constexpr float kScrollPixelsPerSecond = 480.0f;
constexpr float kCloseButtonSize = 22.0f;
constexpr float kHintScale = 1.2f;
constexpr float kHintPadding = 6.0f;
constexpr ImGuiWindowFlags kWindowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

void DrawHeader(const rex::ui::AchievementsStyle& style, const AchievementSummary& summary) {
  ImGui::PushStyleColor(ImGuiCol_Text, style.header_text);
  ImGui::Text("%d / %d unlocked", summary.unlocked, summary.total);
  ImGui::PopStyleColor();
  ImGui::SameLine();
  ImGui::TextColored(style.badge_gamerscore, "%dG / %dG", summary.earned_gamerscore, summary.total_gamerscore);

  const float fraction =
      summary.total > 0 ? static_cast<float>(summary.unlocked) / static_cast<float>(summary.total) : 0.0f;
  ImGui::PushStyleColor(ImGuiCol_PlotHistogram, style.progress_bar);
  ImGui::ProgressBar(fraction, ImVec2(-1.0f, 6.0f), "");
  ImGui::PopStyleColor();

  ImGui::TextDisabled("Unlock progress is saved automatically");
  ImGui::Separator();
  ImGui::Spacing();
}

// Splits the draw list so the row band (channel 0) renders behind the text (channel 1).
void DrawRow(const rex::ui::AchievementsStyle& style, rex::ui::ImmediateTexture* icon, const AchievementEntry& entry,
             ImDrawList* draw_list) {
  const rex::system::AchievementInfo& a = entry.info;
  const bool unlocked = entry.unlocked;
  const std::string& description = EntryDescription(entry);
  const float pad = 4.0f;

  ImGui::PushID(static_cast<int>(a.id));
  draw_list->ChannelsSplit(2);
  draw_list->ChannelsSetCurrent(1);

  const float row_start_y = ImGui::GetCursorScreenPos().y;
  ImGui::Dummy(ImVec2(0.0f, pad * 0.5f));

  if (icon) {
    const ImVec4 tint = unlocked ? style.unlocked_icon_tint : style.locked_icon_tint;
    ImGui::ImageWithBg(reinterpret_cast<ImTextureID>(icon), ImVec2(style.icon_size, style.icon_size), ImVec2(0, 0),
                       ImVec2(1, 1), ImVec4(0, 0, 0, 0), tint);
  } else {
    ImGui::Dummy(ImVec2(style.icon_size, style.icon_size));
  }
  ImGui::SameLine();

  ImGui::BeginGroup();
  const ImVec4& title_color = unlocked ? style.unlocked_title : style.locked_title;
  ImGui::TextColored(title_color, "%s", unlocked ? "[*]" : "[ ]");
  ImGui::SameLine();
  ImGui::TextColored(style.badge_gamerscore, "%dG", static_cast<int>(a.gamerscore));
  ImGui::SameLine();
  ImGui::TextColored(title_color, "%s", a.label.c_str());
  ImGui::PushStyleColor(ImGuiCol_Text, unlocked ? style.unlocked_desc : style.locked_desc);
  ImGui::TextWrapped("%s", description.c_str());
  ImGui::PopStyleColor();
  ImGui::EndGroup();

  ImGui::Dummy(ImVec2(0.0f, pad * 0.5f));
  const float row_end_y = ImGui::GetCursorScreenPos().y;

  if (unlocked) {
    draw_list->ChannelsSetCurrent(0);
    const float x0 = ImGui::GetWindowPos().x + 2.0f;
    const float x1 = x0 + ImGui::GetWindowSize().x - 4.0f;
    draw_list->AddRectFilled(ImVec2(x0, row_start_y), ImVec2(x1, row_end_y), ImGui::GetColorU32(style.row_unlocked_bg),
                             style.row_rounding);
  }
  draw_list->ChannelsMerge();

  ImGui::Separator();
  ImGui::PopID();
}

// A square X at the right end of the first header row.
bool DrawCloseButton(const rex::ui::AchievementsStyle& style, float row_y) {
  const float button_y = row_y + (ImGui::GetTextLineHeight() - kCloseButtonSize) * 0.5f - 2.0f;
  const ImVec2 restore = ImGui::GetCursorPos();
  ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - style.window_padding.x - kCloseButtonSize, button_y));
  const ImVec4& tint = style.progress_bar;
  ImGui::PushStyleColor(ImGuiCol_Text, tint);
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(tint.x, tint.y, tint.z, 0.30f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(tint.x, tint.y, tint.z, 0.50f));
  ImGui::PushStyleColor(ImGuiCol_Border, tint);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
  const bool clicked = ImGui::Button("X##close_achievements", ImVec2(kCloseButtonSize, kCloseButtonSize));
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(5);
  ImGui::SetCursorPos(restore);
  return clicked;
}

float ScrollAxis(const ImGuiIO& io) {
  const auto analog = [&io](ImGuiKey key) { return io.KeysData[key - ImGuiKey_NamedKey_BEGIN].AnalogValue; };
  const bool up = ImGui::IsKeyDown(ImGuiKey_GamepadDpadUp) || ImGui::IsKeyDown(ImGuiKey_UpArrow);
  const bool down = ImGui::IsKeyDown(ImGuiKey_GamepadDpadDown) || ImGui::IsKeyDown(ImGuiKey_DownArrow);
  return ScrollDirection(up, down, analog(ImGuiKey_GamepadLStickUp) - analog(ImGuiKey_GamepadLStickDown));
}

}  // namespace

void ApplyAchievementsStyle(rex::ui::AchievementsStyle& style) {
  const ImVec4 accent = LauncherAccent();
  style.unlocked_title = ImVec4(1.00f, 0.74f, 0.38f, 1.00f);
  style.unlocked_desc = ImVec4(0.90f, 0.78f, 0.66f, 1.00f);
  style.row_unlocked_bg = ImVec4(0.36f, 0.17f, 0.04f, 0.55f);
  style.header_text = ImVec4(1.00f, 0.66f, 0.30f, 1.00f);
  style.progress_bar = accent;
}

AchievementsOverlay::AchievementsOverlay(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate_drawer,
                                         rex::Runtime* runtime, rex::system::AchievementManager* achievements,
                                         std::function<void()> closed)
    : ModalDialog(drawer),
      achievements_(achievements),
      icon_cache_(immediate_drawer, runtime),
      closed_(std::move(closed)) {}

AchievementsOverlay::~AchievementsOverlay() {
  if (closed_) closed_();
}

void AchievementsOverlay::DrawContent(ImGuiIO& io) {
  const auto& style = imgui_drawer()->style().achievements;
  const ImVec2 viewport = io.DisplaySize;
  ImGui::PushFont(LauncherRegularFont());
  ImGui::SetNextWindowPos(ImVec2(viewport.x * 0.5f, viewport.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(
      ImVec2(FitWindowExtent(viewport.x, kMinWidth, kMaxWidth), FitWindowExtent(viewport.y, kMinHeight, kMaxHeight)),
      ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.92f);
  if (!focused_) {
    focused_ = true;
    ImGui::SetNextWindowFocus();
  }

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, style.window_padding);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, style.item_spacing);
  if (ImGui::Begin("Achievements##overlay", nullptr, kWindowFlags)) {
    const std::vector<AchievementEntry> achievements = ListAchievements(*achievements_);
    const float header_row_y = ImGui::GetCursorPosY();
    DrawHeader(style, Summarize(achievements));
    if (DrawCloseButton(style, header_row_y)) {
      RequestClose();
    }

    const float hint_size = ImGui::GetFontSize() * kHintScale;
    ImGui::BeginChild("##achlist", ImVec2(0.0f, -(hint_size + 2.0f * style.item_spacing.y + kHintPadding * 0.5f)),
                      false);
    if (const float axis = ScrollAxis(io); axis != 0.0f) {
      const float target = ImGui::GetScrollY() + axis * kScrollPixelsPerSecond * io.DeltaTime;
      ImGui::SetScrollY(std::clamp(target, 0.0f, ImGui::GetScrollMaxY()));
    }
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    for (const AchievementEntry& entry : achievements) {
      DrawRow(style, icon_cache_.GetIcon(entry.info), entry, draw_list);
    }
    ImGui::EndChild();

    ImGui::Dummy(ImVec2(0.0f, kHintPadding * 0.5f));
    ImGui::PushFont(nullptr, hint_size);
    ImGui::TextColored(style.header_text, "B  Back");
    ImGui::PopFont();
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
  ImGui::PopFont();
}

}  // namespace ssf2t
