#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "character/roster_cells.h"
#include "character/ultra_hook_table.h"
#include "character/ultra_select_presentation.h"

namespace {

using namespace ssf2t::character;

int failures = 0;

void Check(bool condition, const char* what) {
  if (condition) return;
  std::printf("FAIL %s\n", what);
  ++failures;
}

bool Near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

constexpr uint32_t kImageBase = 0x82000000;
constexpr uint32_t kCmpwiMode2 = 0x2F0B0002;
constexpr uint32_t kBlMask = 0xFC000003;
constexpr uint32_t kBl = 0x48000001;
constexpr int kNativeCellSize = 32;
constexpr int kOriginalCells = 16;

struct Build {
  const char* file;
  uint32_t frame, scene, fade_quad, fade_word, solid_quad, portraits, roster, cell_quad;
  uint32_t roster_x, roster_y;
  uint32_t mini_portraits, mystery_large;
};

const std::array<Build, 2> kBuilds = {{
    {"game/analysis/default-0.0.1.3.image.bin", 0x823EDB18, 0x82181AA8, 0x821D3608, 0x83C132D0, 0x82175C78,
     Base013UltraHooks::kDrawPortraits1, Base013UltraHooks::kDrawRoster1, 0x82175E48, 0x83A21380, 0x83A21490, 0x83BE59E8, 0x83BE59E4},
    {"game/analysis/default.image.bin", 0x823EE208, 0x82181AB8, 0x821D3728, 0x83C13350, 0x82175C98,
     TitleUpdate023UltraHooks::kDrawPortraits1, TitleUpdate023UltraHooks::kDrawRoster1, 0x82175E68, 0x83A21380,
     0x83A21490, 0x83BE5A68, 0x83BE5A64},
}};

struct Image {
  std::vector<uint8_t> bytes;

  uint32_t Word(uint32_t address) const {
    const size_t at = address - kImageBase;
    return uint32_t(bytes[at]) << 24 | uint32_t(bytes[at + 1]) << 16 | uint32_t(bytes[at + 2]) << 8 | bytes[at + 3];
  }
  float Float(uint32_t address) const {
    const uint32_t word = Word(address);
    float value;
    std::memcpy(&value, &word, sizeof(value));
    return value;
  }
  bool Calls(uint32_t site, uint32_t target) const {
    const uint32_t branch = Word(site);
    const int32_t reach = int32_t((branch & 0x03FFFFFC) << 6) >> 6;
    return (branch & kBlMask) == kBl && site + uint32_t(reach) == target;
  }
  std::vector<uint32_t> CallSites(uint32_t first, uint32_t last, uint32_t target) const {
    std::vector<uint32_t> sites;
    for (uint32_t at = first; at < last; at += 4) {
      if (Calls(at, target)) sites.push_back(at);
    }
    return sites;
  }
  std::optional<uint32_t> Find(uint32_t first, uint32_t last, uint32_t word) const {
    for (uint32_t at = first; at < last; at += 4) {
      if (Word(at) == word) return at;
    }
    return std::nullopt;
  }
};

// The pixel a fade of `word` leaves from `cell`: the guest's black quad, blended over what the frame drew before it.
uint8_t AfterFade(uint8_t cell, uint16_t word) { return uint8_t(cell * (255 - (word & 0xFF)) / 255); }

void TestFadeComposition() {
  for (const int cell : {255, 200, 128, 17}) {
    const uint8_t native = uint8_t(cell);
    for (const uint16_t word : {uint16_t(255), uint16_t(191), uint16_t(127), uint16_t(63), uint16_t(0)}) {
      const int shown = 255 - word;
      Check(AfterFade(native, word) == cell * shown / 255, "a cell shows at the roster's alpha");
    }
    Check(AfterFade(native, 255) == 0, "alpha 0 is fully dark");
    Check(AfterFade(native, 0) == native, "alpha 255 is the plain cell");
  }
}

// Everything lifetime and fade depend on is a fact of the guest frame, so it is proven on both images.
void TestGuestFrame(const Build& build) {
  std::ifstream in(std::string(SSF2T_SOURCE_DIR) + "/" + build.file, std::ios::binary);
  if (!in) {
    std::printf("note: %s not found, image checks skipped\n", build.file);
    return;
  }
  Image image{std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>())};
  const uint32_t text_end = 0x83A00000;

