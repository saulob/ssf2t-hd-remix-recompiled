// The Win32 clipboard backend over an in-memory stand-in for the Win32 calls: the write and read sequences, UTF-8 and
// UTF-16, ownership of the clipboard memory, every failure step and its log line. The real Windows clipboard is a
// manual test. Headless.
// Run: ssf2t_clipboard_tests

#include <cstdio>
#include <string>
#include <vector>

#include "clipboard_win32.h"
#include "fake_win32_clipboard.h"
#include "online/protocol.h"

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

const HWND kWindow = reinterpret_cast<HWND>(static_cast<intptr_t>(0x1234));

struct Logs {
  std::vector<std::string> lines;
  ClipboardLog sink() {
    return [this](const std::string& line) { lines.push_back(line); };
  }
  bool Has(const std::string& line) const {
    for (const std::string& l : lines) {
      if (l == line) return true;
    }
    return false;
  }
  int Count(const std::string& part) const {
    int n = 0;
    for (const std::string& l : lines) n += l.find(part) != std::string::npos ? 1 : 0;
    return n;
  }
};

ClipboardText Read(Logs& logs) { return ReadClipboardText(fake_clipboard::Api(), kWindow, logs.sink()); }

void TestWriteThenReadIsExact() {
  fake_clipboard::Reset();
  Logs write;
  CHECK(WriteClipboardText(fake_clipboard::Api(), kWindow, "CEQN-Z7SE", write.sink()));
  CHECK(fake_clipboard::Content() == L"CEQN-Z7SE");
  CHECK(fake_clipboard::Get().owner == kWindow);
  CHECK(fake_clipboard::Get().frees == 0);  // the clipboard owns the memory after SetClipboardData
  CHECK(fake_clipboard::Get().closes == 1 && !fake_clipboard::Get().opened);
  const std::vector<std::string> expected = {
      "DEBUG_SAULO online clipboard copy begin code=CEQN-Z7SE backend=win32",
      "DEBUG_SAULO online clipboard open ok attempt=1 owner=window",
      "DEBUG_SAULO online clipboard empty ok",
      "DEBUG_SAULO online clipboard alloc bytes=20",
      "DEBUG_SAULO online clipboard set ok format=CF_UNICODETEXT",
      "DEBUG_SAULO online clipboard close ok",
  };
  CHECK(write.lines == expected);

  Logs read;
  const ClipboardText text = Read(read);
  CHECK(text.kind == ClipboardText::Kind::kText && text.text == "CEQN-Z7SE");
  const std::vector<std::string> read_expected = {
      "DEBUG_SAULO online clipboard read begin backend=win32",
      "DEBUG_SAULO online clipboard open ok attempt=1 owner=window",
      "DEBUG_SAULO online clipboard format CF_UNICODETEXT available=1",
      "DEBUG_SAULO online clipboard handle ok",
      "DEBUG_SAULO online clipboard lock ok",
      "DEBUG_SAULO online clipboard read chars=9",
      "DEBUG_SAULO online clipboard utf8 len=9",
      "DEBUG_SAULO online clipboard close ok",
  };
  CHECK(read.lines == read_expected);
  CHECK(fake_clipboard::Get().closes == 2);
}

void TestReadTextNormalizes() {
  for (const wchar_t* stored : {L"ceqn-z7se", L"CEQNZ7SE", L"CEQN-Z7SE\r\n", L"  CEQN-Z7SE  ", L"\tceqn z7se\n"}) {
    fake_clipboard::Reset();
    fake_clipboard::Put(stored);
    Logs logs;
    const ClipboardText text = Read(logs);
    CHECK(text.kind == ClipboardText::Kind::kText);
    CHECK(online::NormalizeFriendCode(text.text) == "CEQN-Z7SE");
  }
}

void TestUtfRoundTrip() {
  CHECK(Utf8ToUtf16("CEQN-Z7SE") == std::wstring(L"CEQN-Z7SE"));
  CHECK(Utf16ToUtf8(L"CEQN-Z7SE") == std::string("CEQN-Z7SE"));
  CHECK(Utf16ToUtf8(*Utf8ToUtf16("Ol\xC3\xA1 \xE2\x82\xAC")) == std::string("Ol\xC3\xA1 \xE2\x82\xAC"));
  CHECK(Utf8ToUtf16("") == std::wstring());
  CHECK(!Utf8ToUtf16("\xFF\xFE"));
  CHECK(!Utf16ToUtf8(std::wstring(1, wchar_t(0xD800))));
}

void TestEmptyClipboard() {
  fake_clipboard::Reset();
  Logs logs;
  const ClipboardText text = Read(logs);
  CHECK(text.kind == ClipboardText::Kind::kEmpty);
  CHECK(logs.Has("DEBUG_SAULO online clipboard format CF_UNICODETEXT available=0"));
  CHECK(logs.Has("DEBUG_SAULO online clipboard format CF_TEXT available=0"));
  CHECK(logs.Has("DEBUG_SAULO online clipboard close ok"));
}

