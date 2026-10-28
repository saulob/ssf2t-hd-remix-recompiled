// Fight Area tests: the setting values, the mask decision and the hook target. Pure logic, no window or game files.
// Run: ssf2t_fight_area_tests

#include <algorithm>
#include <bit>
#include <cmath>
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
#include <utility>
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
  static uint8_t memory[0x440000] = {};
  uint8_t* base = memory - 0x83A00000u;
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

constexpr PreFightScene kSelectScene{true, false};
constexpr PreFightScene kVersusScene{false, true};
constexpr PreFightScene kNoScene{false, false};

void TestOriginalNeverActsBeforeFights() {
  for (const PreFightScene scene : {kSelectScene, kVersusScene}) {
    for (int fight = 0; fight <= 1; ++fight) {
      CHECK(!PreFightFillActive(false, 1, 1, uint8_t(fight), kAspect, scene));
      CHECK(!CharacterSelectFillActive(false, 1, 1, uint8_t(fight), kAspect, scene));
    }
  }
}

void TestPreFightScenes() {
  CHECK(IsVersus(MatchFlow{2, 4, 4}));
  for (const MatchFlow flow : {MatchFlow{2, 4, 2}, MatchFlow{2, 4, 6}, MatchFlow{2, 4, 10}, MatchFlow{2, 0, 0},
                               MatchFlow{2, 8, 4}, MatchFlow{0, 4, 4}}) {
    CHECK(!IsVersus(flow));
  }
  CHECK(IsCharacterSelectBackground(1, 2));
  CHECK(IsCharacterSelectBackground(1, 9));
  for (const uint32_t background : {0u, 1u, 3u, 4u, 5u, 6u, 7u, 8u, 10u, 0xFFFFFFFFu}) {
    CHECK(!IsCharacterSelectBackground(1, background));
  }
  CHECK(!IsCharacterSelectBackground(0, 2));
  CHECK(!IsCharacterSelectBackground(0, 9));
}

void TestPreFightFillActiveOnlyInSelectAndVersus() {
  CHECK(PreFightFillActive(true, 1, 1, 0, kAspect, kSelectScene));
  CHECK(PreFightFillActive(true, 1, 1, 0, kAspect, kVersusScene));
  CHECK(!PreFightFillActive(true, 1, 1, 0, kAspect, kNoScene));
  CHECK(!PreFightFillActive(true, 0, 1, 0, kAspect, kSelectScene));
  CHECK(!PreFightFillActive(true, 1, 0, 0, kAspect, kSelectScene));
  CHECK(!PreFightFillActive(true, 1, 2, 0, kAspect, kVersusScene));
  CHECK(!PreFightFillActive(true, 1, 1, 0, 1.0f, kVersusScene));
  CHECK(CharacterSelectFillActive(true, 1, 1, 0, kAspect, kSelectScene));
  CHECK(!CharacterSelectFillActive(true, 1, 1, 0, kAspect, kVersusScene));
  CHECK(!CharacterSelectFillActive(true, 1, 1, 0, kAspect, PreFightScene{true, true}));
  CHECK(!CharacterSelectFillActive(true, 1, 1, 0, kAspect, kNoScene));
}

void TestFightAndPreFightNeverOverlap() {
  for (int fight = 0; fight <= 1; ++fight) {
    for (const PreFightScene scene : {kSelectScene, kVersusScene, kNoScene}) {
      const bool in_fight = FightFillActive(true, 1, 1, uint8_t(fight), kAspect, 0.5f);
      CHECK(!(in_fight && PreFightFillActive(true, 1, 1, uint8_t(fight), kAspect, scene)));
      CHECK(!(in_fight && CharacterSelectFillActive(true, 1, 1, uint8_t(fight), kAspect, scene)));
    }
  }
  CHECK(FightFillActive(true, 1, 1, 1, kAspect, 0.5f));
  CHECK(!PreFightFillActive(true, 1, 1, 1, kAspect, kSelectScene));
}

void TestPreFightReadsGuestGlobals() {
  static uint8_t memory[0x440000] = {};
  uint8_t* base = memory - 0x83A00000u;
  constexpr uint32_t kRam = 0x83E20000;
  StoreBE32(base, kWidescreenMode, 1);
  base[kWidescreenDisplayFlag] = 1;
  base[kFightActiveFlag] = 0;
  StoreBE32(base, kWidescreenAspect, std::bit_cast<uint32_t>(kAspect));
  StoreBE32(base, kGameRam, kRam);
  base[kHdBackgroundEnabled] = 1;

  // The state recorded at runtime while Character Select was on screen.
  StoreBE16(base, kRam + kMainModeOffset, 2);
  StoreBE16(base, kRam + kGameModeOffset, 0);
  StoreBE16(base, kRam + kMatchStepOffset, 0);
  StoreBE32(base, kHdBackground, kCharacterSelectBackground);
  CHECK(PreFightFillActive(true, base));
  CHECK(CharacterSelectFillActive(true, base));
  CHECK(!PreFightFillActive(false, base));
  CHECK(!CharacterSelectFillActive(false, base));
  StoreBE32(base, kHdBackground, kCharacterSelectBackgroundAlt);
  CHECK(CharacterSelectFillActive(true, base));

  StoreBE32(base, kHdBackground, 0);
  CHECK(!PreFightFillActive(true, base));
  StoreBE32(base, kHdBackground, 3);
  CHECK(!PreFightFillActive(true, base));
  StoreBE32(base, kHdBackground, kCharacterSelectBackground);
  base[kHdBackgroundEnabled] = 0;
  CHECK(!PreFightFillActive(true, base));
  base[kHdBackgroundEnabled] = 1;

  StoreBE32(base, kHdBackground, 3);
  StoreBE16(base, kRam + kGameModeOffset, 4);
  StoreBE16(base, kRam + kMatchStepOffset, 4);
  CHECK(PreFightFillActive(true, base));
  CHECK(!CharacterSelectFillActive(true, base));
  StoreBE32(base, kHdBackground, kCharacterSelectBackgroundAlt);
  CHECK(PreFightFillActive(true, base));
  CHECK(!CharacterSelectFillActive(true, base));
  StoreBE16(base, kRam + kMatchStepOffset, 10);
  StoreBE32(base, kHdBackground, 10);
  CHECK(!PreFightFillActive(true, base));

  StoreBE32(base, kHdBackground, kCharacterSelectBackground);
  base[kFightActiveFlag] = 1;
  CHECK(!PreFightFillActive(true, base));
  CHECK(!CharacterSelectFillActive(true, base));
  base[kFightActiveFlag] = 0;
  StoreBE32(base, kGameRam, kRam + 0x10);
  StoreBE16(base, kRam + kMatchStepOffset, 4);
  CHECK(CharacterSelectFillActive(true, base));
  StoreBE32(base, kHdBackground, 3);
  CHECK(!PreFightFillActive(true, base));
}

void TestGameRamInPhysicalHeap() {
  constexpr uint32_t kPhysicalRam = 0xFC2A0000;
  CHECK(GuestHostOffset(kPhysicalRam) == kPhysicalHeapHostOffset);
  CHECK(GuestHostOffset(0xDFFFFFFF) == 0);
  CHECK(GuestHostOffset(kGameRam) == 0);
  CHECK(GuestHostOffset(kMatchStepTable) == 0);
#ifdef _WIN32
  CHECK(kPhysicalHeapHostOffset == 0x1000);
#endif

  static uint8_t host[0x2000] = {};
  StoreBE16(host + kPhysicalHeapHostOffset, kMainModeOffset, 2);
  StoreBE16(host + kPhysicalHeapHostOffset, kGameModeOffset, 4);
  StoreBE16(host + kPhysicalHeapHostOffset, kMatchStepOffset, 4);
  const MatchFlow flow = ReadMatchFlow(host + GuestHostOffset(kPhysicalRam));
  CHECK(IsVersus(flow));
  if (kPhysicalHeapHostOffset != 0) {
    CHECK(!IsVersus(ReadMatchFlow(host)));
  }
}

constexpr float kInvalidAspects[] = {1.0f, 0.0f, -0.5f, 1.5f, std::numeric_limits<float>::quiet_NaN(),
                                     std::numeric_limits<float>::infinity()};
bool Near(double value, double expected) {
  return value > expected - 0.01 && value < expected + 0.01;
}