  const std::vector<uint32_t> portrait_passes = image.CallSites(kImageBase, text_end, build.portraits);
  Check(portrait_passes.size() == 1 && portrait_passes[0] >= build.scene && portrait_passes[0] < build.scene + 0x400,
        "the scene draw owns the portraits pass");

  const std::vector<uint32_t> roster_calls = image.CallSites(kImageBase, text_end, build.roster);
  Check(roster_calls.size() == 1, "the roster pass has one caller: no other screen can run it");
  const std::optional<uint32_t> compare = image.Find(build.portraits, build.portraits + 0x60, kCmpwiMode2);
  Check(compare.has_value(), "the portraits pass tests presentation mode 2 first");
  if (compare && !roster_calls.empty()) {
    const uint32_t branch = image.Word(*compare + 4);
    Check((branch & 0xFFFF0003) == 0x419A0000, "mode 2 is a taken branch");
    const uint32_t target = *compare + 4 + (branch & 0xFFFC);
    Check(target + 12 == roster_calls[0], "only mode 2 reaches the roster pass: VS and the map phase never do");
  }

  const uint32_t mini_high = 0x3D400000u | (build.mini_portraits >> 16);
  const uint32_t mini_low = 0x394A0000u | (build.mini_portraits & 0xFFFF);
  const std::optional<uint32_t> mini_table = image.Find(build.roster, build.roster + 0x60, mini_low);
  Check(mini_table && image.Word(*mini_table - 4) == mini_high, "the roster loop reads its 16 cells from the mini table");
  Check(build.mini_portraits == build.mystery_large + 4, "the mini table starts right after the last large portrait id");
  Check(kLargePortraitIds == kEvilPortraitSlot + 1 && build.mystery_large + (kLargePortraitIds - kEvilPortraitSlot) * 4 ==
                                                          build.mini_portraits,
        "the first id a loan refuses is the one that would land on Ryu's mini");

  const std::vector<uint32_t> fades = image.CallSites(kImageBase, text_end, build.fade_quad);
  const std::vector<uint32_t> scenes = image.CallSites(build.frame, build.frame + 0x1000, build.scene);
  Check(fades.size() == 1 && fades[0] > build.frame && fades[0] < build.frame + 0x1000,
        "the frame function owns the fade quad");
  Check(!scenes.empty() && !fades.empty() && scenes.back() < fades[0],
        "the fade quad is drawn after the scene that holds the roster pass");

  const uint32_t word_high = 0x3D600000u | (build.fade_word + 0x8000) >> 16;
  const uint32_t word_low = 0xA16B0000u | (build.fade_word & 0xFFFF);
  const std::optional<uint32_t> alpha = image.Find(build.fade_quad, build.fade_quad + 0x90, word_low);
  Check(alpha.has_value() && image.Word(*alpha - 4) == word_high, "the fade quad reads the fade word");
  const std::optional<uint32_t> shift = image.Find(*alpha, build.fade_quad + 0x90, 0x5567C00E);
  Check(shift.has_value(), "the fade word is the alpha of the quad colour");
  const std::vector<uint32_t> solid = image.CallSites(build.fade_quad, build.fade_quad + 0x90, build.solid_quad);
  Check(solid.size() == 1, "the fade is one solid quad");

