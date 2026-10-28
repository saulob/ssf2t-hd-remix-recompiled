#include "game_files.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
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
constexpr const char* kXexName = "default.xex";
constexpr uint32_t kExecutionInfoHeader = 0x00040006;
constexpr uint32_t kDeltaPatchHeader = 0x000005FF;

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

uint32_t Be32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
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
  return std::filesystem::is_empty(dir, ec) || std::filesystem::exists(dir / kXexName, ec) ||
         std::filesystem::exists(dir / kAssetArchive, ec) || std::filesystem::exists(dir / kTitleUpdateFile, ec);
}

bool EqualsIgnoreAsciiCase(const std::string& a, const char* b) {
  const size_t n = std::strlen(b);
  if (a.size() != n) {
    return false;
  }
  for (size_t i = 0; i < n; ++i) {
    if (std::tolower(uint8_t(a[i])) != std::tolower(uint8_t(b[i]))) {
      return false;
    }
  }
  return true;
}

// Renames a case-variant such as Default.xex to default.xex; false when several variants exist.
bool NormalizeName(const std::filesystem::path& dir, const char* name) {
  std::error_code ec;
  std::filesystem::path variant;
  int found = 0;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (EqualsIgnoreAsciiCase(entry.path().filename().string(), name)) {
      variant = entry.path();
      ++found;
    }
  }
  if (found != 1) {
    return found == 0;
  }
  if (variant.filename() == name) {
    return true;
  }
  const auto temp = dir / (std::string(name) + ".rename");
  std::filesystem::rename(variant, temp, ec);
  if (!ec) {
    std::filesystem::rename(temp, dir / name, ec);
  }
  return !ec;
}

// Size first, so a wrong file is never read in full; `crc` is set once the file was read.
bool FileMatches(const std::filesystem::path& file, uint64_t size, uint32_t expected_crc, uint32_t& crc) {
  std::error_code ec;
  if (std::filesystem::file_size(file, ec) != size || ec) {
    return false;
  }
  std::ifstream in(file, std::ios::binary);
  crc = Crc32(in);
  return crc == expected_crc;
}

bool SupportedTitleUpdate(const std::filesystem::path& file) {
  uint32_t crc = 0;
  std::error_code ec;
  return std::filesystem::is_regular_file(file, ec) && FileMatches(file, kXexpSize, kXexpCrc32, crc);
}

