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
  Check(!page.empty(), "the Evil Ryu page is a group of the screen");
  Check(Group(screen, "moves_akuma") == Group(original, "moves_akuma"), "the Akuma page is untouched");
  Check(Group(screen, "moves_ryu") == Group(original, "moves_ryu"), "the Ryu page is untouched");
  std::string without = screen;
  const size_t at = without.find(page);
  Check(at != std::string::npos, "the page text is contiguous");
  if (at != std::string::npos) {
    const size_t line = without.rfind('\n', at);
    without.erase(line, at + page.size() - line);
    Check(without == original, "the patch only inserts the page");
  }

  std::string wrong = "<UIScreenSet><UIScreen><name>Other</name></UIScreen></UIScreenSet>";
  Check(!PatchCharacterMovesScreen(wrong), "a screen set without the Akuma page is left whole");

  std::string scripts = kMiniScripts;
  Check(PatchCharacterMovesScripts(scripts), "the script patch applies");
  Check(scripts.find("eq 17 )") != std::string::npos && scripts.find("moves_evilryu,1.0f") != std::string::npos,
        "index 17 shows the Evil Ryu page");
  Check(scripts.find("moves_evilryu,0.0f") != std::string::npos, "every page is hidden before one is shown");
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
  for (int i = 0; i < 12; ++i) table += "line" + std::to_string(i) + "\n";
  patched = table;
  Check(PatchMoveListText(patched) == kEvilRyuMoveListText.size(), "every Evil Ryu line is replaced");
  const std::vector<std::string> before = Lines(table), after = Lines(patched);
  size_t changed = 0;
  for (size_t i = 0; i < before.size(); ++i) changed += before[i] != after[i];
  Check(changed == kEvilRyuMoveListText.size() && after[2] == "Evil Ryu", "only the page lines change");
  std::string short_table = "tid_a\ntid_b\n";
  Check(PatchMoveListText(short_table) == 0 && short_table == "tid_a\ntid_b\n", "a table without the lines is left whole");
  std::string foreign;
  for (int i = 0; i < 12; ++i) foreign += "text" + std::to_string(i) + "\n";
  Check(PatchMoveListText(foreign) == 0, "lines that are not placeholders are never replaced");
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
    Check(!move.host_move || move.classic_id == kNotAvailableId, "moves that need the Remixed rules are n/a in Classic");
    Check(move.host_move || move.classic_id != kNotAvailableId, "guest moves are listed in both columns");
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

  for (size_t language = 0; language < archive.tables.size(); ++language) {
    std::string table;
    for (const std::string& line : archive.tables[language]) table += line + "\n";
    const std::vector<std::string> english = archive.tables[language];
    std::string patched = table;
    Check(PatchMoveListText(patched) == kEvilRyuMoveListText.size(), "every language takes the page text");
    const std::vector<std::string> after = Lines(patched);
    Check(after.size() == english.size(), "the table keeps its line count");
    std::vector<uint32_t> replaced;
    for (size_t i = 0; i < english.size(); ++i) {
      if (english[i] != after[i]) replaced.push_back(uint32_t(i));
    }
    Check(replaced.size() == kEvilRyuMoveListText.size() + 1, "only the page lines and the banner line change");
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
      Check(TextOf(archive.ids, after, "CMD_HADOKEN") == "Hadoken" && TextOf(archive.ids, after, "CMD_SHORYUKEN") == "Shoryuken",
            "shared names are the game's own");
      Check(TextOf(archive.ids, after, kNotAvailableId) == "n/a", "the Classic placeholder reads n/a");
      Check(TextOf(archive.ids, after, kRemixedSuperId) == "\xC2\x90,\xC2\x90,\xC2\x86,\xC2\x93,\xC2\x92",
            "the super command is LP, LP, F, LK, HP");
      Check(TextOf(archive.ids, after, "tid_Ryu_R_Super") != TextOf(archive.ids, after, kRemixedSuperId),
            "Ryu's super command is a different one");
      Check(TextOf(archive.ids, after, "tid_Ryu_C_RedHadouken") == "\xC2\x82+\xC2\x96", "Shakunetsu is 63214 + punch");
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
  if (failures == 0) std::printf("character moves tests passed\n");
  return failures == 0 ? 0 : 1;
}
