// Game file check tests: default.xex and default.xexp casing is normalized without weakening the version checks, the
// base game alone is ready and the Title Update is optional (and may be installed before or after the base game),
// Install Game and Install Title Update each accept only their own package, Remove Title Update deletes only the
// installed default.xexp, and the Play page text, buttons and image selection are derived from the same check.
// Run: ssf2t_game_files_tests [default.xex]

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "game_files.h"

using namespace ssf2t;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

fs::path MakeDir(const char* name) {
  const fs::path dir = fs::temp_directory_path() / "ssf2t_game_files_tests" / name;
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  return dir;
}

bool HasEntry(const fs::path& dir, const char* name) {
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (entry.path().filename().string() == name) return true;
  }
  return false;
}

bool Contains(const std::string& text, const std::string& part) {
  return text.find(part) != std::string::npos;
}

void TestUnsupportedXexStillRejected() {
  for (const char* name : {"default.xex", "Default.xex", "DEFAULT.XEX"}) {
    const fs::path dir = MakeDir("unsupported");
    std::ofstream(dir / name, std::ios::binary) << "not a supported xex";
    CHECK(CheckGameFiles(dir).state == GameFiles::kUnsupported);
    CHECK(HasEntry(dir, "default.xex"));
    fs::remove_all(dir);
  }
}

void TestMixedCaseSupportedXexIsNormalized(const fs::path& real_xex) {
  for (const char* name : {"default.xex", "Default.xex", "DEFAULT.XEX"}) {
    const fs::path dir = MakeDir("normalize");
    fs::copy_file(real_xex, dir / name);
    const GameCheck check = CheckGameFiles(dir);
    CHECK(check.xex_crc32 == kXexCrc32);
    CHECK(check.state != GameFiles::kUnsupported);
    CHECK(HasEntry(dir, "default.xex"));
    CHECK(!HasEntry(dir, "default.xex.rename"));
    fs::remove_all(dir);
  }
}

// Only the archive's size is checked, so an empty file of that size stands in for it.
void TestTitleUpdateIsOptional(const fs::path& real_xex, const fs::path& real_xexp) {
  const fs::path dir = MakeDir("title_update");
  fs::copy_file(real_xex, dir / "default.xex");
  std::ofstream(dir / kAssetArchive, std::ios::binary).close();
  fs::resize_file(dir / kAssetArchive, kAssetArchiveSize);

  GameCheck check = CheckGameFiles(dir);
  CHECK(check.state == GameFiles::kReady);
  CHECK(check.guest == GuestVersion::kBase013);
  CHECK(SelectGuest(check) == GuestVersion::kBase013);
  CHECK(check.xex_version == kXexVersion);
  CHECK(DescribeGameFilesProblem(check).empty());
  CHECK(GameFilesLogText(check) == "Game files: valid (default.xex 0.0.1.3, CRC32 B734F075)");

  std::ofstream(dir / kTitleUpdateFile, std::ios::binary) << "not the title update";
  check = CheckGameFiles(dir);
  CHECK(check.state == GameFiles::kUpdateUnsupported);
  CHECK(Contains(GameFilesLogText(check), "unsupported Title Update"));
  CHECK(Contains(GameFilesLogText(check), "expected default.xex 0.0.1.3 CRC32 B734F075, optional default.xexp to 0.0.2.3"));
  CHECK(Contains(DescribeGameFilesProblem(check), kTitleUpdateVersion));
  CHECK(!SelectGuest(check));

  if (!real_xexp.empty()) {
    fs::remove(dir / kTitleUpdateFile);
    fs::copy_file(real_xexp, dir / "DEFAULT.XEXP");
    check = CheckGameFiles(dir);
    CHECK(check.state == GameFiles::kReady);
    CHECK(check.guest == GuestVersion::kTitleUpdate023);
    CHECK(SelectGuest(check) == GuestVersion::kTitleUpdate023);
    CHECK(check.xexp_crc32 == kXexpCrc32);
    CHECK(check.xexp_crc32 == 0x38DE2310 && check.xexp_version == "0.0.2.3");
    CHECK(SelectGuest(check, HostGuests{true}) == GuestVersion::kTitleUpdate023);
    CHECK(DescribePlayCard(check, HostGuests{true}).title == "Ready to play \xC2\xB7 Title Update #1");
    CHECK(DescribeGameButtons(check, HostGuests{true}).update_label == "Remove Title Update");
    // A program without the update image still sees a supported file, and says so in other words.
    CHECK(check.state == GameFiles::kReady);
    CHECK(!SelectGuest(check, HostGuests{false}));
    CHECK(!Contains(DescribePlayCard(check, HostGuests{false}).title, "Different Title Update"));
    CHECK(!Contains(DescribeGameFilesProblem(check, HostGuests{false}), "not a supported"));
    CHECK(check.xexp_version == kTitleUpdateVersion);
    CHECK(HasEntry(dir, kTitleUpdateFile));
    CHECK(GameFilesLogText(check) ==
          "Game files: valid (default.xex 0.0.1.3, CRC32 B734F075; default.xexp to 0.0.2.3, CRC32 38DE2310)");
    CHECK(DescribeGameFilesProblem(check).empty());

    // Remove Title Update returns to the base game; putting the file back returns to the update.
    const RemoveResult removed = RemoveTitleUpdate(dir);
    CHECK(removed.removed);
    CHECK(removed.files.state == GameFiles::kReady && removed.files.guest == GuestVersion::kBase013);
    CHECK(!HasEntry(dir, kTitleUpdateFile));
    CHECK(HasEntry(dir, "default.xex") && HasEntry(dir, kAssetArchive));
    fs::copy_file(real_xexp, dir / kTitleUpdateFile);
    check = CheckGameFiles(dir);
    CHECK(check.state == GameFiles::kReady && check.guest == GuestVersion::kTitleUpdate023);
  }
  fs::remove_all(dir);
}

