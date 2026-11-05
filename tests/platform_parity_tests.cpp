// Windows and Linux offer the same features: each platform-specific implementation has its counterpart, and source
// that only the other platform compiles is at least lexically sound. Static and headless.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#ifndef SSF2T_SOURCE_DIR
#error SSF2T_SOURCE_DIR must name the repository root
#endif

namespace fs = std::filesystem;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::printf("  PARITY FAIL: %s\n", what.c_str());
  }
}

fs::path Root() { return fs::path(SSF2T_SOURCE_DIR); }

std::string Read(const std::string& relative);

// feature_parity.h kDormantSources: the sources allowed to be built by no target.
std::string DormantList() {
  const std::string registry = Read("src/feature_parity.h");
  const size_t list = registry.find("kDormantSources[]");
  return list == std::string::npos ? std::string() : registry.substr(list, registry.find("};", list) - list);
}

std::string Read(const std::string& relative) {
  std::ifstream in(Root() / relative, std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  std::string out = text.str();
  std::erase(out, '\r');
  return out;
}

bool Has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

std::string Lower(std::string text) {
  for (char& c : text) c = char(std::tolower(uint8_t(c)));
  return text;
}

// From `marker` to the first line that starts with `end` after it.
std::string Section(const std::string& text, const std::string& marker, const std::string& end) {
  const size_t at = text.find(marker);
  if (at == std::string::npos) return {};
  const size_t stop = text.find(end, at + marker.size());
  return text.substr(at, stop == std::string::npos ? std::string::npos : stop - at);
}

// An empty string, or where a string or character literal, or a block comment, is left open.
std::string LexProblem(const std::string& text) {
  int line = 1;
  const auto at = [&](const char* what) { return what + std::string(" at line ") + std::to_string(line); };
  const auto ident = [](char c) { return std::isalnum(uint8_t(c)) || c == '_'; };
  bool line_start = true;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '\n') {
      ++line;
      line_start = true;
      continue;
    }
    if (line_start && c == '#') {
      const size_t end = text.find('\n', i);
      const std::string directive = text.substr(i, end - i);
      if (directive.find("error") != std::string::npos || directive.find("warning") != std::string::npos) {
        i = end - 1;
        continue;
      }
    }
    if (!std::isspace(uint8_t(c))) line_start = false;
    if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
      while (i + 1 < text.size() && text[i + 1] != '\n') ++i;
      continue;
    }
    if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
      const size_t end = text.find("*/", i + 2);
      if (end == std::string::npos) return at("unterminated block comment");
      line += int(std::count(text.begin() + long(i), text.begin() + long(end), '\n'));
      i = end + 1;
      continue;
    }
    if (c == '"' && i > 0 && text[i - 1] == 'R' && (i < 2 || !ident(text[i - 2]) || text[i - 2] == '8' ||
                                                    text[i - 2] == 'u' || text[i - 2] == 'U' || text[i - 2] == 'L')) {
      const size_t open = text.find('(', i);
      const std::string delimiter = ")" + text.substr(i + 1, open - i - 1) + "\"";
      const size_t end = text.find(delimiter, open);
      if (open == std::string::npos || end == std::string::npos) return at("unterminated raw string");
      line += int(std::count(text.begin() + long(i), text.begin() + long(end), '\n'));
      i = end + delimiter.size() - 1;
      continue;
    }
    if (c == '\'' && i > 0 && std::isalnum(uint8_t(text[i - 1]))) {
      size_t start = i;
      while (start > 0 && (ident(text[start - 1]) || text[start - 1] == '\'' || text[start - 1] == '.')) --start;
      if (std::isdigit(uint8_t(text[start]))) continue;  // digit separator
    }
    if (c == '"' || c == '\'') {
      for (++i; i < text.size() && text[i] != c; ++i) {
        if (text[i] == '\\') {
          ++i;
          if (i < text.size() && text[i] == '\n') ++line;
        } else if (text[i] == '\n') {
          return at(c == '"' ? "string literal broken across lines" : "character literal broken across lines");
        }
      }
      if (i >= text.size()) return at("unterminated literal");
    }
  }
  return {};
}

void TestEverySourceFileLexes() {
  int files = 0;
  for (const char* folder : {"src", "tests"}) {
    for (const auto& entry : fs::recursive_directory_iterator(Root() / folder)) {
      const std::string ext = entry.path().extension().string();
      if (!entry.is_regular_file() || (ext != ".cpp" && ext != ".h" && ext != ".c")) continue;
      ++files;
      const std::string relative = fs::relative(entry.path(), Root()).generic_string();
      const std::string problem = LexProblem(Read(relative));
      Check(problem.empty(), relative + ": " + problem + " (this file may only be compiled on the other platform)");
    }
  }
  Check(files > 150, "the source lexer saw too few files");
}

