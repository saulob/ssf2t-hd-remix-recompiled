// Window-close lifecycle policy: a host window close hard-exits without runtime teardown, while a normal
// guest exit keeps the graceful teardown in OnShutdown. The launcher reaches the game only through Play, and a
// controller drives it only while the program is in front. Read from the sources (the calls need the running
// game and a window).
// Run: ssf2t_lifecycle_tests

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

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

std::string ReadSource(const char* name) {
  std::ifstream in(std::string(SSF2T_SRC_DIR) + "/" + name, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

int Count(const std::string& text, const char* needle) {
  int count = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
    ++count;
  }
  return count;
}

std::string Between(const std::string& source, const char* from, const char* to) {
  const size_t start = source.find(from);
  if (start == std::string::npos) {
    return {};
  }
  const size_t end = source.find(to, start + 1);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

// The text of the member function that starts at `declaration`, up to its column-2 closing brace.
std::string FunctionBody(const std::string& source, const char* declaration) {
  const size_t start = source.find(declaration);
  if (start == std::string::npos) {
    return {};
  }
  const size_t end = source.find("\n  }\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

void TestHostWindowCloseHardExits() {
  std::printf("host window close hard-exits without runtime teardown\n");
  const std::string app = ReadSource("ssf2t_hd_remix_app.h");
  CHECK(Has(app, "void OnClosing(rex::ui::UIEvent&) override"));
  const std::string body = FunctionBody(app, "void OnClosing(");
  CHECK(Has(body, "PrepareHardExit();"));
  CHECK(Has(body, "std::_Exit(0);"));
  CHECK(!Has(body, "ShutdownLogging"));
  CHECK(!Has(body, "TerminateTitle"));
  CHECK(!Has(body, "StopTitleThreads"));
  CHECK(!Has(body, "Shutdown("));
  CHECK(!Has(body, "DetachWindowHost"));
}

void TestNormalExitKeepsTeardown() {
  std::printf("normal guest exit keeps the graceful teardown\n");
  const std::string app = ReadSource("ssf2t_hd_remix_app.h");
  CHECK(Has(app, "void OnShutdown() override"));
  const std::string body = FunctionBody(app, "void OnShutdown(");
  CHECK(Has(body, "input->Shutdown();"));
  CHECK(Has(body, "DetachWindowHost();"));
}

void TestCrashReportsInstalled() {
  std::printf("single-log crash reporting is installed once logging starts\n");
  const std::string app = ReadSource("ssf2t_hd_remix_app.h");
  const std::string post_init = FunctionBody(app, "void OnPostInitLogging(");
  CHECK(Has(post_init, "InstallCrashReports();"));
  CHECK(!Has(FunctionBody(app, "void OnPostSetup("), "InstallCrashReports();"));

  const std::string platform = ReadSource("platform.cpp");
  const std::string install = Between(platform, "void InstallCrashReports(", "UiFontFiles FindUiFonts(");
  CHECK(Has(install, "ActiveLogFile()"));
  CHECK(Has(install, "InstallCrashReporting("));
  CHECK(Has(install, "\"Windows crash reporting enabled for current log.\""));
  CHECK(!Has(platform, "trg::InstallCrashReports("));
  CHECK(!Has(platform, "MiniDump"));

  const std::string header = ReadSource("platform.h");
  CHECK(Has(header, "void InstallCrashReports();"));

  const std::string report = ReadSource("crash_report.cpp");
  CHECK(Has(report, "STATUS_HEAP_CORRUPTION"));
  CHECK(Has(report, "SetUnhandledExceptionFilter("));
  CHECK(!Has(report, "MiniDump"));
}

void TestSessionLogFinalizationWiring() {
  std::printf("clean-session log deletion is wired to the normal and hard exits\n");
  const std::string app = ReadSource("ssf2t_hd_remix_app.h");
  const std::string post_init = Between(app, "void OnPostInitLogging(", "void OnConfigureFonts(");
  CHECK(Has(post_init, "InstallSessionLogCleanup();"));

  const std::string closing =
      Between(app, "void OnClosing(", "std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay");
  CHECK(Has(closing, "DiscardSessionLogBeforeHardExit();"));
  CHECK(Has(closing, "PrepareHardExit();"));
  CHECK(Has(closing, "std::_Exit(0);"));
  CHECK(!Has(closing, "FinalizeSessionLog"));
  CHECK(!Has(closing, "ShutdownLogging"));
  CHECK(!Has(Between(app, "void OnShutdown() override", "void OnClosing("), "FinalizeSessionLog();"));

  const std::string session = ReadSource("session_log.cpp");
  CHECK(Has(session, "ActiveLogFile()"));
  CHECK(Has(session, "DetailedLoggingEnabled()"));
  CHECK(Has(session, "CRASH REPORT"));
  CHECK(Has(session, "std::atexit("));
  CHECK(Has(session, "ShutdownLogging()"));
  CHECK(Has(session, "std::filesystem::remove("));
}

void TestNoSeparateCrashArtifacts() {
  std::printf("no separate crash text or minidump is written\n");
  for (const char* source : {"platform.cpp", "crash_report.cpp", "ssf2t_hd_remix_app.h"}) {
    const std::string text = ReadSource(source);
    CHECK(!Has(text, "MiniDump"));
    CHECK(!Has(text, "crash-"));
    CHECK(!Has(text, ".dmp"));
  }
}

void TestIntentionalExitsWriteNoCrashBlock() {
  std::printf("window close and normal shutdown never write a crash block\n");
  const std::string app = ReadSource("ssf2t_hd_remix_app.h");
  const std::string closing = FunctionBody(app, "void OnClosing(");
  CHECK(Has(closing, "PrepareHardExit();"));
  CHECK(Has(closing, "std::_Exit(0);"));
  CHECK(!Has(closing, "CrashReport"));
  const std::string shutdown = FunctionBody(app, "void OnShutdown(");
  CHECK(!Has(shutdown, "CrashReport"));
}

void TestOnlyPlayStartsTheGame() {
  std::printf("only an explicit Play resumes the runtime; the updater cannot\n");
  const std::string app = ReadSource("ssf2t_hd_remix_app.h");
  CHECK(Count(app, "resume(") == 1);
  CHECK(Has(Between(app, "callbacks.play =", "callbacks.window_changed ="), "resume(defaults);"));

  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Count(launcher, "cb_.play") == 1);
  CHECK(Has(FunctionBody(launcher, "void StartGame("), "Finish(cb_.play);"));
  CHECK(Count(launcher, "StartGame();") == 1);
  CHECK(Has(Between(launcher, "if (starting_ && ImGui::GetTime() - play_started_", "return;"), "StartGame();"));
  CHECK(Count(launcher, "starting_ = true;") == 1);
  CHECK(Has(FunctionBody(launcher, "void RequestPlay("), "starting_ = true;"));
  CHECK(Count(launcher, "RequestPlay();") == 1);
  CHECK(Has(Between(launcher, "case trg::Result::kPlay:", "break;"), "RequestPlay();"));

  const std::string updater = Between(launcher, "void ReleaseUpdater(", "void* WindowHandle()");
  CHECK(!updater.empty());
  for (const char* start : {"cb_.play", "RequestPlay", "StartGame", "starting_"}) {
    CHECK(!Has(updater, start));
  }
  CHECK(Has(FunctionBody(launcher, "void RestartToUpdate("), "Finish(cb_.quit);"));
}

void TestLauncherAchievementsRuntimeOnlyFromItsPage() {
  std::printf("the launcher builds its achievements runtime only on the Achievements page\n");
  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Count(launcher, "std::make_unique<LauncherAchievements>(") == 1);
  CHECK(Has(FunctionBody(launcher, "void PageAchievements("), "std::make_unique<LauncherAchievements>("));
}

void TestControllerIgnoredInBackground() {
  std::printf("a controller drives the launcher and overlays only while the program is in front\n");
  const std::string gamepad = ReadSource("imgui_gamepad.cpp");
  const std::string foreground = Between(gamepad, "bool ProgramInForeground()", "}  // namespace");
  CHECK(Has(foreground, "GetForegroundWindow()"));
  CHECK(Has(foreground, "GetCurrentProcessId()"));

  const std::string update = Between(gamepad, "void ImGuiGamepad::Update(", "void ImGuiGamepad::Release(");
  const size_t gate = update.find("if (!ProgramInForeground()) {");
  const size_t feed = update.find("io.AddKeyEvent(kButtons[i].key");
  CHECK(feed != std::string::npos);
  CHECK(gate < feed);
  const std::string ignored = gate < feed ? update.substr(gate, feed - gate) : std::string();
  CHECK(Has(ignored, "down = {};"));
  CHECK(Has(ignored, "amount = {};"));
}

}  // namespace

int main() {
  TestHostWindowCloseHardExits();
  TestNormalExitKeepsTeardown();
  TestCrashReportsInstalled();
  TestSessionLogFinalizationWiring();
  TestNoSeparateCrashArtifacts();
  TestIntentionalExitsWriteNoCrashBlock();
  TestOnlyPlayStartsTheGame();
  TestLauncherAchievementsRuntimeOnlyFromItsPage();
  TestControllerIgnoredInBackground();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