void TestExecutableVersion(const fs::path& real_xex, const fs::path& real_xexp) {
  CHECK(FormatXexVersion(0x00000103) == kXexVersion);
  CHECK(FormatXexVersion(0x00000203) == kTitleUpdateVersion);
  CHECK(FormatXexVersion(0x20345608) == "2.0.13398.8");
  const fs::path dir = MakeDir("version");
  std::ofstream(dir / "short.xex", std::ios::binary) << "XEX2 too short";
  CHECK(ExecutableVersion(dir / "short.xex").empty());
  CHECK(ExecutableVersion(dir / "missing.xex").empty());
  // One execution info header whose offset points past the header must be rejected, not read.
  const unsigned char hostile[32] = {'X', 'E', 'X', '2', 0, 0, 0, 0, 0, 0, 0, 32, 0, 0, 0, 0,
                                     0,   0,   0,   0,   0, 0, 0, 1, 0, 4, 0, 6,  0xFF, 0xFF, 0xFF, 0xFC};
  std::ofstream(dir / "hostile.xex", std::ios::binary).write(reinterpret_cast<const char*>(hostile), sizeof(hostile));
  CHECK(ExecutableVersion(dir / "hostile.xex").empty());
  if (!real_xex.empty()) CHECK(ExecutableVersion(real_xex) == kXexVersion);
  if (!real_xexp.empty()) CHECK(ExecutableVersion(real_xexp) == kTitleUpdateVersion);
  fs::remove_all(dir);
}