bool SelectFill(bool fill, int fight, PreFightScene scene) {
  return CharacterSelectFillActive(fill, 1, 1, uint8_t(fight), kAspect, scene);
}

bool VersusFill(bool fill, int fight, PreFightScene scene) {
  return VersusFillActive(fill, 1, 1, uint8_t(fight), kAspect, scene);
}

constexpr uint32_t kColor = 0xFFFFFFFF;

// The quads the game builds: Character Select over the arcade area, VS padded by 96 and scrolling vertically.
constexpr QuadVertices kSelectQuad{{
    {16.0f, 16.0f, 0.0f, kColor, 0.0f, 1.0f},
    {624.0f, 16.0f, 0.0f, kColor, 1.0f, 1.0f},
    {16.0f, 464.0f, 0.0f, kColor, 0.0f, 0.0f},
    {624.0f, 464.0f, 0.0f, kColor, 1.0f, 0.0f},
}};
constexpr QuadVertices kVersusQuad{{
    {-80.0f, 16.0f, 0.0f, kColor, 0.0f, 1.25f},
    {720.0f, 16.0f, 0.0f, kColor, 1.0f, 1.25f},
    {-80.0f, 464.0f, 0.0f, kColor, 0.0f, 0.25f},
    {720.0f, 464.0f, 0.0f, kColor, 1.0f, 0.25f},
}};

bool Extend(const QuadVertices& center, bool scene_active, BackgroundExtensions& out, float aspect = kAspect) {
  out = {};
  return scene_active && BackgroundExtensionPieces(center, aspect, out);
}

struct Bounds {
  float x0, x1, y0, y1, u0, u1, v0, v1;
};

Bounds BoundsOf(const QuadVertices& quad) {
  Bounds b{quad[0].x, quad[0].x, quad[0].y, quad[0].y, quad[0].u, quad[0].u, quad[0].v, quad[0].v};
  for (const QuadVertex& vertex : quad) {
    b.x0 = std::min(b.x0, vertex.x), b.x1 = std::max(b.x1, vertex.x);
    b.y0 = std::min(b.y0, vertex.y), b.y1 = std::max(b.y1, vertex.y);
    b.u0 = std::min(b.u0, vertex.u), b.u1 = std::max(b.u1, vertex.u);
    b.v0 = std::min(b.v0, vertex.v), b.v1 = std::max(b.v1, vertex.v);
  }
  return b;
}

const QuadVertices* Piece(const BackgroundExtensions& out, std::string_view name) {
  for (size_t i = 0; i < out.count; ++i) {
    if (name == out.pieces[i].name) {
      return &out.pieces[i].quad;
    }
  }
  return nullptr;
}

bool SameQuad(const QuadVertices& a, const QuadVertices& b) {
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::bit_cast<uint32_t>(a[i].x) != std::bit_cast<uint32_t>(b[i].x) ||
        std::bit_cast<uint32_t>(a[i].y) != std::bit_cast<uint32_t>(b[i].y) || a[i].z != b[i].z ||
        a[i].color != b[i].color || std::bit_cast<uint32_t>(a[i].u) != std::bit_cast<uint32_t>(b[i].u) ||
        std::bit_cast<uint32_t>(a[i].v) != std::bit_cast<uint32_t>(b[i].v)) {
      return false;
    }
  }
  return true;
}

constexpr std::string_view kEightPieces[] = {
    "left_extension",     "right_extension",     "top_extension",         "bottom_extension",
    "top_left_extension", "top_right_extension", "bottom_left_extension", "bottom_right_extension",
};

double Overlap(double a0, double a1, double b0, double b1) {
  return std::max(0.0, std::min(a1, b1) - std::max(a0, b0));
}

double Area(const Bounds& b) {
  return (double(b.x1) - double(b.x0)) * (double(b.y1) - double(b.y0));
}

// Pieces inside the screen, disjoint from each other and from the background, whose areas add up to the screen:
// the background and its eight pieces tile the whole visible rectangle.
void CheckCoversScreen(const QuadVertices& center, const BackgroundExtensions& out) {
  const Span vx = NonFightVisibleX(kAspect);
  const Span vy = NonFightVisibleY();
  std::vector<Bounds> tiles{BoundsOf(center)};
  for (size_t i = 0; i < out.count; ++i) {
    tiles.push_back(BoundsOf(out.pieces[i].quad));
  }
  double area = 0.0;
  for (size_t i = 0; i < tiles.size(); ++i) {
    const Bounds& t = tiles[i];
    CHECK(t.x0 >= float(vx.start) && t.x1 <= float(vx.end) && t.y0 >= float(vy.start) && t.y1 <= float(vy.end));
    CHECK(t.x0 < t.x1 && t.y0 < t.y1);
    area += Area(t);
    for (size_t j = i + 1; j < tiles.size(); ++j) {
      const Bounds& o = tiles[j];
      CHECK(Overlap(t.x0, t.x1, o.x0, o.x1) * Overlap(t.y0, t.y1, o.y0, o.y1) == 0.0);
    }
  }
  const double screen = (double(float(vx.end)) - double(float(vx.start))) * (vy.end - vy.start);
  CHECK(std::abs(area - screen) < 1e-6);
}

// Each piece vertex keeps the z and colour of a background vertex, and on a seam the u or v of that edge.
void CheckSeams(const QuadVertices& center, const QuadVertices& piece) {
  const Bounds c = BoundsOf(center);
  for (const QuadVertex& vertex : piece) {
    bool keeps_z_color = false;
    for (const QuadVertex& source : center) {
      keeps_z_color |= source.z == vertex.z && source.color == vertex.color;
      if (source.x == vertex.x) {
        CHECK(source.u == vertex.u);
      }
      if (source.y == vertex.y) {
        CHECK(source.v == vertex.v);
      }
    }
    CHECK(keeps_z_color);
  }
  BackgroundRect rect{};
  CHECK(ReadBackgroundRect(piece, rect));
  const Bounds p = BoundsOf(piece);
  CHECK(Overlap(p.x0, p.x1, c.x0, c.x1) * Overlap(p.y0, p.y1, c.y0, c.y1) == 0.0);
}

void CheckPiece(const BackgroundExtensions& out, std::string_view name, const Bounds& e) {
  const QuadVertices* piece = Piece(out, name);
  CHECK(piece != nullptr);
  if (piece == nullptr) {
    return;
  }
  const Bounds b = BoundsOf(*piece);
  CHECK(Near(b.x0, e.x0) && Near(b.x1, e.x1) && b.y0 == e.y0 && b.y1 == e.y1);
  const auto same = [](float a, float b) { return std::abs(a - b) < 1e-6f; };
  CHECK(same(b.u0, e.u0) && same(b.u1, e.u1) && same(b.v0, e.v0) && same(b.v1, e.v1));
}

// The u (or v) span a strip samples: one edge of the texture plus kEdgeStripFraction of the background's span.
float Strip(float edge, float far) {
  return edge + kEdgeStripFraction * (far - edge);
}

void CheckLayout(const QuadVertices& center, const BackgroundExtensions& out) {
  CHECK(out.count == 8);
  for (const std::string_view name : kEightPieces) {
    CHECK(Piece(out, name) != nullptr);
  }
  for (size_t i = 0; i < out.count; ++i) {
    CheckSeams(center, out.pieces[i].quad);
  }
  CheckCoversScreen(center, out);

  const Bounds c = BoundsOf(center);
  const float vx0 = float(NonFightVisibleX(kAspect).start);
  const float vx1 = float(NonFightVisibleX(kAspect).end);
  float left_u = 0.0f, right_u = 0.0f, low_v = 0.0f, high_v = 0.0f;
  for (const QuadVertex& vertex : center) {
    (vertex.x == c.x0 ? left_u : right_u) = vertex.u;
    (vertex.y == c.y0 ? low_v : high_v) = vertex.v;
  }
  const auto span = [](float a, float b) { return std::pair{std::min(a, b), std::max(a, b)}; };
  const auto [lu0, lu1] = span(left_u, Strip(left_u, right_u));
  const auto [ru0, ru1] = span(right_u, Strip(right_u, left_u));
  const auto [tv0, tv1] = span(low_v, Strip(low_v, high_v));
  const auto [bv0, bv1] = span(high_v, Strip(high_v, low_v));

  CheckPiece(out, "left_extension", {vx0, c.x0, c.y0, c.y1, lu0, lu1, c.v0, c.v1});
  CheckPiece(out, "right_extension", {c.x1, vx1, c.y0, c.y1, ru0, ru1, c.v0, c.v1});
  CheckPiece(out, "top_extension", {c.x0, c.x1, 0.0f, c.y0, c.u0, c.u1, tv0, tv1});
  CheckPiece(out, "bottom_extension", {c.x0, c.x1, c.y1, 480.0f, c.u0, c.u1, bv0, bv1});
  CheckPiece(out, "top_left_extension", {vx0, c.x0, 0.0f, c.y0, lu0, lu1, tv0, tv1});
  CheckPiece(out, "top_right_extension", {c.x1, vx1, 0.0f, c.y0, ru0, ru1, tv0, tv1});
  CheckPiece(out, "bottom_left_extension", {vx0, c.x0, c.y1, 480.0f, lu0, lu1, bv0, bv1});
  CheckPiece(out, "bottom_right_extension", {c.x1, vx1, c.y1, 480.0f, ru0, ru1, bv0, bv1});
}

