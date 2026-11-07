#pragma once

#include <array>
#include <string_view>

namespace ssf2t {

enum class LauncherPage { kPlay, kDisplay, kGraphics, kGameplay, kOnline, kAchievements, kControls, kBugReport, kAbout };

struct LauncherPageInfo {
  LauncherPage page;
  const char* name;
  const char* icon_file;
  float icon_scale;  // share of the icon box the artwork's longer side takes
};

inline constexpr std::array<LauncherPageInfo, 9> kLauncherPages = {{
    {LauncherPage::kPlay, "Play", "play.svg", 0.84f},
    {LauncherPage::kDisplay, "Display", "display.svg", 0.90f},
    {LauncherPage::kGraphics, "Graphics", "graphics.svg", 0.84f},
    {LauncherPage::kGameplay, "Gameplay", "gameplay.svg", 0.92f},
    {LauncherPage::kOnline, "Online", "online.svg", 0.90f},
    {LauncherPage::kAchievements, "Achievements", "trophies.svg", 0.86f},
    {LauncherPage::kControls, "Controls", "controls.svg", 0.94f},
    {LauncherPage::kBugReport, "Bug Report", "bug-report.svg", 0.90f},
    {LauncherPage::kAbout, "About", "about.svg", 0.88f},
}};

constexpr const LauncherPageInfo& PageInfo(LauncherPage page) {
  for (const LauncherPageInfo& info : kLauncherPages) {
    if (info.page == page) return info;
  }
  return kLauncherPages.front();
}

}  // namespace ssf2t