void TestInstallMessages() {
  InstallResult game;
  InstallResult update;
  update.title_update = true;
  CHECK(DescribeInstalled(game, GameFiles::kReady).title == "Game package installed");
  CHECK(DescribeInstalled(game, GameFiles::kReady).text == "Press Play to start the game.");
  CHECK(DescribeInstalled(update, GameFiles::kReady).text == "Press Play to start the game.");
  CHECK(DescribeInstalled(update, GameFiles::kMissing).title == "Title Update #1 installed");
  CHECK(Contains(DescribeInstalled(update, GameFiles::kMissing).text, "game package"));
  CHECK(Contains(DescribeInstalled(update, GameFiles::kUpdateOnly).text, "game package"));
  CHECK(Contains(DescribeInstall(InstallStatus::kWrongTitleUpdate).text, kTitleUpdateVersion));

  // The install-failure message includes the detected version, not only the expected one, once known.
  InstallResult wrong_version;
  wrong_version.status = InstallStatus::kWrongVersion;
  wrong_version.xex_version = "0.0.1.5";
  const InstallMessage wrong_version_message = DescribeInstall(wrong_version);
  CHECK(Contains(wrong_version_message.text, "Detected game version: 0.0.1.5"));
  CHECK(Contains(wrong_version_message.text, std::string("Expected game version: ") + kXexVersion));

  InstallResult wrong_update;
  wrong_update.status = InstallStatus::kWrongTitleUpdate;
  wrong_update.xexp_version = "0.0.2.5";
  const InstallMessage wrong_update_message = DescribeInstall(wrong_update);
  CHECK(Contains(wrong_update_message.text, "Detected game version: 0.0.2.5"));
  CHECK(Contains(wrong_update_message.text, std::string("Expected game version: ") + kTitleUpdateVersion));

  InstallResult unknown_update;
  unknown_update.status = InstallStatus::kWrongTitleUpdate;
  CHECK(Contains(DescribeInstall(unknown_update).text, "Detected game version: unknown"));
}

// The Play card's status/identity line and its version line come only from the validated GameCheck.
void TestPlayCardText() {
  GameCheck ready;
  ready.state = GameFiles::kReady;
  ready.guest = GuestVersion::kTitleUpdate023;
  CHECK(DescribePlayCard(ready).title == "Ready to play \xC2\xB7 Title Update #1");
  CHECK(Contains(DescribePlayCard(ready).detail, kTitleUpdateVersion));
  CHECK(Contains(DescribePlayCard(ready).detail, "584108A4"));

  GameCheck base_only;
  base_only.state = GameFiles::kReady;
  base_only.guest = GuestVersion::kBase013;
  CHECK(DescribePlayCard(base_only).title == "Ready to play");
  CHECK(Contains(DescribePlayCard(base_only).detail, kXexVersion));
  CHECK(Contains(DescribePlayCard(base_only).detail, "584108A4"));

  GameCheck update_only;
  update_only.state = GameFiles::kUpdateOnly;
  CHECK(DescribePlayCard(update_only).title == "Game package needed");
  CHECK(Contains(DescribePlayCard(update_only).detail, "Title Update #1 installed"));
  CHECK(Contains(DescribePlayCard(update_only).detail, kTitleUpdateVersion));

  GameCheck nothing;
  CHECK(nothing.state == GameFiles::kMissing);
  CHECK(DescribePlayCard(nothing).title == "Game package needed");

  GameCheck wrong_version;
  wrong_version.state = GameFiles::kUnsupported;
  wrong_version.xex_version = "0.0.1.5";
  CHECK(DescribePlayCard(wrong_version).title == "Different version of the game");
  CHECK(Contains(DescribePlayCard(wrong_version).detail, "0.0.1.5"));

  GameCheck wrong_update;
  wrong_update.state = GameFiles::kUpdateUnsupported;
  wrong_update.xexp_version = "0.0.2.5";
  CHECK(DescribePlayCard(wrong_update).title == "Different Title Update");
  CHECK(Contains(DescribePlayCard(wrong_update).detail, "0.0.2.5"));
}

GameCheck Check(GameFiles state, GuestVersion guest = GuestVersion::kBase013) {
  GameCheck check;
  check.state = state;
  check.guest = guest;
  return check;
}