void TestNonFightWindowIsTheGamesOwn() {
  const Span visible = NonFightVisibleX(kAspect);
  CHECK(Near(visible.start, -91.43) && Near(visible.end, 731.43));
  CHECK(Near((visible.end - visible.start) * kAspect, 640.0));
  CHECK(Near((visible.start + visible.end) / 2.0, 320.0));
  CHECK(NonFightVisibleY().start == 0.0 && NonFightVisibleY().end == 480.0);
}

void TestVersusBackgroundIsSurrounded() {
  BackgroundExtensions out{};
  CHECK(!Extend(kVersusQuad, VersusFill(false, 0, kVersusScene), out));
  CHECK(out.count == 0);
  const QuadVertices center = kVersusQuad;
  CHECK(Extend(center, VersusFill(true, 0, kVersusScene), out));
  CHECK(SameQuad(center, kVersusQuad));
  CheckLayout(kVersusQuad, out);
  CheckPiece(out, "left_extension", {-91.43f, -80.0f, 16.0f, 464.0f, 0.0f, 1.0f / 32.0f, 0.25f, 1.25f});
  CheckPiece(out, "top_extension", {-80.0f, 720.0f, 0.0f, 16.0f, 0.0f, 1.0f, 1.21875f, 1.25f});
  CheckPiece(out, "bottom_right_extension",
             {720.0f, 731.43f, 464.0f, 480.0f, 1.0f - 1.0f / 32.0f, 1.0f, 0.25f, 0.28125f});
}

// The VS pieces exactly as the fixed 1/32 strips have always produced them.
void TestVersusPiecesAreBitIdentical() {
  const float l = float(NonFightVisibleX(kAspect).start);
  const float r = float(NonFightVisibleX(kAspect).end);
  const auto quad = [](float x0, float x1, float y0, float y1, float u0, float u1, float v0, float v1) {
    return QuadVertices{{{x0, y0, 0.0f, kColor, u0, v0},
                         {x1, y0, 0.0f, kColor, u1, v0},
                         {x0, y1, 0.0f, kColor, u0, v1},
                         {x1, y1, 0.0f, kColor, u1, v1}}};
  };
  const BackgroundPiece golden[] = {
      {"left_extension", quad(l, -80.0f, 16.0f, 464.0f, 0.03125f, 0.0f, 1.25f, 0.25f)},
      {"right_extension", quad(720.0f, r, 16.0f, 464.0f, 1.0f, 0.96875f, 1.25f, 0.25f)},
      {"top_extension", quad(-80.0f, 720.0f, 0.0f, 16.0f, 0.0f, 1.0f, 1.21875f, 1.25f)},
      {"top_left_extension", quad(l, -80.0f, 0.0f, 16.0f, 0.03125f, 0.0f, 1.21875f, 1.25f)},
      {"top_right_extension", quad(720.0f, r, 0.0f, 16.0f, 1.0f, 0.96875f, 1.21875f, 1.25f)},
      {"bottom_extension", quad(-80.0f, 720.0f, 464.0f, 480.0f, 0.0f, 1.0f, 0.25f, 0.28125f)},
      {"bottom_left_extension", quad(l, -80.0f, 464.0f, 480.0f, 0.03125f, 0.0f, 0.25f, 0.28125f)},
      {"bottom_right_extension", quad(720.0f, r, 464.0f, 480.0f, 1.0f, 0.96875f, 0.25f, 0.28125f)},
  };
  const QuadVertices center = kVersusQuad;
  BackgroundExtensions out{};
  CHECK(Extend(center, VersusFill(true, 0, kVersusScene), out));
  CHECK(SameQuad(center, kVersusQuad));
  CHECK(out.count == 8);
  for (size_t i = 0; i < out.count; ++i) {
    CHECK(std::string_view(out.pieces[i].name) == golden[i].name && SameQuad(out.pieces[i].quad, golden[i].quad));
  }
}

// The vertex order of the game's quad must not matter, and each piece keeps that order.
void TestVertexOrderIsKept() {
  const QuadVertices shuffled{{kVersusQuad[3], kVersusQuad[0], kVersusQuad[2], kVersusQuad[1]}};
  BackgroundExtensions out{};
  CHECK(Extend(shuffled, true, out));
  CheckLayout(shuffled, out);
  for (size_t i = 0; i < out.count; ++i) {
    const QuadVertices& piece = out.pieces[i].quad;
    for (size_t v = 0; v < piece.size(); ++v) {
      CHECK((piece[v].x < piece[(v + 1) % 4].x) == (shuffled[v].x < shuffled[(v + 1) % 4].x));
      CHECK((piece[v].y < piece[(v + 1) % 4].y) == (shuffled[v].y < shuffled[(v + 1) % 4].y));
    }
  }
}

// A background already reaching the top and bottom of the screen gets only its side pieces.
void TestFullHeightBackgroundGetsOnlySides() {
  QuadVertices tall = kVersusQuad;
  for (QuadVertex& vertex : tall) {
    vertex.y = vertex.y == 16.0f ? -2.0f : 494.0f;
  }
  BackgroundExtensions out{};
  CHECK(Extend(tall, true, out));
  CHECK(out.count == 2);
  CHECK(Piece(out, "left_extension") != nullptr && Piece(out, "right_extension") != nullptr);
  CHECK(Piece(out, "top_extension") == nullptr && Piece(out, "bottom_extension") == nullptr);
}

struct TextureAnchor {
  const char* name;
  double u;
  double v;
};

constexpr double kGlobeMidU = (kCharacterSelectGlobeHalo.u[0] + kCharacterSelectGlobeHalo.u[1]) / 2.0;
constexpr double kGlobeMidV = (kCharacterSelectGlobeHalo.v[0] + kCharacterSelectGlobeHalo.v[1]) / 2.0;
constexpr TextureAnchor kCharacterSelectGlobeAnchors[] = {
    {"globe_center", kGlobeMidU, kGlobeMidV},
    {"globe_left", kCharacterSelectGlobeHalo.u[0], kGlobeMidV},
    {"globe_right", kCharacterSelectGlobeHalo.u[1], kGlobeMidV},
    {"globe_top", kGlobeMidU, kCharacterSelectGlobeHalo.v[0]},
    {"globe_bottom", kGlobeMidU, kCharacterSelectGlobeHalo.v[1]},
};

struct CanvasPoint {
  double x;
  double y;
};

constexpr bool Between(double value, float a, float b) {
  return (a <= value && value <= b) || (b <= value && value <= a);
}

// Where a quad that is an axis-aligned rectangle shows texture point (u, v).
constexpr bool QuadPosition(const QuadVertices& quad, double u, double v, CanvasPoint& point) {
  BackgroundRect rect{};
  if (!ReadBackgroundRect(quad, rect) || !Between(u, rect.u[0], rect.u[1]) || !Between(v, rect.v[0], rect.v[1])) {
    return false;
  }
  point = {OriginalPosition(rect.x, rect.u, u), OriginalPosition(rect.y, rect.v, v)};
  return true;
}