  const float left = image.Float(0x82006444);
  const float bottom = image.Float(0x8200181C);
  const float width = image.Float(0x82006448);
  const float height = image.Float(0x8200644C);
  Check(left == -80.0f && width == 800.0f && bottom == -9.0f && height == 498.0f, "the fade quad covers the whole view");
  Check(image.Float(0x82000B28) == kCanvasPerArcadeX && image.Float(0x82000AE0) == kCanvasPerArcadeY &&
            image.Float(0x82000AE8) == kCanvasOrigin,
        "the cell quad transform is the guest's");
  Check(image.CallSites(build.roster, build.roster + 0x1200, build.cell_quad).size() > 16,
        "the original cells are drawn with the same quad");

  const auto native_x = [&](int mode, int cell) {
    return float(int32_t(image.Word(build.roster_x + uint32_t(mode) * 68 + uint32_t(cell) * 4)));
  };
  const auto native_y = [&](int mode, int cell) {
    return float(int32_t(image.Word(build.roster_y + uint32_t(mode) * 68 + uint32_t(cell) * 4)));
  };
  const auto center_x = [](const ArcadeQuad& quad) { return quad.x + quad.width * 0.5f; };
  const auto center_y = [](const ArcadeQuad& quad) { return quad.y + quad.height * 0.5f; };

  float arcade_right = 0;
  for (int cell = 0; cell < kOriginalCells; ++cell) arcade_right = std::max(arcade_right, native_x(0, cell));
  float versus_right = 0;
  for (int cell = 0; cell < kOriginalCells; ++cell) versus_right = std::max(versus_right, native_x(1, cell));
  constexpr float half = kNativeCellSize * 0.5f;

  const ArcadeQuad evil = PictureToArcade(UltraPortraitRect(kGuestPicture, kPictureWidth, UltraPortrait::kEvilRyu));
  const ArcadeQuad violent =
      PictureToArcade(UltraPortraitRect(kGuestPicture, kPictureWidth, UltraPortrait::kViolentKen));
  Check(Near(center_x(evil), arcade_right + kNativeCellSize + half, 0.1f), "arcade: Evil Ryu is one column past the grid");
  Check(Near(center_x(violent), center_x(evil), 0.001f), "arcade: both cells share the column");
  Check(Near(center_y(evil), native_y(0, 0) + half, 0.1f), "arcade: Evil Ryu sits on the top roster row");
  Check(Near(center_y(violent), native_y(0, 4) + half, 0.1f), "arcade: Violent Ken sits on the bottom roster row");

  const ArcadeQuad versus_evil =
      PictureToArcade(UltraPortraitRect(kGuestPicture, kPictureWidth, UltraPortrait::kEvilRyu, SelectLayout::kVersus));
  Check(Near(center_x(versus_evil), versus_right + kNativeCellSize + half, 0.1f),
        "versus: the cells are one column past the grid");

  const ArcadeQuad akuma = PictureToArcade(AkumaPortraitRect(kGuestPicture, kPictureWidth));
  Check(Near(center_x(akuma), image.Float(0x82000BE0) + half, 0.2f) &&
            Near(center_y(akuma), image.Float(0x82000AD0) + half, 0.1f),
        "Akuma stays centred on the guest's Akuma cell");
}

void TestGeometryIsKept() {
  const ViewRect picture = {100, 200, 80, 92};
  const ArcadeQuad quad = PictureToArcade(picture);
  const float canvas_x = quad.x * kCanvasPerArcadeX + kCanvasOrigin;
  const float canvas_y = quad.y * kCanvasPerArcadeY + kCanvasOrigin;
  const float width = quad.width * kCanvasPerArcadeX * kPicturePerCanvasX;
  const float height = quad.height * kCanvasPerArcadeY * kPicturePerCanvasY;
  Check(Near((canvas_x - kCanvasLeft) * kPicturePerCanvasX, picture.x, 0.01f), "the quad keeps the picture x");
  Check(Near(kPictureHeight - canvas_y * kPicturePerCanvasY - height, picture.y, 0.01f), "the quad keeps the picture y");
  Check(Near(width, picture.width, 0.01f) && Near(height, picture.height, 0.01f), "the quad keeps the picture size");
}

