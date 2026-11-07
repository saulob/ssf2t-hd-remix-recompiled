#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "character/select_mode.h"

namespace ssf2t::character {

struct Base013VersusCpu {
  static constexpr const char* kName = "base013";
  static constexpr uint32_t kUpdateLocalLobby = 0x823CCDD8;
  static constexpr uint32_t kInitLocalLobby = 0x823C9530;
  static constexpr uint32_t kIsSlotOccupied = 0x823C7FC0;
  static constexpr uint32_t kIsSlotOccupiedResults = 0x823C80A8;
  static constexpr uint32_t kGetPlayerName = 0x823CD580;
  static constexpr uint32_t kTextPlayerName = 0x823D4078;
  static constexpr uint32_t kTextHudPlayerName = 0x823D40A0;
  static constexpr uint32_t kSetGameDifficulty = 0x823C7618;
  static constexpr uint32_t kEnterSelect = 0x823E9B98;
  static constexpr uint32_t kSelectInput = 0x82247E90;
  static constexpr uint32_t kLoadMatch = 0x82248188;
  static constexpr uint32_t kGameMode = 0x83CA294F;
  static constexpr uint32_t kLobbySlots = 0x83A37C08;
  static constexpr uint32_t kSelectDone = 0x83C133A4;
  static constexpr uint32_t kSelectMask = 0x83C133A7;
  static constexpr uint32_t kSideState = 0x83C133C0;
  static constexpr uint32_t kColourPrompt = 0x83CA6C78;
  static constexpr uint32_t kSideHuman = 0x83C133AC;
  static constexpr uint32_t kGameState = 0x83CD52DC;
  static constexpr uint32_t kFighterBlock = 0x83E09CC0;
};

struct TitleUpdate023VersusCpu {
  static constexpr const char* kName = "tu023";
  static constexpr uint32_t kUpdateLocalLobby = 0x823CD3B8;
  static constexpr uint32_t kInitLocalLobby = 0x823C9B28;
  static constexpr uint32_t kIsSlotOccupied = 0x823C85B8;
  static constexpr uint32_t kIsSlotOccupiedResults = 0x823C86A0;
  static constexpr uint32_t kGetPlayerName = 0x823CDB60;
  static constexpr uint32_t kTextPlayerName = 0x823D4740;
  static constexpr uint32_t kTextHudPlayerName = 0x823D4768;
  static constexpr uint32_t kSetGameDifficulty = 0x823C7C10;
  static constexpr uint32_t kEnterSelect = 0x823EA2D0;
  static constexpr uint32_t kSelectInput = 0x82247ED8;
  static constexpr uint32_t kLoadMatch = 0x822481D0;
  static constexpr uint32_t kGameMode = 0x83CA2E67;
  static constexpr uint32_t kLobbySlots = 0x83A37C08;
  static constexpr uint32_t kSelectDone = 0x83C13424;
  static constexpr uint32_t kSelectMask = 0x83C13427;
  static constexpr uint32_t kSideState = 0x83C13440;
  static constexpr uint32_t kColourPrompt = 0x83CA7190;
  static constexpr uint32_t kSideHuman = 0x83C1342C;
  static constexpr uint32_t kGameState = 0x83CD57F4;
  static constexpr uint32_t kFighterBlock = 0x83E0A1D8;
};

inline constexpr uint32_t kFighterHumanOffset = 898;
inline constexpr uint32_t kFighterOldVersionOffset = 950;
inline constexpr uint32_t kFighterPadOffset = 912;
inline constexpr uint32_t kFighterPadPreviousOffset = 914;
inline constexpr uint32_t kFighterLockedOffset = 899;
inline constexpr uint32_t kFighterConfirmWindowOffset = 940;
inline constexpr uint32_t kFighterCommitOffset = 162;
inline constexpr uint32_t kGameDifficultyOffset = 901;
inline constexpr uint32_t kGameLadderOffset = 1232;

inline constexpr int32_t kEmptyLobbySlot = -1;
// Reads as seated to the lobby's comparisons and is never an index: it only exists inside the lobby update.
inline constexpr int32_t kCpuLobbySlot = -2;

// The AI table has eight rows of sixteen ladder steps. The Arcade menu only reaches the first four rows.
inline constexpr int kCpuDifficultyLevels = 8;
inline constexpr int kCpuLadderSteps = 16;

// Out of the menu's range, so the lobby script can reach the port through the one native that takes a difficulty.
inline constexpr uint32_t kCycleCpuOpponentArgument = kCpuDifficultyLevels;
inline constexpr uint32_t kRemoveCpuOpponentArgument = kCycleCpuOpponentArgument + 1;

// The native answers 0 past seat 7, so only the lobby's own script can ever get a yes from this seat.
inline constexpr int32_t kCpuActiveQuerySeat = 8;

constexpr int EmptyLobbySide(int32_t slot0, int32_t slot1) {
  const bool seated0 = slot0 >= 0;
  const bool seated1 = slot1 >= 0;
  if (seated0 == seated1) return -1;
  return seated0 ? 1 : 0;
}

constexpr uint8_t CpuGuestDifficulty(int level) { return uint8_t(level - 1); }

// Remix's AI bounds rise with the Arcade opponent count, which never moves in a Local Match. Without a step the top
// rows would stop at the first opponent's bounds; Classic's rows do not depend on it.
constexpr uint16_t CpuLadderStep(int level) { return uint16_t((level - 1) * 2 + 1); }

class VersusCpuOpponent {
 public:
  void Reset() {
    level_ = 0;
    side_ = -1;
    in_lobby_ = true;
  }

