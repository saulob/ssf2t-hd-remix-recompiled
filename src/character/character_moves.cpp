#include "character/character_moves.h"

#include <atomic>
#include <cstdio>

namespace ssf2t::character {
namespace {

constexpr size_t npos = std::string::npos;

std::atomic<bool> g_screen_patched{false};
std::atomic<bool> g_scripts_patched{false};
std::atomic<bool> g_text_patched{false};

constexpr uint32_t kBlockHeaderLine = 1;
constexpr std::string_view kBlockHeader = "// Ryu's Moves";

constexpr std::string_view kOpen = "<UIElementGroup>";
constexpr std::string_view kClose = "</UIElementGroup>";

// End of the <UIElementGroup> that opens at `begin`, one past its closing tag.
size_t GroupEnd(const std::string& xml, size_t begin) {
  int depth = 0;
  for (size_t at = begin; at < xml.size();) {
    const size_t open = xml.find(kOpen, at);
    const size_t close = xml.find(kClose, at);
    if (close == npos) return npos;
    if (open != npos && open < close) {
      ++depth;
      at = open + kOpen.size();
    } else {
      --depth;
      at = close + kClose.size();
      if (depth == 0) return at;
    }
  }
  return npos;
}

// Start of the group named `name` and the whitespace that indents its first line.
bool FindGroup(const std::string& xml, std::string_view name, size_t* begin, std::string* indent) {
  const std::string tag = "<name>" + std::string(name) + "</name>";
  const size_t named = xml.find(tag);
  if (named == npos || xml.find(tag, named + 1) != npos) return false;
  const size_t open = xml.rfind(kOpen, named);
  if (open == npos || xml.find_first_not_of(" \t\r\n", open + kOpen.size()) != named) return false;
  size_t line = open;
  while (line > 0 && (xml[line - 1] == '\t' || xml[line - 1] == ' ')) --line;
  *begin = open;
  *indent = xml.substr(line, open - line);
  return true;
}

struct Xml {
  std::string text;
  std::string newline;
  std::string indent;
  int depth = 0;