// Where the mesh shows texture coordinate `t` along one axis, interpolated across the segment holding it.
constexpr bool LinesPosition(const WarpLines& lines, double t, double& position) {
  for (int i = 0; i + 1 < kWarpLines; ++i) {
    const float segment_position[2] = {lines.position[i], lines.position[i + 1]};
    const float segment_texture[2] = {lines.texture[i], lines.texture[i + 1]};
    if (Between(t, segment_texture[0], segment_texture[1])) {
      position = OriginalPosition(segment_position, segment_texture, t);
      return true;
    }
  }
  return false;
}

constexpr bool WarpMeshPosition(const WarpMesh& mesh, double u, double v, CanvasPoint& point) {
  return LinesPosition(mesh.lines[0], u, point.x) && LinesPosition(mesh.lines[1], v, point.y);
}

constexpr TextureSize kSelectTexture{1920, 1024};
// Far below a pixel: one canvas unit is about 2.25 pixels at 1080p.
constexpr double kCanvasEpsilon = 1e-4;
constexpr float kFillAspects[] = {kAspect, 0.5f, 0.75f, 0.999f};

bool Close(double value, double expected, double tolerance = 1e-6) {
  return std::abs(value - expected) <= tolerance;
}

bool Warp(const QuadVertices& center, bool active, WarpMesh& out, TextureSize texture = kSelectTexture,
          float aspect = kAspect) {
  out = {};
  return active && CharacterSelectWarp(center, aspect, texture, out);
}

// The mesh cell between lines `step` and `step + 1` of a strip, as a quad in the strip's vertex order.
QuadVertices Cell(const WarpMesh& mesh, int strip, int step) {
  QuadVertices cell{};
  for (int i = 0; i < 4; ++i) {
    cell[i] = mesh.strips[strip][2 * step + i];
  }
  return cell;
}

// Which side of its own rectangle each vertex sits on, along x and along y.
std::array<std::pair<bool, bool>, 4> Corners(const QuadVertices& quad) {
  const Bounds b = BoundsOf(quad);
  std::array<std::pair<bool, bool>, 4> corners{};
  for (size_t i = 0; i < quad.size(); ++i) {
    corners[i] = {quad[i].x == b.x1, quad[i].y == b.y1};
  }
  return corners;
}

double Winding(const QuadVertex& a, const QuadVertex& b, const QuadVertex& c) {
  return (double(b.x) - a.x) * (double(c.y) - a.y) - (double(b.y) - a.y) * (double(c.x) - a.x);
}

double Slope(const WarpAxis& axis, double t, double step) {
  return (WarpPosition(axis, t + step) - WarpPosition(axis, t)) / step;
}

double SegmentSlope(const WarpLines& lines, int i) {
  return (double(lines.position[i + 1]) - lines.position[i]) / (double(lines.texture[i + 1]) - lines.texture[i]);
}

void TestCharacterSelectCenterIsOriginal() {
  CHECK(Contains(kCharacterSelectProtected, kCharacterSelectGlobeHalo));
  const QuadVertices center = kSelectQuad;
  WarpMesh mesh{};
  CHECK(Warp(center, SelectFill(true, 0, kSelectScene), mesh));
  CHECK(SameQuad(center, kSelectQuad));

  // The protected part is one cell, exactly where the original quad shows it.
  const QuadVertices protected_part = Cell(mesh, kWarpCenter, kWarpCenter);
  const QuadVertices expected{{
      {137.6f, 65.0f, 0.0f, kColor, 0.2f, 0.890625f},
      {502.4f, 65.0f, 0.0f, kColor, 0.8f, 0.890625f},
      {137.6f, 422.0f, 0.0f, kColor, 0.2f, 0.09375f},
      {502.4f, 422.0f, 0.0f, kColor, 0.8f, 0.09375f},
  }};
  CHECK(SameQuad(protected_part, expected));
  BackgroundRect original{};
  CHECK(ReadBackgroundRect(kSelectQuad, original));
  for (const QuadVertex& vertex : protected_part) {
    CHECK(Close(vertex.x, OriginalPosition(original.x, original.u, vertex.u), kCanvasEpsilon));
    CHECK(Close(vertex.y, OriginalPosition(original.y, original.v, vertex.v), kCanvasEpsilon));
  }
  const double original_width =
      OriginalPosition(original.x, original.u, 0.8) - OriginalPosition(original.x, original.u, 0.2);
  const double original_height =
      OriginalPosition(original.y, original.v, 0.09375) - OriginalPosition(original.y, original.v, 0.890625);
  CHECK(Close(original_width, 364.8, 1e-9) && Close(original_height, 357.0, 1e-9));
  const Bounds c = BoundsOf(protected_part);
  CHECK(Close(double(c.x1) - c.x0, original_width, kCanvasEpsilon));
  CHECK(Close(double(c.y1) - c.y0, original_height, kCanvasEpsilon));
  for (double t = 0.2; t <= 0.8; t += 0.01) {
    CHECK(WarpPosition(mesh.axes[0], t) == OriginalPosition(original.x, original.u, t));
  }
  for (double t = 0.09375; t <= 0.890625; t += 0.01) {
    CHECK(WarpPosition(mesh.axes[1], t) == OriginalPosition(original.y, original.v, t));
  }

  // Every globe anchor, and a grid over the whole protected part, lands where Original puts it.
  for (const TextureAnchor& anchor : kCharacterSelectGlobeAnchors) {
    CanvasPoint from{}, to{}, in_center{};
    CHECK(QuadPosition(kSelectQuad, anchor.u, anchor.v, from));
    CHECK(WarpMeshPosition(mesh, anchor.u, anchor.v, to));
    CHECK(QuadPosition(protected_part, anchor.u, anchor.v, in_center));
    CHECK(Close(to.x, from.x, kCanvasEpsilon) && Close(to.y, from.y, kCanvasEpsilon));
    CHECK(to.x == in_center.x && to.y == in_center.y);
  }
  CanvasPoint globe{};
  CHECK(QuadPosition(kSelectQuad, kGlobeMidU, kGlobeMidV, globe));
  CHECK(Close(globe.x, 16.0 + 608.0 * kGlobeMidU, 1e-9) && Close(globe.y, 16.0 + 448.0 * (1.0 - kGlobeMidV), 1e-9));
  bool grid_matches = true;
  for (int i = 0; i <= 32; ++i) {
    for (int j = 0; j <= 32; ++j) {
      const double u = double(0.2f) + (double(0.8f) - double(0.2f)) * i / 32.0;
      const double v = 0.09375 + (0.890625 - 0.09375) * j / 32.0;
      CanvasPoint from{}, to{};
      grid_matches &= QuadPosition(kSelectQuad, u, v, from) && WarpMeshPosition(mesh, u, v, to) &&
                      Close(to.x, from.x, kCanvasEpsilon) && Close(to.y, from.y, kCanvasEpsilon);
    }
  }
  CHECK(grid_matches);
}

void TestWarpEase() {
  constexpr double kStretch = 4.0 / 3.0;
  const auto slope = [](double r, double h) { return (WarpEase(r + h) - WarpEase(r)) / h; };
  const auto bend = [](double r, double h) {
    return (WarpEase(r + h) - 2.0 * WarpEase(r) + WarpEase(r - h)) / (h * h);
  };
  CHECK(kWarpEaseLength == 0.5);
  CHECK(WarpEase(0.0) == 0.0 && Close(WarpEase(1.0), 1.0, 1e-15));
  CHECK(Close(WarpEase(0.5), 1.0 / 3.0, 1e-15) && Close(WarpEase(0.25), 16.0 / 3.0 / 64.0 * 0.75, 1e-15));
  CHECK(Close(slope(0.0, 1e-4), 0.0, 1e-6) && Close(slope(1.0, -1e-6), kStretch, 1e-6));
  CHECK(Close((WarpEase(2e-3) - 2.0 * WarpEase(1e-3)) / 1e-6, 0.0, 0.05));
  CHECK(Close(slope(0.5, -1e-6), kStretch, 1e-5) && Close(slope(0.5, 1e-6), kStretch, 1e-9));
  CHECK(Close(bend(0.5 - 1e-3, 1e-3), 0.0, 0.1) && Close(bend(0.5 + 1e-3, 1e-3), 0.0, 1e-6));
  CHECK(Close(bend(0.25, 1e-4), 4.0, 1e-3));
  double previous_value = -1.0, previous_slope = -1.0;
  for (int i = 0; i < 1000; ++i) {
    const double r = i / 1000.0;
    const double s = slope(r, 1e-6);
    CHECK(WarpEase(r) > previous_value && WarpEase(r) >= 0.0 && WarpEase(r) <= 1.0);
    CHECK(s >= previous_slope - 1e-6 && s >= 0.0 && s <= kStretch + 1e-6);
    previous_value = WarpEase(r);
    previous_slope = s;
  }
}

