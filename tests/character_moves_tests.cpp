#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "character/character_moves.h"
#include "character/fighter_identity.h"
#include "character/select_mode.h"
#include "character/ultra_ending.h"
#include "character/ultra_quote.h"
#include "updater/third_party/miniz/miniz.h"

namespace {

using namespace ssf2t::character;

int failures = 0;

void Check(bool condition, const char* what) {
  if (condition) return;
  std::printf("FAIL %s\n", what);
  ++failures;
}

std::vector<std::string> Lines(const std::string& text) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    lines.push_back(text.substr(start, end - start));
    start = end + 1;
  }
  return lines;
}

size_t Count(const std::string& text, std::string_view needle) {
  size_t count = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) ++count;
  return count;
}

bool Balanced(const std::string& xml) {
  for (const char* tag : {"UIText", "UIGraphic", "UIElementGroup"}) {
    if (Count(xml, std::string("<") + tag + ">") != Count(xml, std::string("</") + tag + ">")) return false;
  }
  return true;
}

std::string Group(const std::string& xml, std::string_view name) {
  const size_t named = xml.find("<name>" + std::string(name) + "</name>");
  if (named == std::string::npos) return {};
  const size_t begin = xml.rfind("<UIElementGroup>", named);
  int depth = 0;
  for (size_t at = begin; at < xml.size();) {
    const size_t open = xml.find("<UIElementGroup>", at);
    const size_t close = xml.find("</UIElementGroup>", at);
    if (open != std::string::npos && open < close) {
      ++depth;
      at = open + 16;
    } else {
      --depth;
      at = close + 17;
      if (depth == 0) return xml.substr(begin, at - begin);
    }
  }
  return {};
}

std::string TextOf(const std::vector<std::string>& ids, const std::vector<std::string>& table, std::string_view id) {
  for (size_t i = 0; i < ids.size() && i < table.size(); ++i) {
    if (ids[i] == id) return table[i];
  }
  return "<missing>";
}

const char* kMiniScreen =
    "<UIScreenSet>\n\t<UIScreen>\n\t\t<name>CharacterMoves</name>\n\t\t\t\t<UIElementGroup>\n"
    "\t\t\t\t\t<name>moves_akuma</name>\n\t\t\t\t\t<UIText>\n\t\t\t\t\t\t<name>move_akuma1</name>\n\t\t\t\t\t</UIText>\n"
    "\t\t\t\t\t<UIElementGroup>\n\t\t\t\t\t\t<name>moves_akuma_C</name>\n\t\t\t\t\t</UIElementGroup>\n"
    "\t\t\t\t</UIElementGroup>\n\t\t\t\t<UIElementGroup>\n\t\t\t\t\t<name>moves_ryu</name>\n\t\t\t\t</UIElementGroup>\n"
    "\t</UIScreen>\n</UIScreenSet>\n";

const char* kMiniScripts =
    "<scripts>\n  <script name=\"hide_moves_except\">\n    UIElement_SetColor_FloatsA(moves_akuma,0.0f);      \n"
    "    if( GetResultRegister() eq 0 )\n    {\n      UIElement_SetColor_FloatsA(moves_ryu,1.0f);\n    }\n"
    "    else if( GetResultRegister() eq 16 )\n    {\n      UIElement_SetColor_FloatsA(moves_akuma,1.0f);\n    }    \n"
    "  </script>\n</scripts>\n";

