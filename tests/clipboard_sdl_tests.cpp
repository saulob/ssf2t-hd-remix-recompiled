// The Linux clipboard (src/clipboard_sdl.cpp) against SDL's offscreen video driver (the runtime's SDL has no dummy driver):
// no window, no X11, no Wayland, no desktop session. SDL keeps the clipboard text in the video device, so
// writes and reads go through the real production code and SDL's own clipboard. Run: ssf2t_clipboard_sdl_tests

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "clipboard.h"

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

using ssf2t::ClipboardText;

std::vector<std::string> g_logs;
const ssf2t::ClipboardLog kLog = [](const std::string& line) { g_logs.push_back(line); };

ClipboardText Read() { return ssf2t::ReadClipboardText(nullptr, kLog); }
bool Write(const std::string& text) { return ssf2t::WriteClipboardText(nullptr, text, kLog); }

void ExpectText(const std::string& expected) {
  const ClipboardText read = Read();
  CHECK(read.kind == ClipboardText::Kind::kText);
  CHECK(read.text == expected);
}

void TestInitialization() {
  std::printf("offscreen video driver, no window\n");
  CHECK(SDL_Init(SDL_INIT_VIDEO));
  const char* driver = SDL_GetCurrentVideoDriver();
  CHECK(driver != nullptr && std::string(driver) == "offscreen");
  int windows = -1;
  SDL_Window** list = SDL_GetWindows(&windows);
  CHECK(windows == 0);
  SDL_free(list);
}

void TestEmptyClipboard() {
  std::printf("an empty clipboard reads as empty\n");
  SDL_ClearClipboardData();
  const ClipboardText read = Read();
  CHECK(read.kind == ClipboardText::Kind::kEmpty);
  CHECK(read.text.empty());
}

void TestWriteAndRead() {
  std::printf("write then read\n");
  g_logs.clear();
  CHECK(Write("CEQN-Z7SE"));
  ExpectText("CEQN-Z7SE");
  CHECK(!g_logs.empty());
  for (const std::string& line : g_logs) CHECK(line.find("DEBUG_SAULO") == 0);
  char* direct = SDL_GetClipboardText();
  CHECK(direct != nullptr && std::string(direct) == "CEQN-Z7SE");
  SDL_free(direct);
}

void TestUtf8() {
  std::printf("UTF-8 text survives the round trip\n");
  for (const std::string& text : {std::string("plain ASCII 123"), std::string("Ação – naïve café"),
                                  std::string("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"),
                                  std::string("\xF0\x9F\x8E\xAE \xE2\x9C\x93"), std::string("two\nlines\ttabbed")}) {
    CHECK(Write(text));
    ExpectText(text);
  }
}

void TestReplacement() {
  std::printf("a write replaces the previous contents\n");
  CHECK(Write("FIRST-CODE"));
  ExpectText("FIRST-CODE");
  CHECK(Write("SECOND"));
  ExpectText("SECOND");
  CHECK(Write("THIRD-CODE-LONGER-THAN-BEFORE"));
  ExpectText("THIRD-CODE-LONGER-THAN-BEFORE");
  CHECK(Write("x"));
  ExpectText("x");
  ExpectText("x");
}

void TestEmptyString() {
  std::printf("writing an empty string empties the clipboard\n");
  CHECK(Write("something"));
  CHECK(Write(""));
  const ClipboardText read = Read();
  CHECK(read.kind == ClipboardText::Kind::kEmpty);
  CHECK(read.text.empty());
  CHECK(Write("back"));
  ExpectText("back");
}

void TestCleanup() {
  std::printf("after SDL shuts down the clipboard reports failure instead of text\n");
  CHECK(Write("before quit"));
  SDL_Quit();
  CHECK(!Write("after quit"));
  const ClipboardText read = Read();
  CHECK(read.kind != ClipboardText::Kind::kText);
  CHECK(read.text != "before quit");
  CHECK(SDL_Init(SDL_INIT_VIDEO));
  CHECK(Write("again"));
  ExpectText("again");
  SDL_Quit();
}

}  // namespace

int main() {
  unsetenv("DISPLAY");
  unsetenv("WAYLAND_DISPLAY");
  setenv("SDL_VIDEODRIVER", "offscreen", 1);
  TestInitialization();
  TestEmptyClipboard();
  TestWriteAndRead();
  TestUtf8();
  TestReplacement();
  TestEmptyString();
  TestCleanup();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
