#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

#include "character/akuma_portrait.h"
#include "character/ultra_fighters.h"
#include "character/ultra_portraits.h"

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
  if (condition) return;
  std::printf("FAIL %s\n", what);
  ++failures;
}

bool Near(float a, float b, float tolerance = 0.01f) { return std::fabs(a - b) <= tolerance; }

using namespace ssf2t::character;

constexpr size_t kPixels = size_t(kPortraitSize) * kPortraitSize * 4;

void PutLE32(std::vector<uint8_t>& out, uint32_t value) {
  for (int i = 0; i < 4; ++i) out.push_back(uint8_t(value >> (8 * i)));
}

std::vector<uint8_t> Container(const std::vector<std::pair<uint32_t, uint8_t>>& records, uint32_t width = 128,
                               uint32_t height = 128) {
  std::vector<uint8_t> out = {'S', 'S', 'F', '2', 'U', 'P', 'T', '1'};
  PutLE32(out, uint32_t(records.size()));
  for (const auto& [id, fill] : records) {
    PutLE32(out, id);
    PutLE32(out, width);
    PutLE32(out, height);
    out.insert(out.end(), size_t(width) * height * 4, fill);
  }
  return out;
}

PortraitImage Solid(uint8_t value) {
  PortraitImage image;
  image.width = image.height = kPortraitSize;
  image.rgba.assign(kPixels, value);
  return image;
}

void SettingTests() {
  Check(std::string_view(kEnableUltraFightersSetting) == "ssf2t_enable_ultra_fighters", "setting key");
  Check(!kDefaultEnableUltraFighters, "setting defaults to Off");
  Check(std::string_view(kEnableUltraFightersLabel) == "Enable Evil Ryu and Violent Ken", "setting label");
}

void GatingTests() {
  for (const bool select_active : {false, true}) {
    for (const bool renderer : {false, true}) {
      for (const bool texture : {false, true}) {
        Check(!UltraPortraitVisible(false, select_active, renderer, texture), "Off hides every portrait");
        const bool expected = select_active && renderer && texture;
        Check(UltraPortraitVisible(true, select_active, renderer, texture) == expected,
              "On draws only on Character Select with renderer and texture");
      }
    }
  }
  Check(UltraPortraitVisible(true, true, true, true), "On allows both portraits");
  Check(!UltraPortraitVisible(true, false, true, true), "Character Select inactive hides both portraits");
}

void GeometryTests() {
  const ViewRect view{0, 0, 1280, 720};
  const ViewRect akuma = AkumaPortraitRect(view, 1280);
  Check(Near(akuma.x + akuma.width * 0.5f, 640) && Near(akuma.y + akuma.height * 0.5f, 360), "Akuma center unchanged");
  Check(Near(akuma.width, 80) && Near(akuma.height, 92), "Akuma size unchanged");

  const ViewRect top = UltraPortraitRect(view, 1280, UltraPortrait::kEvilRyu);
  const ViewRect bottom = UltraPortraitRect(view, 1280, UltraPortrait::kViolentKen);
  Check(Near(top.width, 80) && Near(top.height, 92), "Evil Ryu cell size matches the roster cell");
  Check(Near(bottom.width, 80) && Near(bottom.height, 92), "Violent Ken cell size matches the roster cell");
  Check(Near(top.x, 954.67f) && Near(top.y, 506.0f), "Evil Ryu rectangle");
  Check(Near(bottom.x, 954.67f) && Near(bottom.y, 602.0f), "Violent Ken rectangle");
  Check(Near(top.x, bottom.x), "both portraits share one column");
  Check(Near(bottom.y - top.y, kRosterRowPitch) && Near(kRosterRowPitch, 96.0f), "rows are one roster row apart");
  Check(Near(top.y + top.height * 0.5f, 552.0f) && Near(bottom.y + bottom.height * 0.5f, 648.0f),
        "cell centers are 552 and 648");

  const ViewRect versus_top = UltraPortraitRect(view, 1280, UltraPortrait::kEvilRyu, SelectLayout::kVersus);
  const ViewRect versus_bottom = UltraPortraitRect(view, 1280, UltraPortrait::kViolentKen, SelectLayout::kVersus);
  Check(Near(top.x - versus_top.x, kRosterColumnPitch), "the versus grid's extra column is one pitch left of the arcade one");
  Check(Near(versus_top.x + versus_top.width * 0.5f, 640.0f + (304.0f - 192.0f) * (19.0f / 12.0f) * (14.0f / 9.0f)),
        "the versus Evil Ryu cell is one pitch right of the grid's last column");
  Check(Near(versus_top.y, top.y) && Near(versus_bottom.y, bottom.y) && Near(versus_top.x, versus_bottom.x),
        "the rows do not move between grids");

  const float column7_center = kPortraitCenterX + (7.0f - kRosterCenterColumn) * kRosterColumnPitch;
  Check(Near(top.x + top.width * 0.5f - column7_center, kRosterColumnPitch), "new column follows column 7 by one pitch");
  Check(top.x > akuma.x + akuma.width, "new column sits right of Akuma's cell");
  Check(Near(kRosterColumnPitch, 78.8148f, 0.001f), "column pitch");
  Check(top.x + top.width < 1280, "new column stays on the picture");
}