void TestSynthetic() {
  std::string screen = kMiniScreen;
  const std::string original = screen;
  Check(PatchCharacterMovesScreen(screen), "the screen patch applies");
  Check(Balanced(screen), "the patched screen stays balanced");
  Check(!PatchCharacterMovesScreen(screen), "the screen patch applies once");
  const std::string page = Group(screen, "moves_evilryu");
  const std::string violent = Group(screen, "moves_violentken");
  Check(!page.empty(), "the Evil Ryu page is a group of the screen");
  Check(!violent.empty(), "the Violent Ken page is a group of the screen");
  Check(screen.find(page) < screen.find(violent), "the Violent Ken page follows the Evil Ryu page");
  Check(Group(screen, "moves_akuma") == Group(original, "moves_akuma"), "the Akuma page is untouched");
  Check(Group(screen, "moves_ryu") == Group(original, "moves_ryu"), "the Ryu page is untouched");
  std::string without = screen;
  for (const std::string& inserted : {violent, page}) {
    const size_t at = without.find(inserted);
    Check(at != std::string::npos, "the page text is contiguous");
    if (at != std::string::npos) {
      const size_t line = without.rfind('\n', at);
      without.erase(line, at + inserted.size() - line);
    }
  }
  Check(without == original, "the patch only inserts the pages");

  std::string wrong = "<UIScreenSet><UIScreen><name>Other</name></UIScreen></UIScreenSet>";
  Check(!PatchCharacterMovesScreen(wrong), "a screen set without the Akuma page is left whole");

  std::string scripts = kMiniScripts;
  Check(PatchCharacterMovesScripts(scripts), "the script patch applies");
  Check(scripts.find("eq 17 )") != std::string::npos && scripts.find("moves_evilryu,1.0f") != std::string::npos,
        "index 17 shows the Evil Ryu page");
  Check(scripts.find("eq 18 )") != std::string::npos && scripts.find("moves_violentken,1.0f") != std::string::npos,
        "index 18 shows the Violent Ken page");
  Check(scripts.find("moves_evilryu,0.0f") != std::string::npos && scripts.find("moves_violentken,0.0f") != std::string::npos,
        "every page is hidden before one is shown");
  Check(scripts.find("eq 16 )") != std::string::npos && scripts.find("moves_akuma,1.0f") != std::string::npos,
        "the Akuma branch stays");
  Check(!PatchCharacterMovesScripts(scripts), "the script patch applies once");
  std::string other = "<scripts><script name=\"other\">x();</script></scripts>";
  Check(!PatchCharacterMovesScripts(other), "another script document is left whole");

  std::string table;
  for (int i = 0; i < 12; ++i) table += "tid_line" + std::to_string(i) + "\n";
  std::string patched = table;
  Check(PatchMoveListText(patched) == 0, "a table without the block header is left whole");
  table = "1989\t1\n// Ryu's Moves\n";
  for (int i = 0; i < 40; ++i) table += "line" + std::to_string(i) + "\n";
  patched = table;
  const size_t page_lines = kEvilRyuMoveListText.size() + kViolentKenMoveListText.size();
  Check(PatchMoveListText(patched) == page_lines, "every page line is replaced");
  const std::vector<std::string> before = Lines(table), after = Lines(patched);
  size_t changed = 0;
  for (size_t i = 0; i < before.size(); ++i) changed += before[i] != after[i];
  Check(changed == page_lines && after[2] == "Evil Ryu" && after[12] == "Violent Ken", "only the page lines change");
  std::string short_table = "tid_a\ntid_b\n";
  Check(PatchMoveListText(short_table) == 0 && short_table == "tid_a\ntid_b\n", "a table without the lines is left whole");
  std::string foreign;
  for (int i = 0; i < 12; ++i) foreign += "text" + std::to_string(i) + "\n";
  Check(PatchMoveListText(foreign) == 0, "lines that are not placeholders are never replaced");
}

void TestEnding() {
  using F = FighterIdentity;
  const std::array<F, 2> p1{F::kViolentKen, F::kGuest};
  const std::array<F, 2> p2{F::kGuest, F::kViolentKen};
  const std::array<uint32_t, 2> ken{4, 7};
  const std::array<uint32_t, 2> ken_right{7, 4};
  Check(UltraEndingSide(4, F::kViolentKen, 4, p1, {true, false}, ken) == 0, "P1 Violent Ken gets his ending");
  Check(UltraEndingSide(4, F::kViolentKen, 4, p2, {false, true}, ken_right) == 1, "so does P2");
  Check(UltraEndingSide(4, F::kViolentKen, 4, {F::kGuest, F::kGuest}, {false, false}, ken) == -1, "Ken keeps Ken's ending");
  Check(UltraEndingSide(4, F::kViolentKen, 4, p1, {false, false}, ken) == -1, "leaving the cell gives Ken's ending back");
  Check(UltraEndingSide(0, F::kViolentKen, 4, p1, {true, false}, ken) == -1, "another winner keeps that ending");
  Check(UltraEndingSide(4, F::kViolentKen, 4, p1, {true, false}, {7, 4}) == -1, "a request on another fighter is no ending");
  Check(UltraEndingSide(0, F::kEvilRyu, 0, {F::kEvilRyu, F::kGuest}, {true, false}, {0, 7}) == 0, "the rule holds per identity");

  Check(kEndingCutSeconds[1] * 60 == 1200 && kEndingCutSeconds[2] * 60 == 2100 && kEndingCutSeconds[3] * 60 == 2400,
        "the cuts keep USF2's frame numbers");
  Check(kEndingSeconds * 60 == 3000 && kEndingLineSeconds * 60 == 300, "the scene keeps USF2's length and line time");
  for (const std::string_view wins : {"KEN WINS", "KEN GAGNE", "KEN SIEGT", "KEN VINCE", "KEN GANA", "KEN VENCE"}) {
    for (const std::string_view line : ViolentKenEnding(wins)) {
      Check(!line.empty() && line.find_first_of("\r\n") == std::string_view::npos, "a line fits one table line");
    }
  }
  for (const std::string_view line : ViolentKenEnding("KEN VENCE")) {
    for (const std::string_view missing : {"\xC3\x80", "\xC3\x81", "\xC3\x82", "\xC3\x83"}) {
      Check(line.find(missing) == std::string_view::npos, "pt-BR uses no glyph the fonts lack");
    }
  }
  Check(ViolentKenEnding("KEN WINS")[9].find("waiting for me at the end of it") != std::string_view::npos,
        "the last line is end_180");

  std::string other = "<UIScreenSet></UIScreenSet>";
  Check(!PatchEndingScreen(other) && other == "<UIScreenSet></UIScreenSet>", "another screen set is left alone");
  other = "<UIElementDescriptors></UIElementDescriptors>";
  Check(!PatchEndingAnimations(other), "another descriptor file is left alone");
  const std::string ken_screen =
      "<UIScreenSet><UIScreen><name>EndKen</name><uiElements><UIElementGroup><name>end_ken</name></UIElementGroup>\n"
      "</uiElements></UIScreen></UIScreenSet>";
  std::string screen = ken_screen;
  Check(PatchEndingScreen(screen), "EndKen takes the group");
  Check(!PatchEndingScreen(screen), "only once");
  Check(screen.find("<name>end_ken</name>") == ken_screen.find("<name>end_ken</name>"), "Ken's group stays in place");
  Check(Count(screen, "<UIGraphic>") == kEndingCuts && Count(screen, "<UIText>") == kViolentKenEndingLines,
        "four cuts and ten lines");
  Check(Count(screen, "<UIElementGroup>") == 2 && Count(screen, "</UIElementGroup>") == 2, "balanced");
  Check(screen.find(EndingFrameName(3)) != std::string::npos, "the hook finds the last frame by name");
  for (const std::string_view id : kViolentKenEndingIds) Check(screen.find(id) != std::string::npos, "each line is shown");

  const std::string ken_animations =
      "<UIElementDescriptors><UIAnim><name>end_ken</name></UIAnim><UIAnim><name>end_short_fadein</name></UIAnim>\n"
      "</UIElementDescriptors>";
  std::string animations = ken_animations;
  Check(PatchEndingAnimations(animations), "the descriptors take the timeline");
  Check(!PatchEndingAnimations(animations), "only once");
  Check(animations.find("<name>end_violentken</name>") != std::string::npos, "the timeline has the group's name");
  Check(Count(animations, "GameFlowSignal(cinematic_done);") == 1, "the scene ends the native way");
  Check(animations.find("<totalTime>50.000</totalTime>") != std::string::npos, "the scene lasts 50 seconds");
  for (int line = 1; line <= kViolentKenEndingLines; ++line) {
    const std::string prefix = "end_violentken_tx" + std::to_string(line);
    Check(Count(animations, prefix + "fadein") == 1 && Count(animations, prefix + "fadeout") == 1 &&
              Count(screen, prefix + "fadein") == 1 && Count(screen, prefix + "fadeout") == 1,
          "every text event has one key and one listener");
  }
  for (int cut = 1; cut <= kEndingCuts; ++cut) {
    const std::string prefix = "end_violentken_fm" + std::to_string(cut);
    Check(Count(animations, prefix + "fadein") == 1 && Count(screen, prefix + "fadein") == 1,
          "every cut event has one key and one listener");
  }
}

