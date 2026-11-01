// The Add a friend field in a real ImGui context, headless, fed the key events the SDK's window glue sends (LeftCtrl and
// a hand-set io.KeyCtrl, never ImGuiMod_Ctrl): Ctrl+V reads the clipboard reader directly, typing works, Copy writes the
// exact code, and the diagnostics are bounded. The clipboard is a fake; on Windows the Win32 backend also
// runs end to end over an in-memory stand-in for the Win32 calls.
// Run: ssf2t_friend_field_tests

#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include "friend_code_field.h"
#include "online/protocol.h"

#ifdef _WIN32
#include "clipboard_win32.h"
#include "fake_win32_clipboard.h"
#endif

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

ClipboardText g_clipboard;
int g_reads = 0;
std::optional<std::string> g_written;

ClipboardText FakeRead(const FieldLog&) {
  ++g_reads;
  return g_clipboard;
}

bool FakeWrite(const std::string& text, const FieldLog&) {
  g_written = text;
  return true;
}

bool FailingWrite(const std::string&, const FieldLog& log) {
  log("online clipboard open failed attempt=3 error=5");
  return false;
}

void SetClipboard(const char* text) { g_clipboard = {ClipboardText::Kind::kText, text}; }

class Harness {
 public:
  explicit Harness(ClipboardReader read = FakeRead) : field_(std::move(read)) {
    context_ = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(800, 600);
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    g_reads = 0;
    Frame();
  }
  ~Harness() { ImGui::DestroyContext(context_); }

  void Frame() {
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(700, 200));
    ImGui::Begin("page");
    ImGui::Button("elsewhere");
    other_min_ = ImGui::GetItemRectMin();
    other_max_ = ImGui::GetItemRectMax();
    field_.Draw(buffer, sizeof(buffer), [this](const std::string& line) { logs.push_back(line); });
    field_min_ = ImGui::GetItemRectMin();
    field_max_ = ImGui::GetItemRectMax();
    ImGui::End();
    ImGui::Render();
  }

  void Frames(int count) {
    for (int i = 0; i < count; ++i) Frame();
  }

  void ClickAt(ImVec2 min, ImVec2 max) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    Frames(2);
    io.AddMouseButtonEvent(0, true);
    Frames(2);
    io.AddMouseButtonEvent(0, false);
    Frames(2);
  }
  void ClickField() { ClickAt(field_min_, field_max_); }
  void ClickElsewhere() { ClickAt(other_min_, other_max_); }

  // What rex::ui::ImGuiDrawer::OnKey does for the Ctrl key.
  void Ctrl(bool down) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiKey_LeftCtrl, down);
    io.KeyCtrl = down;
    Frames(2);
  }

  void CtrlV() {
    Ctrl(true);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_V, true);
    Frames(2);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_V, false);
    Frames(2);
    Ctrl(false);
  }

  void Type(const char* text) {
    ImGui::GetIO().AddInputCharactersUTF8(text);
    Frames(3);
  }

  int Count(const std::string& part) const {
    int n = 0;
    for (const std::string& line : logs) n += line.find(part) != std::string::npos ? 1 : 0;
    return n;
  }

  char buffer[kFriendCodeFieldSize] = {};
  std::vector<std::string> logs;

 private:
  ImGuiContext* context_ = nullptr;
  FriendCodeField field_;
  ImVec2 field_min_, field_max_, other_min_, other_max_;
};

std::string Paste(const char* clipboard) {
  Harness h;
  SetClipboard(clipboard);
  h.ClickField();
  h.CtrlV();
  return h.buffer;
}

void TestTheSdkKeyEventsNeverSetImGuisCtrl() {
  Harness h;
  h.Ctrl(true);
  CHECK(ImGui::IsKeyDown(ImGuiKey_LeftCtrl));
  CHECK(!ImGui::GetIO().KeyCtrl);
  CHECK(!ImGui::IsKeyDown(ImGuiMod_Ctrl));
  h.Ctrl(false);
}

void TestCtrlVWhileFocusedReadsTheClipboard() {
  Harness h;
  SetClipboard("CEQN-Z7SE");
  h.ClickField();
  h.CtrlV();
  CHECK(g_reads == 1);
  CHECK(std::string(h.buffer) == "CEQN-Z7SE");
  CHECK(online::NormalizeFriendCode(h.buffer) == "CEQN-Z7SE");
  CHECK(h.Count("online friend ctrl-v detected left_ctrl=1 right_ctrl=0") == 1);
  CHECK(h.Count("online friend paste raw=CEQN-Z7SE") == 1);
  CHECK(h.Count("online friend paste normalized=CEQN-Z7SE") == 1);
  CHECK(h.Count("online friend field value=CEQN-Z7SE") == 1);
}

void TestCtrlVIsIgnoredWhenTheFieldIsNotFocused() {
  Harness h;
  SetClipboard("CEQN-Z7SE");
  h.CtrlV();
  CHECK(g_reads == 0 && h.buffer[0] == 0);
  h.ClickField();
  h.ClickElsewhere();
  h.CtrlV();
  CHECK(g_reads == 0 && h.buffer[0] == 0);
  CHECK(h.Count("ctrl-v detected") == 0);
}

void TestPastedFormsAreNormalized() {
  for (const char* clipboard : {"CEQN-Z7SE", "ceqn-z7se", "CEQNZ7SE", "ceqnz7se", " CEQN-Z7SE ", "CEQN-Z7SE\r\n",
                                "\tceqn z7se\n"}) {
    CHECK(Paste(clipboard) == "CEQN-Z7SE");
  }
  CHECK(PastedFriendCode("CEQN-Z7SE\r\n") == "CEQN-Z7SE");
  CHECK(PastedFriendCode("abcd-1234") == "ABCD-1234");
  CHECK(online::NormalizeFriendCode(PastedFriendCode("abcd-1234")).empty());
}