std::string Hex32(uint32_t value) {
  char text[16];
  std::snprintf(text, sizeof(text), "%08X", value);
  return text;
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

// Clears and creates the staging folder beside the game folder, checking the free space for `bytes`.
InstallResult PrepareStaging(const std::filesystem::path& parent, uint64_t bytes) {
  std::error_code ec;
  std::filesystem::create_directories(parent, ec);
  if (ec) {
    return Failure(InstallStatus::kWriteFailed, "cannot create " + parent.string() + ": " + ec.message());
  }
  const std::filesystem::path staging = parent / kStagingFolder;
  std::filesystem::remove_all(staging, ec);
  if (ec || std::filesystem::exists(staging, ec)) {
    return Failure(InstallStatus::kWriteFailed, "cannot clear " + staging.string());
  }
  const auto space = std::filesystem::space(parent, ec);
  if (!ec && space.available < bytes + kSpaceMargin) {
    return Failure(InstallStatus::kNoSpace,
                   "needs " + std::to_string(bytes) + " bytes, " + std::to_string(space.available) + " available");
  }
  std::filesystem::create_directories(staging, ec);
  if (ec) {
    return Failure(InstallStatus::kWriteFailed, "cannot create " + staging.string() + ": " + ec.message());
  }
  return {};
}

InstallResult InstallGame(stfs::Package& pkg, const std::filesystem::path& target, const InstallHooks& hooks,
                          const std::function<void(const char*)>& phase) {
  const std::filesystem::path parent = target.parent_path();
  const std::filesystem::path staging = parent / kStagingFolder;
  const std::filesystem::path backup = parent / kBackupFolder;
  std::error_code ec;
  if (std::filesystem::exists(target, ec) && !IsReplaceable(target)) {
    return Failure(InstallStatus::kForeignFolder, target.string());
  }
  if (InstallResult prepared = PrepareStaging(parent, pkg.total_bytes()); !prepared.ok()) {
    return prepared;
  }
  StagingGuard guard(staging);

  phase("Extracting game files...");
  const stfs::Result extracted = pkg.ExtractTo(staging, hooks.progress);
  if (!extracted.ok()) {
    return Failure(FromStfs(extracted.status), extracted.detail);
  }
  if (SupportedTitleUpdate(target / kTitleUpdateFile)) {
    std::filesystem::copy_file(target / kTitleUpdateFile, staging / kTitleUpdateFile,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
      return Failure(InstallStatus::kWriteFailed, "cannot keep " + std::string(kTitleUpdateFile) + ": " + ec.message());
    }
  }

  phase("Verifying installation...");
  const GameCheck staged = CheckGameFiles(staging);
  switch (staged.state) {
    case GameFiles::kReady:
    case GameFiles::kUpdateMissing:
    case GameFiles::kUpdateUnsupported:
      break;
    case GameFiles::kUnsupported: {
      InstallResult failure = Failure(InstallStatus::kWrongVersion);
      failure.xex_version = staged.xex_version;
      return failure;
    }
    case GameFiles::kUpdateOnly:
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

// Only default.xexp is taken from the package, and only when it is the supported Title Update.
InstallResult InstallTitleUpdate(stfs::Package& pkg, const std::filesystem::path& target, const InstallHooks& hooks,
                                 const std::function<void(const char*)>& phase) {
  const std::filesystem::path parent = target.parent_path();
  const std::filesystem::path staging = parent / kStagingFolder;
  std::error_code ec;
  if (std::filesystem::exists(target, ec) && !IsReplaceable(target)) {
    return Failure(InstallStatus::kForeignFolder, target.string());
  }
  if (InstallResult prepared = PrepareStaging(parent, pkg.total_bytes()); !prepared.ok()) {
    return prepared;
  }
  StagingGuard guard(staging);

  phase("Extracting the Title Update...");
  const stfs::Result extracted = pkg.ExtractTo(staging, hooks.progress);
  if (!extracted.ok()) {
    return Failure(FromStfs(extracted.status), extracted.detail);
  }

  phase("Verifying the Title Update...");
  const std::filesystem::path patch = staging / kTitleUpdateFile;
  if (!NormalizeName(staging, kTitleUpdateFile) || !SupportedTitleUpdate(patch)) {
    const std::string version = ExecutableVersion(patch);
    InstallResult failure = Failure(InstallStatus::kWrongTitleUpdate,
                                     std::filesystem::exists(patch, ec)
                                         ? "default.xexp for version " + (version.empty() ? "?" : version)
                                         : "no default.xexp in the package");
    failure.xexp_version = version;
    return failure;
  }
  if (hooks.progress && !hooks.progress(pkg.total_bytes(), pkg.total_bytes())) {
    return Failure(InstallStatus::kCancelled);
  }

  phase("Finishing...");
  std::filesystem::create_directories(target, ec);
  if (ec) {
    return Failure(InstallStatus::kWriteFailed, "cannot create " + target.string() + ": " + ec.message());
  }
  const std::filesystem::path installed = target / kTitleUpdateFile;
  const std::filesystem::path previous = target / (std::string(kTitleUpdateFile) + ".previous");
  const bool replacing = std::filesystem::exists(installed, ec);
  std::filesystem::remove(previous, ec);
  if (replacing && !RenameWithRetry(installed, previous)) {
    return Failure(InstallStatus::kWriteFailed, "cannot move aside " + installed.string());
  }
  if (!RenameWithRetry(patch, installed)) {
    if (replacing) {
      RenameWithRetry(previous, installed);
    }
    return Failure(InstallStatus::kWriteFailed, "cannot activate " + installed.string());
  }
  std::filesystem::remove(previous, ec);

  InstallResult result;
  result.bytes = kXexpSize;
  result.files = 1;
  result.title_id = pkg.title_id();
  result.title_update = true;
  return result;
}

}  // namespace

std::string FormatXexVersion(uint32_t value) {
  char text[32];
  std::snprintf(text, sizeof(text), "%u.%u.%u.%u", value >> 28, (value >> 24) & 0xF, (value >> 8) & 0xFFFF,
                value & 0xFF);
  return text;
}

std::string ExecutableVersion(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  uint8_t head[24] = {};
  if (!in.read(reinterpret_cast<char*>(head), sizeof(head)) || std::memcmp(head, "XEX2", 4) != 0) {
    return {};
  }
  const uint32_t header_size = Be32(head + 8);
  const uint32_t count = Be32(head + 20);
  if (header_size > (1u << 20) || count > 64 || 24 + 8 * count > header_size) {
    return {};
  }
  std::vector<uint8_t> header(header_size);
  in.seekg(0);
  if (!in.read(reinterpret_cast<char*>(header.data()), std::streamsize(header.size()))) {
    return {};
  }
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t key = Be32(&header[24 + 8 * i]);
    const uint32_t offset = Be32(&header[28 + 8 * i]);
    // Both headers keep the version in their second word: the executable's own, or the one the patch produces.
    if ((key == kExecutionInfoHeader || key == kDeltaPatchHeader) && offset <= header_size - 8) {
      return FormatXexVersion(Be32(&header[offset + 4]));
    }
  }
  return {};
}

GameCheck CheckGameFiles(const std::filesystem::path& game_dir) {
  GameCheck check;
  const auto xex = game_dir / kXexName;
  std::error_code ec;
  if (!game_dir.empty() && std::filesystem::is_directory(game_dir, ec) &&
      (!NormalizeName(game_dir, kXexName) || !NormalizeName(game_dir, kTitleUpdateFile))) {
    check.state = GameFiles::kUnsupported;
    return check;
  }
  if (game_dir.empty() || !std::filesystem::is_regular_file(xex, ec)) {
    // The Title Update can be installed before the base game; its default.xexp is kept either way.
    if (!game_dir.empty() && FileMatches(game_dir / kTitleUpdateFile, kXexpSize, kXexpCrc32, check.xexp_crc32)) {
      check.state = GameFiles::kUpdateOnly;
      check.xexp_version = ExecutableVersion(game_dir / kTitleUpdateFile);
    }
    return check;
  }
  check.state = GameFiles::kUnsupported;
  check.xex_version = ExecutableVersion(xex);
  if (!FileMatches(xex, kXexSize, kXexCrc32, check.xex_crc32)) {
    return check;
  }
  const auto archive = game_dir / kAssetArchive;
  if (!std::filesystem::is_regular_file(archive, ec) || std::filesystem::file_size(archive, ec) != kAssetArchiveSize) {
    check.state = GameFiles::kMissing;
    return check;
  }
  const auto patch = game_dir / kTitleUpdateFile;
  if (!std::filesystem::is_regular_file(patch, ec)) {
    check.state = GameFiles::kUpdateMissing;
    return check;
  }
  check.state = GameFiles::kUpdateUnsupported;
  check.xexp_version = ExecutableVersion(patch);
  if (!FileMatches(patch, kXexpSize, kXexpCrc32, check.xexp_crc32)) {
    return check;
  }
  check.state = GameFiles::kReady;
  return check;
}

std::string GameFilesLogText(const GameCheck& check) {
  if (check.state == GameFiles::kUpdateOnly) {
    std::string text = std::string("Game files: Title Update installed, game package missing (") + kTitleUpdateFile;
    if (!check.xexp_version.empty()) {
      text += " to " + check.xexp_version;
    }
    if (check.xexp_crc32 != 0) {
      text += ", CRC32 " + Hex32(check.xexp_crc32);
    }
    return text + ")";
  }
  const char* state = "missing";
  switch (check.state) {
    case GameFiles::kReady:
      state = "valid";
      break;
    case GameFiles::kUnsupported:
      state = "unsupported version";
      break;
    case GameFiles::kUpdateMissing:
      state = "Title Update missing";
      break;
    case GameFiles::kUpdateUnsupported:
      state = "unsupported Title Update";
      break;
    case GameFiles::kUpdateOnly:
    case GameFiles::kMissing:
      break;
  }
  std::string text = std::string("Game files: ") + state + " (default.xex";
  if (!check.xex_version.empty()) {
    text += " " + check.xex_version;
  }
  if (check.xex_crc32 != 0) {
    text += ", CRC32 " + Hex32(check.xex_crc32);
  }
  if (check.state == GameFiles::kReady || check.state == GameFiles::kUpdateUnsupported) {
    text += std::string("; ") + kTitleUpdateFile;
    if (!check.xexp_version.empty()) {
      text += " to " + check.xexp_version;
    }
    if (check.xexp_crc32 != 0) {
      text += ", CRC32 " + Hex32(check.xexp_crc32);
    }
  }
  if (check.state == GameFiles::kUnsupported || check.state == GameFiles::kUpdateUnsupported) {
    text += std::string("; expected default.xex ") + kXexVersion + " CRC32 " + Hex32(kXexCrc32) + " with " +
            kTitleUpdateFile + " to " + kTitleUpdateVersion + " CRC32 " + Hex32(kXexpCrc32);
  }
  return text + ")";
}

std::string DescribeGameFilesProblem(const GameCheck& check) {
  const std::string needed = std::string("This port needs game version ") + kXexVersion + " with Title Update #1 (" +
                             kTitleUpdateVersion + ").";
  const auto crc = [](uint32_t value) { return value != 0 ? ", CRC32 " + Hex32(value) : std::string(); };
  switch (check.state) {
    case GameFiles::kUnsupported:
      return "The installed default.xex is " +
             (check.xex_version.empty() ? std::string("not a supported game executable")
                                        : "version " + check.xex_version + crc(check.xex_crc32)) +
             ". " + needed;
    case GameFiles::kUpdateMissing:
      return "Installing the game package does not install the Title Update. Click Install Title Update and select "
             "the Title Update package. On an Xbox 360 drive it is in "
             "Content\\0000000000000000\\584108A4\\000B0000\\. " +
             needed;
    case GameFiles::kUpdateUnsupported:
      return "The installed default.xexp " +
             (check.xexp_version.empty() ? std::string("is not a supported Title Update")
                                         : "updates the game to " + check.xexp_version + crc(check.xexp_crc32)) +
             ". " + needed;
    case GameFiles::kUpdateOnly:
      return "The Title Update is installed and will be kept. Click Install Game and select the Xbox Live Arcade "
             "game package. " +
             needed;
    case GameFiles::kMissing:
      return "This port requires your own Super Street Fighter II Turbo HD Remix Xbox Live Arcade package and its "
             "Title Update #1, installed separately with Install Game and Install Title Update.";
    case GameFiles::kReady:
      break;
  }
  return {};
}

PlayCardText DescribePlayCard(const GameCheck& check) {
  const std::string title_id = Hex32(kTitleId);
  switch (check.state) {
    case GameFiles::kReady:
      return {"Ready to play \xC2\xB7 Title Update #1",
              "GAME VER " + std::string(kTitleUpdateVersion) + " \xC2\xB7 TITLE ID " + title_id};
    case GameFiles::kUpdateMissing:
      return {"Title Update #1 needed",
              "GAME VER " + (check.xex_version.empty() ? std::string(kXexVersion) : check.xex_version) +
                  " \xC2\xB7 TITLE ID " + title_id};
    case GameFiles::kUpdateOnly:
      return {"Game package needed",
              "Title Update #1 installed \xC2\xB7 GAME VER target " + std::string(kTitleUpdateVersion)};
    case GameFiles::kUnsupported:
      return {"Different version of the game",
              "GAME VER " + (check.xex_version.empty() ? std::string("unknown") : check.xex_version) +
                  " \xC2\xB7 TITLE ID " + title_id};
    case GameFiles::kUpdateUnsupported:
      return {"Different Title Update",
              "TITLE UPDATE " + (check.xexp_version.empty() ? std::string("unknown") : check.xexp_version) +
                  " \xC2\xB7 TARGET " + std::string(kTitleUpdateVersion)};
    case GameFiles::kMissing:
      break;
  }
  return {"Game package needed", "Install the game package and its Title Update #1 to play."};
}

GameButtons DescribeGameButtons(GameFiles state) {
  GameButtons buttons;
  switch (state) {
    case GameFiles::kReady:
    case GameFiles::kUpdateMissing:
    case GameFiles::kUpdateUnsupported:
    case GameFiles::kUnsupported:
      buttons.game_installed = true;
      break;
    case GameFiles::kMissing:
    case GameFiles::kUpdateOnly:
      break;
  }
  switch (state) {
    case GameFiles::kReady:
    case GameFiles::kUpdateOnly:
    case GameFiles::kUpdateUnsupported:
      buttons.update_installed = true;
      break;
    case GameFiles::kMissing:
    case GameFiles::kUpdateMissing:
    case GameFiles::kUnsupported:
      break;
  }
  buttons.game_label = buttons.game_installed ? "Reinstall Game" : "Install Game";
  buttons.update_label = buttons.update_installed ? "Remove Title Update" : "Install Title Update";
  return buttons;
}

InstallResult InstallFromPackage(const std::filesystem::path& package, const std::filesystem::path& game_dir,
                                 InstallKind kind, const InstallHooks& hooks) {
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
  const bool is_game_package = pkg.content_type() == kGameContentType;
  const bool is_update_package = pkg.content_type() == kTitleUpdateContentType;
  if (!is_game_package && !is_update_package) {
    char detail[48];
    std::snprintf(detail, sizeof(detail), "content type %08X", pkg.content_type());
    return Failure(InstallStatus::kNotGamePackage, detail);
  }
  if (kind == InstallKind::kGame && !is_game_package) {
    return Failure(InstallStatus::kSelectedTitleUpdate);
  }
  if (kind == InstallKind::kTitleUpdate && !is_update_package) {
    return Failure(InstallStatus::kSelectedGamePackage);
  }
  read = pkg.LoadFiles();
  if (!read.ok()) {
    return Failure(FromStfs(read.status), read.detail);
  }
  return kind == InstallKind::kTitleUpdate ? InstallTitleUpdate(pkg, target, hooks, phase)
                                           : InstallGame(pkg, target, hooks, phase);
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
              "This port requires the supported Super Street Fighter II Turbo HD Remix Xbox Live Arcade release, game "
              "version 0.0.1.3."};
    case InstallStatus::kIncomplete:
      return {"Game files missing", "The package does not contain the game files this port needs."};
    case InstallStatus::kNotGamePackage:
      return {"Not the game package",
              "This package is neither the game nor its Title Update. Install the Xbox Live Arcade package and its "
              "Title Update."};
    case InstallStatus::kWrongTitleUpdate:
      return {"Unsupported Title Update",
              "This port requires Super Street Fighter II Turbo HD Remix Title Update #1, which updates the game to "
              "version 0.0.2.3."};
    case InstallStatus::kSelectedTitleUpdate:
      return {"This is a Title Update package", "Use Install Title Update to install it."};
    case InstallStatus::kSelectedGamePackage:
      return {"This is the Xbox Live Arcade game package", "Use Install Game to install it."};
  }
  return {"Install failed", ""};
}