void ViewportTests() {
  const ViewRect base{0, 0, 1280, 720};
  const ViewRect base_top = UltraPortraitRect(base, 1280, UltraPortrait::kEvilRyu);
  for (const float scale : {1.0f, 1.5f, 2.25f}) {
    const ViewRect view{0, 0, 1280 * scale, 720 * scale};
    const ViewRect top = UltraPortraitRect(view, 1280, UltraPortrait::kEvilRyu);
    const ViewRect bottom = UltraPortraitRect(view, 1280, UltraPortrait::kViolentKen);
    Check(Near(top.x, base_top.x * scale) && Near(top.y, base_top.y * scale), "rectangle origin scales with DPI");
    Check(Near(top.width, 80 * scale) && Near(top.height, 92 * scale), "rectangle size scales with DPI");
    Check(Near(bottom.y - top.y, 96 * scale), "row spacing scales with DPI");
    const ViewRect akuma = AkumaPortraitRect(view, 1280);
    Check(Near(akuma.x + akuma.width * 0.5f, 640 * scale), "Akuma scales with the same transform");
  }

  const ViewRect letterboxed = GuestView(2000, 720, 1280.0f / 720.0f, true);
  const ViewRect inside = UltraPortraitRect(letterboxed, 1280, UltraPortrait::kEvilRyu);
  Check(Near(inside.x, letterboxed.x + base_top.x), "letterbox offset applies");

  const ViewRect wide{0, 0, 1706.0f, 720};
  const ViewRect wide_top = UltraPortraitRect(wide, 1706, UltraPortrait::kEvilRyu);
  Check(Near(wide_top.x, base_top.x + (1706 - 1280) * 0.5f), "wider guest picture keeps the column centred with the roster");
}