// The button labels for every installation state.
void TestGameButtons() {
  const GameButtons nothing = DescribeGameButtons(Check(GameFiles::kMissing));
  CHECK(nothing.game_label == "Install Game");
  CHECK(nothing.update_label == "Install Title Update");
  CHECK(!nothing.game_installed && !nothing.update_installed);

  const GameButtons base_only = DescribeGameButtons(Check(GameFiles::kReady));
  CHECK(base_only.game_label == "Reinstall Game");
  CHECK(base_only.update_label == "Install Title Update");
  CHECK(base_only.game_installed && !base_only.update_installed);

  const GameButtons update_only = DescribeGameButtons(Check(GameFiles::kUpdateOnly));
  CHECK(update_only.game_label == "Install Game");
  CHECK(update_only.update_label == "Remove Title Update");
  CHECK(!update_only.game_installed && update_only.update_installed);

  const GameButtons ready = DescribeGameButtons(Check(GameFiles::kReady, GuestVersion::kTitleUpdate023));
  CHECK(ready.game_label == "Reinstall Game");
  CHECK(ready.update_label == "Remove Title Update");
  CHECK(ready.game_installed && ready.update_installed);

  const GameButtons invalid = DescribeGameButtons(Check(GameFiles::kUpdateUnsupported));
  CHECK(invalid.game_label == "Reinstall Game" && invalid.update_label == "Remove Title Update");
}

// The image follows the installed files: base alone, base with the update, nothing for blocked states.
void TestGuestSelection() {
  CHECK(SelectGuest(Check(GameFiles::kReady)) == GuestVersion::kBase013);
  CHECK(SelectGuest(Check(GameFiles::kReady, GuestVersion::kTitleUpdate023)) == GuestVersion::kTitleUpdate023);
  for (GameFiles blocked :
       {GameFiles::kMissing, GameFiles::kUnsupported, GameFiles::kUpdateUnsupported, GameFiles::kUpdateOnly}) {
    CHECK(!SelectGuest(Check(blocked)));
  }
  CHECK(std::string(GuestExecutableVersion(GuestVersion::kBase013)) == kXexVersion);
  CHECK(std::string(GuestExecutableVersion(GuestVersion::kTitleUpdate023)) == kTitleUpdateVersion);

  // Installing or removing the update after the program started asks for a restart, nothing else does.
  const GameCheck base = Check(GameFiles::kReady);
  const GameCheck update = Check(GameFiles::kReady, GuestVersion::kTitleUpdate023);
  CHECK(NeedsOtherGuest(update, GuestVersion::kBase013));
  CHECK(NeedsOtherGuest(base, GuestVersion::kTitleUpdate023));
  CHECK(!NeedsOtherGuest(base, GuestVersion::kBase013));
  CHECK(!NeedsOtherGuest(update, GuestVersion::kTitleUpdate023));
  CHECK(!NeedsOtherGuest(Check(GameFiles::kUpdateUnsupported), GuestVersion::kBase013));

  // The loaded executable must be exactly the selected image's version.
  CHECK(LoadedVersionMatches(GuestVersion::kBase013, "0.0.1.3"));
  CHECK(LoadedVersionMatches(GuestVersion::kTitleUpdate023, "0.0.2.3"));
  CHECK(!LoadedVersionMatches(GuestVersion::kBase013, "0.0.2.3"));
  CHECK(!LoadedVersionMatches(GuestVersion::kTitleUpdate023, "0.0.1.3"));
  CHECK(!LoadedVersionMatches(GuestVersion::kBase013, ""));
  const std::string message = DescribeLoadedVersionMismatch(GuestVersion::kBase013, "0.0.2.3");
  CHECK(Contains(message, "0.0.2.3") && Contains(message, "0.0.1.3"));
}

// The text never speaks of building or generating code.
void TestNoDeveloperWording() {
  const GameFiles states[] = {GameFiles::kReady, GameFiles::kMissing, GameFiles::kUnsupported,
                              GameFiles::kUpdateUnsupported, GameFiles::kUpdateOnly};
  for (GameFiles state : states) {
    for (GuestVersion guest : {GuestVersion::kBase013, GuestVersion::kTitleUpdate023}) {
      const GameCheck check = Check(state, guest);
      RemoveResult removed;
      removed.removed = true;
      removed.files = check;
      const std::string texts[] = {DescribeGameFilesProblem(check), DescribePlayCard(check).title,
                                   DescribePlayCard(check).detail, DescribeRemoved(removed).text,
                                   DescribeLoadedVersionMismatch(guest, "0.0.1.5")};
      for (const std::string& text : texts) {
        for (const char* word : {"codegen", "recompile", "build", "SSF2T_GAME_BASE", "mismatch", "generated"}) {
          CHECK(!Contains(text, word));
        }
      }
    }
  }
}