size_t SlotsFor(const QuoteSet& set, size_t quote) {
  size_t slots = 0;
  for (size_t slot = 0; slot < kTauntSlots; ++slot) slots += QuoteForSlot(slot, set.ids.size()) == quote;
  return slots;
}

// USF2's CHR_MES_TBL rows 17 and 18 pick through sixteen entries; six taunt slots keep the nearest whole shares.
void TestQuotes() {
  const QuoteSet* evil = QuoteSetFor(kRyuGuestCharacter, FighterIdentity::kEvilRyu);
  const QuoteSet* violent = QuoteSetFor(kKenGuestCharacter, FighterIdentity::kViolentKen);
  Check(evil && violent && evil != violent, "each Ultra fighter has its own quote set");
  Check(evil->ids.size() == 5 && violent->ids.size() == 4, "the real USF2 counts are kept, not the carrier's six");
  Check(QuoteSetFor(kRyuGuestCharacter, FighterIdentity::kGuest) == nullptr, "normal Ryu keeps Ryu's quotes");
  Check(QuoteSetFor(kKenGuestCharacter, FighterIdentity::kGuest) == nullptr, "normal Ken keeps Ken's quotes");
  Check(QuoteSetFor(kRyuGuestCharacter, FighterIdentity::kViolentKen) == nullptr, "Violent Ken never rides on Ryu");
  Check(QuoteSetFor(kKenGuestCharacter, FighterIdentity::kEvilRyu) == nullptr, "Evil Ryu never rides on Ken");
  Check(QuoteSetFor(int(kAkumaGuestCharacter), FighterIdentity::kEvilRyu) == nullptr, "no other carrier takes the quotes");
  for (const QuoteSet& set : kQuoteSets) {
    size_t sum = 0;
    for (const uint8_t weight : set.weights) sum += weight;
    Check(sum == 16 && set.weights.size() == set.ids.size(), "the weights are USF2's sixteen entries");
    size_t slots = 0;
    for (size_t quote = 0; quote < set.ids.size(); ++quote) {
      const size_t share = SlotsFor(set, quote);
      slots += share;
      const double wanted = double(set.weights[quote]) * kTauntSlots / 16.0;
      Check(share >= 1 && double(share) > wanted - 1.0 && double(share) < wanted + 1.0, "no quote strays a slot from its USF2 share");
    }
    Check(slots == kTauntSlots, "every taunt slot holds a quote");
  }
  Check(kEvilRyuQuoteWeights[0] == 4 && kViolentKenQuoteWeights[0] == 5, "the first quote is the most frequent");
  for (const char* wins : {"RYU WINS", "RYU GAGNE", "RYU SIEGT", "RYU VINCE", "RYU GANA", "RYU VENCE"}) {
    for (const std::string_view quote : EvilRyuQuotes(wins)) {
      Check(!quote.empty() && quote.find('\n') == std::string_view::npos && quote.find('\r') == std::string_view::npos,
            "an Evil Ryu quote stays one line");
    }
  }
  Check(EvilRyuQuotes("RYU WINS")[1] == "A match as worthless as this is a far cry from the true blood match I seek...",
        "the English quote is USF2's win_128");
  Check(ViolentKenQuotes("KEN WINS")[1] == "I'm not looking for a good fight. I just want to win!",
        "the screenshot phrase is Violent Ken's win_133");
  for (const char* wins : {"RYU WINS", "RYU GAGNE", "RYU SIEGT", "RYU VINCE", "RYU GANA", "RYU VENCE"}) {
    for (const char* other : {"KEN WINS", "KEN GAGNE", "KEN SIEGT", "KEN VINCE", "KEN GANA", "KEN VENCE"}) {
      for (const std::string_view mine : EvilRyuQuotes(wins)) {
        for (const std::string_view theirs : ViolentKenQuotes(other)) Check(mine != theirs, "no quote is shared");
      }
    }
  }
  Check(EvilRyuQuotes("RYU VENCE")[0] != EvilRyuQuotes("RYU WINS")[0], "pt-BR is the project's own translation");
  for (const std::string_view quote : EvilRyuQuotes("RYU VENCE")) {
    for (const char glyph : {'\x80', '\x81', '\x82', '\x83'}) {
      Check(quote.find(std::string("\xC3") + glyph) == std::string_view::npos, "pt-BR text stays inside the font");
    }
  }
  Check(kEvilRyuQuoteLines[0] == 39 && kEvilRyuQuoteLines.back() == 43, "the Evil Ryu quotes sit on the Zangief lines");
}

