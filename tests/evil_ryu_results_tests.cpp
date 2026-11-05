#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "character/evil_ryu_results.h"
#include "updater/third_party/miniz/miniz.h"

namespace {

using namespace ssf2t::character;

int failures = 0;

void Check(bool condition, const char* what) {
  if (condition) return;
  std::printf("FAIL %s\n", what);
  ++failures;
}

size_t Count(const std::string& text, std::string_view needle) {
  size_t count = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) ++count;
  return count;
}

bool Balanced(const std::string& xml) {
  for (const char* tag : {"UIText", "UIGraphic", "UIElementGroup", "UIScreen", "UIGrid", "UIAnim", "animEvent", "row"}) {
    if (Count(xml, std::string("<") + tag + ">") != Count(xml, std::string("</") + tag + ">")) return false;
  }
  return true;
}

// Every opening tag closes in order, so a block inserted at a stale offset fails here even when the counts agree.
bool WellFormed(const std::string& xml) {
  std::vector<std::string> open;
  for (size_t at = xml.find('<'); at != std::string::npos; at = xml.find('<', at + 1)) {
    if (xml.compare(at, 2, "<?") == 0 || xml.compare(at, 2, "<!") == 0) continue;
    const size_t end = xml.find('>', at);
    if (end == std::string::npos) return false;
    if (xml[end - 1] == '/') continue;
    if (xml[at + 1] == '/') {
      if (open.empty() || xml.compare(at + 2, end - at - 2, open.back()) != 0) return false;
      open.pop_back();
    } else {
      open.push_back(xml.substr(at + 1, xml.find_first_of(" >", at) - at - 1));
    }
  }
  return open.empty();
}

void TestRows() {
  Check(kResultsRows == 19, "both Ultra fighters have a row");
  Check(ResultsDisplayRow(0) == 0, "Ryu keeps the first row");
  Check(kEvilRyuResultsRow == 1, "Evil Ryu is second");
  Check(ResultsDisplayRow(4) == 2, "Ken is third");
  Check(kViolentKenResultsRow == 3, "Violent Ken is fourth");
  Check(ResultsDisplayRow(1) == 4, "Honda follows Violent Ken");
  Check(ResultsDisplayRow(5) == 7, "Chun-Li follows Guile");
  Check(ResultsDisplayRow(16) == 18, "Akuma is the last row");
  Check(ResultsNativeRow(kEvilRyuResultsRow) == -1 && ResultsNativeRow(kViolentKenResultsRow) == -1,
        "the Ultra rows have no guest row");
  Check(ResultsHostRow(kEvilRyuResultsRow) == 0 && ResultsHostRow(kViolentKenResultsRow) == 1, "each Ultra row has its book");
  std::array<int, kResultsRows> used{};
  for (int row = 0; row < kResultsNativeRows; ++row) {
    Check(ResultsNativeRow(ResultsDisplayRow(row)) == row, "every guest row keeps its data");
    Check(ResultsHostRow(ResultsDisplayRow(row)) == -1, "no fighter is displaced by an Ultra row");
    ++used[size_t(ResultsDisplayRow(row))];
  }
  for (const HostResultsRow& host : kHostResultsRows) ++used[size_t(host.display_row)];
  for (const int count : used) Check(count == 1, "every display row has exactly one owner");
  Check(kHostResultsRows[0].character == 17 && kHostResultsRows[1].character == 18, "the identities are 17 and 18");
  Check(kHostResultsRows[1].carrier == 4 && kHostResultsRows[1].identity == FighterIdentity::kViolentKen,
        "Violent Ken's carrier is Ken");
}