void TestCharacterSelectWarpSeams() {
  WarpMesh mesh{};
  CHECK(Warp(kSelectQuad, true, mesh));
  const double original_slope[2] = {608.0, -448.0};
  const double cuts[2][2] = {{0.2, 0.8}, {0.890625, 0.09375}};
  const double ends[2][2] = {{0.05, 1.0}, {0.9921875, 0.0}};
  const double gaps[2][2] = {{-137.828571, 107.428571}, {-19.5, 16.0}};
  for (int axis = 0; axis < 2; ++axis) {
    const WarpAxis& warp = mesh.axes[axis];
    const WarpLines& lines = mesh.lines[axis];
    CHECK(Close(Slope(warp, 0.5, 1e-7), original_slope[axis], 1e-3));
    for (int side = 0; side < 2; ++side) {
      const double cut = warp.cut[side];
      CHECK(cut == cuts[axis][side]);
      CHECK(warp.end[side] == ends[axis][side]);
      const double outward = warp.end[side] - cut;
      const double step = outward * 1e-7;
      const double end_position = OriginalPosition(warp.position, warp.texture, warp.end[side]);
      CHECK(Close(warp.visible[side] - end_position, gaps[axis][side], 1e-5));

      // The cut: same position, and the same slope and no curvature on both sides of it.
      CHECK(WarpPosition(warp, cut) == OriginalPosition(warp.position, warp.texture, cut));
      CHECK(Close(Slope(warp, cut, step), original_slope[axis], 1e-3));
      CHECK(Close(Slope(warp, cut, -step), original_slope[axis], 1e-3));
      CHECK(Close(Slope(warp, cut - outward * 1e-3, step), original_slope[axis], 1e-3));
      CHECK(std::abs(Slope(warp, cut + outward * 1e-3, step) / original_slope[axis] - 1.0) < 1e-4);
      CHECK(std::abs(Slope(warp, cut + outward * 1e-2, step) / original_slope[axis] - 1.0) < 1e-2);

      // The edge: exactly the visible edge, reached with the constant outer stretch.
      const double edge_slope = original_slope[axis] + gaps[axis][side] * (4.0 / 3.0) / outward;
      CHECK(Close(WarpPosition(warp, warp.end[side]), warp.visible[side], 1e-9));
      CHECK(Close(Slope(warp, warp.end[side], -step) / edge_slope, 1.0, 1e-5));
      for (int i = 50; i <= 100; i += 5) {
        CHECK(Close(Slope(warp, cut + outward * i / 100.0, -step) / edge_slope, 1.0, 1e-5));
      }
      double previous = 0.0;
      for (int i = 0; i <= 100; ++i) {
        const double slope = Slope(warp, cut + outward * i / 100.0, i == 100 ? -step : step) / original_slope[axis];
        CHECK(slope >= 1.0 - 1e-6 && slope >= previous - 1e-6);
        previous = slope;
      }

      // The mesh follows the curve: the segment next to the cut keeps nearly the original slope, each next one
      // steps up gently, and the last one is the constant outer stretch.
      const int at = side == 0 ? kWarpCenter : kWarpCenter + 1;
      const int direction = side == 0 ? -1 : 1;
      double segment = 1.0;
      for (int k = 0; k < kWarpBandLines - 1; ++k) {
        const int i = at + direction * k;
        const double next = SegmentSlope(lines, direction < 0 ? i - 1 : i) / original_slope[axis];
        CHECK(next > segment && next < segment * (k == 0 ? 1.01 : 1.12));
        segment = next;
      }
      CHECK(Close(segment, edge_slope / original_slope[axis], 1e-4));
    }
    CHECK(Close(SegmentSlope(lines, kWarpCenter) / original_slope[axis], 1.0, 1e-6));
  }
}

void TestCharacterSelectWarpFillsScreen() {
  for (const float aspect : kFillAspects) {
    WarpMesh mesh{};
    CHECK(Warp(kSelectQuad, true, mesh, kSelectTexture, aspect));
    const Span visible[2] = {NonFightVisibleX(aspect), NonFightVisibleY()};
    const float texture_edges[2][2] = {{0.05f, 1.0f}, {0.9921875f, 0.0f}};
    const float cut_positions[2][2] = {{137.6f, 502.4f}, {65.0f, 422.0f}};
    double extent[2] = {};
    for (int axis = 0; axis < 2; ++axis) {
      const WarpLines& lines = mesh.lines[axis];
      CHECK(lines.position[0] == float(visible[axis].start));
      CHECK(lines.position[kWarpLines - 1] == float(visible[axis].end));
      CHECK(lines.texture[0] == texture_edges[axis][0] && lines.texture[kWarpLines - 1] == texture_edges[axis][1]);
      CHECK(lines.position[kWarpCenter] == cut_positions[axis][0]);
      CHECK(lines.position[kWarpCenter + 1] == cut_positions[axis][1]);
      const float direction = texture_edges[axis][1] - texture_edges[axis][0];
      for (int i = 0; i + 1 < kWarpLines; ++i) {
        CHECK(lines.position[i] < lines.position[i + 1]);
        CHECK((lines.texture[i + 1] - lines.texture[i]) * direction > 0.0f);
        CHECK(lines.texture[i] >= 0.0f && lines.texture[i] <= 1.0f);
      }
      // Only the outer segments are larger than what the original quad gives the same texture range.
      for (int i = 0; i + 1 < kWarpLines; ++i) {
        const double width = double(lines.position[i + 1]) - lines.position[i];
        const double original = std::abs(OriginalPosition(mesh.axes[axis].position, mesh.axes[axis].texture,
                                                          lines.texture[i + 1]) -
                                          OriginalPosition(mesh.axes[axis].position, mesh.axes[axis].texture,
                                                           lines.texture[i]));
        CHECK(i == kWarpCenter ? Close(width, original, kCanvasEpsilon) : width > original + 1e-3);
      }
      extent[axis] = double(lines.position[kWarpLines - 1]) - lines.position[0];
    }

    // Each strip is one column of cells: neighbours share their edge exactly, every cell keeps the original
    // winding, and the cells add up to the visible rectangle.
    const double winding = Winding(kSelectQuad[0], kSelectQuad[1], kSelectQuad[2]);
    double area = 0.0;
    for (int strip = 0; strip < kWarpStrips; ++strip) {
      for (int step = 0; step < kWarpLines; ++step) {
        for (int k = 0; k < 2; ++k) {
          const QuadVertex& vertex = mesh.strips[strip][2 * step + k];
          CHECK(vertex.x == mesh.lines[0].position[strip + k] && vertex.u == mesh.lines[0].texture[strip + k]);
          CHECK(vertex.y == mesh.lines[1].position[step] && vertex.v == mesh.lines[1].texture[step]);
          CHECK(vertex.z == 0.0f && vertex.color == kColor);
        }
        if (strip + 1 < kWarpStrips) {
          const QuadVertex& edge = mesh.strips[strip][2 * step + 1];
          const QuadVertex& next = mesh.strips[strip + 1][2 * step];
          CHECK(edge.x == next.x && edge.y == next.y && edge.u == next.u && edge.v == next.v);
        }
      }
      for (int step = 0; step + 1 < kWarpLines; ++step) {
        const QuadVertices cell = Cell(mesh, strip, step);
        CHECK(Corners(cell) == Corners(kSelectQuad));
        CHECK(Winding(cell[0], cell[1], cell[2]) * winding > 0.0);
        area += Area(BoundsOf(cell));
      }
    }
    CHECK(Close(area, extent[0] * extent[1], 1e-6 * extent[0] * extent[1]));
    CHECK(Close(extent[0], visible[0].end - visible[0].start, 1e-4) && extent[1] == 480.0);
  }

  // Another strip order of the same quad: the strips run the other way and keep that order.
  const QuadVertices other_order{{kSelectQuad[1], kSelectQuad[3], kSelectQuad[0], kSelectQuad[2]}};
  WarpMesh mesh{}, reference{};
  CHECK(Warp(other_order, true, mesh));
  CHECK(Warp(kSelectQuad, true, reference));
  const double winding = Winding(other_order[0], other_order[1], other_order[2]);
  for (int strip = 0; strip < kWarpStrips; ++strip) {
    for (int step = 0; step + 1 < kWarpLines; ++step) {
      const QuadVertices cell = Cell(mesh, strip, step);
      CHECK(Corners(cell) == Corners(other_order));
      CHECK(Winding(cell[0], cell[1], cell[2]) * winding > 0.0);
    }
  }
  CHECK(std::equal(std::begin(mesh.lines[0].position), std::end(mesh.lines[0].position),
                   std::begin(reference.lines[0].position)));
  CHECK(std::equal(std::begin(mesh.lines[1].position), std::end(mesh.lines[1].position),
                   std::begin(reference.lines[1].position)));
}