void TestOpenFailures() {
  fake_clipboard::Reset();
  fake_clipboard::Get().open_failures = 3;
  Logs logs;
  const ClipboardText text = Read(logs);
  CHECK(text.kind == ClipboardText::Kind::kError && text.text == "open");
  CHECK(fake_clipboard::Get().opens == 3 && fake_clipboard::Get().sleeps == 2 && fake_clipboard::Get().closes == 0);
  CHECK(logs.Count("DEBUG_SAULO online clipboard open failed attempt=") == 3);
  CHECK(logs.Has("DEBUG_SAULO online clipboard open failed attempt=3 error=5"));

  fake_clipboard::Reset();
  fake_clipboard::Get().open_failures = 1;
  fake_clipboard::Put(L"CEQN-Z7SE");
  Logs retried;
  CHECK(Read(retried).text == "CEQN-Z7SE");
  CHECK(retried.Has("DEBUG_SAULO online clipboard open ok attempt=2 owner=window"));

  fake_clipboard::Reset();
  fake_clipboard::Get().open_failures = 3;
  Logs write;
  CHECK(!WriteClipboardText(fake_clipboard::Api(), nullptr, "CEQN-Z7SE", write.sink()));
  CHECK(fake_clipboard::Get().closes == 0);
}

void TestReadStepFailures() {
  fake_clipboard::Reset();
  fake_clipboard::Put(L"CEQN-Z7SE");
  fake_clipboard::Get().fail_get = true;
  Logs get;
  CHECK(Read(get).text == "handle" && get.Has("DEBUG_SAULO online clipboard handle failed error=6"));
  CHECK(fake_clipboard::Get().closes == 1);

  fake_clipboard::Reset();
  fake_clipboard::Put(L"CEQN-Z7SE");
  fake_clipboard::Get().fail_lock = true;
  Logs lock;
  CHECK(Read(lock).text == "lock" && lock.Has("DEBUG_SAULO online clipboard lock failed error=6"));
  CHECK(fake_clipboard::Get().closes == 1);

  fake_clipboard::Reset();
  fake_clipboard::Put(std::wstring(1, wchar_t(0xD800)));
  Logs utf;
  const ClipboardText bad = Read(utf);
  CHECK(bad.kind == ClipboardText::Kind::kError && bad.text == "utf8");
  CHECK(utf.Has("DEBUG_SAULO online clipboard utf8 conversion failed"));
}

void TestWriteStepFailures() {
  fake_clipboard::Reset();
  fake_clipboard::Get().fail_empty = true;
  Logs empty;
  CHECK(!WriteClipboardText(fake_clipboard::Api(), kWindow, "CEQN-Z7SE", empty.sink()));
  CHECK(empty.Has("DEBUG_SAULO online clipboard empty failed error=1418") && fake_clipboard::Get().closes == 1);

  fake_clipboard::Reset();
  fake_clipboard::Get().fail_alloc = true;
  Logs alloc;
  CHECK(!WriteClipboardText(fake_clipboard::Api(), kWindow, "CEQN-Z7SE", alloc.sink()));
  CHECK(alloc.Has("DEBUG_SAULO online clipboard alloc failed error=8"));

  fake_clipboard::Reset();
  fake_clipboard::Get().fail_lock = true;
  Logs lock;
  CHECK(!WriteClipboardText(fake_clipboard::Api(), kWindow, "CEQN-Z7SE", lock.sink()));
  CHECK(lock.Has("DEBUG_SAULO online clipboard lock failed error=6") && fake_clipboard::Get().frees == 1);

  fake_clipboard::Reset();
  fake_clipboard::Get().fail_set = true;
  Logs set;
  CHECK(!WriteClipboardText(fake_clipboard::Api(), kWindow, "CEQN-Z7SE", set.sink()));
  CHECK(set.Has("DEBUG_SAULO online clipboard set failed error=1418"));
  CHECK(fake_clipboard::Get().frees == 1 && fake_clipboard::Get().closes == 1);

  fake_clipboard::Reset();
  Logs utf;
  CHECK(!WriteClipboardText(fake_clipboard::Api(), kWindow, "\xFF\xFE", utf.sink()));
  CHECK(utf.Has("DEBUG_SAULO online clipboard utf16 conversion failed") && fake_clipboard::Get().opens == 0);
}

void TestNoOwnerWindow() {
  fake_clipboard::Reset();
  Logs logs;
  CHECK(WriteClipboardText(fake_clipboard::Api(), nullptr, "CEQN-Z7SE", logs.sink()));
  CHECK(logs.Has("DEBUG_SAULO online clipboard open ok attempt=1 owner=none"));
}

}  // namespace

int main() {
  std::printf("clipboard tests\n");
  TestWriteThenReadIsExact();
  TestReadTextNormalizes();
  TestUtfRoundTrip();
  TestEmptyClipboard();
  TestOpenFailures();
  TestReadStepFailures();
  TestWriteStepFailures();
  TestNoOwnerWindow();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