void TestDefinition() {
  Check(kEvilRyuMoveListIndex == 17, "Evil Ryu is the page after Akuma's");
  Check(kEvilRyuMoves.size() == 7, "seven rows: five moves, a second teleport row and the super");
  Check(kEvilRyuMoves[4].name_id == kAshuraForwardId && kEvilRyuMoves[5].name_id == kAshuraBackId,
        "both teleport directions are listed");
  Check(kEvilRyuMoves[6].name_id == kSuperNameId && kEvilRyuMoves[6].remixed_id == kRemixedSuperId,
        "the last row is the super, with Shun Goku Satsu's command");
  for (const EvilRyuMove& move : kEvilRyuMoves) {
    Check(move.remixed_id.find("Ryu_R_Super") == std::string_view::npos &&
              move.classic_id.find("Ryu_C_Super") == std::string_view::npos,
          "Ryu's Shinku Hadoken is never listed");
    Check(!move.host_move || move.classic_id == move.remixed_id, "host moves work in Classic Arcade with the same command");
    Check(move.classic_id != kNotAvailableId, "every move is listed in both columns");
  }
  std::string_view forward, back;
  for (const MoveListText& entry : kEvilRyuMoveListText) {
    if (entry.id == kAshuraForwardCommandId) forward = entry.text;
    if (entry.id == kAshuraBackCommandId) back = entry.text;
  }
  Check(forward.substr(0, 2) == "\xC2\x84" && back.substr(0, 2) == "\xC2\x85",
        "the teleports are 623 and 421, three punches or three kicks");
  Check(forward.substr(2) == back.substr(2), "both teleports share the button part");
  uint32_t last = 0;
  for (const MoveListText& entry : kEvilRyuMoveListText) {
    Check(entry.line >= last && entry.line < 120, "page text lives in the legacy placeholder block");
    last = entry.line;
  }
  last = 0;
  for (const MoveListText& entry : kViolentKenMoveListText) {
    Check(entry.line >= last && entry.line < 120, "page text lives in the legacy placeholder block");
    last = entry.line;
  }

  Check(kViolentKenMoveListIndex == 18, "Violent Ken is the page after Evil Ryu's");
  const std::array<std::string_view, 7> order = {"CMD_HADOKEN", "CMD_SHORYUKEN", kTatsumakiId, kAirTatsumakiId,
                                                 kRasetsuId,    kInazumaId,      kShinbuId};
  for (size_t row = 0; row < order.size(); ++row) {
    const EvilRyuMove& move = kViolentKenMoves[row];
    Check(move.name_id == order[row], "the rows follow the USF2 order");
    Check(move.remixed_id.find("Ken_R_Super") == std::string_view::npos, "Ken's Shoryureppa is never listed");
    Check(!move.host_move || move.classic_id == move.remixed_id, "host moves work in Classic Arcade with the same command");
    Check(move.classic_id != kNotAvailableId, "every move is listed in both columns");
    Check(move.name_id.find("Kamabarai") == std::string_view::npos, "the stale Ken kick list is never listed");
  }
  Check(ViolentKenWinsText("KEN WINS") == "VIOLENT KEN WINS", "the banner reads VIOLENT KEN WINS");
  Check(ViolentKenWinsText("KEN SIEGT") == "VIOLENT KEN SIEGT", "the banner keeps the language's wording");
  Check(ViolentKenQuotes("KEN WINS")[0] != ViolentKenQuotes("KEN GAGNE")[0], "quotes follow the table's language");
  Check(ViolentKenQuotes("unknown")[0] == ViolentKenQuotes("KEN WINS")[0], "an unknown table falls back to English");
  TestEnding();
  TestQuotes();
  for (const char* wins : {"KEN WINS", "KEN GAGNE", "KEN SIEGT", "KEN VINCE", "KEN GANA", "KEN VENCE"}) {
    for (const std::string_view quote : ViolentKenQuotes(wins)) {
      Check(quote.find('\n') == std::string_view::npos && quote.find('\r') == std::string_view::npos,
            "a table line stays one line");
    }
  }
  for (const std::string_view quote : ViolentKenQuotes("KEN VENCE")) {
    for (const char glyph : {'\x80', '\x81', '\x82', '\x83'}) {
      Check(quote.find(std::string("\xC3") + glyph) == std::string_view::npos, "pt-BR text stays inside the font");
    }
  }
}

