#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace ssf2t {

inline constexpr uint32_t kTitleId = 0x584108A4;
inline constexpr uint32_t kGameContentType = 0x000D0000;

// The default.xex the code was recompiled from (XBLA game version 0.0.1.3, see tools/codegen.ps1).
inline constexpr uint64_t kXexSize = 3932160;
inline constexpr uint32_t kXexCrc32 = 0xB734F075;
inline constexpr char kAssetArchive[] = "flog_x360.fpg";
inline constexpr uint64_t kAssetArchiveSize = 378257408;

enum class GameFiles { kReady, kMissing, kUnsupported };

struct GameCheck {
  GameFiles state = GameFiles::kMissing;
  uint32_t xex_crc32 = 0;  // set when default.xex was read
};

// The one readiness check: launcher state, install verification and the clean-start decision all use it.
GameCheck CheckGameFiles(const std::filesystem::path& game_dir);

enum class InstallStatus {
  kInstalled,
  kCancelled,
  kNoGameFolder,
  kForeignFolder,
  kOpenFailed,
  kNotPackage,
  kWrongTitle,
  kUnsupportedPackage,
  kDamagedPackage,
  kNoSpace,
  kWriteFailed,
  kWrongVersion,
  kIncomplete,
  kNotGamePackage,
};

struct InstallResult {
  InstallStatus status = InstallStatus::kInstalled;
  std::string detail;  // for the log, not the UI
  uint32_t title_id = 0;
  uint64_t bytes = 0;
  size_t files = 0;
  bool ok() const { return status == InstallStatus::kInstalled; }
};

struct InstallHooks {
  std::function<void(const char* phase)> phase;
  // Returns false to cancel. Cancelling is possible until the new files are activated.
  std::function<bool(uint64_t done, uint64_t total)> progress;
};

// Extracts the package into a staging folder next to `game_dir`, checks it with CheckGameFiles and only
// then swaps it in. On any failure or cancellation `game_dir` is left as it was.
InstallResult InstallFromPackage(const std::filesystem::path& package, const std::filesystem::path& game_dir,
                                 const InstallHooks& hooks = {});

struct InstallMessage {
  const char* title;
  const char* text;
};
InstallMessage DescribeInstall(InstallStatus status);

}  // namespace ssf2t
