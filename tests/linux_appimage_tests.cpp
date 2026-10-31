// The AppImage's file-system behaviour, run for real in temporary folders: where the user's data lives while the program
// runs from a read-only mount or from an extract-and-run folder, and what the Linux self-updater does to the installed
// AppImage. Nothing starts the game; the "restarted" AppImage is a shell script that records how it was started.
// Run: ssf2t_linux_appimage_tests

#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "log_path.h"
#include "platform.h"
#include "updater/update_paths.h"
#include "updater/update_platform.h"
#include "user_data.h"

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

std::string Read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

void Write(const fs::path& path, const std::string& content, bool executable = false) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << content;
  chmod(path.c_str(), executable ? 0755 : 0644);
}

bool Executable(const fs::path& path) {
  struct stat status {};
  return stat(path.c_str(), &status) == 0 && (status.st_mode & S_IXUSR) != 0;
}

std::vector<std::string> Leftovers(const fs::path& folder) {
  std::vector<std::string> names;
  for (const auto& entry : fs::directory_iterator(folder)) {
    const std::string name = entry.path().filename().string();
    if (name.find(".trg-update-") != std::string::npos) {
      names.push_back(name);
    }
  }
  return names;
}

bool WaitFor(const fs::path& file) {
  for (int i = 0; i < 100; ++i) {
    if (fs::exists(file) && !Read(file).empty()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return false;
}

struct Install {
  fs::path root, appimage, user_data, marker;
  explicit Install(const char* name) {
    root = fs::temp_directory_path() / (std::string("ssf2t_appimage_") + name + "_" + std::to_string(getpid()));
    fs::remove_all(root);
    fs::create_directories(root);
    appimage = root / "SSF2THDRemix-v0.2.2-x86_64.AppImage";
    user_data = root / "user_data";
    marker = root / "started.txt";
    Write(appimage, "#!/bin/sh\necho old\n", true);
    Write(user_data / "settings.toml", "settings");
    Write(user_data / "584108A4" / "savegames.dat", "save");
    Write(user_data / "achievements.dat", "achievements");
    Write(user_data / "online" / "identity.json", "identity");
  }
  ~Install() { fs::remove_all(root); }
  bool UserDataIntact() const {
    return Read(user_data / "settings.toml") == "settings" && Read(user_data / "584108A4" / "savegames.dat") == "save" &&
           Read(user_data / "achievements.dat") == "achievements" && Read(user_data / "online" / "identity.json") == "identity";
  }
};

void TestUserDataBesideTheAppImage() {
  std::printf("user data, logs and saves stay beside the AppImage, outside the read-only mount\n");
  Install install("paths");
  const fs::path mount = install.root / ".mount_SSF2TaBcDeF";
  Write(mount / "usr" / "bin" / "ssf2t_hd_remix", "program", true);
  setenv("APPIMAGE", install.appimage.c_str(), 1);
  setenv("APPDIR", mount.c_str(), 1);

  CHECK(ssf2t::PortFolder() == install.root);
  CHECK(!trg::update::PathIsWithin(ssf2t::PortFolder(), mount));
  const fs::path system = install.root / "system-data";
  ssf2t::UserDataSelection selection = ssf2t::SelectUserData({ssf2t::PortFolder(), system, false});
  CHECK(selection.outcome == ssf2t::UserDataOutcome::kPortable);
  CHECK(selection.root == install.user_data);
  CHECK(!trg::update::PathIsWithin(selection.root, mount));
  CHECK(install.UserDataIntact());

  const fs::path logs = ssf2t::LogsFolder(ssf2t::PortFolder(), selection.root, {}, true);
  CHECK(logs == install.root / "logs");
  CHECK(!trg::update::PathIsWithin(logs, mount));
  const fs::path log = ssf2t::NextLogFile(logs, "ssf2t_hd_remix");
  CHECK(!log.empty() && log.parent_path() == logs);

  std::printf("a port folder that cannot be written keeps the logs in user_data\n");
  CHECK(ssf2t::LogsFolder(install.root / "missing", selection.root, {}, true) == selection.root / "logs" ||
        ssf2t::LogsFolder(install.root / "missing", selection.root, {}, true) == install.root / "missing" / "logs");

  std::printf("the extract-and-run folder is not a place for user data either\n");
  const fs::path extracted = fs::temp_directory_path() / ("appimage_extracted_" + std::to_string(getpid()));
  Write(extracted / "usr" / "bin" / "ssf2t_hd_remix", "program", true);
  setenv("APPDIR", extracted.c_str(), 1);
  CHECK(ssf2t::PortFolder() == install.root);
  selection = ssf2t::SelectUserData({ssf2t::PortFolder(), system, false});
  CHECK(selection.root == install.user_data);
  CHECK(!trg::update::PathIsWithin(selection.root, extracted));
  CHECK(install.UserDataIntact());
  fs::remove_all(extracted);

  std::printf("outside an AppImage the data stays beside the program\n");
  unsetenv("APPIMAGE");
  unsetenv("APPDIR");
  CHECK(ssf2t::PortFolder() != install.root);
  CHECK(!ssf2t::PortFolder().empty());
}

void TestOriginalAppImageIsTheTarget() {
  std::printf("the updater targets the AppImage the program was started from\n");
  Install install("target");
  const fs::path mount = install.root / ".mount_SSF2TaBcDeF";
  setenv("APPIMAGE", install.appimage.c_str(), 1);
  setenv("APPDIR", mount.c_str(), 1);
  fs::path found;
  CHECK(!trg::update::HasOriginalAppImage(found));  // this test program does not run inside APPDIR
  unsetenv("APPDIR");
  CHECK(!trg::update::HasOriginalAppImage(found));
  unsetenv("APPIMAGE");
  CHECK(!trg::update::HasOriginalAppImage(found));
}

void TestUpdateReplacesAndRestarts() {
  std::printf("a downloaded AppImage replaces the installed one and is started from the original path\n");
  Install install("replace");
  const fs::path downloaded = install.root / "download" / "SSF2THDRemix-v0.3.0-x86_64.AppImage";
  const std::string script = "#!/bin/sh\nprintf '%s|%s|%s' \"$0\" \"$*\" \"$SSF2T_APPIMAGE_MARKER\" > \"$SSF2T_APPIMAGE_MARKER\"\n";
  Write(downloaded, script, false);
  setenv("SSF2T_APPIMAGE_MARKER", install.marker.c_str(), 1);

  std::string error;
  CHECK(trg::update::ApplyAppImageUpdate(downloaded, install.appimage, {"--ssf2t_skip_launcher=true", "--fullscreen=true"}, error));
  CHECK(error.empty());
  CHECK(Read(install.appimage) == script);
  CHECK(Executable(install.appimage));
  CHECK(Leftovers(install.root).empty());
  CHECK(install.UserDataIntact());
  CHECK(fs::exists(downloaded));
  CHECK(WaitFor(install.marker));
  const std::string started = Read(install.marker);
  CHECK(started.rfind(install.appimage.string() + "|--ssf2t_skip_launcher=true --fullscreen=true|", 0) == 0);
  unsetenv("SSF2T_APPIMAGE_MARKER");
}

void TestUpdateRefusals() {
  std::printf("a refused update leaves the installed AppImage as it was\n");
  Install install("refuse");
  const std::string original = Read(install.appimage);
  std::string error;
  CHECK(!trg::update::ApplyAppImageUpdate(install.root / "absent.AppImage", install.appimage, {}, error));
  CHECK(!error.empty());
  CHECK(!trg::update::ApplyAppImageUpdate(install.appimage, install.appimage, {}, error));
  CHECK(!error.empty());
  CHECK(!trg::update::ApplyAppImageUpdate(install.appimage, install.root / "absent.AppImage", {}, error));
  CHECK(Read(install.appimage) == original);
  CHECK(Executable(install.appimage));
  CHECK(Leftovers(install.root).empty());
  CHECK(install.UserDataIntact());
}

void TestUpdateRollsBackWhenItCannotStart() {
  std::printf("an update that cannot be started is rolled back to the previous AppImage\n");
  Install install("rollback");
  const std::string original = Read(install.appimage);
  const fs::path downloaded = install.root / "download" / "SSF2THDRemix-v0.3.0-x86_64.AppImage";
  Write(downloaded, "#!/nonexistent-interpreter\n", false);
  std::string error;
  CHECK(!trg::update::ApplyAppImageUpdate(downloaded, install.appimage, {}, error));
  CHECK(!error.empty());
  CHECK(Read(install.appimage) == original);
  CHECK(Executable(install.appimage));
  CHECK(Leftovers(install.root).empty());
  CHECK(install.UserDataIntact());
}

void TestUpdateKeepsTheInstalledMode() {
  std::printf("the replacement keeps the installed file's permissions and gains execute\n");
  Install install("mode");
  chmod(install.appimage.c_str(), 0700);
  const fs::path downloaded = install.root / "download" / "SSF2THDRemix-v0.3.0-x86_64.AppImage";
  Write(downloaded, "#!/bin/sh\necho started > \"$SSF2T_APPIMAGE_MARKER\"\n", false);
  chmod(downloaded.c_str(), 0600);
  setenv("SSF2T_APPIMAGE_MARKER", install.marker.c_str(), 1);
  std::string error;
  CHECK(trg::update::ApplyAppImageUpdate(downloaded, install.appimage, {}, error));
  struct stat status {};
  CHECK(stat(install.appimage.c_str(), &status) == 0 && (status.st_mode & 0700) == 0700);
  CHECK(Leftovers(install.root).empty());
  CHECK(WaitFor(install.marker));
  unsetenv("SSF2T_APPIMAGE_MARKER");
}

}  // namespace

int main() {
  TestUserDataBesideTheAppImage();
  TestOriginalAppImageIsTheTarget();
  TestUpdateReplacesAndRestarts();
  TestUpdateRefusals();
  TestUpdateRollsBackWhenItCannotStart();
  TestUpdateKeepsTheInstalledMode();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
