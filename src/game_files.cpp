#include "game_files.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "stfs.h"

namespace ssf2t {
namespace {

constexpr const char* kStagingFolder = "game-install-temp";
constexpr const char* kBackupFolder = "game-install-backup";
constexpr uint64_t kSpaceMargin = 64ull << 20;
constexpr int kRenameAttempts = 10;

uint32_t Crc32(std::istream& in) {
  static const auto table = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) {
        c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      }
      t[i] = c;
    }
    return t;
  }();
  uint32_t crc = 0xFFFFFFFFu;
  std::vector<char> chunk(1 << 20);
  while (in) {
    in.read(chunk.data(), std::streamsize(chunk.size()));
    for (std::streamsize i = 0; i < in.gcount(); ++i) {
      crc = table[(crc ^ uint8_t(chunk[size_t(i)])) & 0xFF] ^ (crc >> 8);
    }
  }
  return ~crc;
}

// The antivirus scanner can hold a freshly written folder for a moment.
bool RenameWithRetry(const std::filesystem::path& from, const std::filesystem::path& to) {
  for (int attempt = 0; attempt < kRenameAttempts; ++attempt) {
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    if (!ec) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
  }
  return false;
}

// An existing folder is only replaced when it is empty or already holds game files.
bool IsReplaceable(const std::filesystem::path& dir) {
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec)) {
    return false;
  }
  return std::filesystem::is_empty(dir, ec) || std::filesystem::exists(dir / "default.xex", ec) ||
         std::filesystem::exists(dir / kAssetArchive, ec);
}

class StagingGuard {
 public:
  explicit StagingGuard(std::filesystem::path dir) : dir_(std::move(dir)) {}
  ~StagingGuard() {
    if (armed_) {
      std::error_code ec;
      std::filesystem::remove_all(dir_, ec);
    }
  }
  void Release() { armed_ = false; }

 private:
  std::filesystem::path dir_;
  bool armed_ = true;
};

InstallResult Failure(InstallStatus status, std::string detail = {}) {
  InstallResult result;
  result.status = status;
  result.detail = std::move(detail);
  return result;
}

InstallStatus FromStfs(stfs::Status status) {
  switch (status) {
    case stfs::Status::kOpenFailed:
      return InstallStatus::kOpenFailed;
    case stfs::Status::kNotPackage:
      return InstallStatus::kNotPackage;
    case stfs::Status::kUnsupported:
      return InstallStatus::kUnsupportedPackage;
    case stfs::Status::kCorrupt:
    case stfs::Status::kUnsafePath:
      return InstallStatus::kDamagedPackage;
    case stfs::Status::kCancelled:
      return InstallStatus::kCancelled;
    case stfs::Status::kWriteFailed:
    case stfs::Status::kOk:
      break;
  }
  return InstallStatus::kWriteFailed;
}

}  // namespace

GameCheck CheckGameFiles(const std::filesystem::path& game_dir) {
  GameCheck check;
  const auto xex = game_dir / "default.xex";
  std::error_code ec;
  if (game_dir.empty() || !std::filesystem::is_regular_file(xex, ec)) {
    return check;
  }
  check.state = GameFiles::kUnsupported;
  if (std::filesystem::file_size(xex, ec) != kXexSize || ec) {
    return check;
  }
  std::ifstream in(xex, std::ios::binary);
  check.xex_crc32 = Crc32(in);
  if (check.xex_crc32 != kXexCrc32) {
    return check;
  }
  const auto archive = game_dir / kAssetArchive;
  if (!std::filesystem::is_regular_file(archive, ec) || std::filesystem::file_size(archive, ec) != kAssetArchiveSize) {
    check.state = GameFiles::kMissing;
    return check;
  }
  check.state = GameFiles::kReady;
  return check;
}

