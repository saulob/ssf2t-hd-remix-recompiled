// Main menu exit item tests: the packed string-table patch, the supported languages and the hook wiring.
// Pure logic and source reads, no window or game files.
// Run: ssf2t_menu_text_tests

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>

#include "menu_text.h"

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

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool Has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

std::string Pack(std::initializer_list<std::string> entries, char separator = '\0') {
  std::string packed;
  for (const auto& entry : entries) packed += entry + separator;
  return packed;
}

std::string Padded(const char* text, size_t width) { return std::string(text) + std::string(width - std::strlen(text), ' '); }

MenuTextPatch Patch(std::string& buffer) { return PatchMainMenuExitBuffer(buffer.data(), buffer.size()); }

}  // namespace

int main() {
  struct Language {
    const char *title, *label, *description, *new_title, *new_label, *new_description;
  };
  const Language languages[] = {
      {"RETURN TO ARCADE", "Return To Xbox LIVE Arcade", "Quit game and return to Xbox LIVE Arcade", "EXIT GAME", "Exit Game",
       "Quit game and exit"},
      {"REVENIR EN MODE ARCADE", "Retour vers Xbox LIVE Arcade", "Quitter la partie et revenir au Xbox LIVE Arcade",
       "QUITTER LE JEU", "Quitter le jeu", "Quitter la partie et fermer le jeu"},
      {"ZUR\xC3\x9C" "CK ZU ARCADE", "Zur\xC3\xBC" "ck zu Xbox LIVE Arcade", "Spiel beenden und zur\xC3\xBC" "ck zu Xbox LIVE Arcade",
       "SPIEL BEENDEN", "Spiel beenden", "Spiel beenden und verlassen"},
      {"TORNA AD ARCADE", "Torna a Xbox LIVE Arcade", "Esci dal gioco e torna a Xbox LIVE Arcade", "ESCI DAL GIOCO",
       "Esci dal gioco", "Esci dal gioco e chiudi"},
      {"VOLVER A ARCADE", "Volver a Xbox LIVE Arcade", "Salir del juego y volver a Xbox LIVE Arcade", "SALIR DEL JUEGO",
       "Salir del juego", "Salir del juego y cerrar"},
  };
  const std::string pause = "Quit game and go back to the Main Menu";

  for (const char separator : {'\0', '\n'}) {
    for (const auto& lang : languages) {
      std::string buffer = Pack({"//PauseMenu", "EXIT GAME", "Exit Game", pause, lang.title, lang.label, "XBOX LIVE", lang.description,
                                 "Play a Ranked Match on Xbox LIVE"},
                                separator);
      const std::string expected =
          Pack({"//PauseMenu", "EXIT GAME", "Exit Game", pause, Padded(lang.new_title, std::strlen(lang.title)),
                Padded(lang.new_label, std::strlen(lang.label)), "XBOX LIVE",
                Padded(lang.new_description, std::strlen(lang.description)), "Play a Ranked Match on Xbox LIVE"},
               separator);
      const size_t size = buffer.size();
      const MenuTextPatch patch = Patch(buffer);
      CHECK(patch.label && patch.description);
      CHECK(buffer.size() == size);
      CHECK(buffer == expected);

      std::string again = buffer;
      CHECK(!Patch(again).label && again == buffer);
    }
  }

  const std::string untouched = Pack({"//PauseMenu", "EXIT GAME", "Exit Game", pause, "XBOX LIVE", "XBOX LIVE ARCADE",
                                      "Play a Player Match on Xbox LIVE", "Return to Arcade", "Xbox LIVE Arcade"});
  std::string copy = untouched;
  const MenuTextPatch none = Patch(copy);
  CHECK(!none.label && !none.description && copy == untouched);

  std::string partial = Pack({"//PauseMenu"}) + "Return To Xbox LIVE Arcade";
  const std::string partial_before = partial;
  CHECK(!Patch(partial).label && partial == partial_before);
  std::string truncated = Pack({"Return To Xbox LIVE Arc"});
  CHECK(!Patch(truncated).label);
  CHECK(!PatchMainMenuExitBuffer(nullptr, 0).label);

  for (const auto& entry : kMainMenuExitText) {
    CHECK(entry.replacement.size() <= entry.original.size());
    CHECK(entry.original.find('\n') == std::string_view::npos);
    CHECK(entry.original.find('\0') == std::string_view::npos);
    if (!entry.description) continue;
    CHECK(entry.replacement.find("desktop") == std::string_view::npos);
    CHECK(entry.replacement.find("Windows") == std::string_view::npos);
    CHECK(entry.replacement.find("Xbox LIVE Arcade") == std::string_view::npos);
  }

  const std::filesystem::path root(SSF2T_SOURCE_DIR);
  const std::string cmake = ReadFile(root / "CMakeLists.txt");
  const std::string hook = ReadFile(root / "src" / "menu_text_hook.cpp");
  const size_t sources = cmake.find("set(SSF2T_SOURCES");
  const size_t sources_end = cmake.find(')', sources);
  CHECK(sources != std::string::npos && cmake.substr(sources, sources_end - sources).find("src/menu_text_hook.cpp") != std::string::npos);
  CHECK(Has(hook, "REX_HOOK_RAW(sub_82439FA0)") && Has(hook, "REX_EXTERN(__imp__sub_82439FA0)"));
  CHECK(Has(hook, "__imp__sub_82439FA0(ctx, base)"));
  CHECK(Has(hook, "PatchMainMenuExitBuffer"));

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