void TestPasteReplacesWhatWasTyped() {
  Harness h;
  SetClipboard("sxff8p9t");
  h.ClickField();
  h.Type("ff");
  CHECK(std::string(h.buffer) == "FF");
  h.CtrlV();
  CHECK(std::string(h.buffer) == "SXFF-8P9T");
}

void TestEmptyClipboardAndErrorsLeaveTheField() {
  Harness h;
  h.ClickField();
  h.Type("ab");
  g_clipboard = {ClipboardText::Kind::kEmpty, {}};
  h.CtrlV();
  CHECK(std::string(h.buffer) == "AB");
  CHECK(h.Count("online friend paste empty") == 1);
  g_clipboard = {ClipboardText::Kind::kError, "open"};
  h.CtrlV();
  CHECK(std::string(h.buffer) == "AB");
  CHECK(h.Count("online friend paste failed step=open") == 1);
  CHECK(h.Count("paste normalized=") == 0);
}

void TestTypingStillWorks() {
  Harness h;
  h.ClickField();
  h.Type("ceqn-z7se");
  CHECK(std::string(h.buffer) == "CEQN-Z7SE");
  CHECK(h.Count("online friend field focused") == 1);
  CHECK(h.Count("online friend input changed value=") >= 1);
}

void TestValidPasteEnablesSendAndInvalidDoesNot() {
  CHECK(!online::NormalizeFriendCode(Paste("ceqn z7se\r\n")).empty());
  CHECK(online::NormalizeFriendCode(Paste("CEQN")).empty());
  CHECK(online::NormalizeFriendCode(Paste("ABCD-1234")).empty());
}

void TestCopyWritesTheExactCode() {
  std::vector<std::string> logs;
  const auto log = [&logs](const std::string& line) { logs.push_back(line); };
  g_written.reset();
  CHECK(CopyFriendCode("SXFF-8P9T", FakeWrite, log));
  CHECK(g_written && *g_written == "SXFF-8P9T");
  CHECK((logs == std::vector<std::string>{"online friend copy clicked code=SXFF-8P9T",
                                          "online friend copy success code=SXFF-8P9T"}));
  logs.clear();
  CHECK(!CopyFriendCode("SXFF-8P9T", FailingWrite, log));
  CHECK(logs.size() == 3 && logs[2] == "online friend copy failed code=SXFF-8P9T");

  SetClipboard(g_written->c_str());
  CHECK(Paste(g_written->c_str()) == "SXFF-8P9T");
}

void TestDiagnosticsAreBounded() {
  Harness h;
  SetClipboard("CEQN-Z7SE");
  h.ClickField();
  h.Frames(30);
  h.CtrlV();
  h.Frames(30);
  CHECK(h.logs.size() <= 6);
}

#ifdef _WIN32
void TestWin32CopyOnOneInstanceThenPasteOnAnother() {
  fake_clipboard::Reset();
  std::vector<std::string> a_logs;
  const auto a_log = [&a_logs](const std::string& line) { a_logs.push_back(line); };
  const auto write = [](const std::string& text, const FieldLog& log) {
    return WriteClipboardText(fake_clipboard::Api(), nullptr, text, log);
  };
  CHECK(CopyFriendCode("CEQN-Z7SE", write, a_log));
  CHECK(fake_clipboard::Content() == L"CEQN-Z7SE");
  CHECK(a_logs.front() == "online friend copy clicked code=CEQN-Z7SE");
  CHECK(a_logs.back() == "online friend copy success code=CEQN-Z7SE");
  for (const std::string& line : a_logs) CHECK(line.find("token") == std::string::npos);

  Harness b([](const FieldLog& log) { return ReadClipboardText(fake_clipboard::Api(), nullptr, log); });
  b.ClickField();
  b.CtrlV();
  CHECK(std::string(b.buffer) == "CEQN-Z7SE");
  CHECK(!online::NormalizeFriendCode(b.buffer).empty());
  CHECK(b.Count("online clipboard read begin backend=win32") == 1);
  CHECK(b.Count("online clipboard format CF_UNICODETEXT available=1") == 1);
  CHECK(b.Count("online clipboard read chars=9") == 1);
  CHECK(b.Count("online friend paste normalized=CEQN-Z7SE") == 1);

  fake_clipboard::Get().open_failures = 3;
  Harness busy([](const FieldLog& log) { return ReadClipboardText(fake_clipboard::Api(), nullptr, log); });
  busy.ClickField();
  busy.Type("ce");
  busy.CtrlV();
  CHECK(std::string(busy.buffer) == "CE");
  CHECK(busy.Count("online friend paste failed step=open") == 1);
}
#endif

}  // namespace

int main() {
  IMGUI_CHECKVERSION();
  std::printf("friend field tests\n");
  TestTheSdkKeyEventsNeverSetImGuisCtrl();
  TestCtrlVWhileFocusedReadsTheClipboard();
  TestCtrlVIsIgnoredWhenTheFieldIsNotFocused();
  TestPastedFormsAreNormalized();
  TestPasteReplacesWhatWasTyped();
  TestEmptyClipboardAndErrorsLeaveTheField();
  TestTypingStillWorks();
  TestValidPasteEnablesSendAndInvalidDoesNot();
  TestCopyWritesTheExactCode();
  TestDiagnosticsAreBounded();
#ifdef _WIN32
  TestWin32CopyOnOneInstanceThenPasteOnAnother();
#endif
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
