#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <utility>
#include <vector>
#include <string_view>

#include "character/akuma.h"
#include "character/akuma_navigation.h"
#include "character/akuma_portrait.h"
#include "character/akuma_select.h"
#include "netplay/fight_snapshot.h"

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
  if (condition) return;
  std::printf("FAIL %s\n", what);
  ++failures;
}

struct TestMemory {
  std::array<uint8_t, 0x400> bytes{};
  uint8_t U8(uint32_t address) const { return bytes[address]; }
};

constexpr uint8_t kBlanka = 2;
constexpr uint8_t kGuile = 3;
constexpr uint8_t kAkumaLive = 32;
constexpr uint8_t kAkumaPortrait = 16;
enum Direction { kUp, kDown, kLeft, kRight };

// Rows of the Base013 and TU023 Character Select navigation table at 0x83A2D320.
constexpr uint8_t kNav[][4] = {
    {4, 4, 9, 1}, {5, 5, 0, 2}, {6, 6, 1, 3}, {32, 7, 2, 13},
};
constexpr uint8_t kAkumaNav[4] = {32, 3, 32, 32};

struct SelectState {
  uint8_t live = 0;
  uint8_t latch = 0;
  uint8_t Live() const { return live; }
  uint8_t Portrait() const { return live == kAkumaLive ? kAkumaPortrait : live; }
  void Move(Direction direction) {
    live = live == kAkumaLive ? kAkumaNav[direction] : kNav[live][direction];
    latch = live == kAkumaLive ? 1 : 0;
  }
};

}  // namespace