void TestCharacterSelectWarpFailsClosed() {
  WarpMesh out{};
  CHECK(!Warp(kSelectQuad, SelectFill(false, 0, kSelectScene), out));
  CHECK(!Warp(kSelectQuad, SelectFill(true, 1, kSelectScene), out));
  CHECK(!Warp(kSelectQuad, SelectFill(true, 0, kVersusScene), out));
  CHECK(!Warp(kSelectQuad, SelectFill(true, 0, kNoScene), out));
  CHECK(!Warp(kSelectQuad, SelectFill(true, 0, PreFightScene{true, true}), out));
  for (const float aspect : kInvalidAspects) {
    CHECK(!Warp(kSelectQuad, true, out, kSelectTexture, aspect));
  }
  for (const TextureSize texture : {TextureSize{0, 0}, TextureSize{1920, 1080}, TextureSize{1024, 1024},
                                    TextureSize{1280, 720}}) {
    CHECK(!Warp(kSelectQuad, true, out, texture));
  }
  QuadVertices not_rectangle = kSelectQuad;
  not_rectangle[2].x = 300.0f;
  CHECK(!Warp(not_rectangle, true, out));
  QuadVertices uneven_u = kSelectQuad;
  uneven_u[2].u = 0.5f;
  CHECK(!Warp(uneven_u, true, out));
  QuadVertices not_a_number = kSelectQuad;
  not_a_number[1].y = std::numeric_limits<float>::quiet_NaN();
  CHECK(!Warp(not_a_number, true, out));
  QuadVertices narrow_texture = kSelectQuad;
  for (QuadVertex& vertex : narrow_texture) {
    vertex.u = vertex.u == 0.0f ? 0.3f : 0.7f;
  }
  CHECK(!Warp(narrow_texture, true, out));
  QuadVertices past_edges = kSelectQuad;
  for (QuadVertex& vertex : past_edges) {
    vertex.x = vertex.x == 16.0f ? -200.0f : 900.0f;
  }
  CHECK(!Warp(past_edges, true, out));
  const float position[2] = {16.0f, 624.0f};
  const float texture[2] = {0.0f, 1.0f};
  const double cut[2] = {0.2, 0.8};
  const Span visible = NonFightVisibleX(kAspect);
  WarpAxis axis{};
  CHECK(MakeWarpAxis(position, texture, cut, kCharacterSelectSource.u, visible, axis));
  for (const auto& source : {std::array<double, 2>{-0.05, 1.0}, std::array<double, 2>{0.05, 1.1},
                             std::array<double, 2>{0.2, 1.0}, std::array<double, 2>{0.25, 1.0},
                             std::array<double, 2>{0.05, 0.8}}) {
    const double ends[2] = {source[0], source[1]};
    CHECK(!MakeWarpAxis(position, texture, cut, ends, visible, axis));
  }
  const QuadVertices crossed{{kSelectQuad[3], kSelectQuad[0], kSelectQuad[2], kSelectQuad[1]}};
  CHECK(!Warp(crossed, true, out));
  const QuadVertices around{{kSelectQuad[0], kSelectQuad[1], kSelectQuad[3], kSelectQuad[2]}};
  CHECK(!Warp(around, true, out));
}

// FNV-1a over every bit of the mesh: the warp output the runtime test accepted.
void TestCharacterSelectWarpIsUnchanged() {
  WarpMesh mesh{};
  CHECK(Warp(kSelectQuad, true, mesh));
  uint64_t hash = 0xCBF29CE484222325ull;
  for (const auto& strip : mesh.strips) {
    for (const QuadVertex& v : strip) {
      for (const uint32_t word : {std::bit_cast<uint32_t>(v.x), std::bit_cast<uint32_t>(v.y),
                                  std::bit_cast<uint32_t>(v.z), v.color, std::bit_cast<uint32_t>(v.u),
                                  std::bit_cast<uint32_t>(v.v)}) {
        for (int byte = 0; byte < 4; ++byte) {
          hash = (hash ^ ((word >> (8 * byte)) & 0xFF)) * 0x100000001B3ull;
        }
      }
    }
  }
  CHECK(hash == 0xB2A013FB33FDD99Dull);
}

void TestCharacterSelectSidePanels() {
  constexpr uint32_t kLeft = 0x82174AAC, kRight = 0x82174C7C;
  CHECK(kLeftSidePanelQuadReturn == kLeft && kRightSidePanelQuadReturn == kRight);
  CHECK(SidePanelAt(kLeft) == SidePanel::kLeft && SidePanelAt(kRight) == SidePanel::kRight);
  for (const uint32_t other : {kCharacterSelectQuadReturn, kScrollingQuadReturn, kLeft - 4, kRight + 4, 0u}) {
    CHECK(SidePanelAt(other) == SidePanel::kNone);
    CHECK(!SkipsSidePanel(true, true, other));
  }
  for (const uint32_t caller : {kLeft, kRight}) {
    CHECK(SkipsSidePanel(SelectFill(true, 0, kSelectScene), true, caller));
    CHECK(!SkipsSidePanel(SelectFill(false, 0, kSelectScene), true, caller));
    CHECK(!SkipsSidePanel(SelectFill(true, 1, kSelectScene), true, caller));
    CHECK(!SkipsSidePanel(SelectFill(true, 0, kVersusScene), true, caller));
    CHECK(!SkipsSidePanel(SelectFill(true, 0, kNoScene), true, caller));
    CHECK(!SkipsSidePanel(SelectFill(true, 0, PreFightScene{true, true}), true, caller));
    CHECK(!SkipsSidePanel(SelectFill(true, 0, kSelectScene), false, caller));
    CHECK(!SkipsSidePanel(CharacterSelectFillActive(true, 0, 1, 0, kAspect, kSelectScene), true, caller));
    CHECK(!SkipsSidePanel(CharacterSelectFillActive(true, 1, 1, 0, 1.0f, kSelectScene), true, caller));
    CHECK(!SkipsSidePanel(VersusFill(true, 0, kVersusScene), false, caller));
  }
  CHECK(VersusFill(true, 0, kVersusScene) && !SelectFill(true, 0, kVersusScene));
}

void WriteFetch(uint8_t* base, uint32_t address, const TextureFetch& fetch) {
  for (uint32_t i = 0; i < fetch.size(); ++i) {
    StoreBE32(base, address + 4 * i, fetch[i]);
  }
}

