#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "guest_memory.h"

namespace ssf2t {

// The game's wrapper around XamShowMessageBoxUI(user, title, text, button count, buttons, focused button, flags,
// result*, overlapped*): it forwards the ninth argument from the caller's stack (r1+84) and branches to the import.
inline constexpr uint32_t kMessageBoxWrapperWords[5] = {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x816100B4, 0x91610054};
inline constexpr uint32_t kMessageBoxOverlappedStackOffset = 84;
inline constexpr uint32_t kMaxMessageBoxButtons = 3;
inline constexpr uint32_t kMaxMessageBoxTextUnits = 1024;
// What a box closed with B completes with on the console; the game then reads no button.
inline constexpr uint32_t kMessageBoxCancelled = 1223;  // ERROR_CANCELLED

struct Base013MessageBox {
  static constexpr uint32_t kShowMessageBoxWrapper = 0x839C3D48;
};

struct TitleUpdate023MessageBox {
  static constexpr uint32_t kShowMessageBoxWrapper = 0x839C44A8;
};

inline bool IsMessageBoxWrapper(const uint8_t* code) {
  for (uint32_t i = 0; i < 5; ++i) {
    if (LoadBE32(code, i * 4) != kMessageBoxWrapperWords[i]) return false;
  }
  return true;
}

struct MessageBoxRequest {
  std::string title;
  std::string text;
  std::vector<std::string> buttons;
  uint32_t focused = 0;
};

struct MessageBoxResult {
  uint32_t status = kMessageBoxCancelled;
  std::optional<uint32_t> button;
};

inline MessageBoxResult ChosenButton(uint32_t button) { return {0, button}; }
inline MessageBoxResult CancelledBox() { return {}; }

inline bool MessageBoxShape(uint32_t button_count, uint32_t result_ptr, uint32_t overlapped) {
  return button_count > 0 && button_count <= kMaxMessageBoxButtons && result_ptr && overlapped;
}

constexpr uint32_t FocusedButton(uint32_t requested, uint32_t count) { return requested < count ? requested : 0; }

// Guest UTF-16 (big-endian, NUL-terminated, at most `limit` units) as UTF-8.
inline std::string Utf16BeToUtf8(const uint8_t* text, uint32_t limit = kMaxMessageBoxTextUnits) {
  std::string out;
  if (!text) return out;
  for (uint32_t i = 0; i < limit; ++i) {
    uint32_t c = LoadBE16(text, i * 2);
    if (c == 0) break;
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < limit) {
      const uint32_t low = LoadBE16(text, (i + 1) * 2);
      if (low >= 0xDC00 && low <= 0xDFFF) {
        c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
        ++i;
      }
    }
    if (c >= 0xD800 && c <= 0xDFFF) c = 0xFFFD;
    if (c < 0x80) {
      out += char(c);
    } else if (c < 0x800) {
      out += char(0xC0 | (c >> 6));
      out += char(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      out += char(0xE0 | (c >> 12));
      out += char(0x80 | ((c >> 6) & 0x3F));
      out += char(0x80 | (c & 0x3F));
    } else {
      out += char(0xF0 | (c >> 18));
      out += char(0x80 | ((c >> 12) & 0x3F));
      out += char(0x80 | ((c >> 6) & 0x3F));
      out += char(0x80 | (c & 0x3F));
    }
  }
  return out;
}

// Called on the kernel's dispatch thread; `done` is called exactly once, from the UI thread. Set before the guest
// starts.
using MessageBoxOpener = std::function<void(MessageBoxRequest, std::function<void(MessageBoxResult)>)>;
void SetMessageBoxOpener(MessageBoxOpener open);

}  // namespace ssf2t
