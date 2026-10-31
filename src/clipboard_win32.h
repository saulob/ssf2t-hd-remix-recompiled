#pragma once

#include <optional>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "clipboard.h"

namespace ssf2t {

// The Win32 calls the clipboard uses, replaceable by tests.
struct Win32ClipboardApi {
  BOOL (*open)(HWND owner);
  BOOL (*close)();
  BOOL (*empty)();
  BOOL (*available)(UINT format);
  HANDLE (*get)(UINT format);
  HANDLE (*set)(UINT format, HANDLE memory);
  HGLOBAL (*alloc)(UINT flags, SIZE_T bytes);
  LPVOID (*lock)(HGLOBAL memory);
  BOOL (*unlock)(HGLOBAL memory);
  HGLOBAL (*free)(HGLOBAL memory);
  DWORD (*last_error)();
  void (*sleep)(DWORD ms);
};

const Win32ClipboardApi& SystemClipboardApi();

ClipboardText ReadClipboardText(const Win32ClipboardApi& api, HWND owner, const ClipboardLog& log);
bool WriteClipboardText(const Win32ClipboardApi& api, HWND owner, const std::string& text, const ClipboardLog& log);

std::optional<std::wstring> Utf8ToUtf16(const std::string& text);
std::optional<std::string> Utf16ToUtf8(const std::wstring& text);

}  // namespace ssf2t