  void Open(std::string_view tag) {
    Line("<" + std::string(tag) + ">");
    ++depth;
  }
  void Close(std::string_view tag) {
    --depth;
    Line("</" + std::string(tag) + ">");
  }
  void Field(std::string_view tag, std::string_view value) {
    Line("<" + std::string(tag) + ">" + std::string(value) + "</" + std::string(tag) + ">");
  }
  void Line(const std::string& content) {
    if (!text.empty()) text += newline;
    text += indent;
    for (int i = 0; i < depth; ++i) text += '\t';
    text += content;
  }
};

std::string Number(float value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%g", double(value));
  return buffer;
}

std::string Triple(float x, float y, float z) { return Number(x) + " " + Number(y) + " " + Number(z); }

struct TextCell {
  std::string_view name;
  std::string_view font;
  std::string_view text_id;
  float x, y, width, height;
  float shade;
  int wrap;
  bool centered;
};

void WriteText(Xml& xml, const TextCell& cell) {
  xml.Open("UIText");
  xml.Field("name", cell.name);
  xml.Field("descName", "default_text");
  xml.Field("pos", Triple(cell.x, cell.y, 11));
  xml.Field("size", Triple(cell.width, cell.height, 1));
  xml.Field("color", Number(cell.shade) + " " + Number(cell.shade) + " " + Number(cell.shade) + " 1");
  xml.Field("fontType", "Rf");
  xml.Field("font", cell.font);
  xml.Field("textID", cell.text_id);
  if (cell.wrap) xml.Field("wrapLength", std::to_string(cell.wrap));
  if (cell.centered) xml.Field("hAlign", "Center");
  xml.Close("UIText");
}

constexpr std::string_view kNameFont = "HelveticaBoldItalic60";
constexpr std::string_view kCommandFont = "charactermoves";
constexpr float kLabelShade = 0.784314f;
constexpr float kCommandX[2] = {838, 1470};

struct Row {
  float name_y;
  float name_size;
  float command_y;
  float label_x;
  int command_wrap;
};

constexpr std::array<Row, 7> kRows = {{
    {270, 45, 273, 220, 600},
    {330, 45, 330, 220, 600},
    {383, 45, 383, 220, 600},
    {425, 41, 440, 220, 600},
    {493, 45, 494, 220, 1200},
    {548, 45, 549, 220, 1200},
    {601, 45, 604, 390, 1200},
}};
constexpr float kAirNoteX = 240;
constexpr float kAirNoteY = 454;
constexpr float kSuperBadgeY = 589;
constexpr std::array<float, 4> kSeparatorY = {260, 370, 480, 590};

std::string RowName(const char* prefix, int row) { return std::string(prefix) + std::to_string(row + 1); }

void WriteColumn(Xml& xml, const char* group, const char* suffix, bool remixed) {
  xml.Open("UIElementGroup");
  xml.Field("name", group);
  for (size_t row = 0; row < kRows.size(); ++row) {
    const EvilRyuMove& move = kEvilRyuMoves[row];
    const std::string name = "move_evilryu_" + std::to_string(row + 1) + suffix;
    WriteText(xml, {name, kCommandFont, remixed ? move.remixed_id : move.classic_id, kCommandX[remixed ? 1 : 0],
                    kRows[row].command_y, 45, 45, 1.0f, kRows[row].command_wrap, true});
  }
  xml.Close("UIElementGroup");
}

std::string BuildEvilRyuGroup(const std::string& indent, const std::string& newline) {
  Xml xml;
  xml.indent = indent;
  xml.newline = newline;
  xml.Open("UIElementGroup");
  xml.Field("name", "moves_evilryu");
  xml.Field("pos", "-30 0 0");
  xml.Field("color", "0.749020 0.749020 0.749020 0");
  for (size_t row = 0; row < kRows.size(); ++row) {
    const std::string name = RowName("move_evilryu", int(row));
    WriteText(xml, {name, kNameFont, kEvilRyuMoves[row].name_id, kRows[row].label_x, kRows[row].name_y,
                    row == 3 ? 37.0f : 40.0f, kRows[row].name_size, kLabelShade, 600, false});
    if (row == 3) {
      WriteText(xml, {"move_evilryu4b", kNameFont, "CMD_HURRICANEAIR", kAirNoteX, kAirNoteY, 32, 38, kLabelShade, 0,
                      false});
    }
  }
  WriteColumn(xml, "moves_evilryu_C", "C", false);
  WriteColumn(xml, "moves_evilryu_R", "R", true);
  xml.Open("UIGraphic");
  xml.Field("name", "moves_super_17");
  xml.Field("descName", "HUD_supertext_art");
  xml.Field("pos", Triple(220, kSuperBadgeY, 8));
  xml.Field("size", "150 75 1");
  xml.Field("fileName", "user_interface\\In_game\\GameScreen\\HUD_Super_Text_Hot1");
  xml.Close("UIGraphic");
  xml.Open("UIText");
  xml.Field("name", "name_evilryu");
  xml.Field("descName", "default_text");
  xml.Field("pos", "227 168 15");
  xml.Field("size", "50 55 1");
  xml.Field("color", "0.815686 0.815686 0.815686 1");
  xml.Field("fontType", "Rf");
  xml.Field("font", kNameFont);
  xml.Field("textID", kEvilRyuTitleId);
  xml.Field("wrapLength", "600");
  xml.Close("UIText");
  xml.Open("UIElementGroup");
  xml.Field("name", "rowLines_evilryu");
  xml.Field("pos", "30 10 0");
  int line = 1;
  for (const float y : kSeparatorY) {
    xml.Open("UIGraphic");
    xml.Field("name", "line0" + std::to_string(line++) + "_evilryu");
    xml.Field("descName", "null_graphic");
    xml.Field("visible", "true");
    xml.Field("pos", Triple(175, y, y == 260 ? 12.0f : 14.0f));
    xml.Field("size", "1550 55 1");
    xml.Field("color", "1 1 1 0.200000");
    xml.Close("UIGraphic");
  }
  xml.Close("UIElementGroup");
  xml.Close("UIElementGroup");
  return xml.text;
}

}  // namespace

bool PatchCharacterMovesScreen(std::string& xml) {
  if (xml.find("<name>moves_evilryu</name>") != npos) return false;
  size_t begin = 0;
  std::string indent;
  if (!FindGroup(xml, "moves_akuma", &begin, &indent)) return false;
  const size_t end = GroupEnd(xml, begin);
  if (end == npos) return false;
  const std::string newline = xml.find("\r\n") != npos ? "\r\n" : "\n";
  xml.insert(end, newline + BuildEvilRyuGroup(indent, newline));
  g_screen_patched.store(true, std::memory_order_relaxed);
  return true;
}

bool PatchCharacterMovesScripts(std::string& xml) {
  constexpr std::string_view kHide = "UIElement_SetColor_FloatsA(moves_akuma,0.0f);";
  constexpr std::string_view kShow = "UIElement_SetColor_FloatsA(moves_akuma,1.0f);";
  if (xml.find("moves_evilryu") != npos || xml.find("name=\"hide_moves_except\"") == npos) return false;
  const size_t hide = xml.find(kHide);
  const size_t show = xml.find(kShow);
  if (hide == npos || show == npos || xml.find(kHide, hide + 1) != npos || xml.find(kShow, show + 1) != npos ||
      show < hide) {
    return false;
  }
  const size_t brace = xml.find('}', show);
  if (brace == npos) return false;
  const std::string newline = xml.find("\r\n") != npos ? "\r\n" : "\n";
  xml.insert(brace + 1, newline + "    else if( GetResultRegister() eq " + std::to_string(kEvilRyuMoveListIndex) +
                            " )" + newline + "    {" + newline +
                            "      UIElement_SetColor_FloatsA(moves_evilryu,1.0f);" + newline + "    }");
  xml.insert(hide + kHide.size(), newline + "    UIElement_SetColor_FloatsA(moves_evilryu,0.0f);");
  g_scripts_patched.store(true, std::memory_order_relaxed);
  return true;
}

std::string EvilRyuWinsText(std::string_view ryu_wins) {
  constexpr std::string_view kName = "RYU";
  const size_t at = ryu_wins.find(kName);
  std::string text(ryu_wins);
  text.insert(at == std::string_view::npos ? 0 : at, "EVIL ");
  return text;
}

uint32_t PatchMoveListText(std::string& table) {
  size_t line_start = 0;
  for (uint32_t line = 0; line < kRyuWinsLine && line_start != npos; ++line) {
    const size_t next = table.find('\n', line_start);
    line_start = next == npos ? npos : next + 1;
  }
  std::string wins;
  if (line_start != npos) {
    const size_t line_end = table.find_first_of("\r\n", line_start);
    wins = EvilRyuWinsText(std::string_view(table).substr(line_start, line_end - line_start));
  }

  std::string patched;
  patched.reserve(table.size() + 128);
  uint32_t changed = 0;
  uint32_t number = 0;
  bool anchored = false;
  size_t start = 0;
  while (start < table.size()) {
    size_t end = table.find('\n', start);
    const bool has_newline = end != npos;
    if (!has_newline) end = table.size();
    std::string_view line = std::string_view(table).substr(start, end - start);
    const bool carriage = !line.empty() && line.back() == '\r';
    if (carriage) line.remove_suffix(1);
    if (number == kBlockHeaderLine) anchored = line == kBlockHeader;
    const MoveListText* match = nullptr;
    for (const MoveListText& entry : kEvilRyuMoveListText) {
      if (entry.line == number) match = &entry;
    }
    if (match && anchored) {
      patched.append(match->text);
      ++changed;
    } else if (number == kEvilRyuWinsLine && anchored && !wins.empty()) {
      patched.append(wins);
    } else {
      patched.append(line);
    }
    if (carriage) patched.push_back('\r');
    if (has_newline) patched.push_back('\n');
    start = end + 1;
    ++number;
  }
  if (changed != kEvilRyuMoveListText.size()) return 0;
  table = std::move(patched);
  g_text_patched.store(true, std::memory_order_relaxed);
  return changed;
}

bool CharacterMovesPageReady() {
  return g_screen_patched.load(std::memory_order_relaxed) && g_scripts_patched.load(std::memory_order_relaxed) &&
         g_text_patched.load(std::memory_order_relaxed);
}

}  // namespace ssf2t::character
