#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace ssf2t::character {

// The Character Moves screen picks a page by the guest character index (0..16); the Ultra fighters follow it.
inline constexpr int kEvilRyuMoveListIndex = 17;
inline constexpr int kViolentKenMoveListIndex = 18;

// A line of the guest's string table. The ids of the legacy "// Ryu's Moves" block are referenced by no screen, script or
// code, so their lines carry the Ultra fighters' text in every language.
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
inline constexpr std::string_view kShakunetsuCommandId = "Zangief_Greenhand";

// Glyphs are UTF-8 U+0080..U+009F: \x83 = 63214, \x84 = 623, \x85 = 421, \x96 = any punch, \x97 = any kick.
inline constexpr std::array<MoveListText, 9> kEvilRyuMoveListText = {{
    {2, kEvilRyuTitleId, "Evil Ryu"},
    {3, kShakunetsuId, "Shakunetsu Hadoken"},
    {4, kTatsumakiId, "Tatsumaki Senpukyaku"},
    {5, kAshuraForwardId, "Ashura Senku (Forward)"},
    {6, kAshuraBackId, "Ashura Senku (Back)"},
    {7, kSuperNameId, "Shun Goku Satsu"},
    {9, kAshuraForwardCommandId, "\xC2\x84+\xC2\x96\xC2\x96\xC2\x96 or \xC2\x97\xC2\x97\xC2\x97"},
    {10, kAshuraBackCommandId, "\xC2\x85+\xC2\x96\xC2\x96\xC2\x96 or \xC2\x97\xC2\x97\xC2\x97"},
    {38, kShakunetsuCommandId, "\xC2\x83+\xC2\x96"},
}};

inline constexpr std::string_view kViolentKenTitleId = "Ken_SpecialKick1";
inline constexpr std::string_view kAirTatsumakiId = "Ken_SpecialKick2";
inline constexpr std::string_view kRasetsuId = "Ken_SpecialKick3";
inline constexpr std::string_view kInazumaId = "Ken_OcerheadKick";
inline constexpr std::string_view kShinbuId = "Ken_Super";
inline constexpr std::string_view kInazumaCommandId = "Honda_FlyingButt";

// \x86 = forward, \x94 = medium kick.
inline constexpr std::array<MoveListText, 6> kViolentKenMoveListText = {{
    {12, kViolentKenTitleId, "Violent Ken"},
    {13, kAirTatsumakiId, "Airborne Tatsumaki Senpukyaku"},
    {14, kRasetsuId, "Rasetsukyaku"},
    {15, kInazumaId, "Inazuma Kick"},
    {16, kShinbuId, "Shinbu Messatsu"},
    {19, kInazumaCommandId, "\xC2\x86+\xC2\x94"},
}};

// Built from each language's own CMD_INAIR line and \x81+\x97 (214 + any kick).
inline constexpr std::string_view kAirTatsumakiCommandId = "Honda_Torpedo";
inline constexpr uint32_t kAirTatsumakiCommandLine = 18;
inline constexpr uint32_t kInAirLine = 1123;

// Legacy lines that carry the winner banners: each language's own WINS_ line with the Ultra fighter's name.
inline constexpr std::string_view kEvilRyuWinsId = "Ken_HurricaneKick";
inline constexpr uint32_t kEvilRyuWinsLine = 11;
inline constexpr uint32_t kRyuWinsLine = 1516;
inline constexpr std::string_view kViolentKenWinsId = "Honda_HundredHandSlap";
inline constexpr uint32_t kViolentKenWinsLine = 20;
inline constexpr uint32_t kKenWinsLine = 1520;

// The banner in a language's own wording: "RYU WINS" -> "EVIL RYU WINS".
std::string EvilRyuWinsText(std::string_view ryu_wins);
std::string ViolentKenWinsText(std::string_view ken_wins);

// The winner screen quotes come from USF2's win message table: row 17 messages 126..130 for Evil Ryu, row 18 messages
// 131..134 for Violent Ken. A row picks with rand & 15 through 16 entries, so the weights are how often each line occurs.
inline constexpr int kEvilRyuQuotes = 5;
inline constexpr std::array<std::string_view, kEvilRyuQuotes> kEvilRyuQuoteIds = {
    "Zangief_FastLariat", "Zangief_SlowLariat", "Zangief_SpinningPiledrive", "Zangief_RunningGrab",
    "Zangief_DoubleSuplex"};
inline constexpr std::array<uint32_t, kEvilRyuQuotes> kEvilRyuQuoteLines = {39, 40, 41, 42, 43};
inline constexpr std::array<uint8_t, kEvilRyuQuotes> kEvilRyuQuoteWeights = {4, 3, 3, 3, 3};
inline constexpr std::array<uint8_t, 4> kViolentKenQuoteWeights = {5, 4, 4, 3};
inline constexpr int kViolentKenQuotes = 4;
inline constexpr std::array<std::string_view, kViolentKenQuotes> kViolentKenQuoteIds = {
    "Honda_OchioThrow", "Honda_Super", "ChunLi_Kikoken", "ChunLi_UpKick"};