struct Archive {
  std::string screen, scripts;
  std::vector<std::string> ids;
  std::vector<std::vector<std::string>> tables;
};

bool LoadArchive(Archive* out) {
  std::ifstream in("game/extracted/flog_x360.fpg", std::ios::binary);
  if (!in) return false;
  uint32_t count = 0;
  in.seekg(4);
  in.read(reinterpret_cast<char*>(&count), 4);
  for (uint32_t slot = 0; slot < 70 && slot < count; ++slot) {
    uint32_t entry[4];
    in.seekg(0x800 + slot * 16);
    in.read(reinterpret_cast<char*>(entry), 16);
    if (!in || entry[2] == 0 || entry[2] > 4000000) continue;
    std::vector<unsigned char> stored(entry[2]);
    in.seekg(entry[1]);
    in.read(reinterpret_cast<char*>(stored.data()), entry[2]);
    std::vector<unsigned char> data(8 << 20);
    mz_ulong size = mz_ulong(data.size());
    if (mz_uncompress(data.data(), &size, stored.data(), mz_ulong(stored.size())) != MZ_OK) continue;
    const std::string text(reinterpret_cast<const char*>(data.data()), size);
    if (text.rfind("<UIScreenSet>", 0) == 0) out->screen = text;
    else if (text.find("name=\"hide_moves_except\"") != std::string::npos) out->scripts = text;
    else if (text.rfind("1989\t1\n// Ryu's Moves\nRyu_Hadouken", 0) == 0) out->ids = Lines(text);
    else if (text.rfind("1989\t1\n// Ryu's Moves\ntid_Ryu_", 0) == 0) out->tables.push_back(Lines(text));
  }
  return !out->screen.empty() && !out->scripts.empty() && !out->ids.empty() && out->tables.size() == 5;
}

// The menu scripts are the only place that says what the mode byte means.
void TestMenuScriptsNameTheGameModes() {
  std::ifstream in("game/extracted/flog_x360.fpg", std::ios::binary);
  if (!in) return;
  const auto entry = [&](uint32_t slot) {
    uint32_t row[4];
    in.seekg(0x800 + slot * 16);
    in.read(reinterpret_cast<char*>(row), 16);
    std::vector<unsigned char> stored(row[2]);
    in.seekg(row[1]);
    in.read(reinterpret_cast<char*>(stored.data()), row[2]);
    std::vector<unsigned char> data(1 << 21);
    mz_ulong size = mz_ulong(data.size());
    const int status = mz_uncompress(data.data(), &size, stored.data(), mz_ulong(stored.size()));
    return status == MZ_OK ? std::string(reinterpret_cast<const char*>(data.data()), size) : std::string();
  };
  const auto sets_mode = [](const std::string& script, const char* signal, uint8_t mode) {
    const size_t at = script.find(signal);
    const size_t end = script.find("</Transition>", at);
    const std::string call = "SetGameMode(" + std::to_string(int(mode)) + ")";
    return at != std::string::npos && script.substr(at, end - at).find(call) != std::string::npos;
  };
  const std::string single_player = entry(466);
  const std::string multiplayer = entry(462);
  Check(sets_mode(single_player, "accept_ArcadeModeSelect", kArcadeGameMode), "the Arcade menu item");
  Check(sets_mode(single_player, "accept_TrainingModeSelect", kTrainingGameMode), "guest mode 2 is the Training menu item");
  Check(sets_mode(multiplayer, "accept_VersusModeSelect", kLocalVersusGameMode), "the Local Versus menu item");
  Check(sets_mode(multiplayer, "accept_RankedMatch", kOnlineSelectMode), "the Ranked Match menu item");
  Check(sets_mode(multiplayer, "accept_PlayerMatch", kOnlineSelectMode), "the Player Match menu item");
  Check(sets_mode(multiplayer, "accept_TournamentMatch", kOnlineSelectMode), "the Tournament menu item");
}

