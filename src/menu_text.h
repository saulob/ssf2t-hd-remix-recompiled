#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace ssf2t {

// The string table splitter of each game version, the function the menu text patches run before.
struct Base013MenuText {
  static constexpr uint32_t kStringTableSplitter = 0x82439830;
};

struct TitleUpdate023MenuText {
  static constexpr uint32_t kStringTableSplitter = 0x82439FA0;
};

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

// Player Match (label, caption, descriptions) shown as Online Match in EN, FR, DE, IT and ES.
inline constexpr MenuTextReplacement kOnlineMatchText[] = {
    {"PLAYER MATCH", "ONLINE MATCH", false},
    {"Player Match", "Online Match", false},
    {"Play a Player Match on Xbox LIVE", "Play an Online Match vs a friend", true},
    {"Create a custom Player Match", "Create a custom Online Match", true},
    {"PARTIE ENTRE AMIS", "PARTIE EN LIGNE", false},
    {"Partie entre amis", "Partie en ligne", false},
    {"Jouer  une partie entre amis sur Xbox LIVE", "Jouer une partie en ligne contre un ami", true},
    {"FREUNDSCHAFTSMATCH", "ONLINE-MATCH", false},
    {"Freundschaftsmatch", "Online-Match", false},
    {"Freundschaftsmatch auf Xbox LIVE spielen", "Online-Match gegen einen Freund spielen", true},
    {"PARTITA DEL GIOCATORE", "PARTITA ONLINE", false},
    {"Partita del giocatore", "Partita online", false},
    {"Gioca una partita del giocatore su Xbox LIVE", "Gioca una partita online contro un amico", true},
    {"PARTIDA DE JUGADOR", "PARTIDA EN L\xC3\x8D" "NEA", false},
    {"Partida de jugador", "Partida en l\xC3\xAD" "nea", false},
    {"Jugar una Partida de jugador en Xbox LIVE", "Jugar una partida en l\xC3\xAD" "nea con un amigo", true},
};

// Quick Match (label, caption, help line, no-match prompt) shown as Find Match: it only finds friends' Online Matches.
inline constexpr MenuTextReplacement kFindMatchText[] = {
    {"QUICK MATCH", "FIND MATCH", false},
    {"Quick Match", "Find Match", false},
    {"Join first available session", "Find a friend's Online Match", true},
    {"No quick match was found. Do you wish to create a lobby?",
     "No Online Match was found. Do you wish to create one?", true},
    {"PARTIE RAPIDE", "RECHERCHER", false},
    {"Partie rapide", "Rechercher", false},
    {"Rejoindre la premi\xC3\xA8re session disponible", "Trouver la partie en ligne d'un ami", true},
    {"Aucune partie rapide trouv\xC3\xA9" "e. Cr\xC3\xA9" "er un accueil ?",
     "Aucune partie trouv\xC3\xA9" "e. Cr\xC3\xA9" "er un accueil ?", true},
    {"SCHNELLES MATCH", "MATCH SUCHEN", false},
    {"Schnelles Match", "Match suchen", false},
    {"An erster verf\xC3\xBC" "gbarer Sitzung teilnehmen", "Online-Match eines Freundes finden", true},
    {"Kein Schnelles Match gefunden. M\xC3\xB6" "chtest du eine Lobby einrichten?",
     "Kein Online-Match gefunden. M\xC3\xB6" "chtest du eine Lobby einrichten?", true},
    {"PARTITA RAPIDA", "TROVA PARTITA", false},
    {"Partita rapida", "Trova partita", false},
    {"Partecipa alla prima partita disponibile", "Trova la partita online di un amico", true},
    {"Nessuna partita veloce trovata. Vuoi creare una sala d'attesa?",
     "Nessuna partita online trovata. Vuoi creare una sala d'attesa?", true},
    {"PARTIDA R\xC3\x81PIDA", "BUSCAR PARTIDA", false},
    {"Partida r\xC3\xA1pida", "Buscar partida", false},
    {"\xC3\x9Anete a la primera sesi\xC3\xB3n disponible", "Busca la partida de un amigo", true},
    {"No se ha encontrado ninguna partida r\xC3\xA1pida. \xC2\xBF" "Deseas crear un punto de encuentro?",
     "No se ha encontrado ninguna partida. \xC2\xBF" "Deseas crear un punto de encuentro?", true},
};

