// The ReXGlue console overlay (Backtick) is removed from the game; file logging and crash reports stay.
// Source and documentation checks only; nothing is started.
// Run: ssf2t_console_removed_tests

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

int g_failures = 0;
int g_checks = 0;

void Check(bool condition, const char* what) {
  ++g_checks;
  if (condition) return;
  std::printf("FAIL %s\n", what);
  ++g_failures;
}

std::string ReadText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const std::string& needle) { return text.find(needle) != std::string::npos; }

fs::path Root() { return fs::path(SSF2T_SOURCE_DIR); }

std::vector<fs::path> Files(const char* folder, std::initializer_list<const char*> extensions) {
  std::vector<fs::path> files;
  for (const auto& entry : fs::recursive_directory_iterator(Root() / folder)) {
    if (!entry.is_regular_file()) continue;
    for (const char* extension : extensions) {
      if (entry.path().extension() == extension) files.push_back(entry.path());
    }
  }
  return files;
}

std::string Between(const std::string& text, const char* begin, const char* end) {
  const size_t start = text.find(begin);
  if (start == std::string::npos) return {};
  const size_t stop = text.find(end, start);
  return stop == std::string::npos ? std::string() : text.substr(start, stop - start);
}

void TestConsoleBindIsUnregistered() {
  std::printf("the console bind is removed when the dialogs are created\n");
  const std::string frame_counter = ReadText(Root() / "src/frame_counter.h");
  Check(Has(frame_counter, "kRemovedSdkBinds[] = {\"bind_debug_overlay\", \"bind_console\", \"bind_settings\"}"),
        "bind_console is in the removed SDK binds");
  const std::string app = ReadText(Root() / "src/ssf2t_hd_remix_app.h");
  const std::string dialogs = Between(app, "void OnCreateDialogs(rex::ui::ImGuiDrawer*) override {", "\n  }");
  Check(Has(dialogs, "for (const char* bind : kRemovedSdkBinds) rex::ui::UnregisterBind(bind);"),
        "OnCreateDialogs unregisters every removed SDK bind");
}

void TestNoApplicationConsole() {
  std::printf("no application listener or configuration opens a console\n");
  for (const fs::path& path : Files("src", {".cpp", ".h"})) {
    if (path.filename() == "frame_counter.h") continue;
    const std::string text = ReadText(path);
    const std::string name = path.filename().string();
    Check(!Has(text, "bind_console"), (name + ": no bind_console").c_str());
    Check(!Has(text, "ConsoleDialog") && !Has(text, "console_overlay"), (name + ": no console dialog").c_str());
    Check(!Has(text, "ShowConsole") && !Has(text, "ToggleConsole") && !Has(text, "console_enabled"),
          (name + ": no console toggle").c_str());
    Check(!Has(text, "VirtualKey::kOem3"), (name + ": no listener on the Backtick key").c_str());
  }
  const std::string toml = ReadText(Root() / "config/ssf2t_hd_remix.toml");
  Check(!Has(toml, "bind_console") && !Has(toml, "console"), "the shipped configuration names no console");
}

void TestBacktickIsNotReserved() {
  std::printf("Backtick is no longer reserved\n");
  const std::string keybinds = ReadText(Root() / "src/keybinds.h");
  const std::string reserved = Between(keybinds, "inline bool KeyboardBindingIsReserved(", "\n}");
  Check(!reserved.empty() && !Has(reserved, "GraveAccent"), "the reserved keys do not include Backtick");
  Check(Has(keybinds, "case ImGuiKey_GraveAccent:\n      return \"Backtick\";") ||
            Has(keybinds, "case ImGuiKey_GraveAccent:\r\n      return \"Backtick\";"),
        "Backtick still has a binding name");
  Check(!Has(keybinds, "Backtick console"), "keybinds.h does not describe a Backtick console");
}

void TestDocumentationDoesNotAdvertiseConsole() {
  std::printf("documentation no longer advertises the console\n");
  for (const fs::path& path : Files("docs", {".md"})) {
    Check(!Has(ReadText(path), "Backtick"), (path.filename().string() + ": no Backtick console").c_str());
  }
  Check(!Has(ReadText(Root() / "README.md"), "Backtick"), "README.md: no Backtick console");
  const std::string achievements = ReadText(Root() / "docs/save-and-achievements.md");
  Check(Has(achievements, "Host keys in use: F2 frame counter, F3 ping display, F7 achievements, F11 window mode."),
        "the host key list names the keys still in use");
}

void TestLoggingRemains() {
  std::printf("file logging and crash reports are untouched\n");
  const std::string app = ReadText(Root() / "src/ssf2t_hd_remix_app.h");
  Check(Has(app, "rex::cvar::SetFlagFromCommandLine(\"log_file\""), "the log file is still configured at start");
  Check(Has(app, "rex::cvar::SetFlagByName(\"log_file\""), "the AppImage log file is still configured");
  Check(Has(app, "void OnPostInitLogging() override {"), "the logging hook is kept");
  Check(Has(app, "InstallCrashReports();"), "crash reports are still installed");
  for (const char* file : {"src/session_log.cpp", "src/active_log.cpp", "src/log_filter.cpp", "src/crash_report.cpp",
                           "src/log_path.cpp"}) {
    Check(fs::exists(Root() / file), file);
  }
  Check(Has(ReadText(Root() / "src/active_log.cpp"), "log_file"), "the active log still follows log_file");
  Check(Has(ReadText(Root() / "src/ssf2t_hd_remix_app.h"), "REXLOG_WARN"), "the application still logs through REXLOG");
}

// Only when the SDK source used to build the runtime is on this machine.
void TestSdkHasOneConsolePath() {
  const fs::path sdk = Root() / "out/rexglue-sdk-src/src/ui/rex_app.cpp";
  if (!fs::exists(sdk)) {
    std::printf("note: the SDK source is not on this machine, its console path is not checked\n");
    return;
  }
  std::printf("the SDK opens its console only through bind_console\n");
  const std::string text = ReadText(sdk);
  size_t creations = 0;
  for (size_t at = text.find("make_unique<ui::ConsoleDialog>"); at != std::string::npos;
       at = text.find("make_unique<ui::ConsoleDialog>", at + 1)) {
    ++creations;
  }
  Check(creations == 1, "the console dialog is created in exactly one place");
  const size_t registered = text.find("RegisterBind(\"bind_console\"");
  const size_t created = text.find("make_unique<ui::ConsoleDialog>");
  const size_t next_bind = text.find("RegisterBind(\"bind_settings\"");
  Check(registered != std::string::npos && created > registered && created < next_bind,
        "that place is the bind_console callback");
  Check(text.find("OnCreateDialogs(imgui_drawer_.get())") > text.find("RegisterBind(\"bind_settings\""),
        "the application's dialog hook runs after the SDK registered its binds");
}

}  // namespace

int main() {
  TestConsoleBindIsUnregistered();
  TestNoApplicationConsole();
  TestBacktickIsNotReserved();
  TestDocumentationDoesNotAdvertiseConsole();
  TestLoggingRemains();
  TestSdkHasOneConsolePath();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