// The size comes from sampler 0 of the device only while that is the Character Select texture's fetch constant.
void TestCharacterSelectTextureSize() {
  constexpr TextureFetch kFetch{0x00000002u | (48u << 22) | (1u << 31), 0x1A000054u, 1919u | (1023u << 13),
                                0x00000D10u, 0u, 1u << 9};
  TextureSize size{};
  CHECK(FetchTextureSize(kFetch, size) && size.width == 1920 && size.height == 1024);
  TextureFetch vertex = kFetch;
  vertex[0] = (vertex[0] & ~3u) | 3u;
  CHECK(!FetchTextureSize(vertex, size));
  TextureFetch cube = kFetch;
  cube[5] = 3u << 9;
  CHECK(!FetchTextureSize(cube, size));

  static uint8_t memory[0x2000000] = {};
  uint8_t* base = memory - 0x82000000u;
  constexpr uint32_t kHolder = 0x83F00000;
  constexpr uint32_t kDevice = 0x83F80000;
  StoreBE32(base, kCharacterSelectTexture, kHolder);
  StoreBE32(base, kDevicePointer, kDevice);
  StoreBE32(base, kDevice + kDeviceTextures, kHolder + kGameTextureD3D);
  WriteFetch(base, kDevice + kDeviceFetchConstants, kFetch);
  WriteFetch(base, kHolder + kGameTextureD3D + kD3DTextureFetch, kFetch);
  size = {};
  CHECK(LoadCharacterSelectTextureSize(base, size) && size.width == 1920 && size.height == 1024);
  StoreBE32(base, kDevice + kDeviceTextures, kHolder + 4);
  CHECK(!LoadCharacterSelectTextureSize(base, size));
  StoreBE32(base, kDevice + kDeviceTextures, kHolder + kGameTextureD3D);
  StoreBE32(base, kHolder + kGameTextureD3D + kD3DTextureFetch + 8, 639u | (479u << 13));
  CHECK(!LoadCharacterSelectTextureSize(base, size));
  StoreBE32(base, kHolder + kGameTextureD3D + kD3DTextureFetch + 8, kFetch[2]);
  StoreBE32(base, kCharacterSelectTexture, 0);
  CHECK(!LoadCharacterSelectTextureSize(base, size));
  StoreBE32(base, kCharacterSelectTexture, kHolder);
  StoreBE32(base, kDevicePointer, 0);
  CHECK(!LoadCharacterSelectTextureSize(base, size));
}

void TestBackgroundExtensionsStayOffElsewhere() {
  CHECK(!SelectFill(true, 0, kVersusScene));
  CHECK(!SelectFill(true, 0, PreFightScene{true, true}));
  CHECK(!VersusFill(true, 0, kSelectScene));
  for (const PreFightScene scene : {kSelectScene, kVersusScene, kNoScene}) {
    CHECK(!SelectFill(true, 1, scene));
    CHECK(!VersusFill(true, 1, scene));
  }
  CHECK(!SelectFill(true, 0, kNoScene));
  CHECK(!VersusFill(true, 0, kNoScene));

  BackgroundExtensions out{};
  for (const float aspect : kInvalidAspects) {
    CHECK(!Extend(kSelectQuad, true, out, aspect));
  }
  QuadVertices not_rectangle = kSelectQuad;
  not_rectangle[2].x = 300.0f;
  CHECK(!Extend(not_rectangle, true, out));
  QuadVertices past_edges = kSelectQuad;
  for (QuadVertex& vertex : past_edges) {
    vertex.x = vertex.x == 16.0f ? -200.0f : 900.0f;
  }
  CHECK(!Extend(past_edges, true, out));
  QuadVertices uneven_u = kSelectQuad;
  uneven_u[2].u = 0.5f;
  CHECK(!Extend(uneven_u, true, out));
  QuadVertices same_u = kSelectQuad;
  for (QuadVertex& vertex : same_u) {
    vertex.u = 0.0f;
  }
  CHECK(!Extend(same_u, true, out));
  QuadVertices not_a_number = kSelectQuad;
  not_a_number[1].y = std::numeric_limits<float>::quiet_NaN();
  CHECK(!Extend(not_a_number, true, out));
  QuadVertices uneven_v = kSelectQuad;
  uneven_v[1].v = 0.5f;
  CHECK(!Extend(uneven_v, true, out));
  QuadVertices same_v = kSelectQuad;
  for (QuadVertex& vertex : same_v) {
    vertex.v = 1.0f;
  }
  CHECK(!Extend(same_v, true, out));
  QuadVertices repeated_corner = kSelectQuad;
  repeated_corner[3] = repeated_corner[0];
  CHECK(!Extend(repeated_corner, true, out));
  QuadVertices flat = kSelectQuad;
  for (QuadVertex& vertex : flat) {
    vertex.y = 16.0f;
  }
  CHECK(!Extend(flat, true, out));
  QuadVertices below_screen = kSelectQuad;
  for (QuadVertex& vertex : below_screen) {
    vertex.y += 600.0f;
  }
  CHECK(!Extend(below_screen, true, out));
  QuadVertices huge = kSelectQuad;
  huge[0].u = 1e9f;
  CHECK(!Extend(huge, true, out));
  CHECK(out.count == 0);
}