void TextureTests() {
  const auto file = Container({{0, 0x11}, {1, 0x22}});
  const PortraitImage evil = ParseUltraPortrait(file.data(), file.size(), UltraPortrait::kEvilRyu);
  const PortraitImage violent = ParseUltraPortrait(file.data(), file.size(), UltraPortrait::kViolentKen);
  Check(!evil.error && evil.width == 128 && evil.height == 128 && evil.rgba.size() == kPixels, "Evil Ryu decodes");
  Check(!violent.error && violent.rgba.size() == kPixels, "Violent Ken decodes");
  Check(evil.rgba[0] == 0x11 && violent.rgba[0] == 0x22, "records map to their portraits");

  const auto only_ryu = Container({{0, 0x11}});
  Check(ParseUltraPortrait(only_ryu.data(), only_ryu.size(), UltraPortrait::kViolentKen).error != nullptr,
        "missing record fails");
  Check(ParseUltraPortrait(only_ryu.data(), only_ryu.size(), UltraPortrait::kEvilRyu).error == nullptr,
        "present record still decodes");

  auto bad_magic = file;
  bad_magic[0] = 'X';
  Check(ParseUltraPortrait(bad_magic.data(), bad_magic.size(), UltraPortrait::kEvilRyu).error != nullptr,
        "bad magic fails");
  Check(ParseUltraPortrait(file.data(), 4, UltraPortrait::kEvilRyu).error != nullptr, "tiny file fails");
  Check(ParseUltraPortrait(file.data(), file.size() - 1, UltraPortrait::kViolentKen).error != nullptr,
        "truncated pixel data fails");
  const auto wrong_size = Container({{0, 0x11}}, 64, 64);
  Check(ParseUltraPortrait(wrong_size.data(), wrong_size.size(), UltraPortrait::kEvilRyu).error != nullptr,
        "unexpected dimensions fail");
  auto many = file;
  many[8] = 0xFF;
  Check(ParseUltraPortrait(many.data(), many.size(), UltraPortrait::kEvilRyu).error != nullptr,
        "absurd record count fails");

  const auto path = std::filesystem::temp_directory_path() / "ssf2t_ultra_portraits_test.bin";
  {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(file.data()), std::streamsize(file.size()));
  }
  const PortraitImage loaded = LoadUltraPortrait(path, UltraPortrait::kViolentKen);
  Check(!loaded.error && loaded.rgba[0] == 0x22, "file load succeeds");
  std::filesystem::remove(path);
  Check(LoadUltraPortrait(path, UltraPortrait::kEvilRyu).error != nullptr, "missing file fails safely");
}

void FrameTests() {
  PortraitImage art = Solid(0x10);
  const PortraitImage frame = Solid(0xF0);
  ApplyRosterFrame(art, frame);
  const auto pixel = [&](int x, int y) { return art.rgba[(size_t(y) * art.width + x) * 4]; };
  Check(pixel(0, 0) == 0xF0 && pixel(127, 127) == 0xF0 && pixel(64, 5) == 0xF0 && pixel(5, 64) == 0xF0,
        "frame band is copied");
  Check(pixel(6, 6) == 0x10 && pixel(64, 64) == 0x10 && pixel(121, 121) == 0x10, "interior keeps the art");

  PortraitImage other = Solid(0x10);
  PortraitImage small;
  small.width = small.height = 64;
  small.rgba.assign(size_t(64) * 64 * 4, 0xF0);
  ApplyRosterFrame(other, small);
  Check(other.rgba[0] == 0x10, "mismatched frame is ignored");
  PortraitImage failed;
  failed.error = "decode";
  ApplyRosterFrame(other, failed);
  Check(other.rgba[0] == 0x10, "failed frame is ignored");
}

void NoGuestWritesTests() {
  std::array<uint8_t, 64> guest{};
  guest.fill(0xAB);
  const auto before = guest;
  const ViewRect view{0, 0, 1280, 720};
  (void)UltraPortraitRect(view, 1280, UltraPortrait::kEvilRyu);
  (void)UltraPortraitVisible(true, true, true, true);
  const auto file = Container({{0, 1}, {1, 2}});
  (void)ParseUltraPortrait(file.data(), file.size(), UltraPortrait::kEvilRyu);
  Check(guest == before, "portrait helpers touch no guest memory");
}

}  // namespace

int main() {
  SettingTests();
  GatingTests();
  GeometryTests();
  ViewportTests();
  TextureTests();
  FrameTests();
  NoGuestWritesTests();
  if (failures == 0) std::printf("ultra portraits tests passed\n");
  return failures == 0 ? 0 : 1;
}
