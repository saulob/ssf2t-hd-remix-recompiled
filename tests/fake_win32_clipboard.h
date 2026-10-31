// An in-memory stand-in for the Win32 clipboard calls, with switchable failures. It proves the call sequence and the
// handling of each failure, not the real Windows clipboard.
#pragma once

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "clipboard_win32.h"

namespace fake_clipboard {

struct State {
  std::map<HGLOBAL, std::vector<char>> memory;
  HGLOBAL content = nullptr;  // owned by the clipboard once set
  int next_handle = 1;
  int open_failures = 0;      // the next N OpenClipboard calls fail
  bool fail_empty = false, fail_alloc = false, fail_lock = false, fail_set = false, fail_get = false;
  bool opened = false;
  HWND owner = nullptr;
  int opens = 0, closes = 0, sleeps = 0, frees = 0;
  DWORD error = 0;
};

inline State& Get() {
  static State state;
  return state;
}

inline void Reset() { Get() = State{}; }

inline void Put(const std::wstring& text) {
  State& s = Get();
  const HGLOBAL handle = reinterpret_cast<HGLOBAL>(static_cast<intptr_t>(s.next_handle++));
  std::vector<char> bytes((text.size() + 1) * sizeof(wchar_t), 0);
  std::memcpy(bytes.data(), text.c_str(), bytes.size());
  s.memory[handle] = bytes;
  s.content = handle;
}

inline std::wstring Content() {
  State& s = Get();
  if (!s.content || !s.memory.count(s.content)) return L"";
  return reinterpret_cast<const wchar_t*>(s.memory[s.content].data());
}

inline const ssf2t::Win32ClipboardApi& Api() {
  static const ssf2t::Win32ClipboardApi api{
      [](HWND owner) -> BOOL {
        State& s = Get();
        ++s.opens;
        if (s.open_failures > 0) {
          --s.open_failures;
          s.error = ERROR_ACCESS_DENIED;
          return FALSE;
        }
        s.opened = true;
        s.owner = owner;
        return TRUE;
      },
      []() -> BOOL {
        State& s = Get();
        ++s.closes;
        const bool was = s.opened;
        s.opened = false;
        return was;
      },
      []() -> BOOL {
        State& s = Get();
        if (s.fail_empty) {
          s.error = ERROR_CLIPBOARD_NOT_OPEN;
          return FALSE;
        }
        s.content = nullptr;
        return TRUE;
      },
      [](UINT format) -> BOOL { return format == CF_UNICODETEXT && Get().content != nullptr; },
      [](UINT format) -> HANDLE {
        State& s = Get();
        if (s.fail_get || format != CF_UNICODETEXT) {
          s.error = ERROR_INVALID_HANDLE;
          return nullptr;
        }
        return s.content;
      },
      [](UINT, HANDLE memory) -> HANDLE {
        State& s = Get();
        if (s.fail_set) {
          s.error = ERROR_CLIPBOARD_NOT_OPEN;
          return nullptr;
        }
        s.content = memory;
        return memory;
      },
      [](UINT, SIZE_T bytes) -> HGLOBAL {
        State& s = Get();
        if (s.fail_alloc) {
          s.error = ERROR_NOT_ENOUGH_MEMORY;
          return nullptr;
        }
        const HGLOBAL handle = reinterpret_cast<HGLOBAL>(static_cast<intptr_t>(s.next_handle++));
        s.memory[handle] = std::vector<char>(bytes, 0);
        return handle;
      },
      [](HGLOBAL memory) -> LPVOID {
        State& s = Get();
        if (s.fail_lock || !s.memory.count(memory)) {
          s.error = ERROR_INVALID_HANDLE;
          return nullptr;
        }
        return s.memory[memory].data();
      },
      [](HGLOBAL) -> BOOL { return FALSE; },
      [](HGLOBAL memory) -> HGLOBAL {
        State& s = Get();
        ++s.frees;
        s.memory.erase(memory);
        return nullptr;
      },
      []() -> DWORD { return Get().error; },
      [](DWORD) { ++Get().sleeps; },
  };
  return api;
}

}  // namespace fake_clipboard
