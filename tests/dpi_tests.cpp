// Per-monitor DPI helpers: scale derivation, logical/physical conversions, window fitting and fallbacks.
// Run: ssf2t_dpi_tests

#include <cmath>
#include <cstdio>

#include "dpi.h"

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

bool Near(float a, float b) {
  return std::fabs(a - b) < 0.0001f;
}

void TestScales() {
  std::printf("display scales\n");
  CHECK(Near(DpiScaleFromDpi(96), 1.0f));
  CHECK(Near(DpiScaleFromDpi(144), 1.5f));
  CHECK(Near(DpiScaleFromDpi(216), 2.25f));
}

void TestConversions() {
  std::printf("logical and physical conversions\n");
  CHECK(ScaleLogicalToPhysical(1280, 1.0f) == 1280);
  CHECK(ScaleLogicalToPhysical(1280, 1.5f) == 1920);
  CHECK(ScaleLogicalToPhysical(1280, 2.25f) == 2880);
  CHECK(ScalePhysicalToLogical(1280, 1.0f) == 1280);
  CHECK(ScalePhysicalToLogical(1920, 1.5f) == 1280);
  CHECK(ScalePhysicalToLogical(2880, 2.25f) == 1280);
}

void TestRepeatedConversions() {
  std::printf("repeated monitor moves do not drift\n");
  int size = 1280;
  for (int i = 0; i < 100; ++i) {
    size = ScalePhysicalToLogical(ScaleLogicalToPhysical(size, 2.25f), 2.25f);
    size = ScalePhysicalToLogical(ScaleLogicalToPhysical(size, 1.0f), 1.0f);
  }
  CHECK(size == 1280);

  size = 720;
  for (int i = 0; i < 100; ++i) {
    size = ScalePhysicalToLogical(ScaleLogicalToPhysical(size, 2.25f), 2.25f);
    size = ScalePhysicalToLogical(ScaleLogicalToPhysical(size, 1.5f), 1.5f);
  }
  CHECK(size == 720);
}

void TestWindowFitsTargetMonitor() {
  std::printf("launcher window uses the target monitor DPI\n");
  CHECK((FitWindowToWorkArea(1280, 720, 3840, 2088, 2.25f, 0.9) == DpiWindowSize{1280, 720}));
  CHECK((FitWindowToWorkArea(1280, 720, 1920, 1040, 1.0f, 0.9) == DpiWindowSize{1280, 720}));
  CHECK((FitWindowToWorkArea(1280, 720, 1920, 1040, 1.5f, 0.9) == DpiWindowSize{1152, 624}));
  CHECK((FitWindowToWorkArea(1280, 720, 1920, 1040, 2.25f, 0.9) == DpiWindowSize{768, 416}));
}

void TestBaseScalesDoNotAccumulate() {
  std::printf("font raster scale derives from the base\n");
  const float font_scale = DpiScaleFromDpi(216);
  const int base_size = 18;
  const int raster_size = ScaleLogicalToPhysical(base_size, font_scale);
  CHECK(raster_size == 41);
  for (int i = 0; i < 100; ++i) {
    CHECK(ScaleLogicalToPhysical(base_size, font_scale) == raster_size);
  }
}

void TestInvalidScales() {
  std::printf("invalid DPI and scale fall back to 1\n");
  CHECK(Near(DpiScaleFromDpi(0), 1.0f));
  CHECK(Near(ValidDpiScale(0.0f), 1.0f));
  CHECK(Near(ValidDpiScale(-1.0f), 1.0f));
  CHECK(ScaleLogicalToPhysical(1280, 0.0f) == 1280);
  CHECK(ScalePhysicalToLogical(1280, -1.0f) == 1280);
  CHECK((FitWindowToWorkArea(1280, 720, 1920, 1040, 0.0f, 0.9) == DpiWindowSize{1280, 720}));
}

}  // namespace

int main() {
  TestScales();
  TestConversions();
  TestRepeatedConversions();
  TestWindowFitsTargetMonitor();
  TestBaseScalesDoNotAccumulate();
  TestInvalidScales();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