void TestRealEnding() {
  std::ifstream in("game/extracted/flog_x360.fpg", std::ios::binary);
  if (!in) return;
  const auto entry = [&](uint32_t slot) {
    uint32_t row[4];
    in.seekg(0x800 + slot * 16);
    in.read(reinterpret_cast<char*>(row), 16);
    std::vector<unsigned char> stored(row[2]);
    in.seekg(row[1]);
    in.read(reinterpret_cast<char*>(stored.data()), row[2]);
    std::vector<unsigned char> data(1 << 21);
    mz_ulong size = mz_ulong(data.size());
    const int status = mz_uncompress(data.data(), &size, stored.data(), mz_ulong(stored.size()));
    return status == MZ_OK ? std::string(reinterpret_cast<const char*>(data.data()), size) : std::string();
  };
  const std::string screen_original = entry(354);
  const std::string animations_original = entry(214);
  std::string screen = screen_original;
  std::string animations = animations_original;
  Check(PatchEndingScreen(screen), "the real EndKen document takes the group");
  Check(PatchEndingAnimations(animations), "the real ending animations take the timeline");
  Check(Balanced(screen) && Count(screen, "<UIScreen>") == 1, "the real document stays balanced");
  Check(screen.compare(0, screen.find("<UIElementGroup>"), screen_original, 0, screen_original.find("<UIElementGroup>")) == 0,
        "the head of the screen set is unchanged");
  Check(Group(screen, "end_ken") == Group(screen_original, "end_ken"), "Ken's ending is byte for byte unchanged");
  Check(animations.compare(0, animations_original.size() - 25, animations_original, 0, animations_original.size() - 25) == 0,
        "the guest's animations are byte for byte unchanged");
  std::ifstream package("assets/characters/violent_ken_ending.bin", std::ios::binary);
  if (!package) return;
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(package)), std::istreambuf_iterator<char>());
  UltraEndingArt art;
  Check(ParseUltraEndingArt(bytes.data(), bytes.size(), &art), "the generated ending package parses");
  Check(art.pixels.size() == kEndingCutBytes * kEndingCuts && art.Cut(3)[0] == 0xFF, "four opaque A,R,G,B cuts");
  Check(!ParseUltraEndingArt(bytes.data(), bytes.size() - 1, &art), "a short package is refused");
}

