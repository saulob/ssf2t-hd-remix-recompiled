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
  Check(ResultsDisplayRow(0) == 0, "Ryu keeps the first row");
  Check(ResultsDisplayRow(1) == 2, "Honda moves below the Evil Ryu row");
  Check(ResultsDisplayRow(16) == 17, "Akuma is the last row");
  Check(ResultsNativeRow(kEvilRyuResultsRow) == -1, "the Evil Ryu row has no guest row");
  for (int row = 0; row < kResultsNativeRows; ++row) {
    Check(ResultsNativeRow(ResultsDisplayRow(row)) == row, "every guest row keeps its data");
    Check(ResultsDisplayRow(row) != kEvilRyuResultsRow, "no fighter is displaced by the Evil Ryu row");
  }
}

void TestAccounting() {
  EvilResultsBook book;
  ResultsCorrection fix = book.Record(0, {true, false});
  Check(book.wins(0) == 1 && book.losses(0) == 0, "an Evil Ryu win counts for Evil Ryu");
  Check(fix.ryu_win_side == 0 && fix.ryu_loss_side == -1, "Ryu's row gives the win back");
  Check(book.wins(1) == 0 && book.losses(1) == 0, "the other side's book is untouched");
  Check(book.last_match_evil(0) && !book.last_match_evil(1), "the highlight follows the side");

  fix = book.Record(1, {true, false});
  Check(book.losses(0) == 1, "an Evil Ryu loss counts for Evil Ryu");
  Check(fix.ryu_win_side == -1 && fix.ryu_loss_side == 0, "Ryu's row gives the loss back");

  fix = book.Record(0, {false, false});
  Check(fix.ryu_win_side == -1 && fix.ryu_loss_side == -1, "a normal Ryu match stays on Ryu's row");
  Check(book.wins(0) == 1 && book.losses(0) == 1, "a normal Ryu match leaves the Evil Ryu record");
  Check(!book.last_match_evil(0), "a normal Ryu match moves the highlight back");

  fix = book.Record(1, {false, true});
  Check(book.wins(1) == 1 && fix.ryu_win_side == 1 && fix.ryu_loss_side == -1, "P2 Evil Ryu keeps its own record");
  Check(book.wins(0) == 1 && book.losses(0) == 1, "P2's win does not touch P1's Evil Ryu record");

  fix = book.Record(0, {true, true});
  Check(book.wins(0) == 2 && book.losses(1) == 1, "Evil vs Evil counts both sides");
  Check(fix.ryu_win_side == 0 && fix.ryu_loss_side == 1, "Evil vs Evil gives both counts back");

  fix = book.Record(2, {true, true});
  Check(fix.ryu_win_side == -1 && fix.ryu_loss_side == -1 && book.wins(0) == 2, "no winner, no record");

  book.Reset(0);
  Check(book.wins(0) == 0 && book.losses(0) == 0 && !book.last_match_evil(0), "reset clears that side");
  Check(book.wins(1) == 1 && book.losses(1) == 1, "reset leaves the other side");
  book.Reset(9);
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
    Check(evil > 0 && taken.count(evil) == 0, "the Evil Ryu row has its own place");
    Check(evil < int(PosY(screen, "honda" + number) - top + 0.5f), "the Evil Ryu row sits above Honda");
    Check(int(PosY(screen, "akuma" + number)) < 860, "the last row stays above TOTAL WINS");
    Check(Count(screen, "<name>evilryu" + number + "_highlight</name>") == 1, "the row has a highlight");
    Check(Count(screen, "<name>stripeR" + number + "</name>") == 1, "the row has a stripe");
    Check(Count(screen, "results" + number + "_row18") == 1, "the highlight has its event");
    Check(Count(scripts, "results" + number + "_row18") == 1, "the script sends that event");
    Check(Count(scripts, "UIElement_SetVisibleFlag(evilryu" + number + ", false);") == 2,
          "the row hides under the highlight and in a lobby");
  }
  Check(Count(animations, "<name>results_highlight_position18</name>") == 1, "P1 has the new highlight position");
  Check(Count(animations, "<name>results2_highlight_position18</name>") == 1, "P2 has the new highlight position");
  Check(Count(grid, "<numRows>18</numRows>") == 1 && Count(grid, "<numVisibleRows>18</numVisibleRows>") == 1,
        "only the results grid grows");
  Check(Count(grid, "<numRows>") == Count(grid_original, "<numRows>"), "the other grids stay");
  Check(Count(grid, "<name>grid_text_17_0</name>") == 1 && Count(grid, "<name>grid_text_17_1</name>") == 1,
        "the grid gets a win and a loss cell");
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
