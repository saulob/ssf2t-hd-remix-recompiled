#include "friend_code_field.h"

#include <cctype>

#include <imgui.h>

#include "online/protocol.h"

namespace ssf2t {
namespace {

constexpr size_t kMaxShownPaste = 32;
constexpr size_t kMaxLoggedRaw = 64;

bool CtrlDown() {
  return ImGui::GetIO().KeyCtrl || ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
}

// The clipboard text as one log line: control characters escaped, length bounded.
std::string Printable(std::string_view text) {
  std::string out;
  for (const char c : text.substr(0, kMaxLoggedRaw)) {
    if (c == '\r') out += "\\r";
    else if (c == '\n') out += "\\n";
    else if (c == '\t') out += "\\t";
    else if (static_cast<unsigned char>(c) < ' ') out += '?';
    else out += c;
  }
  return out;
}

}  // namespace

std::string PastedFriendCode(std::string_view clipboard) {
  if (std::string code = online::NormalizeFriendCode(clipboard); !code.empty()) return code;
  std::string shown;
  for (const char c : clipboard) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u <= ' ' || u >= 0x7f) continue;
    if (shown.size() == kMaxShownPaste) break;
    shown += static_cast<char>(std::toupper(u));
  }
  return shown;
}

void FriendCodeField::ReadPaste(const FieldLog& log) {
  log(std::string("DEBUG_SAULO online friend ctrl-v detected left_ctrl=") +
      (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) ? "1" : "0") + " right_ctrl=" + (ImGui::IsKeyDown(ImGuiKey_RightCtrl) ? "1" : "0"));
  const ClipboardText clipboard = read_ ? read_(log) : ClipboardText{};
  if (clipboard.kind == ClipboardText::Kind::kEmpty) {
    log("DEBUG_SAULO online friend paste empty");
    return;
  }
  if (clipboard.kind == ClipboardText::Kind::kError) {
    log("DEBUG_SAULO online friend paste failed step=" + clipboard.text);
    return;
  }
  log("DEBUG_SAULO online friend paste raw=" + Printable(clipboard.text));
  paste_ = PastedFriendCode(clipboard.text);
  const bool code = !online::NormalizeFriendCode(paste_).empty();
  log("DEBUG_SAULO online friend paste normalized=" + (code ? paste_ : "<not a friend code> shown=" + paste_));
  has_paste_ = !paste_.empty();
}

int FriendCodeField::ApplyPaste(ImGuiInputTextCallbackData* data) {
  auto* field = static_cast<FriendCodeField*>(data->UserData);
  if (!field->has_paste_) return 0;
  field->has_paste_ = false;
  field->pasted_ = true;
  data->DeleteChars(0, data->BufTextLen);
  data->InsertChars(0, field->paste_.c_str());
  data->SelectionStart = data->SelectionEnd = data->CursorPos;
  return 0;
}

void FriendCodeField::Draw(char* buffer, size_t size, const FieldLog& log) {
  const FieldLog quiet = [](const std::string&) {};
  const FieldLog& out = log ? log : quiet;
  if (was_active_ && CtrlDown() && ImGui::IsKeyPressed(ImGuiKey_V, false)) ReadPaste(out);

  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
  const bool changed = ImGui::InputTextWithHint("##add_code", "XXXX-XXXX", buffer, size,
                                                ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CallbackAlways,
                                                ApplyPaste, this);
  const bool active = ImGui::IsItemActive();
  if (active != was_active_) out(std::string("DEBUG_SAULO online friend field ") + (active ? "focused" : "unfocused"));
  if (pasted_) {
    pasted_ = false;
    out(std::string("DEBUG_SAULO online friend field value=") + buffer);
  } else if (changed) {
    out(std::string("DEBUG_SAULO online friend input changed value=") + buffer);
  }
  if (!active) has_paste_ = false;
  was_active_ = active;
}

bool CopyFriendCode(const std::string& code, const ClipboardWriter& write, const FieldLog& log) {
  log("DEBUG_SAULO online friend copy clicked code=" + code);
  const bool copied = write && write(code, log);
  log((copied ? "DEBUG_SAULO online friend copy success code=" : "DEBUG_SAULO online friend copy failed code=") + code);
  return copied;
}

}  // namespace ssf2t