// The supported update as a check result: ready, runnable with the update image, never "different" without it.
void TestHostCapabilityIsNotFileValidity() {
  const GameCheck update = Check(GameFiles::kReady, GuestVersion::kTitleUpdate023);
  CHECK(SelectGuest(update, HostGuests{true}) == GuestVersion::kTitleUpdate023);
  CHECK(!SelectGuest(update, HostGuests{false}));
  CHECK(SelectGuest(Check(GameFiles::kReady), HostGuests{false}) == GuestVersion::kBase013);
  CHECK(DescribePlayCard(update, HostGuests{false}).title != DescribePlayCard(Check(GameFiles::kUpdateUnsupported)).title);
  CHECK(!DescribeGameFilesProblem(update, HostGuests{false}).empty());
  CHECK(DescribeGameFilesProblem(update, HostGuests{true}).empty());
  CHECK(DescribeGameButtons(update, HostGuests{false}).update_available);
  CHECK(!DescribeGameButtons(Check(GameFiles::kReady), HostGuests{false}).update_available);
  CHECK(DescribeGameButtons(Check(GameFiles::kReady), HostGuests{true}).update_available);
  CHECK(!NeedsOtherGuest(update, GuestVersion::kBase013, HostGuests{false}));
}

void TestWrongKindMessages() {
  CHECK(DescribeInstall(InstallStatus::kSelectedTitleUpdate).title == "This is a Title Update package");
  CHECK(Contains(DescribeInstall(InstallStatus::kSelectedTitleUpdate).text, "Install Title Update"));
  CHECK(DescribeInstall(InstallStatus::kSelectedGamePackage).title == "This is the Xbox Live Arcade game package");
  CHECK(Contains(DescribeInstall(InstallStatus::kSelectedGamePackage).text, "Install Game"));
}

void TestRemovedMessages() {
  RemoveResult not_installed;
  CHECK(DescribeRemoved(not_installed).title == "Title Update #1 not installed");

  RemoveResult back_to_base;
  back_to_base.removed = true;
  back_to_base.files.state = GameFiles::kReady;
  CHECK(DescribeRemoved(back_to_base).title == "Title Update #1 removed");
  CHECK(Contains(DescribeRemoved(back_to_base).text, "Install Title Update"));
  CHECK(Contains(DescribeRemoved(back_to_base).text, kXexVersion));

  RemoveResult nothing_left;
  nothing_left.removed = true;
  nothing_left.files.state = GameFiles::kMissing;
  CHECK(DescribeRemoved(nothing_left).title == "Title Update #1 removed");
}

uint32_t Be32(const unsigned char* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

// The first STFS package of the given content type for this title, found up to three folders below `game`.
fs::path FindPackage(const fs::path& game, uint32_t content_type) {
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(game, ec); !ec && it != fs::recursive_directory_iterator();
       it.increment(ec)) {
    if (it.depth() >= 3) it.disable_recursion_pending();
    if (!it->is_regular_file(ec)) continue;
    unsigned char head[0x364] = {};
    std::ifstream in(it->path(), std::ios::binary);
    if (in.read(reinterpret_cast<char*>(head), sizeof(head)) && std::memcmp(head, "LIVE", 4) == 0 &&
        Be32(head + 0x344) == content_type && Be32(head + 0x360) == kTitleId) {
      return it->path();
    }
  }
  return {};
}

fs::path FindTitleUpdatePackage(const fs::path& game) { return FindPackage(game, kTitleUpdateContentType); }
fs::path FindGamePackage(const fs::path& game) { return FindPackage(game, kGameContentType); }

