#pragma once

#include <filesystem>

namespace ssf2t {

inline std::filesystem::path LauncherImagePath(const std::filesystem::path& app_folder, const char* file) {
  return app_folder / "assets" / "launcher" / file;
}

}  // namespace ssf2t
