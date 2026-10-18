#pragma once

#include <cstddef>
#include <cstring>
#include <string_view>

namespace ssf2t {

struct MenuTextReplacement {
  std::string_view original;
  std::string_view replacement;
  bool description;
};

// The main menu's "Return To Xbox LIVE Arcade" item (label, caption, description) in EN, FR, DE, IT and ES.
inline constexpr MenuTextReplacement kMainMenuExitText[] = {
    {"RETURN TO ARCADE", "EXIT GAME", false},
    {"Return To Xbox LIVE Arcade", "Exit Game", false},
    {"Quit game and return to Xbox LIVE Arcade", "Quit game and exit", true},
    {"REVENIR EN MODE ARCADE", "QUITTER LE JEU", false},
    {"Retour vers Xbox LIVE Arcade", "Quitter le jeu", false},
    {"Quitter la partie et revenir au Xbox LIVE Arcade", "Quitter la partie et fermer le jeu", true},
    {"ZUR\xC3\x9C" "CK ZU ARCADE", "SPIEL BEENDEN", false},
    {"Zur\xC3\xBC" "ck zu Xbox LIVE Arcade", "Spiel beenden", false},
    {"Spiel beenden und zur\xC3\xBC" "ck zu Xbox LIVE Arcade", "Spiel beenden und verlassen", true},
    {"TORNA AD ARCADE", "ESCI DAL GIOCO", false},
    {"Torna a Xbox LIVE Arcade", "Esci dal gioco", false},
    {"Esci dal gioco e torna a Xbox LIVE Arcade", "Esci dal gioco e chiudi", true},
    {"VOLVER A ARCADE", "SALIR DEL JUEGO", false},
    {"Volver a Xbox LIVE Arcade", "Salir del juego", false},
    {"Salir del juego y volver a Xbox LIVE Arcade", "Salir del juego y cerrar", true},
};

struct MenuTextPatch {
  bool label = false;
  bool description = false;
};

// Entries are NUL- or LF-terminated. Replacements keep the entry length (space padded) so offsets stay valid;
// a final entry without a terminator is left alone.
inline MenuTextPatch PatchMainMenuExitBuffer(char* data, size_t length) {
  MenuTextPatch patch;
  size_t start = 0;
  for (size_t i = 0; i < length; ++i) {
    if (data[i] != '\0' && data[i] != '\n') continue;
    const size_t size = i - start;
    for (const auto& entry : kMainMenuExitText) {
      if (entry.original.size() != size || std::memcmp(data + start, entry.original.data(), size) != 0) continue;
      std::memcpy(data + start, entry.replacement.data(), entry.replacement.size());
      std::memset(data + start + entry.replacement.size(), ' ', size - entry.replacement.size());
      (entry.description ? patch.description : patch.label) = true;
      break;
    }
    start = i + 1;
  }
  return patch;
}

}  // namespace ssf2t