void TestTitleUpdateInstall(const fs::path& package) {
  const fs::path root = MakeDir("install");
  const fs::path game = root / "game";
  InstallResult result = InstallFromPackage(package, game, InstallKind::kTitleUpdate);
  CHECK(result.ok());
  CHECK(result.title_update && result.files == 1);
  CHECK(HasEntry(game, kTitleUpdateFile));
  CHECK(!fs::exists(root / "game-install-temp"));
  // STATE 3: the Title Update installed alone, with the base game still missing.
  const GameCheck update_only = CheckGameFiles(game);
  CHECK(update_only.state == GameFiles::kUpdateOnly);
  CHECK(update_only.xexp_version == kTitleUpdateVersion);
  CHECK(update_only.xexp_crc32 == kXexpCrc32);

  // A stale default.xexp is replaced and nothing else in the folder is touched.
  std::ofstream(game / kTitleUpdateFile, std::ios::binary | std::ios::trunc) << "stale";
  std::ofstream(game / kAssetArchive, std::ios::binary) << "x";
  result = InstallFromPackage(package, game, InstallKind::kTitleUpdate);
  CHECK(result.ok());
  CHECK(fs::file_size(game / kTitleUpdateFile) == kXexpSize);
  CHECK(fs::file_size(game / kAssetArchive) == 1);
  CHECK(!HasEntry(game, "default.xexp.previous"));
  fs::remove_all(root);
}

// STATE 2 then STATE 4: the base game installed alone, then completed with the Title Update.
void TestBaseThenTitleUpdate(const fs::path& game_package, const fs::path& update_package) {
  const fs::path root = MakeDir("base_then_update");
  const fs::path game = root / "game";
  InstallResult base = InstallFromPackage(game_package, game, InstallKind::kGame);
  CHECK(base.ok() && !base.title_update);
  CHECK(CheckGameFiles(game).state == GameFiles::kReady);
  CHECK(CheckGameFiles(game).guest == GuestVersion::kBase013);

  InstallResult update = InstallFromPackage(update_package, game, InstallKind::kTitleUpdate);
  CHECK(update.ok() && update.title_update);
  CHECK(CheckGameFiles(game).state == GameFiles::kReady);
  CHECK(CheckGameFiles(game).guest == GuestVersion::kTitleUpdate023);
  fs::remove_all(root);
}

// STATE 3 then STATE 4, then a base game reinstall that must keep the installed Title Update.
void TestTitleUpdateThenBaseAndReinstall(const fs::path& game_package, const fs::path& update_package) {
  const fs::path root = MakeDir("update_then_base");
  const fs::path game = root / "game";
  InstallResult update = InstallFromPackage(update_package, game, InstallKind::kTitleUpdate);
  CHECK(update.ok() && update.title_update);
  CHECK(CheckGameFiles(game).state == GameFiles::kUpdateOnly);

  InstallResult base = InstallFromPackage(game_package, game, InstallKind::kGame);
  CHECK(base.ok() && !base.title_update);
  CHECK(CheckGameFiles(game).state == GameFiles::kReady);

  InstallResult reinstall = InstallFromPackage(game_package, game, InstallKind::kGame);
  CHECK(reinstall.ok());
  CHECK(CheckGameFiles(game).state == GameFiles::kReady);
  fs::remove_all(root);
}

// Install Game must reject a Title Update package, and Install Title Update must reject the base game package,
// without touching whatever was already installed.
void TestWrongKindRejected(const fs::path& game_package, const fs::path& update_package) {
  const fs::path root = MakeDir("wrong_kind");
  const fs::path game = root / "game";
  InstallResult base = InstallFromPackage(game_package, game, InstallKind::kGame);
  CHECK(base.ok());
  CHECK(CheckGameFiles(game).state == GameFiles::kReady && CheckGameFiles(game).guest == GuestVersion::kBase013);

  InstallResult update_as_game = InstallFromPackage(update_package, game, InstallKind::kGame);
  CHECK(update_as_game.status == InstallStatus::kSelectedTitleUpdate);
  CHECK(CheckGameFiles(game).state == GameFiles::kReady && CheckGameFiles(game).guest == GuestVersion::kBase013);

  InstallResult game_as_update = InstallFromPackage(game_package, game, InstallKind::kTitleUpdate);
  CHECK(game_as_update.status == InstallStatus::kSelectedGamePackage);
  CHECK(CheckGameFiles(game).state == GameFiles::kReady && CheckGameFiles(game).guest == GuestVersion::kBase013);
  fs::remove_all(root);
}