  void LeaveLobby() { in_lobby_ = false; }

  // Two seated players leave no side for a CPU.
  void Cycle(int empty_side) {
    if (empty_side < 0) return;
    side_ = empty_side;
    level_ = level_ % kCpuDifficultyLevels + 1;
  }

  // Swapping slots moves the CPU with them; with nobody seated there is no match to offer.
  void FollowSeats(int empty_side) {
    if (level_ == 0) return;
    if (empty_side < 0) {
      Reset();
    } else {
      side_ = empty_side;
    }
  }

  int SideIn(uint8_t game_mode) const { return game_mode == kLocalVersusGameMode && level_ != 0 ? side_ : -1; }
  int level() const { return level_; }
  bool in_lobby() const { return in_lobby_; }

 private:
  int level_ = 0;
  int side_ = -1;
  bool in_lobby_ = false;
};

VersusCpuOpponent& GlobalVersusCpu();

// A side has finished its pick once the select marked it complete or its final state ran; neither is ever cleared
// before the select ends.
constexpr bool PickFinished(uint8_t final_state, uint8_t complete_mask, int side) {
  return final_state != 0 || (complete_mask >> side & 1) != 0;
}

// A confirm holds the fighter for this long before locking it, and nothing but a pending Akuma commit needs the wait.
inline constexpr uint8_t kConfirmWindowFrames = 90;
inline constexpr uint8_t kAkumaCommitPending = 2;

// The window's first frame is what commits a secret fighter, so it has to have run once.
constexpr bool ShortensConfirmWindow(uint8_t window, uint8_t commit) {
  return window > 1 && window < kConfirmWindowFrames && commit != kAkumaCommitPending;
}

// Past this state a locked side has answered NORMAL / SUPER and reads no pad, unless it picks a colour afterwards.
inline constexpr uint8_t kPromptsAnsweredState = 3;

constexpr bool PlayerHandedOver(uint8_t locked, uint8_t side_state, uint8_t colour_prompt, bool finished) {
  return finished || (locked != 0 && side_state >= kPromptsAnsweredState && colour_prompt == 0);
}

// The player finishes their own pick first, then the same pad picks the opponent, as Training does. Both words are
// copied: the select derives the press edges from them itself.
struct SelectPads {
  uint16_t current = 0;
  uint16_t previous = 0;