void TestAccounting() {
  HostResultsBook book;
  ResultsCorrection fix = book.Record(0, {true, false});
  Check(book.wins(0) == 1 && book.losses(0) == 0, "an Evil Ryu win counts for Evil Ryu");
  Check(fix.carrier_win_side == 0 && fix.carrier_loss_side == -1, "Ryu's row gives the win back");
  Check(book.wins(1) == 0 && book.losses(1) == 0, "the other side's book is untouched");
  Check(book.last_match(0) && !book.last_match(1), "the highlight follows the side");

  fix = book.Record(1, {true, false});
  Check(book.losses(0) == 1, "an Evil Ryu loss counts for Evil Ryu");
  Check(fix.carrier_win_side == -1 && fix.carrier_loss_side == 0, "Ryu's row gives the loss back");

  fix = book.Record(0, {false, false});
  Check(fix.carrier_win_side == -1 && fix.carrier_loss_side == -1, "a normal Ryu match stays on Ryu's row");
  Check(book.wins(0) == 1 && book.losses(0) == 1, "a normal Ryu match leaves the Evil Ryu record");
  Check(!book.last_match(0), "a normal Ryu match moves the highlight back");

  fix = book.Record(1, {false, true});
  Check(book.wins(1) == 1 && fix.carrier_win_side == 1 && fix.carrier_loss_side == -1, "P2 Evil Ryu keeps its own record");
  Check(book.wins(0) == 1 && book.losses(0) == 1, "P2's win does not touch P1's Evil Ryu record");

  fix = book.Record(0, {true, true});
  Check(book.wins(0) == 2 && book.losses(1) == 1, "Evil vs Evil counts both sides");
  Check(fix.carrier_win_side == 0 && fix.carrier_loss_side == 1, "Evil vs Evil gives both counts back");

  fix = book.Record(2, {true, true});
  Check(fix.carrier_win_side == -1 && fix.carrier_loss_side == -1 && book.wins(0) == 2, "no winner, no record");

  book.Reset(0);
  Check(book.wins(0) == 0 && book.losses(0) == 0 && !book.last_match(0), "reset clears that side");
  Check(book.wins(1) == 1 && book.losses(1) == 1, "reset leaves the other side");
  book.Reset(9);

  HostResultsBook& evil = GlobalHostResults(0);
  HostResultsBook& violent = GlobalHostResults(1);
  Check(&evil != &violent, "each Ultra fighter has its own book");
  const ResultsCorrection evil_fix = evil.Record(1, {true, false});
  const ResultsCorrection violent_fix = violent.Record(1, {false, true});
  Check(violent.wins(1) == 1 && violent_fix.carrier_win_side == 1, "a Violent Ken win leaves Ken's row");
  Check(evil.losses(0) == 1 && evil_fix.carrier_loss_side == 0, "Evil Ryu's loss in the same match stays its own");
  Check(evil.wins(1) == 0 && violent.losses(0) == 0, "the two books never mix");
}

const char kSmallScripts[] = R"(<scripts>
  <script name="InitResultsPlayer0">
)";

void TestRefusals() {
  std::string text = "<UIScreenSet></UIScreenSet>";
  Check(!PatchResultsScreen(text) && text == "<UIScreenSet></UIScreenSet>", "another screen set is left alone");
  text = kSmallScripts;
  Check(!PatchResultsScripts(text) && text == kSmallScripts, "another layout is left alone");
  text = "<UIElementDescriptors></UIElementDescriptors>";
  Check(!PatchResultsGrid(text) && !PatchResultsAnimations(text), "another descriptor file is left alone");
  Check(!ResultsRowReady(), "the row stays closed until every document took it");
}

bool ReadEntry(std::ifstream& in, uint32_t slot, std::string* out) {
  uint32_t entry[4];
  in.seekg(0x800 + slot * 16);
  in.read(reinterpret_cast<char*>(entry), 16);
  if (!in || entry[2] == 0 || entry[2] > 4000000) return false;
  std::vector<unsigned char> stored(entry[2]);
  in.seekg(entry[1]);
  in.read(reinterpret_cast<char*>(stored.data()), entry[2]);
  std::vector<unsigned char> data(8 << 20);
  mz_ulong size = mz_ulong(data.size());
  if (mz_uncompress(data.data(), &size, stored.data(), mz_ulong(stored.size())) != MZ_OK) return false;
  out->assign(reinterpret_cast<const char*>(data.data()), size);
  return true;
}

float PosY(const std::string& xml, const std::string& name) {
  const size_t at = xml.find("<name>" + name + "</name>");
  if (at == std::string::npos) return -1;
  const size_t pos = xml.find("<pos>", at);
  float x = 0;
  float y = 0;
  if (std::sscanf(xml.c_str() + pos + 5, "%f %f", &x, &y) != 2) return -1;
  return y;
}