// The hook's scene gates require the pre-fight fingerprints, so a changed image never gets extensions.
void TestFingerprintFailureBlocksExtensions(uint8_t* base) {
  for (const FightRenderFunction& function : kPreFightFunctions) {
    uint32_t offset = 0;
    for (const uint32_t word : function.words) {
      StoreBE32(base, function.address + offset, word);
      offset += 4;
    }
  }
  uint32_t offset = 0;
  for (const uint32_t word : kFightMaskDrawerWords) {
    StoreBE32(base, kFightMaskDrawer + offset, word);
    offset += 4;
  }
  constexpr uint32_t kRam = 0x83E20000;
  StoreBE32(base, kWidescreenMode, 1);
  base[kWidescreenDisplayFlag] = 1;
  base[kFightActiveFlag] = 0;
  StoreBE32(base, kWidescreenAspect, std::bit_cast<uint32_t>(kAspect));
  StoreBE32(base, kGameRam, kRam);
  base[kHdBackgroundEnabled] = 1;
  StoreBE32(base, kHdBackground, kCharacterSelectBackground);
  const auto select_gate = [&] { return PreFightPathMatches(base) && CharacterSelectFillActive(true, base); };
  const auto versus_gate = [&] { return PreFightPathMatches(base) && VersusFillActive(true, base); };
  BackgroundExtensions out{};
  CHECK(Extend(kSelectQuad, select_gate(), out));
  StoreBE32(base, kQuadDraw + 4, 0);
  CHECK(!Extend(kSelectQuad, select_gate(), out));
  StoreBE32(base, kQuadDraw + 4, kQuadDrawWords[1]);

  StoreBE16(base, kRam + kMainModeOffset, 2);
  StoreBE16(base, kRam + kGameModeOffset, 4);
  StoreBE16(base, kRam + kMatchStepOffset, 4);
  StoreBE32(base, kHdBackground, 3);
  CHECK(Extend(kVersusQuad, versus_gate(), out));
  CHECK(!Extend(kVersusQuad, select_gate(), out));
  StoreBE32(base, kCharacterSelectQuadCall, 0);
  CHECK(!Extend(kVersusQuad, versus_gate(), out));
  StoreBE32(base, kCharacterSelectQuadCall, kCharacterSelectQuadCallWords[0]);
  base[kFightActiveFlag] = 1;
  CHECK(!Extend(kVersusQuad, versus_gate(), out));
  CHECK(!Extend(kSelectQuad, select_gate(), out));
  base[kFightActiveFlag] = 0;
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
  CHECK(hook.find("REX_HOOK_RAW(sub_821775B0)") != std::string::npos);
  CHECK(hook.find("REX_HOOK_RAW(sub_823F5888)") != std::string::npos);
  CHECK(hook.find("REX_HOOK_RAW(sub_823EC218)") != std::string::npos);
  CHECK(hook.find("REX_HOOK_RAW(sub_82181AB8)") != std::string::npos);
  CHECK(hook.find("REX_HOOK_RAW(sub_823EDC68)") != std::string::npos);
  CHECK(hook.find("RenderPathMatches(base) && ssf2t::FightFillActive") != std::string::npos);
  CHECK(hook.find("PreFightPathMatches(base) && ssf2t::PreFightFillActive") != std::string::npos);
  CHECK(hook.find("const bool pre_fight = PreFightFillActive(base);") != std::string::npos);
  CHECK(hook.find("if (FillActive(base) || pre_fight) {") != std::string::npos);
  CHECK(hook.find("  if (g_scene_depth > 0 && g_in_projection_setup && FillActive(base)) {") != std::string::npos);
  CHECK(hook.find("REX_HOOK_RAW(sub_823F7D48)") != std::string::npos);
  CHECK(hook.find("REX_HOOK_RAW(sub_82174F40)") == std::string::npos);
  CHECK(hook.find("REX_HOOK_RAW(sub_82174C90)") == std::string::npos);
  CHECK(hook.find("if (caller == ssf2t::kCharacterSelectQuadReturn) {") != std::string::npos);
  CHECK(hook.find("} else if (caller == ssf2t::kScrollingQuadReturn) {") != std::string::npos);
  CHECK(hook.find("    DrawCharacterSelectBackground(ctx, base);") != std::string::npos);
  CHECK(hook.find("    DrawVersusBackground(ctx, base);") != std::string::npos);
  CHECK(hook.find("extend = VersusFillActive(base) && ssf2t::BackgroundExtensionPieces(center, aspect, extensions);") !=
        std::string::npos);
  const size_t select_start = hook.find("void DrawCharacterSelectBackground(");
  const std::string select = hook.substr(select_start, hook.find("\n}\n", select_start) - select_start);
  const auto count = [](const std::string& text, std::string_view needle) {
    size_t n = 0;
    for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
      ++n;
    }
    return n;
  };
  CHECK(count(select, "DrawStrip(") == 1 && count(select, "DrawStrip(ctx, base, strip);") == 1);
  CHECK(select.find("  if (!fill) {\n    __imp__sub_823F7D48(ctx, base);\n    return;\n  }") != std::string::npos);
  CHECK(count(select, "__imp__sub_823F7D48(ctx, base);") == 1);
  CHECK(select.find("fill = CharacterSelectFillActive(base) && ssf2t::LoadCharacterSelectTextureSize(base, texture) "
                    "&&\n           ssf2t::CharacterSelectWarp(original, aspect, texture, mesh);") !=
        std::string::npos);
  CHECK(select.find("if (IsQuadCall(ctx) && TextureBindPathMatches(base)) {") != std::string::npos);
  CHECK(hook.find("WriteVertices(base, address, vertices);") != std::string::npos);
  CHECK(hook.find("WriteVertices(base, ctx.r5") == std::string::npos);
  CHECK(hook.find("ssf2t::GuestHostOffset(address)") != std::string::npos);
  size_t unhooked_draws = 0;
  for (size_t at = hook.find("__imp__sub_823F7D48(ctx, base);"); at != std::string::npos;
       at = hook.find("__imp__sub_823F7D48(ctx, base);", at + 1)) {
    ++unhooked_draws;
  }
  CHECK(unhooked_draws == 5);
  CHECK(hook.find("  } else if (ssf2t::SidePanelAt(caller) != ssf2t::SidePanel::kNone) {\n"
                  "    DrawCharacterSelectSidePanel(ctx, base, caller);") != std::string::npos);
  const size_t panel_start = hook.find("void DrawCharacterSelectSidePanel(");
  const std::string panel = hook.substr(panel_start, hook.find("\n}\n", panel_start) - panel_start);
  CHECK(panel_start != std::string::npos);
  CHECK(panel.find("const bool skip = IsQuadCall(ctx) &&\n                    ssf2t::SkipsSidePanel("
                   "CharacterSelectFillActive(base), SidePanelPathMatches(base), caller);") != std::string::npos);
  CHECK(count(panel, "__imp__sub_823F7D48(ctx, base);") == 1 && count(panel, "if (!skip) {") == 1);
  CHECK(count(panel, "DrawStrip(") == 0 && count(panel, "Versus") == 0);
  CHECK(hook.find("\n  sub_823F7D48(ctx, base);") == std::string::npos);
  const size_t extension_draw = hook.find("  __imp__sub_823F7D48(ctx, base);\n  if (!extend) {");
  CHECK(extension_draw != std::string::npos);
  CHECK(extension_draw < hook.find("    DrawStrip(ctx, base, extensions.pieces[i].quad);"));
  CHECK(hook.find("ssf2t::BackgroundExtensionPieces(center, aspect, extensions)") != std::string::npos);
  CHECK(hook.find("PreFightPathMatches(base) &&\n         ssf2t::CharacterSelectFillActive(true, base)") !=
        std::string::npos);
  CHECK(hook.find("PreFightPathMatches(base) && ssf2t::VersusFillActive(true, base)") != std::string::npos);
  CHECK(hook.find("kBackgroundRect") == std::string::npos);
  CHECK(hook.find("sub_823F5948") == std::string::npos);
  size_t f1_writes = 0;
  for (size_t at = hook.find("ctx.f1.f64 ="); at != std::string::npos; at = hook.find("ctx.f1.f64 =", at + 1)) {
    ++f1_writes;
  }
  CHECK(f1_writes == 1);
  const std::string header = ReadSource("fight_area.h");
  CHECK(header.find("flow = ReadMatchFlow(GuestHost(base, ram));") != std::string::npos);
  const size_t fight_block = hook.find("  if (g_scene_depth > 0 && g_in_projection_setup && FillActive(base)) {");
  const std::string fight_ortho = hook.substr(fight_block, hook.find("\n  }", fight_block) - fight_block);
  CHECK(fight_block != std::string::npos);
  CHECK(fight_ortho.find("ctx.f3") == std::string::npos && fight_ortho.find("ctx.f4") == std::string::npos);
  CHECK(fight_ortho.find("ssf2t::OrthoShift(left, right,") != std::string::npos);
  CHECK(kFightMaskDrawer == 0x821775B0);
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
  CHECK(!PreFightPathMatches(base));
  for (const FightRenderFunction& function : kPreFightFunctions) {
    write(function.address, function.words);
  }
  CHECK(PreFightPathMatches(base));
  for (const FightRenderFunction& function : kPreFightFunctions) {
    const uint32_t last = function.address + 4 * uint32_t(function.words.size() - 1);
    StoreBE32(base, last, 0);
    CHECK(!PreFightPathMatches(base));
    CHECK(FightRenderPathMatches(base));
    StoreBE32(base, last, function.words.back());
  }
  CHECK(PreFightPathMatches(base));
  CHECK(!TextureBindPathMatches(base));
  for (const FightRenderFunction& function : kTextureBindFunctions) {
    write(function.address, function.words);
  }
  CHECK(TextureBindPathMatches(base));
  for (const FightRenderFunction& function : kTextureBindFunctions) {
    const uint32_t last = function.address + 4 * uint32_t(function.words.size() - 1);
    StoreBE32(base, last, 0);
    CHECK(!TextureBindPathMatches(base));
    StoreBE32(base, last, function.words.back());
  }
  CHECK(TextureBindPathMatches(base));
  CHECK(!SidePanelPathMatches(base));
  for (const FightRenderFunction& function : kSidePanelFunctions) {
    write(function.address, function.words);
  }
  CHECK(SidePanelPathMatches(base));
  for (const FightRenderFunction& function : kSidePanelFunctions) {
    const uint32_t last = function.address + 4 * uint32_t(function.words.size() - 1);
    StoreBE32(base, last, 0);
    CHECK(!SidePanelPathMatches(base));
    CHECK(PreFightPathMatches(base) && TextureBindPathMatches(base));
    StoreBE32(base, last, function.words.back());
  }
  CHECK(SidePanelPathMatches(base));
  StoreBE32(base, kFightMaskDrawer, 0);
  CHECK(!FightRenderPathMatches(base));
  CHECK(!PreFightPathMatches(base));
  TestFingerprintFailureBlocksExtensions(base);
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
  CHECK(bytes.size() > kMainModeTable + sizeof(kMainModeTableWords) - kImageBase);
  CHECK(PreFightPathMatches(bytes.data() - kImageBase));
  CHECK(bytes.size() > kXdkSetTexture + sizeof(kXdkSetTextureWords) - kImageBase);
  CHECK(TextureBindPathMatches(bytes.data() - kImageBase));
  CHECK(SidePanelPathMatches(bytes.data() - kImageBase));
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
  TestOriginalNeverActsBeforeFights();
  TestPreFightScenes();
  TestPreFightFillActiveOnlyInSelectAndVersus();
  TestFightAndPreFightNeverOverlap();
  TestPreFightReadsGuestGlobals();
  TestGameRamInPhysicalHeap();
  TestNonFightWindowIsTheGamesOwn();
  TestVersusBackgroundIsSurrounded();
  TestVersusPiecesAreBitIdentical();
  TestVertexOrderIsKept();
  TestFullHeightBackgroundGetsOnlySides();
  TestCharacterSelectCenterIsOriginal();
  TestWarpEase();
  TestCharacterSelectWarpSeams();
  TestCharacterSelectWarpFillsScreen();
  TestCharacterSelectWarpFailsClosed();
  TestCharacterSelectWarpIsUnchanged();
  TestCharacterSelectSidePanels();
  TestCharacterSelectTextureSize();
  TestBackgroundExtensionsStayOffElsewhere();
  TestSettingsAreIndependent();
  TestWiring();
  TestRenderPathFingerprints();
  TestHookTargetsInRealImage();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