void TestPaddedArt() {
  PortraitImage art;
  art.width = 519;
  art.height = 64;
  art.rgba.assign(size_t(art.width) * art.height * 4, 0);
  art.rgba[0] = 10;
  art.rgba[1] = 20;
  art.rgba[2] = 30;
  art.rgba[3] = 40;
  const GuestCellImage image = ToGuestCellImage(art);
  Check(image.width == 1024 && image.height == 64, "a name plate is padded to a power of two");
  Check(image.argb.size() == size_t(1024) * 64 * 4, "the guest texture holds every padded pixel");
  Check(image.argb[0] == 40 && image.argb[1] == 10 && image.argb[2] == 20 && image.argb[3] == 30,
        "guest pixels are A,R,G,B");
  Check(image.argb[size_t(image.width - 1) * 4] == 0, "the padding is transparent");

  const ViewRect plate = NamePlatePicture(0, 519, 64);
  const ViewRect padded = PaddedPicture(plate, image);
  Check(Near(padded.x, plate.x, 0.001f) && Near(padded.y, plate.y, 0.001f), "padding keeps the plate's top left corner");
  Check(Near(padded.width * 519.0f / 1024.0f, plate.width, 0.01f) && Near(padded.height, plate.height, 0.01f),
        "padding keeps the plate's size on screen");

  PortraitImage square;
  square.width = square.height = kPortraitSize;
  square.rgba.assign(size_t(kPortraitSize) * kPortraitSize * 4, 7);
  const GuestCellImage cell = ToGuestCellImage(square);
  const ViewRect rect = {1, 2, 80, 92};
  const ViewRect same = PaddedPicture(rect, cell);
  Check(cell.width == kPortraitSize && same.width == rect.width && same.height == rect.height,
        "a roster cell needs no padding");
}

void TestPlan() {
  Check(!PlanRosterCells({}).any(), "everything disabled adds nothing");

  RosterCellInputs evil;
  evil.evil_ryu = true;
  RosterCellPlan plan = PlanRosterCells(evil);
  Check(plan.evil_ryu && !plan.violent_ken && !plan.akuma, "Evil Ryu enabled alone adds his cell");

  RosterCellInputs violent;
  violent.violent_ken = true;
  plan = PlanRosterCells(violent);
  Check(!plan.evil_ryu && plan.violent_ken, "Violent Ken enabled alone adds his cell");

  RosterCellInputs both;
  both.evil_ryu = both.violent_ken = true;
  plan = PlanRosterCells(both);
  Check(plan.evil_ryu && plan.violent_ken, "both enabled add both cells");

  RosterCellInputs cursors = both;
  for (const auto [p1_evil, p1_violent, p2_evil, p2_violent] :
       {std::array{false, false, false, false}, std::array{true, false, false, false},
        std::array{false, false, true, false}, std::array{false, true, false, false},
        std::array{false, false, false, true}, std::array{true, false, false, true}}) {
    cursors.on_evil_ryu = {p1_evil, p2_evil};
    cursors.on_violent_ken = {p1_violent, p2_violent};
    plan = PlanRosterCells(cursors);
    Check(plan.evil_ryu && plan.violent_ken, "no cursor state changes the cells");
    Check(plan.names[0].has_value() == (p1_evil || p1_violent) && plan.names[1].has_value() == (p2_evil || p2_violent),
          "each side hovering an Ultra cell gets its own name plate");
  }

  RosterCellInputs named = both;
  named.on_evil_ryu = {true, false};
  named.on_violent_ken = {false, true};
  plan = PlanRosterCells(named);
  Check(plan.names[0] == RosterArt::kEvilRyuName && plan.names[1] == RosterArt::kViolentKenName,
        "the name plate follows the fighter on each side");
  named.guest_name_drawn = {true, false};
  plan = PlanRosterCells(named);
  Check(!plan.names[0] && plan.names[1], "a name the guest drew itself gets no plate");

  RosterCellInputs lone_akuma;
  lone_akuma.akuma = true;
  lone_akuma.on_evil_ryu = {true, true};
  plan = PlanRosterCells(lone_akuma);
  Check(plan.akuma && !plan.names[0] && !plan.names[1], "Ultra fighters off draws no name plate");
  lone_akuma.live2 = kAkumaLiveSelect;
  Check(!PlanRosterCells(lone_akuma).akuma, "Akuma on a cursor hands over to the guest's own cell");

  const RosterCellPlan first = PlanRosterCells(both);
  const RosterCellPlan second = PlanRosterCells(both);
  Check(first.evil_ryu == second.evil_ryu && first.violent_ken == second.violent_ken,
        "a new Character Select starts from the same plan: nothing is carried between screens");
}

