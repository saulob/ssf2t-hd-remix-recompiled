#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include "clipboard.h"

struct ImGuiInputTextCallbackData;

namespace ssf2t {

inline constexpr size_t kFriendCodeFieldSize = 64;

using FieldLog = ClipboardLog;
using ClipboardReader = std::function<ClipboardText(const FieldLog&)>;
using ClipboardWriter = std::function<bool(const std::string& text, const FieldLog&)>;

// "ceqn z7se\r\n" -> "CEQN-Z7SE". Text that is not a friend code is kept, upper-cased and without spaces, so the field
// can say why Send request stays off.
std::string PastedFriendCode(std::string_view clipboard);

// The Add a friend text box (Dragon's InputTextWithHint). Ctrl+V is read here from `read` instead of ImGui's own paste:
// the SDK's ImGui never sees Ctrl held, because its window glue does not send ImGuiMod_Ctrl.
class FriendCodeField {
 public:
  explicit FriendCodeField(ClipboardReader read) : read_(std::move(read)) {}
  // A width of 0 takes 60% of the room that is left.
  void Draw(char* buffer, size_t size, const FieldLog& log, float width = 0.0f);

 private:
  static int ApplyPaste(ImGuiInputTextCallbackData* data);
  void ReadPaste(const FieldLog& log);

  ClipboardReader read_;
  bool was_active_ = false;
  bool has_paste_ = false;
  bool pasted_ = false;
  std::string paste_;
};

// Puts exactly `code` on the system clipboard.
bool CopyFriendCode(const std::string& code, const ClipboardWriter& write, const FieldLog& log);

}  // namespace ssf2t