// Settings, guest hooks and in-game hotkeys exist on every platform: none sits inside a platform-only #if branch.
void TestNoFeatureIsPlatformOnly() {
  int features = 0;
  for (const auto& entry : fs::recursive_directory_iterator(Root() / "src")) {
    const std::string ext = entry.path().extension().string();
    if (!entry.is_regular_file() || (ext != ".cpp" && ext != ".h")) continue;
    const std::string relative = fs::relative(entry.path(), Root()).generic_string();
    std::istringstream lines(Read(relative));
    std::vector<bool> platform;  // one entry per open #if: whether it selects a platform
    int n = 0;
    for (std::string line; std::getline(lines, line);) {
      ++n;
      const size_t first = line.find_first_not_of(" \t");
      const std::string code = first == std::string::npos ? std::string() : line.substr(first);
      if (code.starts_with("#if")) {
        platform.push_back(Has(code, "_WIN32") || Has(code, "__linux__") || Has(code, "__unix__"));
      } else if (code.starts_with("#endif") && !platform.empty()) {
        platform.pop_back();
      } else if (code.starts_with("#elif") && !platform.empty()) {
        platform.back() = platform.back() || Has(code, "_WIN32") || Has(code, "__linux__");
      }
      const bool feature = code.starts_with("REXCVAR_DEFINE") || code.starts_with("REX_HOOK_RAW(") ||
                           code.starts_with("SSF2T_NET_HOOK(") || Has(code, "VirtualKey::kF");
      if (!feature) continue;
      ++features;
      const bool platform_only = std::find(platform.begin(), platform.end(), true) != platform.end();
      Check(!platform_only, relative + ":" + std::to_string(n) + " defines a setting, hook or hotkey inside a "
                                                                     "platform-only #if, so the other platform loses it");
    }
  }
  Check(features > 100, "the platform-only feature scan saw too few settings, hooks and hotkeys");
}

void TestWebSocketBackendsMatch() {
  const std::string win = Read("src/online/websocket_win.cpp");
  const std::string posix = Read("src/online/websocket_posix.cpp");
  for (const auto& [name, text] : {std::pair{"Windows (WinHTTP)", &win}, std::pair{"Linux (sockets)", &posix}}) {
    for (const char* part : {"std::unique_ptr<WebSocket> WebSocket::connect(", "bool http_get(", "bool send_text(",
                             "bool send_binary(", "bool receive(std::string&", "void close() override"}) {
      Check(Has(*text, part), std::string(name) + " WebSocket lacks " + part);
    }
  }
  Check(Has(win, "WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE"), "Windows WebSocket sends no binary relay frames");
  Check(Has(posix, "Frame(2, data, size)") && Has(posix, "if (opcode == 2) binary = true;"),
        "Linux relay protocol path lost the binary WebSocket frame");
  Check(Has(posix, "ws::ClientFrame(") && Has(posix, "ws::AcceptFor(key)"),
        "Linux WebSocket no longer uses the framing the wire tests check (src/online/websocket_frame.h)");
  Check(!Has(posix, "#include <openssl/"),
        "Linux WebSocket includes OpenSSL headers: the AppImage would need a build-time OpenSSL and a fixed soname");
  Check(Has(posix, "\"libssl.so.3\"") && Has(posix, "\"libssl.so.1.1\""),
        "Linux WebSocket no longer loads both the OpenSSL 3 and the OpenSSL 1.1 libssl");
  Check(Has(posix, "SSL_set1_host") && Has(posix, "SSL_VERIFY_PEER") && Has(posix, "SSL_CTX_set_default_verify_paths"),
        "Linux wss:// no longer verifies the server certificate and host name like WinHTTP does");
  Check(!fs::exists(Root() / "src/online/websocket_none.cpp"),
        "src/online/websocket_none.cpp is back: a Linux build could ship without online play");
}