constexpr int kArtSize = 128;
constexpr int kArtBand = 6;
constexpr size_t kArtHeaderSize = 12;
constexpr size_t kArtRecordHeaderSize = 12;
constexpr size_t kArtPixelBytes = size_t(kArtSize) * kArtSize * 4;
// The official 342x408 cell, fitted for the 80x92 roster cell: only the two side gutters show the backing.
constexpr int kArtLeft = 8;
constexpr int kArtRight = 120;

bool ShowsArt(const uint8_t* pixel) { return pixel[0] != 0 || pixel[1] != 0 || pixel[2] != 0; }

void TestInstalledUltraArt() {
  const std::string path = std::string(SSF2T_SOURCE_DIR) + "/assets/characters/ultra_portraits.bin";
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::printf("installed Ultra art checks skipped: no %s\n", path.c_str());
    return;
  }
  const std::vector<uint8_t> file{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  Check(file.size() == kArtHeaderSize + 2 * (kArtRecordHeaderSize + kArtPixelBytes), "both portraits are installed");
  if (file.size() != kArtHeaderSize + 2 * (kArtRecordHeaderSize + kArtPixelBytes)) return;

  for (int record = 0; record < 2; ++record) {
    const uint8_t* pixels =
        file.data() + kArtHeaderSize + record * (kArtRecordHeaderSize + kArtPixelBytes) + kArtRecordHeaderSize;
    const auto at = [&](int x, int y) { return pixels + (size_t(y) * kArtSize + x) * 4; };

    const auto column_has_art = [&](int x) {
      for (int y = kArtBand; y < kArtSize - kArtBand; ++y) {
        if (ShowsArt(at(x, y))) return true;
      }
      return false;
    };
    const auto row_has_art = [&](int y) {
      for (int x = kArtLeft; x < kArtRight; ++x) {
        if (ShowsArt(at(x, y))) return true;
      }
      return false;
    };
    Check(!column_has_art(kArtBand) && !column_has_art(kArtLeft - 1), "the left gutter shows only the backing");
    Check(!column_has_art(kArtRight) && !column_has_art(kArtSize - kArtBand - 1), "the right gutter shows only the backing");
    Check(column_has_art(kArtLeft) && column_has_art(kArtRight - 1), "the official cell reaches both sides uncropped");
    Check(row_has_art(kArtBand) && row_has_art(kArtSize - kArtBand - 1), "the official cell reaches the top and the bottom");
    Check(at(kArtBand, kArtBand)[3] >= 94, "the native backing stays behind the art");
  }
}

}  // namespace

int main() {
  TestFadeComposition();
  TestGeometryIsKept();
  TestPaddedArt();
  TestPlan();
  TestInstalledUltraArt();
  for (const Build& build : kBuilds) TestGuestFrame(build);
  if (failures == 0) std::puts("roster cell tests passed");
  return failures == 0 ? 0 : 1;
}
