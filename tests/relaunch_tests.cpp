// Relaunch tests: which program the launcher starts again, and with which arguments. Pure logic and source checks;
// the restart itself needs a running game.
// Run: ssf2t_relaunch_tests

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "relaunch_command.h"

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

std::string ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

using Args = std::vector<std::string>;

constexpr const char* kMountedProgram = "/tmp/.mount_HardCoAbCdEf/usr/bin/ssf2t_hd_remix";
constexpr const char* kAppImage =
    "/home/deck/Pyromancer5/Games/Super Street Fighter II Turbo HD Remix/Ssf2tHdRemixRecompiled-v0.6.1-x86_64.AppImage";
constexpr const char* kSkipLauncher = "--ssf2t_skip_launcher=true";

void TestAppImageSet() {
  std::printf("relaunch from an AppImage\n");
  const auto command = MakeRelaunchCommand({kMountedProgram}, kAppImage, kMountedProgram, kSkipLauncher);
  CHECK(command.has_value());
  CHECK(command->executable == kAppImage);  // the file Steam started, not the closing mount
  CHECK((command->argv == Args{kAppImage, kSkipLauncher}));
  CHECK(!Has(command->executable, ".mount_"));
  CHECK(!Has(command->argv.front(), ".mount_"));
}

void TestAppImageUnset() {
  std::printf("relaunch outside an AppImage\n");
  const std::string program = "/home/user/my games/ssf2t/ssf2t_hd_remix";
  for (const char* appimage : {static_cast<const char*>(nullptr), ""}) {
    const auto command = MakeRelaunchCommand({"./ssf2t_hd_remix"}, appimage, program, kSkipLauncher);
    CHECK(command.has_value());
    CHECK(command->executable == program);
    CHECK((command->argv == Args{program, kSkipLauncher}));
  }
}

void TestArguments() {
  std::printf("relaunch arguments\n");
  const Args original{kMountedProgram, "--user_data_root=/data/with space", "", "--fullscreen=true"};
  const auto command = MakeRelaunchCommand(original, kAppImage, kMountedProgram, kSkipLauncher);
  CHECK(command.has_value());
  CHECK((command->argv ==
         Args{kAppImage, "--user_data_root=/data/with space", "", "--fullscreen=true", kSkipLauncher}));
  CHECK(command->argv.back() == "--ssf2t_skip_launcher=true");

  // A restart that was itself started with the flag keeps it once more at the end; the cvar parser takes the last.
  const auto again = MakeRelaunchCommand({kMountedProgram, kSkipLauncher}, kAppImage, kMountedProgram, kSkipLauncher);
  CHECK(again.has_value());
  CHECK((again->argv == Args{kAppImage, kSkipLauncher, kSkipLauncher}));
}

void TestWorkingDirectory() {
  std::printf("relaunch ignores the working directory\n");
  const auto expected = MakeRelaunchCommand({kMountedProgram}, kAppImage, kMountedProgram, kSkipLauncher);
  std::error_code ec;
  const std::filesystem::path before = std::filesystem::current_path(ec);
  std::filesystem::current_path(std::filesystem::temp_directory_path(), ec);
  const auto elsewhere = MakeRelaunchCommand({kMountedProgram}, kAppImage, kMountedProgram, kSkipLauncher);
  std::filesystem::current_path(before, ec);
  CHECK(expected.has_value() && elsewhere.has_value());
  CHECK(expected->executable == elsewhere->executable);
  CHECK(expected->argv == elsewhere->argv);
  CHECK(elsewhere->executable.front() == '/');
}

void TestUnreadableCommandLine() {
  std::printf("relaunch without a command line\n");
  CHECK(!MakeRelaunchCommand({}, kAppImage, kMountedProgram, kSkipLauncher).has_value());
  CHECK(!MakeRelaunchCommand({"x"}, nullptr, "", kSkipLauncher).has_value());
  CHECK(!MakeRelaunchCommand({}, nullptr, "", kSkipLauncher).has_value());
}

void TestWiring() {
  std::printf("relaunch wiring\n");
  const std::string platform = ReadFile(std::string(SSF2T_SRC_DIR) + "/platform.cpp");
  CHECK(Has(platform, "MakeRelaunchCommand(std::move(args), std::getenv(\"APPIMAGE\")"));
  CHECK(Has(platform, "posix_spawn(&pid, command->executable.c_str()"));  // no shell, no command string
  CHECK(Has(platform, "BeginSessionLogHandoff();"));
  CHECK(Has(platform, "CancelSessionLogHandoff();"));
  CHECK(!Has(platform, "system("));
  CHECK(!Has(platform, "popen("));

  const std::string app = ReadFile(std::string(SSF2T_SRC_DIR) + "/ssf2t_hd_remix_app.h");
  CHECK(Has(app, "RelaunchSelf(L\"--ssf2t_skip_launcher=true\")"));

  const std::string apprun = ReadFile(std::string(SSF2T_SRC_DIR) + "/../packaging/linux/AppRun");
  CHECK(Has(apprun, "HERE=\"$(dirname \"$(readlink -f \"$0\")\")\""));  // not the working directory
  CHECK(Has(apprun, "exec \"$HERE/usr/bin/ssf2t_hd_remix\" \"$@\""));  // quoted, arguments untouched
}

}  // namespace

int main() {
  TestAppImageSet();
  TestAppImageUnset();
  TestArguments();
  TestWorkingDirectory();
  TestUnreadableCommandLine();
  TestWiring();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