// The Game Lobby help lines (screen, spectator, slot rows) without the Xbox Guide actions, Players List and View Gamer
// Card, in EN, FR, DE, IT and ES. A selected slot has nothing left to do, so its line is blank.
inline constexpr MenuTextReplacement kGameLobbyHelpText[] = {
    {"[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Invite Player   "
     "[color=0xFFFFFFFF]\xC2\x80[color=0x01010101] Start Match   "
     "[color=0xFFFFFFFF]\xC2\x84[color=0x01010101] Players List   "
     "[color=0xFFFFFFFF]\xC2\x81[color=0x01010101] Leave Lobby",
     "[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Invite Player   "
     "[color=0xFFFFFFFF]\xC2\x80[color=0x01010101] Start Match   "
     "[color=0xFFFFFFFF]\xC2\x81[color=0x01010101] Leave Lobby", true},
    {"[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Invite Player    "
     "[color=0xFFFFFFFF]\xC2\x84[color=0x01010101] Players List    "
     "[color=0xFFFFFFFF]\xC2\x81[color=0x01010101] Leave Lobby",
     "[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Invite Player    "
     "[color=0xFFFFFFFF]\xC2\x81[color=0x01010101] Leave Lobby", true},
    {"[color=0xFFFFFFFF]\xC2\x8B[color=0x01010101] Select Slot    "
     "[color=0xFFFFFFFF]\xC2\x83[color=0x01010101] View Gamer Card",
     "", true},
    {"[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Invite Player  "
     "[color=0xFFFFFFFF]\xC2\x83[color=0x01010101] View Selected Gamer Card",
     "[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Invite Player", true},
    {"[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Inviter joueur    "
     "[color=0xFFFFFFFF]\xC2\x80[color=0x01010101] Lancer la partie   "
     "[color=0xFFFFFFFF]\xC2\x84[color=0x01010101] Liste des joueurs   "
     "[color=0xFFFFFFFF]\xC2\x81[color=0x01010101] Quitter l'accueil",
     "[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Inviter joueur    "
     "[color=0xFFFFFFFF]\xC2\x80[color=0x01010101] Lancer la partie   "
     "[color=0xFFFFFFFF]\xC2\x81[color=0x01010101] Quitter l'accueil", true},
    {"[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Inviter joueur  "
     "[color=0xFFFFFFFF]\xC2\x84[color=0x01010101] Liste des joueurs    "
     "[color=0xFFFFFFFF]\xC2\x81[color=0x01010101] Quitter l'accueil",
     "[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Inviter joueur  "
     "[color=0xFFFFFFFF]\xC2\x81[color=0x01010101] Quitter l'accueil", true},
    {"[color=0xFFFFFFFF]\xC2\x8B[color=0x01010101] S\xC3\xA9lectionner l'emplacement   "
     "[color=0xFFFFFFFF]\xC2\x83[color=0x01010101] Afficher la carte du joueur",
     "", true},
    {"[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Inviter joueur "
     "[color=0xFFFFFFFF]\xC2\x83[color=0x01010101] Afficher la carte du joueur s\xC3\xA9lectionn\xC3\xA9",
     "[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Inviter joueur", true},
    {"[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Spieler einladen   "
     "[color=0xFFFFFFFF]\xC2\x80[color=0x01010101] Match starten   "
     "[color=0xFFFFFFFF]\xC2\x84[color=0x01010101] Spielerliste   "
     "[color=0xFFFFFFFF]\xC2\x81[color=0x01010101] Lobby verlassen",
     "[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Spieler einladen   "
     "[color=0xFFFFFFFF]\xC2\x80[color=0x01010101] Match starten   "
     "[color=0xFFFFFFFF]\xC2\x81[color=0x01010101] Lobby verlassen", true},
    {"[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Spieler einladen    "
     "[color=0xFFFFFFFF]\xC2\x84[color=0x01010101] Spielerliste    "
     "[color=0xFFFFFFFF]\xC2\x81[color=0x01010101] Lobby verlassen",
     "[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Spieler einladen    "
     "[color=0xFFFFFFFF]\xC2\x81[color=0x01010101] Lobby verlassen", true},
    {"[color=0xFFFFFFFF]\xC2\x8B[color=0x01010101] Slot ausw\xC3\xA4hlen   "
     "[color=0xFFFFFFFF]\xC2\x83[color=0x01010101] Spielerkarte anzeigen",
     "", true},
    {"[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Spieler einladen  "
     "[color=0xFFFFFFFF]\xC2\x83[color=0x01010101] Ausgew\xC3\xA4hlte Spielerkarte anzeigen",
     "[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Spieler einladen", true},
    {"[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Invita giocatore   "
     "[color=0xFFFFFFFF]\xC2\x80[color=0x01010101] Inizia Partita   "
     "[color=0xFFFFFFFF]\xC2\x84[color=0x01010101] Lista giocatori   "
     "[color=0xFFFFFFFF]\xC2\x81[color=0x01010101] Esci da sala d'attesa",
     "[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Invita giocatore   "
     "[color=0xFFFFFFFF]\xC2\x80[color=0x01010101] Inizia Partita   "
     "[color=0xFFFFFFFF]\xC2\x81[color=0x01010101] Esci da sala d'attesa", true},
    {"[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Invita giocatore    "
     "[color=0xFFFFFFFF]\xC2\x84[color=0x01010101] Lista giocatori    "
     "[color=0xFFFFFFFF]\xC2\x81[color=0x01010101] Esci da sala d'attesa",
     "[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Invita giocatore    "
     "[color=0xFFFFFFFF]\xC2\x81[color=0x01010101] Esci da sala d'attesa", true},
    {"[color=0xFFFFFFFF]\xC2\x8B[color=0x01010101] Seleziona slot    "
     "[color=0xFFFFFFFF]\xC2\x83[color=0x01010101] Vedi scheda giocatore",
     "", true},
    {"[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Invita giocatore "
     "[color=0xFFFFFFFF]\xC2\x83[color=0x01010101] Vedi scheda giocatore selezionata",
     "[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Invita giocatore", true},
    {"[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Invitar jugador   "
     "[color=0xFFFFFFFF]\xC2\x80[color=0x01010101] Comenzar partida   "
     "[color=0xFFFFFFFF]\xC2\x84[color=0x01010101] Lista de jugadores   "
     "[color=0xFFFFFFFF]\xC2\x81[color=0x01010101] Salir del punto de encuentro",
     "[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Invitar jugador   "
     "[color=0xFFFFFFFF]\xC2\x80[color=0x01010101] Comenzar partida   "
     "[color=0xFFFFFFFF]\xC2\x81[color=0x01010101] Salir del punto de encuentro", true},
    {"[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Invitar jugador    "
     "[color=0xFFFFFFFF]\xC2\x84[color=0x01010101] Lista de jugadores    "
     "[color=0xFFFFFFFF]\xC2\x81[color=0x01010101] Salir del punto de encuentro",
     "[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Invitar jugador    "
     "[color=0xFFFFFFFF]\xC2\x81[color=0x01010101] Salir del punto de encuentro", true},
    {"[color=0xFFFFFFFF]\xC2\x8B[color=0x01010101] Seleccionar hueco    "
     "[color=0xFFFFFFFF]\xC2\x83[color=0x01010101] Ver tarjeta de jugador",
     "", true},
    {"[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Invitar jugador  "
     "[color=0xFFFFFFFF]\xC2\x83[color=0x01010101] Ver tarjeta de jugador seleccionada",
     "[color=0xFFFFFFFF]\xC2\x82[color=0x01010101] Invitar jugador", true},
};

// Entries are NUL- or LF-terminated. Replacements keep the entry length (space padded) so offsets stay valid;
// a final entry without a terminator is left alone.
template <size_t N>
inline MenuTextPatch PatchMenuTextBuffer(char* data, size_t length, const MenuTextReplacement (&table)[N]) {
  MenuTextPatch patch;
  size_t start = 0;
  for (size_t i = 0; i < length; ++i) {
    if (data[i] != '\0' && data[i] != '\n') continue;
    const size_t size = i - start;
    for (const auto& entry : table) {
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

inline MenuTextPatch PatchMainMenuExitBuffer(char* data, size_t length) {
  return PatchMenuTextBuffer(data, length, kMainMenuExitText);
}

inline MenuTextPatch PatchOnlineMatchBuffer(char* data, size_t length) {
  return PatchMenuTextBuffer(data, length, kOnlineMatchText);
}

inline MenuTextPatch PatchFindMatchBuffer(char* data, size_t length) {
  return PatchMenuTextBuffer(data, length, kFindMatchText);
}

inline MenuTextPatch PatchGameLobbyHelpBuffer(char* data, size_t length) {
  return PatchMenuTextBuffer(data, length, kGameLobbyHelpText);
}

}  // namespace ssf2t
