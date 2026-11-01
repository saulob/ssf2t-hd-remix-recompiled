#pragma once

#include <chrono>
#include <deque>
#include <mutex>
#include <optional>

#include <rex/ui/overlay/achievement_icon_cache.h>
#include <rex/ui/overlay/achievement_notification.h>

namespace rex {
class Runtime;
}  // namespace rex

namespace rex::ui {
class ImmediateDrawer;
}  // namespace rex::ui

namespace ssf2t {

// ReXGlue's unlock toast with the heading and the achievement name in the selected language.
class AchievementToast final : public rex::ui::AchievementNotificationDialog {
 public:
  AchievementToast(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate_drawer, rex::Runtime* runtime);
  ~AchievementToast() override;

  void Push(const rex::system::AchievementEvent& event) override;

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  static constexpr float kDisplaySeconds = 4.5f;

  struct PendingToast {
    rex::system::AchievementEvent event;
    std::chrono::steady_clock::time_point arrived;
    std::optional<std::chrono::steady_clock::time_point> visible_since;
  };

  std::mutex mutex_;
  std::deque<PendingToast> queue_;
  rex::ui::AchievementIconCache icon_cache_;
};

}  // namespace ssf2t