  constexpr uint16_t Pressed() const { return uint16_t(current & ~previous); }
};

constexpr SelectPads CpuSidePads(bool player_done, SelectPads player) { return player_done ? player : SelectPads{}; }

inline constexpr char kCpuLabel[] = "CPU";

constexpr bool ReadsAsCpu(int32_t side, int cpu_side) { return cpu_side >= 0 && side == cpu_side; }

inline std::string CpuOpponentName(int level, bool in_lobby, bool portuguese) {
  if (!in_lobby) return kCpuLabel;
  return std::string(portuguese ? "Dificuldade CPU " : "CPU Difficulty ") + std::to_string(level);
}

inline constexpr std::string_view kLobbyScriptOpen = "<script name=\"LocalLobbyUpdateData\">";
inline constexpr std::string_view kLobbyInputAnchor = "if (IsNewlyPressed(LEFT, 0) or IsNewlyPressed(RIGHT, 0))";
inline constexpr std::string_view kLobbyCycleCpu =
    "if (IsNewlyPressed(BUTTON3, 0))\n"
    "\t\t{\n"
    "\t\t\tGeneralScreenToggleItemNoTextUpdate();\n"
    "\t\t\tSetGameDifficulty(8);\n"
    "\t\t}\n"
    "\t\tif (IsNewlyPressed(BUTTON4, 0))\n"
    "\t\t{\n"
    "\t\t\tif (IsSlotOccupied(8))\n"
    "\t\t\t{\n"
    "\t\t\t\tGeneralScreenToggleItemNoTextUpdate();\n"
    "\t\t\t\tSetGameDifficulty(9);\n"
    "\t\t\t}\n"
    "\t\t}\n"
    "\t\t";

inline constexpr std::string_view kLobbyFooterScriptOpen = "<script name=\"UI_LocalLobbyUpdateData\">";
inline constexpr std::string_view kLobbyFooterAnchor = "if ( IsSlotOccupied(0) and IsSlotOccupied(1) )";
inline constexpr std::string_view kLobbyCpuFooter =
    "if (IsSlotOccupied(8))\n"
    "\t  {\n"
    "\t\t  UIText_SetString_TextID( selectionHelpText, tid_NetOptionsHelp );\n"
    "\t  }\n"
    "\t  else ";

inline bool InsertInScript(std::string& xml, std::string_view open, std::string_view anchor, std::string_view code) {
  const size_t script = xml.find(open);
  if (script == std::string::npos) return false;
  const size_t end = xml.find("</script>", script);
  const size_t at = xml.find(anchor, script);
  if (end == std::string::npos || at == std::string::npos || at > end) return false;
  xml.insert(at, code);
  return true;
}

inline bool PatchLobbyCpuScript(std::string& xml) {
  return InsertInScript(xml, kLobbyScriptOpen, kLobbyInputAnchor, kLobbyCycleCpu);
}

inline bool PatchLobbyFooterScript(std::string& xml) {
  return InsertInScript(xml, kLobbyFooterScriptOpen, kLobbyFooterAnchor, kLobbyCpuFooter);
}

// Line-aligned across the language tables. Nothing in the game shows the footer line, so it can hold the third state.
inline constexpr size_t kLobbyJoinHelpLine = 1687;
inline constexpr size_t kLobbyReadyHelpLine = 1688;
inline constexpr size_t kLobbyCpuHelpLine = 1702;
inline constexpr std::string_view kHelpGlyphOpen = "[color=0xFFFFFFFF]";
inline constexpr std::string_view kHelpGlyphAccept = "\xC2\x80";
inline constexpr std::string_view kHelpGlyphSwap = "\xC2\x8A";
inline constexpr std::string_view kHelpGlyphBack = "\xC2\x81";
inline constexpr std::string_view kHelpGlyphAdd = "\xC2\x82";
inline constexpr std::string_view kHelpGlyphRemove = "\xC2\x83";
inline constexpr std::string_view kHelpLabelOpen = "[color=0x01010101] ";
inline constexpr std::string_view kHelpWideGap = "          ";
inline constexpr std::string_view kHelpGap = "     ";

// Each table says "Back" in its own language. The swap label is only set where five actions would not fit otherwise.
struct LobbyFooterWords {
  std::string_view back;
  std::string_view add;
  std::string_view change;
  std::string_view remove;
  std::string_view swap;
};

inline constexpr LobbyFooterWords kLobbyFooterWords[] = {
    {"Back", "Add CPU", "Change Difficulty", "Remove CPU", ""},
    {"Retour", "Ajouter CPU", "Difficulté", "Retirer CPU", "Échanger"},
    {"Zurück", "CPU hinzufügen", "Schwierigkeit", "CPU entfernen", "Seite wechseln"},
    {"Indietro", "Aggiungi CPU", "Cambia difficoltà", "Rimuovi CPU", ""},
    {"Atrás", "Añadir CPU", "Dificultad", "Quitar CPU", ""},
    {"Voltar", "Adicionar CPU", "Mudar Dificuldade", "Remover CPU", ""},
};

inline const LobbyFooterWords& FooterWordsFor(std::string_view line) {
  for (const LobbyFooterWords& words : kLobbyFooterWords) {
    if (line.ends_with(" " + std::string(words.back))) return words;
  }
  return kLobbyFooterWords[0];
}

inline std::string HelpEntry(std::string_view glyph, std::string_view label) {
  return std::string(kHelpGlyphOpen) + std::string(glyph) + std::string(kHelpLabelOpen) + std::string(label);
}

// The entries go before Swap Slots, and the gaps close up so the longer line still fits the screen.
inline bool InsertHelpEntries(std::string& line, const std::string& entries) {
  const std::string swap = std::string(kHelpGlyphOpen) + std::string(kHelpGlyphSwap);
  const std::string back = std::string(kHelpGlyphOpen) + std::string(kHelpGlyphBack);
  if (line.find(std::string(kHelpGlyphOpen) + std::string(kHelpGlyphAccept)) != 0) return false;
  const size_t at = line.find(swap);
  if (at == std::string::npos || line.find(back, at) == std::string::npos) return false;
  line.insert(at, entries);
  for (size_t gap = line.find(kHelpGap); gap != std::string::npos; gap = line.find(kHelpGap, gap + kHelpGap.size())) {
    const size_t kept = gap + kHelpGap.size();
    line.erase(kept, line.find_first_not_of(' ', kept) - kept);
  }
  return true;
}

inline bool PatchAddCpuHelpLine(std::string& line) {
  return InsertHelpEntries(line, HelpEntry(kHelpGlyphAdd, FooterWordsFor(line).add) + std::string(kHelpGap));
}

inline void ReplaceSwapLabel(std::string& line, std::string_view label) {
  const size_t start = line.find(kHelpGlyphSwap) + kHelpGlyphSwap.size() + kHelpLabelOpen.size();
  line.replace(start, line.find(kHelpGap, start) - start, label);
}

inline bool PatchCpuActiveHelpLine(std::string& line) {
  const LobbyFooterWords& words = FooterWordsFor(line);
  const std::string gap(kHelpGap);
  if (!InsertHelpEntries(line, HelpEntry(kHelpGlyphAdd, words.change) + gap + HelpEntry(kHelpGlyphRemove, words.remove) + gap)) {
    return false;
  }
  if (!words.swap.empty()) ReplaceSwapLabel(line, words.swap);
  return true;
}

struct TextLine {
  size_t start = std::string::npos;
  size_t size = 0;
};

inline TextLine FindTextLine(const std::string& table, size_t index) {
  size_t start = 0;
  for (size_t line = 0; line < index; ++line) {
    const size_t newline = table.find('\n', start);
    if (newline == std::string::npos) return {};
    start = newline + 1;
  }
  const size_t end = table.find_first_of("\r\n", start);
  return {start, (end == std::string::npos ? table.size() : end) - start};
}

// The later line is written first, so the earlier line's offsets stay valid.
inline uint32_t PatchLobbyHelpText(std::string& table) {
  uint32_t patched = 0;
  const TextLine ready = FindTextLine(table, kLobbyReadyHelpLine);
  const TextLine spare = FindTextLine(table, kLobbyCpuHelpLine);
  if (ready.start != std::string::npos && spare.start != std::string::npos) {
    std::string text = table.substr(ready.start, ready.size);
    if (PatchCpuActiveHelpLine(text)) {
      table.replace(spare.start, spare.size, text);
      ++patched;
    }
  }
  const TextLine join = FindTextLine(table, kLobbyJoinHelpLine);
  if (join.start != std::string::npos) {
    std::string text = table.substr(join.start, join.size);
    if (PatchAddCpuHelpLine(text)) {
      table.replace(join.start, join.size, text);
      ++patched;
    }
  }
  return patched;
}

}  // namespace ssf2t::character
