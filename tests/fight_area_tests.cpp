// Fight Area tests: the setting values, the mask decision and the hook target. Pure logic, no window or game files.
// Run: ssf2t_fight_area_tests

#include <bit>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "aspect_ratio.h"
#include "fight_area.h"
#include "presentation_setting.h"

using namespace ssf2t;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

std::string ReadSource(const char* name) {
  std::ifstream in(std::filesystem::path(SSF2T_SOURCE_DIR) / "src" / name, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

void TestDefaultIsOriginal() {
  CHECK(std::string_view(kFightAreaSetting) == "ssf2t_fight_area");
  CHECK(std::string_view(kDefaultFightArea) == "original");
  CHECK(!FightAreaFills(kDefaultFightArea));
  CHECK(!FightAreaFills(""));
  CHECK(!FightAreaFills("Fill 16:9"));
  CHECK(!FightAreaFills("fill"));
  CHECK(FightAreaFills("fill_16_9"));
}

void TestSerialization() {
  CHECK(std::string_view(FightAreaValue(0)) == "original");
  CHECK(std::string_view(FightAreaValue(1)) == "fill_16_9");
  CHECK(FightAreaFills(FightAreaValue(FightAreaSegment(true))));
  CHECK(!FightAreaFills(FightAreaValue(FightAreaSegment(false))));
  CHECK(FightAreaSegment(false) == 0);
  CHECK(FightAreaSegment(true) == 1);
}

constexpr float kAspect = 0.7777778f;

void TestOriginalNeverActs() {
  for (int display = 0; display <= 1; ++display) {
    for (uint32_t mode = 0; mode <= 1; ++mode) {
      for (int fight = 0; fight <= 1; ++fight) {
        CHECK(!FightFillActive(false, uint8_t(display), mode, uint8_t(fight), kAspect, 0.5f));
      }
    }
  }
}

void TestFillActiveOnlyInWidescreenFights() {
  CHECK(FightFillActive(true, 1, 1, 1, kAspect, 0.5f));
  CHECK(!FightFillActive(true, 1, 1, 0, kAspect, 0.5f));
  CHECK(!FightFillActive(true, 1, 0, 1, kAspect, 0.5f));
  CHECK(!FightFillActive(true, 0, 1, 1, kAspect, 0.5f));
  CHECK(!FightFillActive(true, 1, 1, 1, 1.0f, 0.5f));
}

void TestShiftFollowsCamera() {
  CHECK(FightShift(0.0f) == 16.0f);
  CHECK(FightShift(0.5f) == 0.0f);
  CHECK(FightShift(1.0f) == -16.0f);
  CHECK(FightShift(0.25f) == 8.0f);
  CHECK(FightShift(0.75f) == -8.0f);
  CHECK(FightShift(-3.0f) == 16.0f);
  CHECK(FightShift(7.0f) == -16.0f);
}

void TestNonFiniteCameraFallsBackToStock() {
  CHECK(!FightFillActive(true, 1, 1, 1, kAspect, std::numeric_limits<float>::quiet_NaN()));
  CHECK(!FightFillActive(true, 1, 1, 1, kAspect, std::numeric_limits<float>::infinity()));
  CHECK(!FightFillActive(true, 1, 1, 1, kAspect, -std::numeric_limits<float>::infinity()));
}

void TestOrthoShiftKeepsWidth() {
  CHECK(OrthoShift(0.0f, 640.0f, 0.0f) == 16.0f);
  CHECK(OrthoShift(0.0f, 1920.0f, 0.0f) == 48.0f);
  CHECK(OrthoShift(0.0f, 1920.0f, 1.0f) == -48.0f);
  CHECK(OrthoShift(0.0f, 640.0f, 0.5f) == 0.0f);
}

void TestFillReadsGuestGlobals() {
  static uint8_t memory[0x240000] = {};
  uint8_t* base = memory - 0x83C00000u;
  StoreBE32(base, kWidescreenMode, 1);
  base[kWidescreenDisplayFlag] = 1;
  base[kFightActiveFlag] = 1;
  StoreBE32(base, kWidescreenAspect, std::bit_cast<uint32_t>(kAspect));
  StoreBE32(base, kFightCameraX, std::bit_cast<uint32_t>(0.5f));
  CHECK(FightFillActive(true, base));
  CHECK(!FightFillActive(false, base));
  StoreBE32(base, kFightCameraX, 0x7FC00000);
  CHECK(!FightFillActive(true, base));
  StoreBE32(base, kFightCameraX, std::bit_cast<uint32_t>(0.5f));
  base[kFightActiveFlag] = 0;
  CHECK(!FightFillActive(true, base));
}

void TestSettingsAreIndependent() {
  CHECK(std::string_view(kFightAreaSetting) != kLetterboxSetting);
  CHECK(std::string_view(kFightAreaSetting) != "ssf2t_aspect_ratio");
  CHECK(std::string_view(kLetterboxSetting) == "present_letterbox");
  CHECK(LetterboxSegment(true) == 0 && LetterboxSegment(false) == 1);
  CHECK(ParseAspectRatio("21:9") == AspectRatio::k21x9);
  CHECK(ParseAspectRatio("16:9") == AspectRatio::k16x9);
  CHECK(VideoModeWidth(AspectRatio::k16x9) == 1280);
  CHECK(VideoModeWidth(AspectRatio::k21x9) == 1680);
}

void TestWiring() {
  const std::string settings = ReadSource("settings.cpp");
  CHECK(settings.find("REXCVAR_DEFINE_STRING(ssf2t_fight_area, \"original\"") != std::string::npos);
  CHECK(settings.find("      kFightAreaSetting,") != std::string::npos);
  CHECK(settings.find("  ApplyFightArea();") != std::string::npos);
  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(launcher.find("{\"Original\", \"Fill 16:9\"}") != std::string::npos);
  CHECK(launcher.find("kLetterboxSetting, LetterboxFromSegment(i)") != std::string::npos);
  const std::string hook = ReadSource("fight_area_hook.cpp");
  CHECK(hook.find("REX_HOOK_RAW(sub_82177590)") != std::string::npos);
  CHECK(hook.find("REX_HOOK_RAW(sub_823F5118)") != std::string::npos);
  CHECK(hook.find("REX_HOOK_RAW(sub_823EBB28)") != std::string::npos);
  CHECK(hook.find("REX_HOOK_RAW(sub_82181AA8)") != std::string::npos);
  CHECK(hook.find("REX_HOOK_RAW(sub_823ED578)") != std::string::npos);
  CHECK(hook.find("RenderPathMatches(base) && ssf2t::FightFillActive") != std::string::npos);
  CHECK(hook.find("ctx.f3") == std::string::npos && hook.find("ctx.f4") == std::string::npos);
  CHECK(kFightMaskDrawer == 0x82177590);
}

void TestRenderPathFingerprints() {
  static uint8_t memory[0x2000000] = {};
  uint8_t* base = memory - 0x82000000u;
  auto write = [&](uint32_t address, std::span<const uint32_t> words) {
    uint32_t offset = 0;
    for (const uint32_t word : words) {
      StoreBE32(base, address + offset, word);
      offset += 4;
    }
  };
  write(kFightMaskDrawer, kFightMaskDrawerWords);
  for (const FightRenderFunction& function : kFightRenderFunctions) {
    write(function.address, function.words);
  }
  CHECK(FightRenderPathMatches(base));
  for (const FightRenderFunction& function : kFightRenderFunctions) {
    StoreBE32(base, function.address + 4, 0);
    CHECK(!FightRenderPathMatches(base));
    StoreBE32(base, function.address + 4, function.words[1]);
  }
  CHECK(FightRenderPathMatches(base));
  StoreBE32(base, kFightMaskDrawer, 0);
  CHECK(!FightRenderPathMatches(base));
}

// The decrypted image is not in the repository; the check runs only where it was extracted.
void TestHookTargetsInRealImage() {
  const std::filesystem::path image = std::filesystem::path(SSF2T_SOURCE_DIR) / "game" / "analysis" / "default.image.bin";
  std::ifstream in(image, std::ios::binary);
  if (!in) {
    std::printf("  skipped: no decrypted image\n");
    return;
  }
  constexpr uint32_t kImageBase = 0x82000000;
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  CHECK(bytes.size() > 0x1400000);
  CHECK(FightRenderPathMatches(bytes.data() - kImageBase));
}

}  // namespace

int main() {
  TestDefaultIsOriginal();
  TestSerialization();
  TestOriginalNeverActs();
  TestFillActiveOnlyInWidescreenFights();
  TestShiftFollowsCamera();
  TestNonFiniteCameraFallsBackToStock();
  TestOrthoShiftKeepsWidth();
  TestFillReadsGuestGlobals();
  TestSettingsAreIndependent();
  TestWiring();
  TestRenderPathFingerprints();
  TestHookTargetsInRealImage();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