void TestRealData() {
  std::ifstream in("game/extracted/flog_x360.fpg", std::ios::binary);
  if (!in) {
    std::printf("note: game/extracted/flog_x360.fpg not found, real-data checks skipped\n");
    return;
  }
  std::string screen_original;
  std::string scripts_original;
  std::string animations_original;
  std::string grid_original;
  if (!ReadEntry(in, 52, &screen_original) || !ReadEntry(in, 44, &scripts_original) ||
      !ReadEntry(in, 192, &animations_original) || !ReadEntry(in, 193, &grid_original)) {
    std::printf("note: results documents not found, real-data checks skipped\n");
    return;
  }
  std::string screen = screen_original;
  std::string scripts = scripts_original;
  std::string animations = animations_original;
  std::string grid = grid_original;
  Check(PatchResultsScreen(screen), "the real screen set takes the row");
  Check(PatchResultsScripts(scripts), "the real scripts take the row");
  Check(PatchResultsGrid(grid), "the real grid takes the row");
  Check(PatchResultsAnimations(animations), "the real animations take the row");
  Check(ResultsRowReady(), "the row opens once all four are patched");

  Check(Balanced(screen) && Balanced(scripts) && Balanced(animations) && Balanced(grid), "the documents stay balanced");
  Check(WellFormed(screen_original) && WellFormed(animations_original) && WellFormed(grid_original),
        "the well-formed check accepts the guest's own documents");
  Check(WellFormed(screen) && WellFormed(animations) && WellFormed(grid), "the patched documents stay well formed");
  std::string again = screen;
  Check(!PatchResultsScreen(again), "the screen is only patched once");

  constexpr std::array<std::string_view, 17> kRows = {"ryu",   "honda", "blanka",  "guile",   "ken",  "chunli",
                                                       "zangief", "dhalsim", "bison", "sagat",   "balrog", "vega",
                                                       "cammy", "thawk", "feilong", "deejay", "akuma"};
  for (const int player : {1, 2}) {
    const std::string number = std::to_string(player);
    std::map<int, std::string> taken;
    const float top = PosY(screen, "ryu" + number);
    for (int row = 0; row < kResultsNativeRows; ++row) {
      const std::string name = std::string(kRows[size_t(row)]) + number;
      Check(Count(screen, "<name>" + name + "</name>") == Count(screen_original, "<name>" + name + "</name>"),
            "every original row stays");
      const int y = int(PosY(screen, name) - top + 0.5f);
      Check(taken.emplace(y, name).second, "no two rows overlap");
    }
    const int evil = int(PosY(screen, "evilryu" + number) - top + 0.5f);
    const int violent = int(PosY(screen, "violentken" + number) - top + 0.5f);
    Check(evil > 0 && taken.count(evil) == 0, "the Evil Ryu row has its own place");
    Check(violent > 0 && taken.count(violent) == 0, "the Violent Ken row has its own place");
    const int ken = int(PosY(screen, "ken" + number) - top + 0.5f);
    const int honda = int(PosY(screen, "honda" + number) - top + 0.5f);
    Check(0 < evil && evil < ken && ken < violent && violent < honda, "Ryu, Evil Ryu, Ken, Violent Ken, then the rest");
    Check(evil == 31 && honda == 4 * 31, "the rows sit at a pitch of 31");
    Check(PosY(screen, "akuma" + number) <= 267 + 17 * 33, "the last row sits no lower than with 18 rows at 33");
    Check(PosY(screen, "stripeS" + number) + 28.5f <= 271 + 18 * 30.25f, "the stripes stay inside the box");
    for (const char* label : {"evilryu", "violentken"}) {
      const std::string name = label + number;
      Check(Count(screen, "<name>" + name + "_highlight</name>") == 1, "the row has a highlight");
      Check(Count(scripts, "UIElement_SetVisibleFlag(" + name + ", false);") == 2,
            "the row hides under the highlight and in a lobby");
    }
    const size_t bar_group = screen.find("<name>highlight_move_group" + number + "</name>", screen.find("<name>Results</name>"));
    const size_t bar = screen.find("<name>highlight" + number + "</name>", bar_group);
    const size_t bar_original = screen_original.find(
        "<name>highlight" + number + "</name>", screen_original.find("<name>highlight_move_group" + number + "</name>",
                                                               screen_original.find("<name>Results</name>")));
    float bar_x = 0;
    float bar_y = 0;
    float bar_width = 0;
    float bar_height = 0;
    float stock_width = 0;
    float stock_height = 0;
    float x = 0;
    std::sscanf(screen.c_str() + screen.find("<pos>", bar) + 5, "%f %f", &bar_x, &bar_y);
    std::sscanf(screen.c_str() + screen.find("<size>", bar) + 6, "%f %f", &bar_width, &bar_height);
    std::sscanf(screen_original.c_str() + screen_original.find("<size>", bar_original) + 6, "%f %f", &stock_width,
                &stock_height);
    std::sscanf(screen_original.c_str() + screen_original.find("<pos>", bar_original) + 5, "%f", &x);
    Check(stock_height == 39 && bar_height <= 31, "the highlight fills one row and spills into neither neighbour");
    Check(bar_width == stock_width && bar_x == x, "the highlight keeps its width and column");
    Check(bar_y + bar_height / 2 == 322.5f, "the highlight keeps the stock centre on the row text");
    for (const float row : {0.0f, 3.0f, 18.0f}) {
      const float top = (((272.47f + row * 28.40f) * 1.02f) + 9.0f) * 1.07f;
      const float bar_top = bar_y + row * 31;
      Check(top - bar_top < 0.5f && bar_top - top < 0.5f, "stripes and highlight stay on the same rows");
    }
    for (const char* stripe : {"stripeR", "stripeS"}) {
      Check(Count(screen, "<name>" + (stripe + number) + "</name>") == 1, "the row has a stripe");
    }
    for (const char* event : {"_row18", "_row19"}) {
      Check(Count(screen, "results" + number + event) == 1, "the highlight has its event");
      Check(Count(scripts, "results" + number + event) == 1, "the script sends that event");
    }
  }
  for (const char* position : {"18", "19"}) {
    Check(Count(animations, std::string("<name>results_highlight_position") + position + "</name>") == 1,
          "P1 has the new highlight position");
    Check(Count(animations, std::string("<name>results2_highlight_position") + position + "</name>") == 1,
          "P2 has the new highlight position");
  }
  Check(PosY(animations, "results_highlight_position5") == 2 * 31, "Ken's highlight follows his row");
  Check(PosY(animations, "results_highlight_position19") == 3 * 31, "Violent Ken's highlight follows his row");
  Check(Count(scripts, "GetResultRegister() eq 18") == 2, "both players' highlights take Violent Ken's value");
  Check(Count(grid, "<numRows>19</numRows>") == 1 && Count(grid, "<numVisibleRows>19</numVisibleRows>") == 1,
        "only the results grid grows");
  Check(Count(grid, "<numRows>") == Count(grid_original, "<numRows>"), "the other grids stay");
  for (int row : {17, 18}) {
    for (int column : {0, 1}) {
      Check(Count(grid, "<name>grid_text_" + std::to_string(row) + "_" + std::to_string(column) + "</name>") == 1,
            "the grid gets a win and a loss cell");
    }
  }
  Check(PosY(grid, "grid_text_18_0") == 18 * 31, "the last cell follows the pitch");
  Check(screen.find("<name>Results</name>") == screen_original.find("<name>Results</name>"),
        "the screens before Results are byte for byte unchanged");
  const size_t after_old = screen_original.find("</UIScreen>", screen_original.find("<name>Results</name>"));
  const size_t after_new = screen.find("</UIScreen>", screen.find("<name>Results</name>"));
  Check(screen.compare(after_new, std::string::npos, screen_original, after_old, std::string::npos) == 0,
        "the screens after Results are byte for byte unchanged");
}

}  // namespace

int main() {
  TestRows();
  TestAccounting();
  TestRefusals();
  TestRealData();
  if (failures == 0) std::printf("evil ryu results tests passed\n");
  return failures == 0 ? 0 : 1;
}
