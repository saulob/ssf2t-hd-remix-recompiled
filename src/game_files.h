#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace ssf2t {

inline constexpr uint32_t kTitleId = 0x584108A4;
inline constexpr uint32_t kGameContentType = 0x000D0000;
inline constexpr uint32_t kTitleUpdateContentType = 0x000B0000;

// The code is recompiled from default.xex with its Title Update applied (see tools/codegen.ps1). ReXGlue applies
// default.xexp beside default.xex whenever it loads it.
inline constexpr char kXexVersion[] = "0.0.1.3";
inline constexpr uint64_t kXexSize = 3932160;
inline constexpr uint32_t kXexCrc32 = 0xB734F075;
inline constexpr char kXexSha256[] = "6e1c2f9ed7d1102a311a5f5626563527eda5f85b2fbe8db8405b866ded3f31ee";
inline constexpr char kTitleUpdateFile[] = "default.xexp";
inline constexpr char kTitleUpdateVersion[] = "0.0.2.3";
inline constexpr uint64_t kXexpSize = 2166784;
inline constexpr uint32_t kXexpCrc32 = 0x38DE2310;
inline constexpr char kXexpSha256[] = "a2e8e66b482c5d3a0d3a5d3f67a3cecb7a12ce1f6189196694fd3e765910606c";
inline constexpr char kAssetArchive[] = "flog_x360.fpg";
inline constexpr uint64_t kAssetArchiveSize = 378257408;

enum class GameFiles { kReady, kMissing, kUnsupported, kUpdateMissing, kUpdateUnsupported, kUpdateOnly };

struct GameCheck {
  GameFiles state = GameFiles::kMissing;
  uint32_t xex_crc32 = 0;   // set when default.xex was read
  uint32_t xexp_crc32 = 0;  // set when default.xexp was read
  std::string xex_version;   // from the XEX headers, empty when unreadable
  std::string xexp_version;  // the version the Title Update produces
};

// An XEX version word as a.b.c.d (4, 4, 16 and 8 bits).
std::string FormatXexVersion(uint32_t value);
// The version an XEX declares; for a Title Update, the version it updates the game to. Empty when unreadable.
std::string ExecutableVersion(const std::filesystem::path& file);

// The one readiness check: launcher state, install verification and the clean-start decision all use it.
GameCheck CheckGameFiles(const std::filesystem::path& game_dir);

// A support log line about the game files that carries no folder: names, versions, sizes, CRC32s and state.
std::string GameFilesLogText(const GameCheck& check);

// What the Play page says about files that are not ready: what was found and what this port needs.
std::string DescribeGameFilesProblem(const GameCheck& check);

struct PlayCardText {
  std::string title;   // status and, once known, the installed revision's identity
  std::string detail;  // short technical line: detected or expected version and title ID
};
// The Play page's status-card text, built only from the same check used to decide whether Play is allowed.
PlayCardText DescribePlayCard(const GameCheck& check);

struct GameButtons {
  std::string game_label;    // "Install Game" or "Reinstall Game"
  std::string update_label;  // "Install Title Update" or "Remove Title Update"
  bool game_installed = false;
  bool update_installed = false;
};
// The Play page's two package buttons, driven by the same check as the status card.
GameButtons DescribeGameButtons(GameFiles state);

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
  kWrongTitleUpdate,
  kSelectedTitleUpdate,
  kSelectedGamePackage,
};

// Which package a package button accepts.
enum class InstallKind { kGame, kTitleUpdate };

struct InstallResult {
  InstallStatus status = InstallStatus::kInstalled;
  std::string detail;  // for the log, not the UI
  uint32_t title_id = 0;
  uint64_t bytes = 0;
  size_t files = 0;
  bool title_update = false;
  std::string xex_version;   // detected version that failed verification, when known (kWrongVersion)
  std::string xexp_version;  // detected version that failed verification, when known (kWrongTitleUpdate)
  bool ok() const { return status == InstallStatus::kInstalled; }
};

struct InstallHooks {
  std::function<void(const char* phase)> phase;
  // Returns false to cancel. Cancelling is possible until the new files are activated.
  std::function<bool(uint64_t done, uint64_t total)> progress;
};

// The game package is extracted into a staging folder next to `game_dir`, checked and only then swapped in; a
// supported default.xexp already in `game_dir` is kept. A Title Update package only adds its checked default.xexp.
// `kind` rejects a package of the other type before anything is written, so `game_dir` is left as it was. On any
// other failure or cancellation `game_dir` is also left as it was.
InstallResult InstallFromPackage(const std::filesystem::path& package, const std::filesystem::path& game_dir,
                                 InstallKind kind, const InstallHooks& hooks = {});

struct InstallMessage {
  std::string title;
  std::string text;
};
InstallMessage DescribeInstall(InstallStatus status);
// Adds the detected and expected versions to kWrongVersion/kWrongTitleUpdate, when known.
InstallMessage DescribeInstall(const InstallResult& result);
// After a successful install: what was installed and what, if anything, is still needed.
InstallMessage DescribeInstalled(const InstallResult& result, GameFiles files);

struct RemoveResult {
  bool removed = false;
  GameCheck files;  // the game files after removal
};
// Removes only the installed Title Update file(s) in `game_dir`, and only when CheckGameFiles already recognises
// them there. Never touches the base game, saves, settings or anything else.
RemoveResult RemoveTitleUpdate(const std::filesystem::path& game_dir);
InstallMessage DescribeRemoved(const RemoveResult& result);

}  // namespace ssf2t
