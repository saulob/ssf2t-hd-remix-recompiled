#include "platform.h"

#include <cstdlib>

#include <rex/filesystem.h>

namespace ssf2t {

std::filesystem::path PortFolder() {
#if !defined(_WIN32)
  // An AppImage runs from a read-only mount that is gone once it exits.
  if (const char* appimage = std::getenv("APPIMAGE"); appimage && *appimage) {
    return std::filesystem::path(appimage).parent_path();
  }
#endif
  return rex::filesystem::GetExecutableFolder();
}

}  // namespace ssf2t
