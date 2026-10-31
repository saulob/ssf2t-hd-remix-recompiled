// Where saves, settings, achievements, the online identity and friends live: <port folder>/user_data, which for an
// AppImage is the folder that holds the .AppImage file, never its read-only mount. Real files in a temporary folder.

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "user_data.h"

#ifndef SSF2T_SRC_DIR
#error SSF2T_SRC_DIR must name the src folder
#endif

namespace fs = std::filesystem;
using namespace ssf2t;

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

void Write(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << text;
}

std::string Read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

struct Folders {
  fs::path root;
  fs::path port;
  fs::path system;
  explicit Folders(const char* name) {
    root = fs::temp_directory_path() / ("ssf2t-user-data-tests-" + std::string(name));
    fs::remove_all(root);
    port = root / "port";
    system = root / "system" / "ssf2t_hd_remix";
    fs::create_directories(port);
  }
  ~Folders() {
    std::error_code ec;
    fs::remove_all(root, ec);
  }
};

void TestFreshInstallIsPortable() {
  std::printf("a fresh install keeps everything in <port folder>/user_data\n");
  Folders f("fresh");
  const UserDataSelection s = SelectUserData({f.port, f.system, false});
  CHECK(s.outcome == UserDataOutcome::kPortable);
  CHECK(s.root == f.port / "user_data");
  CHECK(fs::is_directory(f.port / "user_data"));
  CHECK(!fs::exists(f.port / "user_data" / ".write_test"));
  CHECK(!DescribeUserData(s).warning);
}

void TestSystemDataMovesIntoThePortFolder() {
  std::printf("data in the system folder (settings, saves, identity, friends) moves into user_data intact\n");
  Folders f("migrate");
  Write(f.system / "settings.toml", "settings");
  Write(f.system / "584108A4" / "savegames.dat", "save");
  Write(f.system / "online" / "identity.json", "identity");
  Write(f.system / "online" / "friends.json", "friends");
  const UserDataSelection s = SelectUserData({f.port, f.system, false});
  CHECK(s.outcome == UserDataOutcome::kMigrated);
  CHECK(s.root == f.port / "user_data");
  CHECK(Read(s.root / "settings.toml") == "settings");
  CHECK(Read(s.root / "584108A4" / "savegames.dat") == "save");
  CHECK(Read(s.root / "online" / "identity.json") == "identity");
  CHECK(Read(s.root / "online" / "friends.json") == "friends");
  CHECK(!fs::exists(f.system));
}

void TestFailedMoveLeavesTheSystemFolderInUse() {
  std::printf("a migration that cannot copy keeps the system folder untouched and in use\n");
  Folders f("failed");
  Write(f.system / "settings.toml", "settings");
  UserDataFileOps ops;
  ops.rename = [](const fs::path&, const fs::path&, std::error_code& ec) {
    ec = std::make_error_code(std::errc::cross_device_link);
  };
  ops.copy_file = [](const fs::path&, const fs::path&, std::error_code& ec) {
    ec = std::make_error_code(std::errc::no_space_on_device);
  };
  const UserDataSelection s = SelectUserData({f.port, f.system, false}, ops);
  CHECK(s.outcome == UserDataOutcome::kMigrationFailed);
  CHECK(s.root == f.system);
  CHECK(Read(f.system / "settings.toml") == "settings");
  CHECK(!fs::exists(f.port / "user_data.migrating"));
  CHECK(DescribeUserData(s).warning);
}

void TestCrossDeviceMoveCopiesAndVerifies() {
  std::printf("a move across file systems copies, verifies and then removes the old folder\n");
  Folders f("copy");
  Write(f.system / "settings.toml", "settings");
  Write(f.system / "584108A4" / "savegames.dat", "save");
  UserDataFileOps ops;
  int renames = 0;
  ops.rename = [&](const fs::path& from, const fs::path& to, std::error_code& ec) {
    if (renames++ == 0) {
      ec = std::make_error_code(std::errc::cross_device_link);
      return;
    }
    fs::rename(from, to, ec);
  };
  const UserDataSelection s = SelectUserData({f.port, f.system, false}, ops);
  CHECK(s.outcome == UserDataOutcome::kMigrated);
  CHECK(Read(f.port / "user_data" / "584108A4" / "savegames.dat") == "save");
  CHECK(!fs::exists(f.system));
}

void TestBothExistKeepsPortableAndLeavesSystemAlone() {
  std::printf("with data in both places the portable data wins and the system folder is left alone\n");
  Folders f("both");
  Write(f.system / "settings.toml", "old");
  Write(f.port / "user_data" / "settings.toml", "new");
  const UserDataSelection s = SelectUserData({f.port, f.system, false});
  CHECK(s.outcome == UserDataOutcome::kBothExist);
  CHECK(Read(s.root / "settings.toml") == "new");
  CHECK(Read(f.system / "settings.toml") == "old");
}

void TestUnwritablePortFolderFallsBack() {
  std::printf("a port folder that cannot hold user_data falls back to the system folder\n");
  Folders f("readonly");
  Write(f.port / "user_data", "a file where the folder should be");
  const UserDataSelection s = SelectUserData({f.port, f.system, false});
  CHECK(s.outcome == UserDataOutcome::kPortableUnavailable);
  CHECK(s.root == f.system);
  CHECK(Has(UserDataFolderHelp(s.outcome), "not writable"));
}

void TestExplicitRootIsUsedAsGiven() {
  std::printf("--user_data_root is used as given and nothing is migrated\n");
  Folders f("explicit");
  Write(f.system / "settings.toml", "settings");
  const UserDataSelection s = SelectUserData({f.port, f.system, true});
  CHECK(s.outcome == UserDataOutcome::kExplicit);
  CHECK(s.root == f.system);
  CHECK(!fs::exists(f.port / "user_data"));
}

void TestAppImageDataLivesBesideTheAppImage() {
  std::printf("an AppImage keeps user data beside the .AppImage file, never in its read-only mount\n");
  const std::string platform = Read(fs::path(SSF2T_SRC_DIR) / "port_folder.cpp");
  const size_t port = platform.find("std::filesystem::path PortFolder() {");
  CHECK(port != std::string::npos);
  const std::string body = platform.substr(port, platform.find("\n}\n", port) - port);
  CHECK(Has(body, "std::getenv(\"APPIMAGE\")"));
  CHECK(Has(body, "return std::filesystem::path(appimage).parent_path();"));
  CHECK(!Has(body, "APPDIR"));
  const std::string app = Read(fs::path(SSF2T_SRC_DIR) / "ssf2t_hd_remix_app.h");
  CHECK(Has(app, "SelectUserData({port_folder,"));
  CHECK(Has(app, "PortFolder()"));
}

}  // namespace

int main() {
  TestFreshInstallIsPortable();
  TestSystemDataMovesIntoThePortFolder();
  TestFailedMoveLeavesTheSystemFolderInUse();
  TestCrossDeviceMoveCopiesAndVerifies();
  TestBothExistKeepsPortableAndLeavesSystemAlone();
  TestUnwritablePortFolderFallsBack();
  TestExplicitRootIsUsedAsGiven();
  TestAppImageDataLivesBesideTheAppImage();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