void TestCMakeBuildsEveryFeatureOnBothPlatforms() {
  const std::string cmake = Read("CMakeLists.txt");
  const std::string online = Section(cmake, "set(SSF2T_ONLINE_LIBS)", "find_package(Threads REQUIRED)");
  Check(Has(online, "if(WIN32)\n    list(APPEND SSF2T_ONLINE_SOURCES src/online/websocket_win.cpp)"),
        "Windows online sources lost websocket_win.cpp");
  Check(Has(online, "list(APPEND SSF2T_ONLINE_SOURCES src/online/websocket_posix.cpp)"),
        "Linux online sources lost websocket_posix.cpp");
  Check(!Has(online, "find_package(OpenSSL") && !Has(online, "OpenSSL::") && !Has(online, "websocket_none"),
        "Linux online play depends on a build-time OpenSSL again and can be compiled out");
  Check(Has(online, "${CMAKE_DL_LIBS}"), "Linux online sources do not link the dynamic loader for libssl");
  Check(Has(cmake, "list(APPEND SSF2T_SOURCES src/clipboard_win32.cpp)") &&
            Has(cmake, "list(APPEND SSF2T_SOURCES src/clipboard_sdl.cpp)"),
        "one platform lost its clipboard implementation (friend code Copy/Paste)");
  Check(Has(cmake, "rexglue_configure_target(ssf2t_hd_remix GPU_PLUGINS xenos)"),
        "the Xenos GPU plugin (D3D12 on Windows, Vulkan on Linux) is no longer configured for every platform");

  // Every source file is built somewhere: an orphan is a feature one platform silently lost.
  for (const auto& entry : fs::recursive_directory_iterator(Root() / "src")) {
    const std::string ext = entry.path().extension().string();
    if (!entry.is_regular_file() || (ext != ".cpp" && ext != ".c")) continue;
    const std::string relative = fs::relative(entry.path(), Root()).generic_string();
    if (Has(DormantList(), "{\"" + relative + "\", ")) continue;
    Check(Has(cmake, relative), relative + " is not built by any target");
  }
}

void TestPlatformImplementationPairs() {
  const std::string sdl = Read("src/clipboard_sdl.cpp");
  const std::string win = Read("src/clipboard_win32.cpp");
  for (const char* part : {"ClipboardText ReadClipboardText(void*", "bool WriteClipboardText(void*"}) {
    Check(Has(sdl, part), std::string("Linux clipboard lacks ") + part);
    Check(Has(win, part), std::string("Windows clipboard lacks ") + part);
  }

  const std::string platform = Read("src/platform.cpp");
  const std::string backend = Section(platform, "const char* GraphicsBackendName() {", "\n}\n");
  Check(Has(backend, "return \"Direct3D 12\";") && Has(backend, "return \"Vulkan\";"),
        "the graphics backend is no longer named for both Windows (D3D12) and Linux (Vulkan)");

  const std::string config = Read("src/update_config.cpp");
  Check(Has(config, "UpdatePackageKind::kZip") && Has(config, "\"-windows-x64.zip\""),
        "the Windows self-updater no longer installs the release ZIP");
  Check(Has(config, "UpdatePackageKind::kAppImage") && Has(config, "\"-x86_64.AppImage\""),
        "the Linux self-updater no longer installs the release AppImage");
  Check(Has(config, "HasOriginalAppImage(appimage)"), "the Linux self-updater no longer replaces only the running AppImage");
  const std::string controller = Read("src/updater/update_controller.cpp");
  Check(Has(controller, "LaunchUpdateHelper(helper, job_file, error)") &&
            Has(controller, "ApplyAppImageUpdate(download_path_, config_.appimage_path"),
        "one platform lost its self-update install step");
  const std::string updater = Read("src/updater/update_platform.cpp");
  const size_t posix = updater.find("\n#else\n\nstd::vector<std::string> CurrentProcessArguments() {");
  Check(posix != std::string::npos && updater.find("bool ApplyAppImageUpdate(", posix) != std::string::npos &&
            Has(updater.substr(posix), "\".trg-update-\""),
        "ApplyAppImageUpdate has no Linux implementation");

  const std::string relaunch = Read("src/relaunch_command.h");
  Check(Has(relaunch, "appimage && *appimage ? std::string(appimage)"),
        "relaunch no longer restarts the original AppImage on Linux");
  Check(Has(Read("src/launcher.cpp") + Read("src/ssf2t_hd_remix_app.h") + Read("src/platform.cpp"), "MakeRelaunchCommand("),
        "nothing builds the relaunch command");

  const std::string gamepad = Read("src/imgui_gamepad.cpp");
  Check(Has(gamepad, "\"XInputGetState\""), "Windows launcher controllers are no longer read with XInput (issue #9)");
  Check(Has(Section(gamepad, "#else\n\nbool ImGuiGamepad::Read(", "#endif"), "SDL_InitSubSystem(SDL_INIT_GAMEPAD)"),
        "Linux launcher controllers are no longer read with SDL");
}

const char* const kForbiddenNames[] = {"default.xex", "default.xexp", "game", "game-install-temp", "game-install-backup",
                                       "user_data", "user_data.migrating", "content", "cache", "logs",
                                       "settings.toml", "584108a4", "$systemupdate", "thumbs.db", "desktop.ini"};
const char* const kForbiddenTypes[] = {"*.xex", "*.xexp", "*.iso", "*.fpg", "*.log",  "*.dmp",  "*.pdb", "*.debug",
                                       "*.xsh", "*.xpso", "*.bak", "*.tmp", "*.sav",  "*.invalid", "*.pyc", "*.obj",
                                       "*.lib", "*.ilk",  "*.orig", "*.rej", "*.rdc", ".git*", "*~", "settings.toml.*"};

