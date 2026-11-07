#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ssf2t {

struct ScreenSetPatch {
  std::string xml;
  bool multiplayer = false;   // Multiplayer lists only Local Match and Online Match
  bool player_match = false;  // Online Match lists only Quick Match and Create Match
  bool create_match = false;  // Create Player Match has no Private Slots or Number of Players row
  bool main_menu = false;     // Main Menu has no Leaderboards row
  bool pause_menu = false;    // Pause Menu has no Leaderboards row
  bool pause_training = false;  // Training Pause Menu has no Leaderboards row
};

// Edits the UIScreenSet document before the guest parses it. A screen whose layout differs from the one these edits
// were made for is left whole. Nothing is returned when `xml` is not the screen set or neither screen changed.
std::optional<ScreenSetPatch> PatchScreenSet(std::string_view xml);

struct LobbyScriptsPatch {
  std::string xml;
  bool gamer_card = false;    // Y no longer opens the Guide gamer card (XamShowGamerCardUIForXUID)
  bool players_list = false;  // LB no longer opens the Guide players list (XamShowPlayersUI)
  bool slot_help = false;     // no "Select Slot / View Gamer Card" line: a selected slot has nothing left to do
};

// Edits the Game Lobby's script document. Nothing is returned when `xml` holds no GameLobbyUpdate script or no edit
// applied; each edit applies only to the layout it was made for.
std::optional<LobbyScriptsPatch> PatchLobbyScripts(std::string_view xml);

// The guest's helperTextIDs table: per screen, the help line of each activation-grid row, as big-endian words
// {screen hash, rows, screen text, row text[20]}.
inline constexpr uint32_t kHelpTextEntries = 100;
inline constexpr uint32_t kHelpTextEntryBytes = 92;
inline constexpr uint32_t kHelpTextRows = 20;

// The guest's screen key: CRC-32 of the upper-case name.
uint32_t ScreenNameHash(std::string_view name);

struct HelpTextPatch {
  bool multiplayer = false;
  bool player_match = false;
  bool create_match = false;
  bool main_menu = false;
  bool pause_menu = false;
  bool pause_training = false;
};

// Drops the help lines of the rows PatchScreenSet removes, so each remaining row keeps its own line.
HelpTextPatch PatchHelpText(uint8_t* table);

// The help line the guest shows for a whole screen, 0 when the screen has no entry.
uint32_t HelpTextScreenText(const uint8_t* table, std::string_view screen);

inline constexpr char kMultiplayerScreen[] = "MultiplayerGame";
inline constexpr char kPlayerMatchScreen[] = "PlayerMatch";
inline constexpr char kCreatePlayerMatchScreen[] = "CreatePlayerMatch";
inline constexpr char kGameLobbyScreen[] = "gameLobby";
inline constexpr char kMainMenuScreen[] = "MainMenu";
inline constexpr char kPauseMenuScreen[] = "PauseMenu_Regular";
inline constexpr char kPauseTrainingScreen[] = "PauseMenu_Training";

}  // namespace ssf2t