InstallMessage DescribeInstall(const InstallResult& result) {
  InstallMessage message = DescribeInstall(result.status);
  if (result.status == InstallStatus::kWrongVersion) {
    message.text += "\nDetected game version: " + (result.xex_version.empty() ? "unknown" : result.xex_version) +
                     "\nExpected game version: " + kXexVersion;
  } else if (result.status == InstallStatus::kWrongTitleUpdate) {
    message.text += "\nDetected game version: " + (result.xexp_version.empty() ? "unknown" : result.xexp_version) +
                     "\nExpected game version: " + kTitleUpdateVersion;
  }
  return message;
}

InstallMessage DescribeInstalled(const InstallResult& result, GameFiles files) {
  const std::string title = result.title_update ? "Title Update #1 installed" : "Game package installed";
  switch (files) {
    case GameFiles::kReady:
      return {title, "Press Play to start the game."};
    case GameFiles::kUpdateMissing:
      return {title, "Now install the Title Update package the same way."};
    case GameFiles::kUpdateOnly:
    case GameFiles::kMissing:
      return {title, "Now install the Xbox Live Arcade game package the same way."};
    case GameFiles::kUnsupported:
    case GameFiles::kUpdateUnsupported:
      break;
  }
  return {title, "See the Play page for the files that are still needed."};
}

RemoveResult RemoveTitleUpdate(const std::filesystem::path& game_dir) {
  RemoveResult result;
  const GameCheck before = CheckGameFiles(game_dir);  // normalizes default.xexp's case and confirms it is the TU
  if (before.state == GameFiles::kReady || before.state == GameFiles::kUpdateOnly ||
      before.state == GameFiles::kUpdateUnsupported) {
    std::error_code ec;
    result.removed = std::filesystem::remove(game_dir / kTitleUpdateFile, ec) && !ec;
  }
  result.files = CheckGameFiles(game_dir);
  return result;
}

InstallMessage DescribeRemoved(const RemoveResult& result) {
  if (!result.removed) {
    return {"Title Update #1 not installed", "There was nothing to remove."};
  }
  if (result.files.state == GameFiles::kUpdateMissing) {
    return {"Title Update #1 removed", "Click Install Title Update to reinstall it."};
  }
  return {"Title Update #1 removed", "Install the game package and its Title Update #1 to play."};
}

}  // namespace ssf2t
