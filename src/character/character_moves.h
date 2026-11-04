#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace ssf2t::character {

// The Character Moves screen picks a page by the guest character index (0..16); Evil Ryu is the next index.
inline constexpr int kEvilRyuMoveListIndex = 17;

// A line of the guest's string table. The ids of the legacy "// Ryu's Moves" block are referenced by no screen, script or
// code, so their lines carry the Evil Ryu page text in every language.
struct MoveListText {
  uint32_t line;
  std::string_view id;
  std::string_view text;
};

inline constexpr std::string_view kEvilRyuTitleId = "Ryu_Hadouken";
inline constexpr std::string_view kShakunetsuId = "Ryu_RedHadoken";
inline constexpr std::string_view kTatsumakiId = "Ryu_FakeHadouken";
inline constexpr std::string_view kAshuraForwardId = "Ryu_Shoryuken";
inline constexpr std::string_view kAshuraBackId = "Ryu_HurricaneKick";
inline constexpr std::string_view kSuperNameId = "Ryu_Super";
inline constexpr std::string_view kAshuraForwardCommandId = "Ken_Hadouken";
inline constexpr std::string_view kAshuraBackCommandId = "Ken_Shoryuken";

// Glyphs are UTF-8 U+0080..U+009F: \x84 = 623, \x85 = 421, \x96 = any punch, \x97 = any kick.
inline constexpr std::array<MoveListText, 8> kEvilRyuMoveListText = {{
    {2, kEvilRyuTitleId, "Evil Ryu"},
    {3, kShakunetsuId, "Shakunetsu Hadoken"},
    {4, kTatsumakiId, "Tatsumaki Senpukyaku"},
    {5, kAshuraForwardId, "Ashura Senku (Forward)"},
    {6, kAshuraBackId, "Ashura Senku (Back)"},
    {7, kSuperNameId, "Shun Goku Satsu"},
    {9, kAshuraForwardCommandId, "\xC2\x84+\xC2\x96\xC2\x96\xC2\x96 or \xC2\x97\xC2\x97\xC2\x97"},
    {10, kAshuraBackCommandId, "\xC2\x85+\xC2\x96\xC2\x96\xC2\x96 or \xC2\x97\xC2\x97\xC2\x97"},
}};

// Legacy line that carries the winner banner: each language's own WINS_RYU line with Evil Ryu's name.
inline constexpr std::string_view kEvilRyuWinsId = "Ken_HurricaneKick";
inline constexpr uint32_t kEvilRyuWinsLine = 11;
inline constexpr uint32_t kRyuWinsLine = 1516;

// The banner in a language's own wording: "RYU WINS" -> "EVIL RYU WINS".
std::string EvilRyuWinsText(std::string_view ryu_wins);

// Existing ids the page reuses.
inline constexpr std::string_view kNotAvailableId = "tid_Ryu_C_FakeHadouken";  // "n/a"
inline constexpr std::string_view kRemixedSuperId = "tid_Akuma_R_Super";        // LP, LP, F, LK, HP

// Moves the host implements need the Remixed rules; the guest's own Ryu moves work in both.
struct EvilRyuMove {
  std::string_view name_id;
  std::string_view classic_id;
  std::string_view remixed_id;
  bool host_move;
};

inline constexpr std::array<EvilRyuMove, 7> kEvilRyuMoves = {{
    {"CMD_HADOKEN", "tid_Ryu_C_Hadouken", "tid_Ryu_R_Hadouken", false},
    {kShakunetsuId, "tid_Ryu_C_RedHadouken", "tid_Ryu_R_RedHadouken", false},
    {"CMD_SHORYUKEN", "tid_Ryu_C_Shoryuken", "tid_Ryu_R_Shoryuken", false},
    {kTatsumakiId, "tid_Ryu_C_HurricaneKick", "tid_Ryu_R_HurricaneKick", false},
    {kAshuraForwardId, kNotAvailableId, kAshuraForwardCommandId, true},
    {kAshuraBackId, kNotAvailableId, kAshuraBackCommandId, true},
    {kSuperNameId, kNotAvailableId, kRemixedSuperId, true},
}};

// Edits the UIScreenSet document: adds the Evil Ryu page to the Character Moves screen. False and `xml` unchanged
// when the screen does not have the layout this was made for.
bool PatchCharacterMovesScreen(std::string& xml);

// Edits the script document that selects the page: index 17 shows the Evil Ryu page. False when `xml` is not that
// document or has another layout.
bool PatchCharacterMovesScripts(std::string& xml);

// Puts the Evil Ryu page text into the placeholder lines of a language's string table. Returns the lines changed.
uint32_t PatchMoveListText(std::string& table);

// True once both documents are patched; only then may the page index be reported.
bool CharacterMovesPageReady();

}  // namespace ssf2t::character
