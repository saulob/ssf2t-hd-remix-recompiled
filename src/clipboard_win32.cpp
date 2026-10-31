#include "clipboard_win32.h"

#include <cstring>
#include <cwchar>

namespace ssf2t {
namespace {

constexpr int kOpenAttempts = 3;
constexpr DWORD kOpenRetryMs = 5;
constexpr size_t kMaxClipboardChars = 1 << 16;

const std::string kPrefix = "DEBUG_SAULO online clipboard ";

std::string Error(const Win32ClipboardApi& api) { return std::to_string(api.last_error()); }

bool Open(const Win32ClipboardApi& api, HWND owner, const ClipboardLog& log) {
  for (int attempt = 1; attempt <= kOpenAttempts; ++attempt) {
    if (api.open(owner)) {
      log(kPrefix + "open ok attempt=" + std::to_string(attempt) + " owner=" + (owner ? "window" : "none"));
      return true;
    }
    log(kPrefix + "open failed attempt=" + std::to_string(attempt) + " error=" + Error(api));
    if (attempt < kOpenAttempts) api.sleep(kOpenRetryMs);
  }
  return false;
}

class OpenClipboardScope {
 public:
  OpenClipboardScope(const Win32ClipboardApi& api, const ClipboardLog& log) : api_(api), log_(log) {}
  ~OpenClipboardScope() {
    if (api_.close()) log_(kPrefix + "close ok");
    else log_(kPrefix + "close failed error=" + Error(api_));
  }

 private:
  const Win32ClipboardApi& api_;
  const ClipboardLog& log_;
};

ClipboardText Failed(const std::string& step) { return {ClipboardText::Kind::kError, step}; }

}  // namespace

const Win32ClipboardApi& SystemClipboardApi() {
  static const Win32ClipboardApi api{
      [](HWND owner) { return ::OpenClipboard(owner); },
      [] { return ::CloseClipboard(); },
      [] { return ::EmptyClipboard(); },
      [](UINT format) { return ::IsClipboardFormatAvailable(format); },
      [](UINT format) { return ::GetClipboardData(format); },
      [](UINT format, HANDLE memory) { return ::SetClipboardData(format, memory); },
      [](UINT flags, SIZE_T bytes) { return ::GlobalAlloc(flags, bytes); },
      [](HGLOBAL memory) { return ::GlobalLock(memory); },
      [](HGLOBAL memory) { return ::GlobalUnlock(memory); },
      [](HGLOBAL memory) { return ::GlobalFree(memory); },
      [] { return ::GetLastError(); },
      [](DWORD ms) { ::Sleep(ms); },
  };
  return api;
}

std::optional<std::wstring> Utf8ToUtf16(const std::string& text) {
  if (text.empty()) return std::wstring();
  const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
  if (length <= 0) return std::nullopt;
  std::wstring wide(size_t(length), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), wide.data(), length);
  return wide;
}

std::optional<std::string> Utf16ToUtf8(const std::wstring& text) {
  if (text.empty()) return std::string();
  const int length =
      WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
  if (length <= 0) return std::nullopt;
  std::string narrow(size_t(length), '\0');
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), int(text.size()), narrow.data(), length, nullptr, nullptr);
  return narrow;
}

ClipboardText ReadClipboardText(const Win32ClipboardApi& api, HWND owner, const ClipboardLog& log) {
  log(kPrefix + "read begin backend=win32");
  if (!Open(api, owner, log)) return Failed("open");
  OpenClipboardScope scope(api, log);

  const bool unicode = api.available(CF_UNICODETEXT);
  log(kPrefix + "format CF_UNICODETEXT available=" + (unicode ? "1" : "0"));
  if (!unicode) {
    log(kPrefix + "format CF_TEXT available=" + (api.available(CF_TEXT) ? "1" : "0"));
    return {ClipboardText::Kind::kEmpty, {}};
  }
  HANDLE handle = api.get(CF_UNICODETEXT);
  if (!handle) {
    log(kPrefix + "handle failed error=" + Error(api));
    return Failed("handle");
  }
  log(kPrefix + "handle ok");
  const auto* chars = static_cast<const wchar_t*>(api.lock(handle));
  if (!chars) {
    log(kPrefix + "lock failed error=" + Error(api));
    return Failed("lock");
  }
  log(kPrefix + "lock ok");
  const std::wstring wide(chars, wcsnlen(chars, kMaxClipboardChars));
  api.unlock(handle);
  log(kPrefix + "read chars=" + std::to_string(wide.size()));

  const std::optional<std::string> text = Utf16ToUtf8(wide);
  if (!text) {
    log(kPrefix + "utf8 conversion failed");
    return Failed("utf8");
  }
  log(kPrefix + "utf8 len=" + std::to_string(text->size()));
  if (text->empty()) return {ClipboardText::Kind::kEmpty, {}};
  return {ClipboardText::Kind::kText, *text};
}

bool WriteClipboardText(const Win32ClipboardApi& api, HWND owner, const std::string& text, const ClipboardLog& log) {
  log(kPrefix + "copy begin code=" + text + " backend=win32");
  const std::optional<std::wstring> wide = Utf8ToUtf16(text);
  if (!wide) {
    log(kPrefix + "utf16 conversion failed");
    return false;
  }
  if (!Open(api, owner, log)) return false;
  OpenClipboardScope scope(api, log);

  if (!api.empty()) {
    log(kPrefix + "empty failed error=" + Error(api));
    return false;
  }
  log(kPrefix + "empty ok");
  const SIZE_T bytes = (wide->size() + 1) * sizeof(wchar_t);
  HGLOBAL memory = api.alloc(GMEM_MOVEABLE, bytes);
  if (!memory) {
    log(kPrefix + "alloc failed error=" + Error(api));
    return false;
  }
  log(kPrefix + "alloc bytes=" + std::to_string(bytes));
  void* target = api.lock(memory);
  if (!target) {
    log(kPrefix + "lock failed error=" + Error(api));
    api.free(memory);
    return false;
  }
  std::memcpy(target, wide->c_str(), bytes);
  api.unlock(memory);
  // On success the clipboard owns `memory`; it is freed here only when SetClipboardData refused it.
  if (!api.set(CF_UNICODETEXT, memory)) {
    log(kPrefix + "set failed error=" + Error(api));
    api.free(memory);
    return false;
  }
  log(kPrefix + "set ok format=CF_UNICODETEXT");
  return true;
}

ClipboardText ReadClipboardText(void* owner_window, const ClipboardLog& log) {
  return ReadClipboardText(SystemClipboardApi(), static_cast<HWND>(owner_window), log);
}

bool WriteClipboardText(void* owner_window, const std::string& text, const ClipboardLog& log) {
  return WriteClipboardText(SystemClipboardApi(), static_cast<HWND>(owner_window), text, log);
}

}  // namespace ssf2t
