#include "achievement_toast.h"

#include <algorithm>

#include <imgui.h>

#include <rex/ui/imgui_drawer.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/ui/style.h>

#include "localization/ui_text.h"

namespace ssf2t {

AchievementToast::AchievementToast(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate_drawer,
                                   rex::Runtime* runtime)
    : AchievementNotificationDialog(drawer), icon_cache_(immediate_drawer, runtime) {}

AchievementToast::~AchievementToast() = default;

void AchievementToast::Push(const rex::system::AchievementEvent& event) {
  std::lock_guard<std::mutex> lock(mutex_);
  queue_.push_back({event, std::chrono::steady_clock::now()});
}

void AchievementToast::OnDraw(ImGuiIO& io) {
  const auto now = std::chrono::steady_clock::now();

  PendingToast toast;
  float age = 0.0f;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!queue_.empty()) {
      if (!queue_.front().visible_since) {
        queue_.front().visible_since = now;
      }
      age = std::chrono::duration<float>(now - *queue_.front().visible_since).count();
      if (age >= kDisplaySeconds) {
        queue_.pop_front();
      } else {
        break;
      }
    }
    if (queue_.empty()) {
      return;
    }
    toast = queue_.front();
  }

  float alpha = 1.0f;
  if (age < 0.3f) {
    alpha = age / 0.3f;
  } else if (age > kDisplaySeconds - 0.5f) {
    alpha = (kDisplaySeconds - age) / 0.5f;
  }
  alpha = std::max(0.0f, std::min(1.0f, alpha));

  const float pad = 16.0f;
  const float width = 320.0f;
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - width - pad, io.DisplaySize.y - 80.0f - pad), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(width, 70.0f), ImGuiCond_Always);
  const rex::ui::ToastStyle& style = imgui_drawer()->style().toast;
  ImGui::SetNextWindowBgAlpha(style.background_alpha * alpha);

  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs;
  const auto faded = [alpha](ImVec4 c) { return ImVec4(c.x, c.y, c.z, c.w * alpha); };

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, style.rounding);
  ImGui::PushStyleColor(ImGuiCol_Text, faded(style.text));
  if (ImGui::Begin("##ach_toast", nullptr, flags)) {
    if (rex::ui::ImmediateTexture* icon = icon_cache_.GetIcon(toast.event.achievement)) {
      ImGui::Image(reinterpret_cast<ImTextureID>(icon), ImVec2(style.icon_size, style.icon_size));
      ImGui::SameLine();
    }
    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_Text, faded(style.title));
    ImGui::TextUnformatted(Tr("Achievement Unlocked"));
    ImGui::PopStyleColor();
    ImGui::TextUnformatted(Tr(toast.event.achievement.label).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(" (%dG)", static_cast<int>(toast.event.achievement.gamerscore));
    ImGui::EndGroup();
  }
  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
}

}  // namespace ssf2t