inline constexpr std::array<uint32_t, kViolentKenQuotes> kViolentKenQuoteLines = {21, 22, 24, 25};

// The quotes in the language of a string table, picked by the wording of its WINS_KEN line.
const std::array<std::string_view, kViolentKenQuotes>& ViolentKenQuotes(std::string_view ken_wins);
const std::array<std::string_view, kEvilRyuQuotes>& EvilRyuQuotes(std::string_view ryu_wins);

// The ending's ten USF2 lines, end_171..end_180, on legacy lines no screen, script or code reads.
inline constexpr int kViolentKenEndingLines = 10;
inline constexpr std::array<std::string_view, kViolentKenEndingLines> kViolentKenEndingIds = {
    "ChunLi_SpinningBirdKick", "ChunLi_AerialSpinningBirdKick", "ChunLi_LightningLegs", "ChunLi_Super",
    "Blanka_HorizontalRoll",   "Blanka_VerticalRoll",           "Blanka_RainbowRoll",   "Blanka_Electricity",
    "Blanka_Hop",              "Blanka_Super"};
inline constexpr std::array<uint32_t, kViolentKenEndingLines> kViolentKenEndingTableLines = {26, 27, 28, 29, 31,
                                                                                            32, 33, 34, 35, 36};
const std::array<std::string_view, kViolentKenEndingLines>& ViolentKenEnding(std::string_view ken_wins);

// Existing ids the page reuses.
inline constexpr std::string_view kNotAvailableId = "tid_Ryu_C_FakeHadouken";  // "n/a"
inline constexpr std::string_view kRemixedSuperId = "tid_Akuma_R_Super";        // LP, LP, F, LK, HP
inline constexpr std::string_view kRasetsuCommandId = "tid_Cammy_C_ThrustKick";  // 623 + any kick
inline constexpr std::string_view kShinbuCommandId = "tid_Cammy_C_Super";        // 236 236 + any kick

// Moves the host implements need the Remixed rules; the guest's own Ryu moves work in both.
struct EvilRyuMove {
  std::string_view name_id;
  std::string_view classic_id;
  std::string_view remixed_id;
  bool host_move;
};

inline constexpr std::array<EvilRyuMove, 7> kEvilRyuMoves = {{
    {"CMD_HADOKEN", "tid_Ryu_C_Hadouken", "tid_Ryu_R_Hadouken", false},
    {kShakunetsuId, kShakunetsuCommandId, kShakunetsuCommandId, false},
    {"CMD_SHORYUKEN", "tid_Ryu_C_Shoryuken", "tid_Ryu_R_Shoryuken", false},
    {kTatsumakiId, "tid_Ryu_C_HurricaneKick", "tid_Ryu_R_HurricaneKick", false},
    {kAshuraForwardId, kNotAvailableId, kAshuraForwardCommandId, true},
    {kAshuraBackId, kNotAvailableId, kAshuraBackCommandId, true},
    {kSuperNameId, kNotAvailableId, kRemixedSuperId, true},
}};

inline constexpr std::array<EvilRyuMove, 7> kViolentKenMoves = {{
    {"CMD_HADOKEN", "tid_Ken_C_Hadouken", "tid_Ken_R_Hadouken", false},
    {"CMD_SHORYUKEN", "tid_Ken_C_Shoryuken", "tid_Ken_R_Shoryuken", false},
    {kTatsumakiId, "tid_Ken_C_HurricaneKick", "tid_Ken_R_HurricaneKick", false},
    {kAirTatsumakiId, kAirTatsumakiCommandId, kAirTatsumakiCommandId, false},
    {kRasetsuId, kNotAvailableId, kRasetsuCommandId, true},
    {kInazumaId, kNotAvailableId, kInazumaCommandId, true},
    {kShinbuId, kNotAvailableId, kShinbuCommandId, true},
}};

// Edits the UIScreenSet document: adds the Ultra fighters' pages to the Character Moves screen. False and `xml`
// unchanged when the screen does not have the layout this was made for.
bool PatchCharacterMovesScreen(std::string& xml);

// Edits the script document that selects the page: indices 17 and 18 show the Ultra pages. False when `xml` is not that
// document or has another layout.
bool PatchCharacterMovesScripts(std::string& xml);

// Puts the Ultra fighters' text into the placeholder lines of a language's string table. Returns the page lines changed.
uint32_t PatchMoveListText(std::string& table);

// True once both documents are patched; only then may the page index be reported.
bool CharacterMovesPageReady();

}  // namespace ssf2t::character