InstallResult InstallFromPackage(const std::filesystem::path& package, const std::filesystem::path& game_dir,
                                 const InstallHooks& hooks) {
  const auto phase = [&](const char* text) {
    if (hooks.phase) {
      hooks.phase(text);
    }
  };

  std::error_code ec;
  std::filesystem::path target = std::filesystem::absolute(game_dir, ec).lexically_normal();
  if (!target.has_filename()) {
    target = target.parent_path();
  }
  const std::filesystem::path parent = target.parent_path();
  if (game_dir.empty() || ec || parent.empty() || parent == target) {
    return Failure(InstallStatus::kNoGameFolder);
  }

  phase("Reading package...");
  stfs::Package pkg;
  stfs::Result read = pkg.Open(package);
  if (!read.ok()) {
    return Failure(FromStfs(read.status), read.detail);
  }
  if (pkg.title_id() != kTitleId) {
    char detail[48];
    std::snprintf(detail, sizeof(detail), "title ID %08X", pkg.title_id());
    InstallResult result = Failure(InstallStatus::kWrongTitle, detail);
    result.title_id = pkg.title_id();
    return result;
  }
  if (pkg.content_type() != kGameContentType) {
    char detail[48];
    std::snprintf(detail, sizeof(detail), "content type %08X", pkg.content_type());
    return Failure(InstallStatus::kNotGamePackage, detail);
  }
  read = pkg.LoadFiles();
  if (!read.ok()) {
    return Failure(FromStfs(read.status), read.detail);
  }

  std::filesystem::create_directories(parent, ec);
  if (ec) {
    return Failure(InstallStatus::kWriteFailed, "cannot create " + parent.string() + ": " + ec.message());
  }
  if (std::filesystem::exists(target, ec) && !IsReplaceable(target)) {
    return Failure(InstallStatus::kForeignFolder, target.string());
  }

  const std::filesystem::path staging = parent / kStagingFolder;
  const std::filesystem::path backup = parent / kBackupFolder;
  std::filesystem::remove_all(staging, ec);
  if (ec || std::filesystem::exists(staging, ec)) {
    return Failure(InstallStatus::kWriteFailed, "cannot clear " + staging.string());
  }
  const auto space = std::filesystem::space(parent, ec);
  if (!ec && space.available < pkg.total_bytes() + kSpaceMargin) {
    return Failure(InstallStatus::kNoSpace, "needs " + std::to_string(pkg.total_bytes()) + " bytes, " +
                                                std::to_string(space.available) + " available");
  }
  std::filesystem::create_directories(staging, ec);
  if (ec) {
    return Failure(InstallStatus::kWriteFailed, "cannot create " + staging.string() + ": " + ec.message());
  }
  StagingGuard guard(staging);

  phase("Extracting game files...");
  const stfs::Result extracted = pkg.ExtractTo(staging, hooks.progress);
  if (!extracted.ok()) {
    return Failure(FromStfs(extracted.status), extracted.detail);
  }

  phase("Verifying installation...");
  switch (CheckGameFiles(staging).state) {
    case GameFiles::kReady:
      break;
    case GameFiles::kUnsupported:
      return Failure(InstallStatus::kWrongVersion);
    case GameFiles::kMissing:
      return Failure(InstallStatus::kIncomplete);
  }
  if (hooks.progress && !hooks.progress(pkg.total_bytes(), pkg.total_bytes())) {
    return Failure(InstallStatus::kCancelled);
  }

  phase("Finishing...");
  const bool replacing = std::filesystem::exists(target, ec);
  if (replacing) {
    std::filesystem::remove_all(backup, ec);
    if (!RenameWithRetry(target, backup)) {
      return Failure(InstallStatus::kWriteFailed, "cannot move aside " + target.string());
    }
  }
  if (!RenameWithRetry(staging, target)) {
    if (replacing) {
      RenameWithRetry(backup, target);
    }
    return Failure(InstallStatus::kWriteFailed, "cannot activate " + target.string());
  }
  guard.Release();
  if (replacing) {
    std::filesystem::remove_all(backup, ec);
  }

  InstallResult result;
  result.bytes = pkg.total_bytes();
  result.files = pkg.file_count();
  result.title_id = pkg.title_id();
  return result;
}

InstallMessage DescribeInstall(InstallStatus status) {
  switch (status) {
    case InstallStatus::kInstalled:
      return {"Game installed", "Press Play to start the game."};
    case InstallStatus::kCancelled:
      return {"Install cancelled", "The game files were not changed."};
    case InstallStatus::kNoGameFolder:
      return {"No game folder", "Start the game with --game_data_root to choose where the game files go."};
    case InstallStatus::kForeignFolder:
      return {"Game folder not empty", "The game folder holds other files, so it was not replaced."};
    case InstallStatus::kOpenFailed:
      return {"Could not open the file", "Check that the file exists and that it is not in use."};
    case InstallStatus::kNotPackage:
      return {"Not an Xbox 360 package", "The selected file is not an Xbox 360 content package."};
    case InstallStatus::kWrongTitle:
      return {"Wrong package", "This is not a Super Street Fighter II Turbo HD Remix package."};
    case InstallStatus::kUnsupportedPackage:
      return {"Unsupported package", "This package uses a layout the installer cannot read."};
    case InstallStatus::kDamagedPackage:
      return {"Damaged package", "The package is damaged or incomplete. Check the file and try again."};
    case InstallStatus::kNoSpace:
      return {"Not enough disk space", "Free up space next to the game files and try again."};
    case InstallStatus::kWriteFailed:
      return {"Could not write the game files", "Check that the game folder is writable and not in use."};
    case InstallStatus::kWrongVersion:
      return {"Different version of the game",
              "This port requires the supported Super Street Fighter II Turbo HD Remix Xbox Live Arcade release."};
    case InstallStatus::kIncomplete:
      return {"Game files missing", "The package does not contain the game files this port needs."};
    case InstallStatus::kNotGamePackage:
      return {"Not the game package", "This package is not the game itself. Install it from the Xbox Live Arcade package."};
  }
  return {"Install failed", ""};
}

}  // namespace ssf2t
