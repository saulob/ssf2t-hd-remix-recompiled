#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include <imgui.h>

#include "achievement_list.h"

namespace rex::ui {
class ImmediateDrawer;
}  // namespace rex::ui

namespace ssf2t {

// The achievements as ReXGlue's store holds them, read for the launcher. The store needs the game's XDBF, so a
// runtime without graphics, audio or input loads default.xex just long enough to read it; it must be destroyed
// before the game's own runtime is built.
class LauncherAchievements {
 public:
  // `language` is the XC_LANGUAGE the strings are read in.
  LauncherAchievements(const std::filesystem::path& game_dir, const std::filesystem::path& user_dir,
                       uint32_t language, rex::ui::ImmediateDrawer* immediate_drawer);
  ~LauncherAchievements();

  bool ok() const { return ok_; }
  uint32_t language() const { return language_; }
  const std::vector<AchievementEntry>& entries() const { return entries_; }
  ImTextureID Icon(const rex::system::AchievementInfo& info);

 private:
  struct Runtime;

  std::unique_ptr<Runtime> runtime_;
  std::vector<AchievementEntry> entries_;
  uint32_t language_ = 0;
  bool ok_ = false;
};

}  // namespace ssf2t
