#pragma once

#include <filesystem>

namespace ssf2t {

inline std::filesystem::path LauncherHeaderArtPath(const std::filesystem::path& app_folder) {
  return app_folder / "assets" / "launcher" / "header-art.png";
}

}  // namespace ssf2t