int main() {
  using namespace ssf2t::character;

  Check(std::string_view(kEnableAkumaSetting) == "ssf2t_enable_akuma", "Akuma setting key");
  Check(!kDefaultEnableAkuma, "Akuma defaults to Off");
  Check(std::string_view(kEnableAkumaLabel) == "Enable Akuma", "Akuma launcher label");

  Check(AkumaUnlockResult(true, 0) == 1, "On unlocks Akuma when the original check fails");
  Check(AkumaUnlockResult(true, 1) == 1, "On keeps an unlocked Akuma unlocked");
  Check(AkumaUnlockResult(false, 0) == 0, "Off leaves the secret lock untouched");
  Check(AkumaUnlockResult(false, 1) == 1, "Off keeps an earned unlock");

  for (const bool enabled : {true, false}) {
    SelectState p1;
    p1.live = kBlanka;
    Check(p1.Live() == kBlanka && p1.latch == 0, "starting on Blanka is a normal selection");
    Check(p1.Portrait() == kBlanka, "Blanka portrait stays Blanka");
    Check((AkumaUnlockResult(enabled, 0) != 0) == enabled, "cell visibility follows the setting only");

    p1.Move(kRight);
    Check(p1.live == kGuile, "Blanka -> Guile");
    p1.Move(kUp);
    Check(p1.live == kAkumaLive && p1.latch == 1, "Guile + Up reaches Akuma");
    Check(p1.Portrait() == kAkumaPortrait, "Akuma portrait only while on Akuma");
    p1.Move(kDown);
    Check(p1.live == kGuile && p1.latch == 0, "leaving Akuma clears the latch");
    Check(p1.Portrait() == kGuile, "portrait returns to the normal fighter");
    Check((AkumaUnlockResult(enabled, 0) != 0) == enabled, "cell visibility unchanged after leaving Akuma");
  }

  SelectState p1;
  SelectState p2;
  p1.live = kGuile;
  p2.live = kBlanka;
  p1.Move(kUp);
  Check(p1.latch == 1 && p2.latch == 0 && p2.Portrait() == kBlanka, "players select independently");

  TestMemory memory;
  Check(ssf2t::netplay::FighterCharacter(memory, 0) == 0, "normal Ryu is guest character 0");
  memory.bytes[ssf2t::netplay::kCharacterOffset] = 3;
  Check(ssf2t::netplay::FighterCharacter(memory, 0) == 3, "normal Guile stays guest character 3");
  memory.bytes[ssf2t::netplay::kHiddenCharacterOffset] = 1;
  memory.bytes[ssf2t::netplay::kCharacterOffset] = 0;
  Check(ssf2t::netplay::FighterCharacter(memory, 0) == 16, "hidden flag is Akuma 16");

  Check(!AkumaPortraitVisible(false, true, 0, 0), "Off draws no overlay");
  Check(!AkumaPortraitVisible(false, true, kAkumaLive, 0), "Off draws no overlay with Akuma selected");
  Check(!AkumaPortraitVisible(true, false, 0, 0), "On outside Character Select draws no overlay");
  for (const uint8_t live : {uint8_t(0), uint8_t(1), kBlanka, kGuile}) {
    Check(AkumaPortraitVisible(true, true, live, 0), "a normal P1 fighter keeps the overlay");
    Check(AkumaPortraitVisible(true, true, 0, live), "a normal P2 fighter keeps the overlay");
  }
  Check(!AkumaPortraitVisible(true, true, kAkumaLive, 0), "P1 on Akuma hides the overlay");
  Check(!AkumaPortraitVisible(true, true, 0, kAkumaLive), "P2 on Akuma hides the overlay");
  Check(!AkumaPortraitVisible(true, true, kAkumaLive, kAkumaLive), "both on Akuma hide the overlay");
  {
    SelectState state;
    state.live = kGuile;
    Check(AkumaPortraitVisible(true, true, state.live, 0), "Guile shows the overlay");
    const SelectState before = state;
    state.Move(kUp);
    Check(!AkumaPortraitVisible(true, true, state.live, 0), "Guile + Up hands over to the guest square");
    state.Move(kDown);
    Check(AkumaPortraitVisible(true, true, state.live, 0) && state.live == kGuile,
          "leaving Akuma shows the overlay again");
    Check(std::memcmp(&before, &state, sizeof(state)) == 0, "the round trip leaves the selection state as it was");
  }

  const ViewRect hd = GuestView(1280, 720, 16.0f / 9.0f, true);
  const ViewRect slot = AkumaPortraitRect(hd, 1280);
  Check(kPortraitCenterX == 640.0f && kPortraitCenterY == 360.0f, "portrait centre is 640,360");
  Check(kPortraitWidth == 80.0f && kPortraitHeight == 92.0f, "portrait size is 80x92");
  Check(std::fabs(slot.x - 600.0f) < 0.01f && std::fabs(slot.y - 314.0f) < 0.01f, "slot top-left at 1280x720");
  Check(std::fabs(slot.x + slot.width - 680.0f) < 0.01f && std::fabs(slot.y + slot.height - 406.0f) < 0.01f,
        "slot bottom-right at 1280x720");
  const ViewRect tall = GuestView(1920, 1200, 16.0f / 9.0f, true);
  Check(std::fabs(tall.height - 1080.0f) < 0.1f && std::fabs(tall.y - 60.0f) < 0.1f, "letterbox centres the picture");
  const ViewRect stretched = GuestView(1920, 1200, 16.0f / 9.0f, false);
  Check(stretched.width == 1920 && stretched.height == 1200, "stretch fills the window");
  const ViewRect scaled = AkumaPortraitRect(GuestView(2560, 1440, 16.0f / 9.0f, true), 1280);
  Check(std::fabs(scaled.width - slot.width * 2.0f) < 0.1f && std::fabs(scaled.height - slot.height * 2.0f) < 0.1f, "the slot follows the viewport scale");

  std::array<uint8_t, 0x2D + 16384> texture{};
  std::memcpy(texture.data(), "SG28XT03", 8);
  const uint8_t block[16] = {0x05, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                             0xF8, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00, 0x00};
  for (int i = 0; i < 1024; ++i) std::memcpy(texture.data() + 0x2D + i * 16, block, 16);
  const PortraitImage image = DecodePortraitTexture(texture.data(), texture.size());
  Check(image.width == 128 && image.height == 128 && image.rgba.size() == 128u * 128u * 4u, "portrait decodes to 128x128");
  Check(image.rgba[0] == 0xFF && image.rgba[1] == 0 && image.rgba[2] == 0 && image.rgba[3] == 0xFF,
        "DXT5 block decodes with swapped words");
  for (size_t i = 3; i < image.rgba.size(); i += 4) {
    if (image.rgba[i] != 0xFF) {
      Check(false, "a flat opaque block decodes to flat opaque alpha");
      break;
    }
  }
  Check(DecodePortraitTexture(texture.data(), 16).rgba.empty(), "a truncated texture is rejected");
  texture[0] = 'X';
  Check(DecodePortraitTexture(texture.data(), texture.size()).rgba.empty(), "a foreign texture is rejected");

  const PortraitImage real = LoadAkumaPortrait("game/extracted/flog_x360.fpg");
  if (!real.error) {
    size_t opaque = 0;
    uint8_t seen[256] = {};
    for (size_t i = 3; i < real.rgba.size(); i += 4) {
      opaque += real.rgba[i] == 0xFF;
      seen[real.rgba[i]] = 1;
    }
    int distinct = 0;
    for (const uint8_t flag : seen) distinct += flag;
    Check(real.rgba.size() == 128u * 128u * 4u, "real portrait is 128x128 RGBA");
    Check(opaque * 10 > real.rgba.size() / 4 * 7, "real portrait is mostly opaque");
    Check(distinct > 16, "real portrait alpha is not four-bit quantised");
  }

  {
    uint8_t next = 0;
    AkumaNavigation off;
    Check(!off.Resolve(false, 0, kGuile, NavDirection::kUp, next), "Off does not redirect Up from the top row");
    Check(off.column[0] == kNoColumn && off.column[1] == kNoColumn, "Off remembers no column");
    Check(!off.Resolve(false, 0, kAkumaLive, NavDirection::kUp, next), "Off does not touch Akuma exits");

    for (int column = 0; column < kRosterColumns; ++column) {
      AkumaNavigation nav;
      Check(nav.Resolve(true, 0, kTopRow[column], NavDirection::kUp, next) && next == kAkumaLive,
            "Up from a top-row fighter enters Akuma");
      Check(nav.column[0] == column && nav.column[1] == kNoColumn, "the source column is remembered for P1 only");
      AkumaNavigation down = nav;
      Check(down.Resolve(true, 0, kAkumaLive, NavDirection::kDown, next) && next == kTopRow[column],
            "Akuma + Down returns to the same top-row fighter");
      Check(down.column[0] == kNoColumn, "leaving Akuma clears the remembered column");
      Check(nav.Resolve(true, 0, kAkumaLive, NavDirection::kUp, next) && next == kBottomRow[column],
            "Akuma + Up reaches the bottom-row fighter in the same column");
      Check(!nav.Resolve(true, 0, kBottomRow[column], NavDirection::kUp, next), "bottom-row Up keeps the original move");
      Check(!nav.Resolve(true, 0, kTopRow[column], NavDirection::kDown, next), "top-row Down keeps the original move");
    }

    Check(kTopRow[kGuileColumn] == kGuile && kBottomRow[kGuileColumn] == 7, "Guile is one of the eight columns");

    AkumaNavigation both;
    both.Resolve(true, 0, kTopRow[1], NavDirection::kUp, next);
    both.Resolve(true, 1, kTopRow[6], NavDirection::kUp, next);
    Check(both.column[0] == 1 && both.column[1] == 6, "P1 and P2 remember their columns independently");
    Check(both.Resolve(true, 0, kAkumaLive, NavDirection::kUp, next) && next == kBottomRow[1], "P1 exits through its column");
    Check(both.column[1] == 6, "P1 leaving Akuma does not touch P2");
    Check(both.Resolve(true, 1, kAkumaLive, NavDirection::kDown, next) && next == kTopRow[6], "P2 exits through its column");

    AkumaNavigation fresh;
    Check(fresh.Resolve(true, 0, kAkumaLive, NavDirection::kDown, next) && next == kGuile, "unknown column falls back to Guile");
    Check(fresh.Resolve(true, 1, kAkumaLive, NavDirection::kUp, next) && next == kBottomRow[kGuileColumn],
          "unknown column Up falls back to Guile's column");
    fresh.column[0] = 99;
    Check(fresh.Resolve(true, 0, kAkumaLive, NavDirection::kDown, next) && next == kGuile, "invalid column falls back to Guile");
    Check(!fresh.Resolve(true, 2, kGuile, NavDirection::kUp, next), "an invalid side is ignored");
    Check(!fresh.Resolve(true, 0, 16, NavDirection::kUp, next), "a non-roster value is not redirected");
  }

  {
    using namespace ssf2t::character;
    uint8_t next = 0;

    for (int column = 0; column < kVersusColumns; ++column) {
      AkumaNavigation nav;
      const uint8_t top = kVersusTopRow[column];
      Check(nav.Resolve(true, SelectLayout::kVersus, 0, top, NavDirection::kUp, next) && next == kAkumaLive,
            "Local Match: Up from every top-row cell enters Akuma");
      Check(nav.column[0] == column && nav.column[1] == kNoColumn, "Local Match: the source column is remembered");
      AkumaNavigation down = nav;
      Check(down.Resolve(true, SelectLayout::kVersus, 0, kAkumaLive, NavDirection::kDown, next) && next == top,
            "Local Match: Akuma + Down returns to the same top-row cell");
      Check(nav.Resolve(true, SelectLayout::kVersus, 0, kAkumaLive, NavDirection::kUp, next) &&
                next == kVersusBottomRow[column],
            "Local Match: Akuma + Up reaches the bottom-row cell of the column");
      Check(!nav.Resolve(true, SelectLayout::kVersus, 0, kVersusBottomRow[column], NavDirection::kUp, next),
            "Local Match: bottom-row Up keeps the original move");
      Check(!nav.Resolve(true, SelectLayout::kVersus, 0, top, NavDirection::kDown, next),
            "Local Match: top-row Down keeps the original move");
    }
    Check(kVersusTopRow[kHondaColumn] == 1, "Local Match: the guest's own Akuma entry is Honda");

    AkumaNavigation off;
    Check(!off.Resolve(false, SelectLayout::kVersus, 0, kVersusTopRow[3], NavDirection::kUp, next),
          "Off: Local Match navigation is untouched");
    for (const uint8_t middle : {uint8_t(10), uint8_t(4), uint8_t(5), uint8_t(6), uint8_t(7), uint8_t(9)}) {
      AkumaNavigation nav;
      Check(!nav.Resolve(true, SelectLayout::kVersus, 0, middle, NavDirection::kUp, next),
            "Local Match: the middle row never enters Akuma");
    }

    AkumaNavigation both;
    both.Resolve(true, SelectLayout::kVersus, 0, kVersusTopRow[1], NavDirection::kUp, next);
    both.Resolve(true, SelectLayout::kVersus, 1, kVersusTopRow[5], NavDirection::kUp, next);
    Check(both.column[0] == 1 && both.column[1] == 5, "Local Match: P1 and P2 remember their own columns");
    Check(both.Resolve(true, SelectLayout::kVersus, 1, kAkumaLive, NavDirection::kDown, next) &&
              next == kVersusTopRow[5],
          "Local Match: P2 returns through its own column");
    Check(both.column[0] == 1, "Local Match: P2 leaving Akuma keeps P1's column");

    AkumaNavigation moved;
    moved.Resolve(true, SelectLayout::kVersus, 0, kVersusTopRow[4], NavDirection::kUp, next);
    Check(moved.Resolve(true, SelectLayout::kArcade, 0, kAkumaLive, NavDirection::kDown, next) &&
              next == kTopRow[kGuileColumn],
          "A column from the other grid is not reused");

    Check(SelectLayoutForMode(0) == SelectLayout::kArcade, "mode 0 is the arcade grid");
    Check(SelectLayoutForMode(1) == SelectLayout::kVersus && SelectLayoutForMode(2) == SelectLayout::kVersus,
          "local modes use the versus grid");
  }

  {
    using namespace ssf2t::character;
    // The guest's own tables decide the visual rows: positions give the grid, the navigation table gives the Akuma cell.
    constexpr uint32_t kImageBase = 0x82000000;
    constexpr uint32_t kArcadeNav = 0x83A2D320;
    constexpr uint32_t kVersusNav = 0x83A2D298;
    constexpr uint32_t kArcadePositions = 0x83A330B8;
    constexpr uint32_t kVersusPositions = 0x83A33140;
    std::ifstream in("game/analysis/default-0.0.1.3.image.bin", std::ios::binary);
    std::vector<uint8_t> image((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (image.size() > kVersusPositions - kImageBase + 34 * 4) {
      const auto nav = [&](uint32_t table, int live, int direction) {
        return image[table - kImageBase + size_t(live) * 4 + size_t(direction)];
      };
      const auto position = [&](uint32_t table, int live) {
        const size_t at = table - kImageBase + size_t(live) * 4;
        return std::pair<int, int>{image[at] << 8 | image[at + 1], image[at + 2] << 8 | image[at + 3]};
      };
      const auto row_of = [&](uint32_t table, int y, int columns) {
        std::vector<std::pair<int, int>> cells;
        for (int live = 0; live < 18; ++live) {
          if (position(table, live).second == y) cells.push_back({position(table, live).first, live});
        }
        std::sort(cells.begin(), cells.end());
        std::vector<int> ids;
        for (const auto& cell : cells) ids.push_back(cell.second);
        return ids.size() == size_t(columns) ? ids : std::vector<int>{};
      };
      const auto matches = [](const std::vector<int>& ids, const auto& row) {
        return std::equal(ids.begin(), ids.end(), row.begin(), row.end());
      };
      Check(matches(row_of(kArcadePositions, 48, kRosterColumns), kTopRow), "the arcade top row is the y=48 row of the guest positions");
      Check(matches(row_of(kArcadePositions, 16, kRosterColumns), kBottomRow), "the arcade bottom row is the y=16 row of the guest positions");
      Check(matches(row_of(kVersusPositions, 80, kVersusColumns), kVersusTopRow), "the versus top row is the y=80 row of the guest positions");
      Check(matches(row_of(kVersusPositions, 16, kVersusColumns), kVersusBottomRow), "the versus bottom row is the y=16 row of the guest positions");
      Check(position(kArcadePositions, kAkumaLive).second == 112 && position(kVersusPositions, kAkumaLive).second == 112,
            "Akuma sits one row above the top row in both grids");
      Check(nav(kArcadeNav, TopRowAt(SelectLayout::kArcade, NativeAkumaColumn(SelectLayout::kArcade)), 0) == kAkumaLive,
            "the arcade grid enters Akuma from the native column");
      Check(nav(kVersusNav, TopRowAt(SelectLayout::kVersus, NativeAkumaColumn(SelectLayout::kVersus)), 0) == kAkumaLive,
            "the versus grid enters Akuma from the native column");
      for (int column = 0; column < kRosterColumns; ++column) {
        Check(nav(kArcadeNav, TopRowAt(SelectLayout::kArcade, column), 1) == BottomRowAt(SelectLayout::kArcade, column),
              "arcade columns pair top and bottom through the guest's Down move");
      }
      for (int column = 0; column < kVersusColumns; ++column) {
        const int top = TopRowAt(SelectLayout::kVersus, column);
        Check(nav(kVersusNav, top, 3) == TopRowAt(SelectLayout::kVersus, (column + 1) % kVersusColumns),
              "the versus top row is chained by the guest's Right move");
      }
    }
  }

  Check(ssf2t::online::kCharacterCount == 19 && ssf2t::online::kEvilRyuCharacter == 17 &&
            ssf2t::online::kViolentKenCharacter == 18,
        "roster is the original seventeen plus Evil Ryu and Violent Ken");
  Check(ssf2t::online::kAkumaCharacter == 16, "Akuma keeps the original roster id");

  if (failures == 0) std::printf("akuma setting tests passed\n");
  return failures == 0 ? 0 : 1;
}