void TestBothPackagesRejectTheSameContent() {
  const std::string windows = Lower(Read("tools/check-release-folder.ps1"));
  const std::string linux_ = Lower(Read("tools/package-linux.sh") + Read("tools/linux-policy.sh"));
  for (const char* name : kForbiddenNames) {
    Check(Has(windows, "'" + std::string(name) + "'"), std::string("Windows package check allows ") + name);
    Check(Has(linux_, " " + std::string(name) + " ") || Has(linux_, "'" + std::string(name) + "'") ||
              Has(linux_, " " + std::string(name) + ")"),
          std::string("Linux package check allows ") + name);
  }
  for (const char* type : kForbiddenTypes) {
    Check(Has(windows, "'" + std::string(type) + "'"), std::string("Windows package check allows ") + type);
    Check(Has(linux_, " " + std::string(type) + " ") || Has(linux_, " " + std::string(type) + ")"),
          std::string("Linux package check allows ") + type);
  }
  for (const char* marker : {"renderdoc", "vklayer", "tracyclient", "debug_saulo"}) {
    Check(Has(windows, marker), std::string("Windows package check does not reject ") + marker);
    Check(Has(linux_, marker), std::string("Linux package check does not reject ") + marker);
  }
  Check(Has(linux_, "rpath/runpath is '$rpath', not \\$origin"), "the AppImage program RPATH/RUNPATH is no longer checked");
  Check(Has(linux_, "live | pirs | \"con \" | xex2"), "the AppImage is no longer scanned for Xbox 360 packages");
  Check(Has(windows, "'live', 'pirs', 'con ', 'xex2'"), "the Windows package is no longer scanned for Xbox 360 packages");
}

void TestOneVersionAndGlibcBaseline() {
  const std::string cmake = Read("CMakeLists.txt");
  std::smatch version;
  Check(std::regex_search(cmake, version, std::regex(R"(project\(ssf2t_hd_remix VERSION (\d+\.\d+\.\d+))")),
        "CMakeLists.txt has no project VERSION");
  Check(Has(cmake, "SSF2T_PORT_VERSION=\"${PROJECT_VERSION}\""), "the program's version does not come from the project VERSION");
  Check(Has(Read("tools/package-release.ps1"), "project\\(\\s*ssf2t_hd_remix\\s+VERSION"),
        "the Windows package name does not come from the project VERSION");
  Check(Has(Read("tools/package-linux.sh"), "project\\([[:space:]]*ssf2t_hd_remix[[:space:]]+VERSION"),
        "the AppImage name does not come from the project VERSION");
  Check(Has(Read("packaging/windows/ssf2t_hd_remix.rc.in"), "@PROJECT_VERSION"),
        "the Windows executable version resource does not come from the project VERSION");
  const std::string config = Read("src/update_config.cpp");
  Check(Has(config, "config.current_version = SSF2T_PORT_VERSION;"), "the updater compares against another version");
  // A version written anywhere else would go stale on the next release.
  if (!version.empty()) {
    const std::string number = version[1];
    for (const auto& entry : fs::recursive_directory_iterator(Root() / "src")) {
      if (!entry.is_regular_file()) continue;
      const std::string relative = fs::relative(entry.path(), Root()).generic_string();
      std::istringstream lines(Read(relative));
      int n = 0;
      for (std::string line; std::getline(lines, line);) {
        ++n;
        const size_t comment = line.find("//");
        const std::string code = line.substr(0, comment);
        Check(!Has(code, "\"" + number + "\"") && !Has(code, "\"v" + number),
              relative + ":" + std::to_string(n) + " writes the version " + number + " instead of SSF2T_PORT_VERSION");
      }
    }
  }
  const std::string linux_ = Read("tools/package-linux.sh") + Read("tools/linux-policy.sh");
  Check(Has(linux_, "MAX_GLIBC=2.31"), "the AppImage glibc baseline is no longer 2.31 (Ubuntu 20.04)");
  Check(Has(linux_, "above the glibc $MAX_GLIBC baseline"), "the AppImage no longer checks the glibc it needs");
  Check(Has(Read("tools/setup-linux-build-env.sh"), "focal"), "the Linux build machine is no longer Ubuntu 20.04");
  Check(fs::exists(Root() / "packaging/linux/AppRun"), "the AppImage AppRun is missing");
  Check(Has(Read("docs/linux.md"), "APPIMAGE_EXTRACT_AND_RUN"), "the FUSE extract-and-run fallback is no longer documented");
}

}  // namespace

int main() {
  TestEverySourceFileLexes();
  TestNoFeatureIsPlatformOnly();
  TestWebSocketBackendsMatch();
  TestCMakeBuildsEveryFeatureOnBothPlatforms();
  TestPlatformImplementationPairs();
  TestBothPackagesRejectTheSameContent();
  TestOneVersionAndGlibcBaseline();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
