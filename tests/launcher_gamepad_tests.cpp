// Launcher and overlay controller reading (issue #9): on Windows XInput is polled synchronously, with no SDL instance
// of the program's own, no thread and no hidden window; empty XInput slots are retried once a second. The slot scan
// is modelled directly; the platform split is read from the sources.
// Run: ssf2t_launcher_gamepad_tests

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <type_traits>

#include "pad_slot_scan.h"

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

using ssf2t::PadSlotScan;

std::string ReadSource(const char* name) {
  std::ifstream in(std::string(SSF2T_SRC_DIR) + "/" + name, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  std::string source = text.str();
  for (size_t at = source.find('\r'); at != std::string::npos; at = source.find('\r', at)) {
    source.erase(at, 1);
  }
  return source;
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

std::string Between(const std::string& source, const char* from, const char* to) {
  const size_t start = source.find(from);
  if (start == std::string::npos) {
    return {};
  }
  const size_t end = source.find(to, start + 1);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

struct FakePads {
  bool present[PadSlotScan::kSlots] = {};
  int reads[PadSlotScan::kSlots] = {};

  int total() const { return reads[0] + reads[1] + reads[2] + reads[3]; }
  auto reader() {
    return [this](int slot) {
      ++reads[slot];
      return present[slot];
    };
  }
};

void TestIdleWithoutPadsRescansOncePerSecond() {
  std::printf("idle with no controller reads each empty slot once a second\n");
  PadSlotScan scan;
  FakePads pads;
  constexpr int kFrames = 60 * 600;
  for (int frame = 0; frame < kFrames; ++frame) {
    scan.Poll(frame / 60.0, pads.reader());
  }
  CHECK(pads.total() == 4 * 600);
  for (int slot = 0; slot < PadSlotScan::kSlots; ++slot) {
    CHECK(!scan.connected(slot));
    CHECK(pads.reads[slot] == 600);
  }
  CHECK(std::is_trivially_copyable_v<PadSlotScan>);
  CHECK(sizeof(PadSlotScan) <= 32);
}

void TestConnectedPadIsReadEveryFrame() {
  std::printf("a connected controller is read every frame, empty slots once a second\n");
  PadSlotScan scan;
  FakePads pads;
  pads.present[1] = true;
  for (int frame = 0; frame < 120; ++frame) {
    scan.Poll(frame / 60.0, pads.reader());
  }
  CHECK(scan.connected(1));
  CHECK(pads.reads[1] == 120);
  CHECK(pads.reads[0] == 2);
  CHECK(pads.reads[3] == 2);
}

void TestUnplugAndReplug() {
  std::printf("an unplugged controller stops being read until the next rescan finds it\n");
  PadSlotScan scan;
  FakePads pads;
  pads.present[0] = true;
  scan.Poll(0.0, pads.reader());
  CHECK(scan.connected(0));
  pads.present[0] = false;
  scan.Poll(0.1, pads.reader());
  CHECK(!scan.connected(0));
  const int after_unplug = pads.reads[0];
  pads.present[0] = true;
  scan.Poll(0.5, pads.reader());
  CHECK(pads.reads[0] == after_unplug);
  CHECK(!scan.connected(0));
  scan.Poll(1.0, pads.reader());
  CHECK(scan.connected(0));
}

void TestResetRescansAtOnce() {
  std::printf("Release forgets the controllers and the next open looks again at once\n");
  PadSlotScan scan;
  FakePads pads;
  pads.present[2] = true;
  scan.Poll(5.0, pads.reader());
  scan.Reset();
  CHECK(!scan.connected(2));
  scan.Poll(5.2, pads.reader());
  CHECK(scan.connected(2));
  CHECK(pads.reads[0] == 2);
}

void TestWindowsUsesXInputOnly() {
  std::printf("Windows reads controllers with XInput and starts no SDL of its own\n");
  const std::string source = ReadSource("imgui_gamepad.cpp");
  const std::string header = ReadSource("imgui_gamepad.h");

  const std::string includes = Between(source, "#if defined(_WIN32)", "namespace ssf2t {");
  const std::string windows_includes = Between(includes, "#if defined(_WIN32)", "#else");
  CHECK(Has(windows_includes, "<xinput.h>"));
  CHECK(!Has(windows_includes, "SDL"));
  CHECK(Has(Between(includes, "#else", "#endif"), "<SDL3/SDL.h>"));

  const std::string header_windows = Between(header, "#if defined(_WIN32)", "#else");
  CHECK(Has(header_windows, "pad_slot_scan.h"));
  CHECK(!Has(header_windows, "SDL"));
  CHECK(Has(Between(header, "#else", "#endif"), "<SDL3/SDL_gamepad.h>"));

  const std::string helpers = Between(source, "#if defined(_WIN32)\n\nstruct ButtonKey", "#else");
  CHECK(Has(helpers, "\"XInputGetState\""));
  CHECK(Has(helpers, "xinput1_4.dll"));
  CHECK(!Has(helpers, "SDL_"));

  const std::string read = Between(source, "#if defined(_WIN32)\n\nbool ImGuiGamepad::Read(", "#else");
  CHECK(Has(read, "scan_.Poll(ImGui::GetTime()"));
  CHECK(Has(read, "GetPadState("));
  CHECK(!Has(read, "SDL_"));

  CHECK(!Has(source, "SDL_PollEvent"));
  CHECK(Has(Between(source, "#else\n\nbool ImGuiGamepad::Read(", "#endif"), "SDL_InitSubSystem(SDL_INIT_GAMEPAD)"));
  const std::string release = Between(source, "void ImGuiGamepad::Release(", "#if defined(_WIN32)\n\nbool");
  CHECK(Has(Between(release, "#if defined(_WIN32)", "#else"), "scan_.Reset();"));
}

void TestNoBackgroundOwnership() {
  std::printf("controller reading owns no thread, callback, timer or window\n");
  for (const char* name : {"imgui_gamepad.cpp", "imgui_gamepad.h", "pad_slot_scan.h"}) {
    const std::string source = ReadSource(name);
    for (const char* banned : {"std::thread", "std::jthread", "std::async", "CreateThread", "SDL_CreateThread",
                               "condition_variable", "SetTimer", "CreateWindow", "RegisterDeviceNotification",
                               "SDL_AddEventWatch", "Sleep(", "WaitFor"}) {
      if (Has(source, banned)) {
        std::printf("  %s contains %s\n", name, banned);
      }
      CHECK(!Has(source, banned));
    }
  }
}

void TestLauncherStartsNoSdlInput() {
  std::printf("the launcher and overlays reach controllers only through ImGuiGamepad\n");
  for (const char* name : {"launcher.cpp", "modal_dialog.cpp", "modal_dialog.h", "launcher_achievements.cpp"}) {
    const std::string source = ReadSource(name);
    CHECK(!Has(source, "SDL_InitSubSystem"));
    CHECK(!Has(source, "SDL_PollEvent"));
    CHECK(!Has(source, "SDL_OpenGamepad"));
  }
  CHECK(Has(ReadSource("launcher.cpp"), "gamepad_.Update(io);"));
}

}  // namespace

int main() {
  TestIdleWithoutPadsRescansOncePerSecond();
  TestConnectedPadIsReadEveryFrame();
  TestUnplugAndReplug();
  TestResetRescansAtOnce();
  TestWindowsUsesXInputOnly();
  TestNoBackgroundOwnership();
  TestLauncherStartsNoSdlInput();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
