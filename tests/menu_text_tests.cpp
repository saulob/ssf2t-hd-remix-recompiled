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
#include <string_view>

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

  // Player Match is shown as Online Match; Ranked and Tournament text and Create Player Match wording stay.
  struct Online {
    const char *upper, *label, *description, *new_upper, *new_label, *new_description;
  };
  const Online online[] = {
      {"PLAYER MATCH", "Player Match", "Play a Player Match on Xbox LIVE", "ONLINE MATCH", "Online Match",
       "Play an Online Match vs a friend"},
      {"PARTIE ENTRE AMIS", "Partie entre amis", "Jouer  une partie entre amis sur Xbox LIVE", "PARTIE EN LIGNE",
       "Partie en ligne", "Jouer une partie en ligne contre un ami"},
      {"FREUNDSCHAFTSMATCH", "Freundschaftsmatch", "Freundschaftsmatch auf Xbox LIVE spielen", "ONLINE-MATCH",
       "Online-Match", "Online-Match gegen einen Freund spielen"},
      {"PARTITA DEL GIOCATORE", "Partita del giocatore", "Gioca una partita del giocatore su Xbox LIVE",
       "PARTITA ONLINE", "Partita online", "Gioca una partita online contro un amico"},
      {"PARTIDA DE JUGADOR", "Partida de jugador", "Jugar una Partida de jugador en Xbox LIVE",
       "PARTIDA EN L\xC3\x8D" "NEA", "Partida en l\xC3\xAD" "nea", "Jugar una partida en l\xC3\xAD" "nea con un amigo"},
  };
  for (const char separator : {'\0', '\n'}) {
    for (const auto& lang : online) {
      std::string buffer = Pack({"RANKED MATCH", lang.upper, "Ranked Match", lang.label, "CREATE PLAYER MATCH",
                                 lang.description, "Tournament Match"},
                                separator);
      const std::string expected =
          Pack({"RANKED MATCH", Padded(lang.new_upper, std::strlen(lang.upper)), "Ranked Match",
                Padded(lang.new_label, std::strlen(lang.label)), "CREATE PLAYER MATCH",
                Padded(lang.new_description, std::strlen(lang.description)), "Tournament Match"},
               separator);
      const MenuTextPatch patch = PatchOnlineMatchBuffer(buffer.data(), buffer.size());
      CHECK(patch.label && patch.description);
      CHECK(buffer == expected);
      std::string again = buffer;
      CHECK(!PatchOnlineMatchBuffer(again.data(), again.size()).label && again == buffer);
    }
  }
  std::string create = Pack({"Create a custom Player Match"});
  CHECK(PatchOnlineMatchBuffer(create.data(), create.size()).description);
  CHECK(create == Pack({"Create a custom Online Match"}));
  std::string exit_only = Pack({"Play a Player Match on Xbox LIVE"});
  CHECK(!PatchMainMenuExitBuffer(exit_only.data(), exit_only.size()).description);
  for (const auto& entry : kOnlineMatchText) {
    CHECK(entry.replacement.size() <= entry.original.size());
    CHECK(entry.original.find('\n') == std::string_view::npos);
    CHECK(entry.replacement.find("Player Match") == std::string_view::npos);
    CHECK(entry.replacement.find("Xbox LIVE") == std::string_view::npos);
  }

  // Quick Match is shown as Find Match, with its help line and no-match prompt; other text stays.
  std::string find = Pack({"QUICK MATCH", "Quick Match", "Join first available session", "CREATE MATCH",
                           "No quick match was found. Do you wish to create a lobby?", "NO MATCH FOUND"});
  const MenuTextPatch found = PatchFindMatchBuffer(find.data(), find.size());
  CHECK(found.label && found.description);
  CHECK(find == Pack({Padded("FIND MATCH", 11), Padded("Find Match", 11),
                      Padded("Find a friend's Online Match", 28), "CREATE MATCH",
                      Padded("No Online Match was found. Do you wish to create one?", 56), "NO MATCH FOUND"}));
  std::string spanish = Pack({"PARTIDA R\xC3\x81PIDA", "Partida r\xC3\xA1pida"});
  CHECK(PatchFindMatchBuffer(spanish.data(), spanish.size()).label);
  CHECK(spanish == Pack({Padded("BUSCAR PARTIDA", 15), Padded("Buscar partida", 15)}));
  for (const auto& entry : kFindMatchText) {
    CHECK(entry.replacement.size() <= entry.original.size());
    CHECK(entry.original.find('\n') == std::string_view::npos);
    CHECK(entry.replacement.find("Quick") == std::string_view::npos);
    CHECK(entry.replacement.find("quick") == std::string_view::npos);
  }
  CHECK(Has(ReadFile(std::filesystem::path(SSF2T_SOURCE_DIR) / "src" / "menu_text_hook.cpp"),
            "PatchFindMatchBuffer(data, span)"));

  // Game Lobby help: no Players List (LB) or View Gamer Card (Y); Invite Player, Start Match and Leave Lobby stay.
  constexpr std::string_view kX = "\xC2\x82", kA = "\xC2\x80", kB = "\xC2\x81", kY = "\xC2\x83", kLB = "\xC2\x84";
  size_t screen_lines = 0;
  for (const auto& entry : kGameLobbyHelpText) {
    CHECK(entry.replacement.size() < entry.original.size());
    CHECK(entry.original.find('\n') == std::string_view::npos);
    CHECK(entry.original.find(kY) != std::string_view::npos || entry.original.find(kLB) != std::string_view::npos);
    CHECK(entry.replacement.find(kY) == std::string_view::npos);
    CHECK(entry.replacement.find(kLB) == std::string_view::npos);
    if (entry.original.find(kLB) != std::string_view::npos) {
      CHECK(entry.replacement.find(kX) != std::string_view::npos && entry.replacement.find(kB) != std::string_view::npos);
      if (entry.original.find(kA) != std::string_view::npos) {
        CHECK(entry.replacement.find(kA) != std::string_view::npos);
        ++screen_lines;
      }
    }
  }
  CHECK(screen_lines == 5);
  const std::string lobby_help =
      "[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Invite Player   [color=0xFFFFFFFF]\xC2\x80[color=0x01010101] Start "
      "Match   [color=0xFFFFFFFF]\xC2\x84[color=0x01010101] Players List   [color=0xFFFFFFFF]\xC2\x81[color=0x01010101] "
      "Leave Lobby";
  const std::string slot_help =
      "[color=0xFFFFFFFF]\xC2\x8B[color=0x01010101] Select Slot    [color=0xFFFFFFFF]\xC2\x83[color=0x01010101] View "
      "Gamer Card";
  std::string lobby = Pack({lobby_help, slot_help, "Leave Lobby"}, '\n');
  CHECK(PatchGameLobbyHelpBuffer(lobby.data(), lobby.size()).description);
  const std::string kept =
      "[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Invite Player   [color=0xFFFFFFFF]\xC2\x80[color=0x01010101] Start "
      "Match   [color=0xFFFFFFFF]\xC2\x81[color=0x01010101] Leave Lobby";
  CHECK(lobby == Pack({kept + std::string(lobby_help.size() - kept.size(), ' '), std::string(slot_help.size(), ' '),
                       "Leave Lobby"},
                      '\n'));
  CHECK(Has(ReadFile(std::filesystem::path(SSF2T_SOURCE_DIR) / "src" / "menu_text_hook.cpp"),
            "PatchGameLobbyHelpBuffer(data, span)"));

  const std::filesystem::path root(SSF2T_SOURCE_DIR);
  const std::string cmake = ReadFile(root / "CMakeLists.txt");
  const std::string hook = ReadFile(root / "src" / "menu_text_hook.cpp");
  const size_t sources = cmake.find("set(SSF2T_SOURCES");
  const size_t sources_end = cmake.find(')', sources);
  CHECK(sources != std::string::npos && cmake.substr(sources, sources_end - sources).find("src/menu_text_hook.cpp") != std::string::npos);
  CHECK(Has(hook, "REX_HOOK_RAW(base013_sub_82439830)") && Has(hook, "REX_EXTERN(__imp__base013_sub_82439830)"));
  CHECK(Has(hook, "REX_HOOK_RAW(tu023_sub_82439FA0)") && Has(hook, "REX_EXTERN(__imp__tu023_sub_82439FA0)"));
  CHECK(Has(hook, "SplitMenuText(ctx, base, __imp__base013_sub_82439830)"));
  CHECK(Has(hook, "SplitMenuText(ctx, base, __imp__tu023_sub_82439FA0)"));
  CHECK(!Has(hook, "REX_HOOK_RAW(sub_"));
  CHECK(Has(hook, "original(ctx, base)"));
  CHECK(Has(hook, "PatchMainMenuExitBuffer"));
  CHECK(Has(hook, "PatchOnlineMatchBuffer(data, span)"));

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