// Remove Title Update from the Ready state must delete only default.xexp, leaving the base game and its assets,
// and must fall back to the base game's own revision.
void TestRemoveTitleUpdateFromReady(const fs::path& game_package, const fs::path& update_package) {
  const fs::path root = MakeDir("remove_from_ready");
  const fs::path game = root / "game";
  InstallFromPackage(game_package, game, InstallKind::kGame);
  InstallFromPackage(update_package, game, InstallKind::kTitleUpdate);
  CHECK(CheckGameFiles(game).state == GameFiles::kReady);

  const RemoveResult result = RemoveTitleUpdate(game);
  CHECK(result.removed);
  CHECK(!HasEntry(game, kTitleUpdateFile));
  CHECK(HasEntry(game, "default.xex"));
  CHECK(HasEntry(game, kAssetArchive));
  CHECK(result.files.state == GameFiles::kReady && result.files.guest == GuestVersion::kBase013);
  CHECK(result.files.xex_version == kXexVersion);
  fs::remove_all(root);
}

// Remove Title Update from the Title-Update-only state must fall back to the normal missing-game state.
void TestRemoveTitleUpdateFromUpdateOnly(const fs::path& update_package) {
  const fs::path root = MakeDir("remove_from_update_only");
  const fs::path game = root / "game";
  InstallFromPackage(update_package, game, InstallKind::kTitleUpdate);
  CHECK(CheckGameFiles(game).state == GameFiles::kUpdateOnly);

  const RemoveResult result = RemoveTitleUpdate(game);
  CHECK(result.removed);
  CHECK(result.files.state == GameFiles::kMissing);
  CHECK(!fs::exists(game / kTitleUpdateFile));
  fs::remove_all(root);
}

void TestRemoveTitleUpdateNoOp() {
  const fs::path root = MakeDir("remove_noop");
  const fs::path game = root / "game";
  std::error_code ec;
  fs::create_directories(game, ec);
  const RemoveResult result = RemoveTitleUpdate(game);
  CHECK(!result.removed);
  CHECK(result.files.state == GameFiles::kMissing);
  fs::remove_all(root);
}

}  // namespace

int main(int argc, char** argv) {
  fs::path xex = fs::path(SSF2T_SOURCE_DIR) / "game" / "extracted" / "default.xex";
  if (argc > 1) xex = argv[1];
  const fs::path xexp = xex.parent_path() / kTitleUpdateFile;
  std::error_code ec;
  const bool have_xex = fs::exists(xex, ec);
  const bool have_xexp = fs::exists(xexp, ec);
  TestUnsupportedXexStillRejected();
  TestInstallMessages();
  TestPlayCardText();
  TestGameButtons();
  TestGuestSelection();
  TestHostCapabilityIsNotFileValidity();
  TestNoDeveloperWording();
  TestWrongKindMessages();
  TestRemovedMessages();
  TestRemoveTitleUpdateNoOp();
  TestExecutableVersion(have_xex ? xex : fs::path(), have_xexp ? xexp : fs::path());
  if (have_xex) {
    TestMixedCaseSupportedXexIsNormalized(xex);
    TestTitleUpdateIsOptional(xex, have_xexp ? xexp : fs::path());
  } else {
    std::printf("skipped normalization: no default.xex at %s\n", xex.string().c_str());
  }
  const fs::path game_dir = fs::path(SSF2T_SOURCE_DIR) / "game";
  const fs::path update_package = FindTitleUpdatePackage(game_dir);
  if (!update_package.empty()) {
    TestTitleUpdateInstall(update_package);
  } else {
    std::printf("skipped Title Update install: no Title Update package under game\n");
  }
  const fs::path game_package = FindGamePackage(game_dir);
  if (!game_package.empty() && !update_package.empty()) {
    TestBaseThenTitleUpdate(game_package, update_package);
    TestTitleUpdateThenBaseAndReinstall(game_package, update_package);
    TestWrongKindRejected(game_package, update_package);
    TestRemoveTitleUpdateFromReady(game_package, update_package);
  } else {
    std::printf("skipped install ordering: no game and Title Update package pair under game\n");
  }
  if (!update_package.empty()) {
    TestRemoveTitleUpdateFromUpdateOnly(update_package);
  }
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
