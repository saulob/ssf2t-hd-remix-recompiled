#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "character/evil_ryu_assets.h"

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
  if (condition) return;
  std::printf("FAIL %s\n", what);
  ++failures;
}

bool Near(float a, float b) { return std::fabs(a - b) < 0.5f; }

void Put32(std::vector<uint8_t>& out, uint32_t v) {
  for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i)));
}

void Record(std::vector<uint8_t>& out, uint32_t id, uint32_t w, uint32_t h, uint8_t fill) {
  Put32(out, id);
  Put32(out, w);
  Put32(out, h);
  out.insert(out.end(), size_t(w) * h * 4, fill);
}

}  // namespace

int main() {
  using namespace ssf2t::character;

  std::vector<uint8_t> file;
  const char magic[] = "SSF2ERA1";
  file.insert(file.end(), magic, magic + 8);
  Put32(file, 2);
  Record(file, 1, 3, 4, 0x11);
  Record(file, 2, 5, 2, 0x22);

  PortraitImage large = ParseEvilRyuAsset(file.data(), file.size(), EvilRyuAsset::kLargePortrait);
  Check(!large.error && large.width == 3 && large.height == 4 && large.rgba.size() == 48 && large.rgba[0] == 0x11,
        "large portrait record");
  PortraitImage name = ParseEvilRyuAsset(file.data(), file.size(), EvilRyuAsset::kNameGraphic);
  Check(!name.error && name.width == 5 && name.height == 2 && name.rgba[0] == 0x22, "name record after a record");

  Check(ParseEvilRyuAsset(file.data(), file.size() - 1, EvilRyuAsset::kNameGraphic).error != nullptr,
        "truncated record fails");
  std::vector<uint8_t> bad = file;
  bad[0] = 'X';
  Check(ParseEvilRyuAsset(bad.data(), bad.size(), EvilRyuAsset::kLargePortrait).error != nullptr, "bad magic fails");
  std::vector<uint8_t> huge(file.begin(), file.begin() + 12);
  Put32(huge, 1);
  Put32(huge, 5000);
  Put32(huge, 5000);
  Check(ParseEvilRyuAsset(huge.data(), huge.size(), EvilRyuAsset::kLargePortrait).error != nullptr,
        "oversized record fails");
  Check(ParseEvilRyuAsset(file.data(), 4, EvilRyuAsset::kLargePortrait).error != nullptr, "short file fails");

  const ViewRect p1 = LargePortraitPicture(0);
  const ViewRect p2 = LargePortraitPicture(1);
  Check(Near(p1.x, 167.1f) && Near(p1.y, 72.0f) && Near(p1.width, 236.4f) && Near(p1.height, 288.0f),
        "player 1 large portrait rect");
  Check(Near(p2.x, 876.4f) && Near(p2.y, 72.0f) && Near(p2.width, 236.4f) && Near(p2.height, 288.0f),
        "player 2 large portrait rect");
  Check(Near(640.0f + (176.0f + 16.0f - 192.0f) * kArcadeToPictureX, 640.0f), "Akuma cell centre keeps the model");

  const ViewRect view{0, 0, 1280, 720};
  const ViewRect wide = ToView({0, 0, 1920, 1080}, 1280.0f, p1);
  Check(Near(wide.x, p1.x * 1.5f) && Near(wide.height, 432.0f), "scaled to the display");
  (void)view;

  if (failures == 0) std::printf("evil ryu assets tests passed\n");
  return failures == 0 ? 0 : 1;
}