void TestRealData() {
  Archive archive;
  if (!LoadArchive(&archive)) {
    std::printf("note: game/extracted/flog_x360.fpg not found, real-data checks skipped\n");
    return;
  }
  std::string screen = archive.screen;
  Check(PatchCharacterMovesScreen(screen), "the real screen set takes the Evil Ryu page");
  Check(Balanced(screen) && Balanced(archive.screen), "the real screen set stays balanced");
  for (const char* page : {"moves_ryu", "moves_akuma", "moves_ken", "moves_vega"}) {
    Check(!Group(screen, page).empty() && Group(screen, page) == Group(archive.screen, page),
          "the original pages are byte for byte unchanged");
  }
  Check(Count(screen, "<name>CharacterMoves</name>") == 1, "the Character Moves screen is not duplicated");

  std::string scripts = archive.scripts;
  Check(PatchCharacterMovesScripts(scripts), "the real script document takes the Evil Ryu branch");
  Check(Count(scripts, "moves_ryu,1.0f") == Count(archive.scripts, "moves_ryu,1.0f"), "the Ryu branch stays");

  const std::string page = Group(screen, "moves_evilryu");
  std::vector<std::string> ids;
  size_t from = 0;
  for (size_t at = page.find("<textID>"); at != std::string::npos; at = page.find("<textID>", from)) {
    const size_t end = page.find("</textID>", at);
    ids.push_back(page.substr(at + 8, end - at - 8));
    from = end;
  }
  Check(ids.size() == 23, "the page uses 23 texts: the title, 8 labels and 14 commands");

  const std::string violent_page = Group(screen, "moves_violentken");
  std::vector<std::string> violent_ids;
  from = 0;
  for (size_t at = violent_page.find("<textID>"); at != std::string::npos; at = violent_page.find("<textID>", from)) {
    const size_t end = violent_page.find("</textID>", at);
    violent_ids.push_back(violent_page.substr(at + 8, end - at - 8));
    from = end;
  }
  Check(violent_ids.size() == 22, "the Violent Ken page uses 22 texts: the title, 7 labels and 14 commands");
  Check(Count(scripts, "eq 18 )") == 1, "the real scripts take the Violent Ken branch once");
  for (const char* id : {"Ken_SpecialKick1", "Ken_SpecialKick2", "Ken_SpecialKick3", "Ken_OcerheadKick", "Ken_Super",
                         "Honda_Torpedo", "Honda_FlyingButt", "Honda_HundredHandSlap", "Honda_OchioThrow", "Honda_Super",
                         "ChunLi_Kikoken", "ChunLi_UpKick"}) {
    Check(Count(archive.screen, id) == 0 && Count(archive.scripts, id) == 0, "a carrier line is unused by the guest");
  }

  for (size_t language = 0; language < archive.tables.size(); ++language) {
    std::string table;
    for (const std::string& line : archive.tables[language]) table += line + "\n";
    const std::vector<std::string> english = archive.tables[language];
    std::string patched = table;
    Check(PatchMoveListText(patched) == kEvilRyuMoveListText.size() + kViolentKenMoveListText.size(),
          "every language takes the page text");
    const std::vector<std::string> after = Lines(patched);
    Check(after.size() == english.size(), "the table keeps its line count");
    std::vector<uint32_t> replaced;
    for (size_t i = 0; i < english.size(); ++i) {
      if (english[i] != after[i]) replaced.push_back(uint32_t(i));
    }
    const size_t language_lines = 2 + kViolentKenQuotes + kEvilRyuQuotes + kViolentKenEndingLines + kEvilRyuEndingLines + 1;
    Check(replaced.size() == kEvilRyuMoveListText.size() + kViolentKenMoveListText.size() + language_lines,
          "only the page lines, the banner lines, the quotes, the ending and the air command change");
    for (size_t i = 0; i < kViolentKenEndingLines; ++i) {
      const uint32_t line = kViolentKenEndingTableLines[i];
      Check(archive.ids[line] == kViolentKenEndingIds[i], "each ending line belongs to its id");
      Check(after[line] == ViolentKenEnding(english[kKenWinsLine])[i], "the table carries the ending");
    }
    Check(archive.ids[kViolentKenWinsLine] == kViolentKenWinsId && archive.ids[kKenWinsLine] == "WINS_KEN",
          "the Violent Ken banner line belongs to its id and comes from WINS_KEN");
    Check(archive.ids[kInAirLine] == "CMD_INAIR" && archive.ids[kAirTatsumakiCommandLine] == kAirTatsumakiCommandId,
          "the air command comes from CMD_INAIR");
    Check(after[kKenWinsLine] == english[kKenWinsLine], "Ken's own banner is unchanged");
    Check(after[kViolentKenWinsLine] == ViolentKenWinsText(english[kKenWinsLine]), "the banner keeps the wording");
    Check(after[kAirTatsumakiCommandLine] == "(" + english[kInAirLine] + ") \xC2\x81+\xC2\x97", "air 214 + kick");
    for (size_t i = 0; i < kEvilRyuQuoteIds.size(); ++i) {
      Check(archive.ids[kEvilRyuQuoteLines[i]] == kEvilRyuQuoteIds[i], "each Evil Ryu quote line belongs to its id");
      Check(after[kEvilRyuQuoteLines[i]] == EvilRyuQuotes(english[kRyuWinsLine])[i], "the line holds the Evil Ryu quote");
    }
    for (size_t i = 0; i < kViolentKenQuoteIds.size(); ++i) {
      Check(archive.ids[kViolentKenQuoteLines[i]] == kViolentKenQuoteIds[i], "each quote line belongs to its id");
      Check(after[kViolentKenQuoteLines[i]] == ViolentKenQuotes(english[kKenWinsLine])[i], "the line holds the quote");
    }
    for (size_t i = 0; i < archive.ids.size(); ++i) {
      if (archive.ids[i].rfind("TAUNT_KEN_", 0) == 0 || archive.ids[i].rfind("tid_Ken_", 0) == 0) {
        Check(after[i] == english[i], "Ken's own text is unchanged");
      }
    }
    for (const MoveListText& entry : kViolentKenMoveListText) {
      Check(archive.ids[entry.line] == entry.id, "each line belongs to the id the page names");
      Check(after[entry.line] == entry.text, "the line holds the page text");
    }
    for (const std::string& id : violent_ids) {
      Check(std::find(archive.ids.begin(), archive.ids.end(), id) != archive.ids.end(), "every referenced id exists");
    }
    Check(TextOf(archive.ids, after, kRasetsuCommandId) == "\xC2\x84+\xC2\x97", "Rasetsukyaku is 623 + kick");
    Check(TextOf(archive.ids, after, kShinbuCommandId) == "\xC2\x80\xC2\x80+\xC2\x97", "Shinbu is 236 236 + kick");
    Check(archive.ids[kEvilRyuWinsLine] == kEvilRyuWinsId, "the banner line belongs to its id");
    Check(archive.ids[kRyuWinsLine] == "WINS_RYU", "the banner text comes from WINS_RYU");
    Check(after[kRyuWinsLine] == english[kRyuWinsLine], "Ryu's own banner is unchanged");
    Check(after[kEvilRyuWinsLine] == EvilRyuWinsText(english[kRyuWinsLine]), "the banner keeps the language's wording");
    Check(after[kEvilRyuWinsLine].find("EVIL ") != std::string::npos, "the banner names Evil Ryu");
    for (const MoveListText& entry : kEvilRyuMoveListText) {
      Check(archive.ids[entry.line] == entry.id, "each line belongs to the id the page names");
      Check(after[entry.line] == entry.text, "the line holds the page text");
    }
    for (const std::string& id : ids) {
      Check(std::find(archive.ids.begin(), archive.ids.end(), id) != archive.ids.end(), "every referenced id exists");
    }
    if (language == 0) {
      Check(TextOf(archive.ids, after, "CHAR_RYU") == "Ryu", "Ryu stays Ryu");
      Check(TextOf(archive.ids, after, "WINS_RYU") == "RYU WINS", "RYU WINS stays");
      Check(TextOf(archive.ids, after, kEvilRyuWinsId) == "EVIL RYU WINS", "the Evil Ryu banner reads EVIL RYU WINS");
      Check(TextOf(archive.ids, after, kEvilRyuTitleId) == "Evil Ryu", "the page title is Evil Ryu");
      Check(TextOf(archive.ids, after, "WINS_KEN") == "KEN WINS", "KEN WINS stays");
      Check(TextOf(archive.ids, after, kViolentKenWinsId) == "VIOLENT KEN WINS", "the banner reads VIOLENT KEN WINS");
      Check(TextOf(archive.ids, after, kViolentKenTitleId) == "Violent Ken", "the page title is Violent Ken");
      Check(TextOf(archive.ids, after, kAirTatsumakiCommandId) == "(In Air) \xC2\x81+\xC2\x97", "the air row's command");
      Check(TextOf(archive.ids, after, "tid_Ken_C_Shoryuken") == "\xC2\x84+\xC2\x96", "Shoryuken is 623 + punch");
      Check(TextOf(archive.ids, after, "tid_Ken_C_HurricaneKick") == "\xC2\x81+\xC2\x97", "Tatsumaki is 214 + kick");

      std::vector<std::string> portuguese = after;
      std::string pt_table;
      for (size_t i = 0; i < english.size(); ++i) {
        pt_table += (i == kRyuWinsLine ? "RYU VENCE" : i == kKenWinsLine ? "KEN VENCE" : i == kInAirLine ? "No ar" : english[i]) + std::string("\n");
      }
      Check(PatchMoveListText(pt_table) == kEvilRyuMoveListText.size() + kViolentKenMoveListText.size(),
            "the translated table takes the text");
      portuguese = Lines(pt_table);
      Check(portuguese[kViolentKenWinsLine] == "VIOLENT KEN VENCE", "the pt-BR banner keeps the translation");
      Check(portuguese[kEvilRyuWinsLine] == "EVIL RYU VENCE", "the pt-BR Evil Ryu banner keeps the translation");
      Check(portuguese[kAirTatsumakiCommandLine] == "(No ar) \xC2\x81+\xC2\x97", "the pt-BR air note is translated");
      Check(portuguese[kViolentKenQuoteLines[0]] == ViolentKenQuotes("KEN VENCE")[0], "pt-BR takes its quotes");
      Check(TextOf(archive.ids, after, "CMD_HADOKEN") == "Hadoken" && TextOf(archive.ids, after, "CMD_SHORYUKEN") == "Shoryuken",
            "shared names are the game's own");
      Check(TextOf(archive.ids, after, kNotAvailableId) == "n/a", "the Classic placeholder reads n/a");
      Check(TextOf(archive.ids, after, kRemixedSuperId) == "\xC2\x90,\xC2\x90,\xC2\x86,\xC2\x93,\xC2\x92",
            "the super command is LP, LP, F, LK, HP");
      Check(TextOf(archive.ids, after, "tid_Ryu_R_Super") != TextOf(archive.ids, after, kRemixedSuperId),
            "Ryu's super command is a different one");
      Check(TextOf(archive.ids, after, "tid_Ryu_C_RedHadouken") == "\xC2\x82+\xC2\x96", "Ryu's half circle stays");
      Check(TextOf(archive.ids, after, kShakunetsuCommandId) == "\xC2\x83+\xC2\x96", "Evil Ryu's is 63214 + punch");
      Check(TextOf(archive.ids, after, "tid_Ryu_C_HurricaneKick") == "\xC2\x81+\xC2\x97", "Tatsumaki is 214 + kick");
      Check(TextOf(archive.ids, after, "tid_Ryu_C_Shoryuken") == "\xC2\x84+\xC2\x96", "Shoryuken is 623 + punch");
      Check(TextOf(archive.ids, after, "tid_Ryu_C_Hadouken") == "\xC2\x80+\xC2\x96", "Hadoken is 236 + punch");
    }
  }
}

}  // namespace

int main() {
  TestDefinition();
  TestSynthetic();
  TestRealData();
  TestRealEnding();
  TestMenuScriptsNameTheGameModes();
  if (failures == 0) std::printf("character moves tests passed\n");
  return failures == 0 ? 0 : 1;
}
