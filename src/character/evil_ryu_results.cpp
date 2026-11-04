#include "character/evil_ryu_results.h"

#include <atomic>
#include <cstdio>
#include <string_view>

namespace ssf2t::character {
namespace {

constexpr size_t npos = std::string::npos;
constexpr float kRowPitch = 33.0f;
constexpr float kStripePitch = 30.25f;
constexpr float kFirstStripeY = 271.0f;
constexpr std::string_view kStripeLetters = "ABCDEFGHIJKLMNOPQ";
constexpr std::array<std::string_view, kResultsNativeRows> kRows = {
    "ryu",     "honda", "blanka", "guile", "ken",   "chunli",  "zangief", "dhalsim", "bison",
    "sagat",   "balrog", "vega",  "cammy", "thawk", "feilong", "deejay",  "akuma"};
constexpr std::string_view kEvilRyuTextId = "Ryu_Hadouken";

std::atomic<bool> g_screen{false};
std::atomic<bool> g_scripts{false};
std::atomic<bool> g_grid{false};
std::atomic<bool> g_animations{false};

std::string Number(float value) {
  char text[32];
  std::snprintf(text, sizeof(text), "%.2f", value);
  return text;
}

std::string NameTag(std::string_view name) { return "<name>" + std::string(name) + "</name>"; }

bool ReplaceAll(std::string& text, std::string_view from, std::string_view to) {
  bool found = false;
  for (size_t at = text.find(from); at != npos; at = text.find(from, at + to.size())) {
    text.replace(at, from.size(), to);
    found = true;
  }
  return found;
}

struct Span {
  size_t begin = npos;
  size_t end = npos;
  bool ok() const { return begin != npos && end != npos; }
};

// The element named `name`, from its opening <tag> to the end of its closing tag, inside [from, to).
Span FindElement(const std::string& xml, std::string_view name, std::string_view tag, size_t from, size_t to) {
  const size_t at = xml.find(NameTag(name), from);
  if (at == npos || at >= to) return {};
  const std::string open = "<" + std::string(tag) + ">";
  const std::string close = "</" + std::string(tag) + ">";
  const size_t begin = xml.rfind(open, at);
  const size_t end = xml.find(close, at);
  if (begin == npos || end == npos || begin < from || end >= to) return {};
  return {begin, end + close.size()};
}

// Sets the second number of the first <pos> at or after `at`.
bool SetPosY(std::string& xml, size_t at, float y) {
  constexpr std::string_view kOpen = "<pos>";
  const size_t begin = xml.find(kOpen, at);
  if (begin == npos) return false;
  const size_t values = begin + kOpen.size();
  const size_t close = xml.find("</pos>", values);
  const size_t first_gap = xml.find(' ', values);
  if (close == npos || first_gap == npos || first_gap > close) return false;
  const size_t second_gap = xml.find(' ', first_gap + 1);
  if (second_gap == npos || second_gap > close) return false;
  xml.replace(first_gap + 1, second_gap - first_gap - 1, Number(y));
  return true;
}

bool ReadPosY(const std::string& xml, size_t at, float* y) {
  constexpr std::string_view kOpen = "<pos>";
  const size_t begin = xml.find(kOpen, at);
  if (begin == npos) return false;
  float x = 0;
  if (std::sscanf(xml.c_str() + begin + kOpen.size(), "%f %f", &x, y) != 2) return false;
  return true;
}

std::string SuffixName(std::string_view row, int player, bool highlight) {
  return std::string(row) + std::to_string(player) + (highlight ? "_highlight" : "");
}

bool ReflowRows(std::string& xml, size_t screen, size_t& screen_end, int player, bool highlight) {
  const std::string first = SuffixName(kRows[0], player, highlight);
  const Span ryu = FindElement(xml, first, "UIText", screen, screen_end);
  float top = 0;
  if (!ryu.ok() || !ReadPosY(xml, ryu.begin, &top)) return false;
  for (int row = 1; row < kResultsNativeRows; ++row) {
    const Span text = FindElement(xml, SuffixName(kRows[size_t(row)], player, highlight), "UIText", screen, screen_end);
    if (!text.ok() || !SetPosY(xml, text.begin, top + float(ResultsDisplayRow(row)) * kRowPitch)) return false;
    screen_end = xml.find("</UIScreen>", screen);
  }
  const Span model = FindElement(xml, SuffixName("honda", player, highlight), "UIText", screen, screen_end);
  if (!model.ok()) return false;
  std::string copy = xml.substr(model.begin, model.end - model.begin);
  if (!ReplaceAll(copy, NameTag(SuffixName("honda", player, highlight)),
                  NameTag(SuffixName("evilryu", player, highlight))) ||
      !ReplaceAll(copy, "<textID>CHAR_HONDA</textID>", "<textID>" + std::string(kEvilRyuTextId) + "</textID>") ||
      !SetPosY(copy, 0, top + float(kEvilRyuResultsRow) * kRowPitch)) {
    return false;
  }
  const Span anchor = FindElement(xml, first, "UIText", screen, screen_end);
  if (!anchor.ok()) return false;
  xml.insert(anchor.end, "\n" + copy);
  screen_end = xml.find("</UIScreen>", screen);
  return true;
}

bool SetSizeY(std::string& xml, size_t at, float y) {
  constexpr std::string_view kOpen = "<size>";
  const size_t begin = xml.find(kOpen, at);
  if (begin == npos) return false;
  const size_t first_gap = xml.find(' ', begin);
  const size_t second_gap = first_gap == npos ? npos : xml.find(' ', first_gap + 1);
  if (second_gap == npos) return false;
  xml.replace(first_gap + 1, second_gap - first_gap - 1, Number(y));
  return true;
}

bool ReflowStripes(std::string& xml, size_t screen, size_t& screen_end, int player) {
  const std::string suffix = std::to_string(player);
  for (size_t row = 0; row < kStripeLetters.size(); ++row) {
    const Span stripe = FindElement(xml, "stripe" + std::string(1, kStripeLetters[row]) + suffix, "UIGraphic", screen,
                                    screen_end);
    if (!stripe.ok()) return false;
    if (!SetPosY(xml, stripe.begin, kFirstStripeY + float(row) * kStripePitch)) return false;
    const Span moved = FindElement(xml, "stripe" + std::string(1, kStripeLetters[row]) + suffix, "UIGraphic", screen,
                                   xml.find("</UIScreen>", screen));
    if (!moved.ok() || !SetSizeY(xml, moved.begin, kStripePitch)) return false;
    screen_end = xml.find("</UIScreen>", screen);
  }
  const Span model = FindElement(xml, "stripeB" + suffix, "UIGraphic", screen, screen_end);
  const Span last = FindElement(xml, "stripeQ" + suffix, "UIGraphic", screen, screen_end);
  if (!model.ok() || !last.ok()) return false;
  std::string copy = xml.substr(model.begin, model.end - model.begin);
  if (!ReplaceAll(copy, NameTag("stripeB" + suffix), NameTag("stripeR" + suffix)) ||
      !SetPosY(copy, 0, kFirstStripeY + float(kResultsNativeRows) * kStripePitch)) {
    return false;
  }
  xml.insert(last.end, "\n" + copy);
  screen_end = xml.find("</UIScreen>", screen);
  return true;
}

std::string HighlightAnimation(int player, int position) {
  return (player == 1 ? std::string("results_highlight_position") : std::string("results2_highlight_position")) +
         std::to_string(position);
}

bool AddHighlightEvent(std::string& xml, size_t screen, size_t& screen_end, int player) {
  const size_t group = xml.find(NameTag("highlight_move_group" + std::to_string(player)), screen);
  if (group == npos || group >= screen_end) return false;
  const size_t event = xml.find("<animEvent>", group);
  if (event == npos || event >= screen_end) return false;
  xml.insert(event, "<animEvent>\n<anim>" + HighlightAnimation(player, kResultsRows) + "</anim>\n<event>results" +
                        std::to_string(player) + "_row" + std::to_string(kResultsRows) + "</event>\n</animEvent>\n");
  screen_end = xml.find("</UIScreen>", screen);
  return true;
}

// Inserts text after the end of the line holding `needle`.
bool InsertAfterLine(std::string& xml, std::string_view needle, std::string_view text) {
  const size_t at = xml.find(needle);
  if (at == npos) return false;
  const size_t end = xml.find('\n', at);
  if (end == npos) return false;
  xml.insert(end + 1, text);
  return true;
}

}  // namespace

ResultsCorrection EvilResultsBook::Record(int winner, const std::array<bool, 2>& evil) {
  ResultsCorrection correction;
  last_match_evil_ = evil;
  if (winner < 0 || winner > 1) return correction;
  const int loser = 1 - winner;
  if (evil[size_t(winner)]) {
    ++wins_[size_t(winner)];
    correction.ryu_win_side = winner;
  }
  if (evil[size_t(loser)]) {
    ++losses_[size_t(loser)];
    correction.ryu_loss_side = loser;
  }
  return correction;
}

void EvilResultsBook::Reset(int side) {
  if (side < 0 || side > 1) return;
  wins_[size_t(side)] = 0;
  losses_[size_t(side)] = 0;
  last_match_evil_[size_t(side)] = false;
}

EvilResultsBook& GlobalEvilResults() {
  static EvilResultsBook book;
  return book;
}

bool PatchResultsScreen(std::string& xml) {
  if (xml.find(NameTag("results1_winslosses_grid")) == npos || xml.find(NameTag("evilryu1")) != npos) return false;
  std::string patched = xml;
  const size_t name = patched.find(NameTag("Results"));
  if (name == npos) return false;
  const size_t screen = patched.rfind("<UIScreen>", name);
  size_t screen_end = patched.find("</UIScreen>", name);
  if (screen == npos || screen_end == npos) return false;
  for (const int player : {1, 2}) {
    if (!ReflowRows(patched, screen, screen_end, player, false) ||
        !ReflowRows(patched, screen, screen_end, player, true) || !ReflowStripes(patched, screen, screen_end, player) ||
        !AddHighlightEvent(patched, screen, screen_end, player)) {
      return false;
    }
  }
  xml = std::move(patched);
  g_screen.store(true, std::memory_order_relaxed);
  return true;
}

bool PatchResultsScripts(std::string& xml) {
  if (xml.find("name=\"InitResultsPlayer0\"") == npos || xml.find("evilryu1") != npos) return false;
  std::string patched = xml;
  const std::string newline = patched.find("\r\n") != npos ? "\r\n" : "\n";
  for (const int player : {1, 2}) {
    const std::string number = std::to_string(player);
    const std::string reset = "UIElement_SetColor_FloatsA(akuma" + number + "_highlight, 0.0f);";
    const std::string label = "evilryu" + number;
    const std::string defaults = "\t\tUIElement_SetVisibleFlag(" + label + ", true);" + newline +
                                 "\t\tUIElement_SetColor_FloatsA(" + label + "_highlight, 0.0f);" + newline +
                                 "\t\tif( InLobby() )" + newline + "\t\t{" + newline +
                                 "\t\t\tUIElement_SetVisibleFlag(" + label + ", false);" + newline + "\t\t}" + newline;
    if (!InsertAfterLine(patched, reset, defaults)) return false;
    const std::string last_row = "UIEvent_SendToTopScreen(results" + number + "_row17);";
    const size_t row = patched.find(last_row);
    if (row == npos) return false;
    const size_t last_else = patched.rfind("else", row);
    if (last_else == npos) return false;
    patched.insert(last_else, "else if( GetResultRegister() eq " + std::to_string(kEvilRyuResultsCharacter) + " )" +
                                  newline + "      {" + newline + "\t\t\t\tUIEvent_SendToTopScreen(results" + number +
                                  "_row" + std::to_string(kResultsRows) + ");" + newline +
                                  "\t\t\t\tUIElement_SetVisibleFlag(" + label + ", false);" + newline +
                                  "\t\t\t\tUIElement_SetColor_Floats(" + label +
                                  "_highlight, 1.0f, 1.0f, 1.0f, 1.0f);" + newline + "      }" + newline + "      ");
  }
  xml = std::move(patched);
  g_scripts.store(true, std::memory_order_relaxed);
  return true;
}

bool PatchResultsGrid(std::string& xml) {
  constexpr std::string_view kRowsTag = "<numRows>17</numRows>";
  constexpr std::string_view kVisibleTag = "<numVisibleRows>17</numVisibleRows>";
  const Span grid = FindElement(xml, "results_winslosses_grid", "UIGrid", 0, xml.size());
  if (!grid.ok()) return false;
  std::string block = xml.substr(grid.begin, grid.end - grid.begin);
  if (block.find("grid_text_17_0") != npos || !ReplaceAll(block, kRowsTag, "<numRows>18</numRows>") ||
      !ReplaceAll(block, kVisibleTag, "<numVisibleRows>18</numVisibleRows>")) {
    return false;
  }
  for (int row = 0; row < kResultsNativeRows; ++row) {
    for (int column = 0; column < kResultsColumns; ++column) {
      const size_t at = block.find(NameTag("grid_text_" + std::to_string(row) + "_" + std::to_string(column)));
      if (at == npos || !SetPosY(block, at, float(row) * kRowPitch)) return false;
    }
  }
  const size_t list_end = block.find("</UIGridItemList>");
  const size_t last_close = list_end == npos ? npos : block.rfind("</row>", list_end);
  const size_t last_open = last_close == npos ? npos : block.rfind("<row>", last_close);
  if (last_open == npos) return false;
  std::string row = block.substr(last_open, last_close + 6 - last_open);
  for (int column = 0; column < kResultsColumns; ++column) {
    const std::string number = std::to_string(column);
    const std::string from = NameTag("grid_text_16_" + number);
    const size_t at = row.find(from);
    if (at == npos || !SetPosY(row, at, float(kResultsNativeRows) * kRowPitch)) return false;
    ReplaceAll(row, from, NameTag("grid_text_17_" + number));
  }
  block.insert(last_close + 6, "\n" + row);
  xml.replace(grid.begin, grid.end - grid.begin, block);
  g_grid.store(true, std::memory_order_relaxed);
  return true;
}

bool PatchResultsAnimations(std::string& xml) {
  if (xml.find(NameTag(HighlightAnimation(1, 1))) == npos || xml.find(NameTag(HighlightAnimation(1, kResultsRows))) != npos) {
    return false;
  }
  std::string patched = xml;
  for (const int player : {1, 2}) {
    for (int position = 1; position <= kResultsNativeRows; ++position) {
      const Span animation = FindElement(patched, HighlightAnimation(player, position), "UIAnim", 0, patched.size());
      if (!animation.ok()) return false;
      const float y = float(ResultsDisplayRow(position - 1)) * kRowPitch;
      size_t end = animation.end;
      for (size_t at = patched.find("<pos>", animation.begin); at != npos && at < end;
           at = patched.find("<pos>", at + 5)) {
        const size_t before = patched.size();
        if (!SetPosY(patched, at, y)) return false;
        end += patched.size() - before;
      }
    }
    const Span model = FindElement(patched, HighlightAnimation(player, 2), "UIAnim", 0, patched.size());
    if (!model.ok()) return false;
    std::string copy = patched.substr(model.begin, model.end - model.begin);
    if (!ReplaceAll(copy, NameTag(HighlightAnimation(player, 2)), NameTag(HighlightAnimation(player, kResultsRows)))) {
      return false;
    }
    for (size_t at = copy.find("<pos>"); at != npos; at = copy.find("<pos>", at + 5)) {
      if (!SetPosY(copy, at, float(kEvilRyuResultsRow) * kRowPitch)) return false;
    }
    patched.insert(model.end, "\n" + copy);
  }
  xml = std::move(patched);
  g_animations.store(true, std::memory_order_relaxed);
  return true;
}

bool ResultsRowReady() {
  return g_screen.load(std::memory_order_relaxed) && g_scripts.load(std::memory_order_relaxed) &&
         g_grid.load(std::memory_order_relaxed) && g_animations.load(std::memory_order_relaxed);
}

}  // namespace ssf2t::character
