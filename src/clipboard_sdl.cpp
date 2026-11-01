#include <SDL3/SDL.h>

#include "clipboard.h"

namespace ssf2t {

ClipboardText ReadClipboardText(void*, const ClipboardLog& log) {
  log("online clipboard read begin backend=sdl");
  if (!SDL_HasClipboardText()) {
    log("online clipboard has text=0 error=" + std::string(SDL_GetError()));
    return {ClipboardText::Kind::kEmpty, {}};
  }
  SDL_ClearError();
  char* text = SDL_GetClipboardText();
  if (!text) return {ClipboardText::Kind::kError, SDL_GetError()};
  std::string copy(text);
  SDL_free(text);
  if (copy.empty()) {
    const std::string error = SDL_GetError();
    return error.empty() ? ClipboardText{ClipboardText::Kind::kEmpty, {}} : ClipboardText{ClipboardText::Kind::kError, error};
  }
  log("online clipboard utf8 len=" + std::to_string(copy.size()));
  return {ClipboardText::Kind::kText, copy};
}

bool WriteClipboardText(void*, const std::string& text, const ClipboardLog& log) {
  log("online clipboard copy begin code=" + text + " backend=sdl");
  if (SDL_SetClipboardText(text.c_str())) return true;
  log("online clipboard set failed error=" + std::string(SDL_GetError()));
  return false;
}

}  // namespace ssf2t
