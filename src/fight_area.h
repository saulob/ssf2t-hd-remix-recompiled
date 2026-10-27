#pragma once

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

#include "guest_memory.h"

namespace ssf2t {

inline constexpr char kFightAreaSetting[] = "ssf2t_fight_area";
inline constexpr char kFightAreaOriginal[] = "original";
inline constexpr char kFightAreaFill[] = "fill_16_9";
inline constexpr const char* kDefaultFightArea = kFightAreaOriginal;
inline constexpr char kFightAreaLabel[] = "Fight Area";
inline constexpr char kFightAreaHelp[] =
    "Original keeps the game's original fight framing. Fill 16:9 expands the fight area to use the full 16:9 frame "
    "and removes the black borders from Character Select and VS.";

// Segment 0 is Original and segment 1 is Fill 16:9. An unknown value keeps the original framing.
constexpr bool FightAreaFills(std::string_view value) {
  return value == kFightAreaFill;
}

constexpr int FightAreaSegment(bool fill) {
  return fill ? 1 : 0;
}

constexpr const char* FightAreaValue(int segment) {
  return segment == 1 ? kFightAreaFill : kFightAreaOriginal;
}

// The fight draws two opaque black quads over the 16-unit margins of its 640x480 canvas; they are the side bars.
constexpr uint32_t kFightMaskDrawer = 0x82177590;
constexpr uint32_t kFightMaskDrawerWords[] = {
    0x7D8802A6, 0x9181FFF8, 0x9421FF60, 0x39600000, 0x91610050, 0x39600000, 0x91610054,
    0x3D6083C2, 0x896BA828, 0x2C0B0000, 0x4182002C, 0x3D6083E3, 0x816B7F68, 0x2F0B0000,
    0x419A001C, 0x3D6083E1, 0x896B9DB0, 0x2C0B0000, 0x4082000C, 0x39600040, 0x91610054,
};
constexpr uint32_t kWidescreenDisplayFlag = 0x83C1A828;
constexpr uint32_t kWidescreenMode = 0x83E37F68;
constexpr uint32_t kFightActiveFlag = 0x83E09DB0;

// `code` is the guest memory at kFightMaskDrawer. The words include the loads of the three globals above.
inline bool IsFightMaskDrawer(const uint8_t* code) {
  uint32_t offset = 0;
  for (const uint32_t word : kFightMaskDrawerWords) {
    if (LoadBE32(code, offset) != word) {
      return false;
    }
    offset += 4;
  }
  return true;
}

inline constexpr uint32_t kWidescreenAspect = 0x83A21350;
inline constexpr uint32_t kFightCameraX = 0x83E384D8;
inline constexpr float kFightCanvasWidth = 640.0f;
inline constexpr float kFightFillMargin = 16.0f;

// Fill needs the widescreen fight projection (the sub_823EBB28 branch that keeps a 640-unit width) and a usable camera.
constexpr bool FightFillActive(bool fill, uint8_t widescreen_display, uint32_t widescreen_mode, uint8_t fight_active,
                               float aspect, float camera_x) {
  return fill && widescreen_display != 0 && widescreen_mode == 1 && fight_active != 0 && aspect != 1.0f &&
         std::isfinite(camera_x);
}

inline bool FightFillActive(bool fill, const uint8_t* base) {
  return FightFillActive(fill, base[kWidescreenDisplayFlag], LoadBE32(base, kWidescreenMode), base[kFightActiveFlag],
                         std::bit_cast<float>(LoadBE32(base, kWidescreenAspect)),
                         std::bit_cast<float>(LoadBE32(base, kFightCameraX)));
}

// Canvas units to slide the visible window: +16 at the left camera limit, -16 at the right one.
constexpr float FightShift(float camera_x) {
  const float t = camera_x < 0.0f ? 0.0f : (camera_x > 1.0f ? 1.0f : camera_x);
  return kFightFillMargin - 2.0f * kFightFillMargin * t;
}

// The same slide in the units of an ortho that is `right - left` wide.
constexpr float OrthoShift(float left, float right, float camera_x) {
  return FightShift(camera_x) * (right - left) / kFightCanvasWidth;
}

struct FightRenderFunction {
  uint32_t address;
  std::span<const uint32_t> words;
};

inline constexpr uint32_t kSceneDraw = 0x82181AA8;
inline constexpr uint32_t kProjectionSetup = 0x823EBB28;
inline constexpr uint32_t kOrtho = 0x823F5118;
inline constexpr uint32_t kFightRender = 0x823ED578;

inline constexpr uint32_t kSceneDrawWords[] = {
    0x7D8802A6, 0x9181FFF8, 0x9421FEF0, 0x39600001, 0x99610050, 0x3D6083A2,
    0x896B127F, 0x2C0B0000, 0x418200B0, 0x3D6083E1, 0x896B9DB0, 0x2C0B0000,
};
inline constexpr uint32_t kProjectionSetupWords[] = {
    0x7D8802A6, 0x9181FFF8, 0x9421FC90, 0xD0210384, 0xD041038C, 0x38600007, 0x48008ED9, 0x480088BD,
};
inline constexpr uint32_t kOrthoWords[] = {
    0x7D8802A6, 0x9181FFF8, 0x9421FF60, 0xEC062828, 0x3D608200, 0xED420828, 0x3D408200, 0xED241828,
};
inline constexpr uint32_t kFightRenderWords[] = {
    0x7D8802A6, 0x9181FFF8, 0xFBC1FFE8, 0xFBE1FFF0, 0xDBE1FFE0, 0x9421FAD0, 0x480068A9, 0x2C030000,
};

inline const FightRenderFunction kFightRenderFunctions[] = {
    {kSceneDraw, kSceneDrawWords},
    {kProjectionSetup, kProjectionSetupWords},
    {kOrtho, kOrthoWords},
    {kFightRender, kFightRenderWords},
};

inline bool MatchesWords(const uint8_t* base, const FightRenderFunction& function) {
  uint32_t offset = 0;
  for (const uint32_t word : function.words) {
    if (LoadBE32(base, function.address + offset) != word) {
      return false;
    }
    offset += 4;
  }
  return true;
}

// Fill acts only when the mask drawer and every function it hooks or relies on is the expected code.
inline bool FightRenderPathMatches(const uint8_t* base) {
  if (!IsFightMaskDrawer(base + kFightMaskDrawer)) {
    return false;
  }
  for (const FightRenderFunction& function : kFightRenderFunctions) {
    if (!MatchesWords(base, function)) {
      return false;
    }
  }
  return true;
}

// Outside fights the mask quads still show at the edges of the wider non-fight projection. VS is told apart by the
// match flow halfwords; Character Select by the HD background, which stays set for the whole visible screen.
inline constexpr uint32_t kGameRam = 0x83E09CC0;
inline constexpr uint32_t kMainModeOffset = 0x0;
inline constexpr uint32_t kGameModeOffset = 0x4;
inline constexpr uint32_t kMatchStepOffset = 0x8;
inline constexpr uint16_t kMainModeRunning = 2;
inline constexpr uint16_t kGameModeMatch = 4;
inline constexpr uint16_t kMatchStepVersus = 4;

struct MatchFlow {
  uint16_t main_mode;
  uint16_t game_mode;
  uint16_t match_step;
};

constexpr bool IsVersus(MatchFlow flow) {
  return flow.main_mode == kMainModeRunning && flow.game_mode == kGameModeMatch && flow.match_step == kMatchStepVersus;
}

inline constexpr uint32_t kHdBackgroundEnabled = 0x83A2127F;
inline constexpr uint32_t kHdBackground = 0x83A2130C;
inline constexpr uint32_t kCharacterSelectBackground = 2;
inline constexpr uint32_t kCharacterSelectBackgroundAlt = 9;

constexpr bool IsCharacterSelectBackground(uint8_t hd_background_enabled, uint32_t hd_background) {
  return hd_background_enabled != 0 &&
         (hd_background == kCharacterSelectBackground || hd_background == kCharacterSelectBackgroundAlt);
}

struct PreFightScene {
  bool character_select;
  bool versus;
};

constexpr bool WidescreenOutsideFight(uint8_t widescreen_display, uint32_t widescreen_mode, uint8_t fight_active,
                                      float aspect) {
  return widescreen_display != 0 && widescreen_mode == 1 && fight_active == 0 && aspect != 1.0f;
}

constexpr bool PreFightFillActive(bool fill, uint8_t widescreen_display, uint32_t widescreen_mode, uint8_t fight_active,
                                  float aspect, PreFightScene scene) {
  return fill && WidescreenOutsideFight(widescreen_display, widescreen_mode, fight_active, aspect) &&
         (scene.character_select || scene.versus);
}

// VS wins when both hold, so VS never draws the Character Select background wider.
constexpr bool CharacterSelectFillActive(bool fill, uint8_t widescreen_display, uint32_t widescreen_mode,
                                         uint8_t fight_active, float aspect, PreFightScene scene) {
  return fill && WidescreenOutsideFight(widescreen_display, widescreen_mode, fight_active, aspect) &&
         scene.character_select && !scene.versus;
}

inline MatchFlow ReadMatchFlow(const uint8_t* ram) {
  return {LoadBE16(ram, kMainModeOffset), LoadBE16(ram, kGameModeOffset), LoadBE16(ram, kMatchStepOffset)};
}

// The game RAM is a 64 KiB-aligned allocation in the physical heap; any other value means it is not set up.
inline bool LoadMatchFlow(const uint8_t* base, MatchFlow& flow) {
  const uint32_t ram = LoadBE32(base, kGameRam);
  if (ram == 0 || (ram & 0xFFFF) != 0) {
    return false;
  }
  flow = ReadMatchFlow(GuestHost(base, ram));
  return true;
}

inline PreFightScene LoadPreFightScene(const uint8_t* base) {
  MatchFlow flow{};
  return {IsCharacterSelectBackground(base[kHdBackgroundEnabled], LoadBE32(base, kHdBackground)),
          LoadMatchFlow(base, flow) && IsVersus(flow)};
}

inline bool PreFightFillActive(bool fill, const uint8_t* base) {
  return fill && PreFightFillActive(true, base[kWidescreenDisplayFlag], LoadBE32(base, kWidescreenMode),
                                    base[kFightActiveFlag], std::bit_cast<float>(LoadBE32(base, kWidescreenAspect)),
                                    LoadPreFightScene(base));
}

constexpr bool VersusFillActive(bool fill, uint8_t widescreen_display, uint32_t widescreen_mode, uint8_t fight_active,
                                float aspect, PreFightScene scene) {
  return fill && WidescreenOutsideFight(widescreen_display, widescreen_mode, fight_active, aspect) && scene.versus;
}

inline bool VersusFillActive(bool fill, const uint8_t* base) {
  return fill && VersusFillActive(true, base[kWidescreenDisplayFlag], LoadBE32(base, kWidescreenMode),
                                  base[kFightActiveFlag], std::bit_cast<float>(LoadBE32(base, kWidescreenAspect)),
                                  LoadPreFightScene(base));
}

inline bool CharacterSelectFillActive(bool fill, const uint8_t* base) {
  return fill && CharacterSelectFillActive(true, base[kWidescreenDisplayFlag], LoadBE32(base, kWidescreenMode),
                                           base[kFightActiveFlag],
                                           std::bit_cast<float>(LoadBE32(base, kWidescreenAspect)),
                                           LoadPreFightScene(base));
}

inline constexpr uint32_t kMainModeDispatch = 0x8223DD58;
inline constexpr uint32_t kGameModeDispatch = 0x8223DCF0;
inline constexpr uint32_t kMatchStepDispatch = 0x8223DC30;
inline constexpr uint32_t kMainModeTable = 0x83A2D040;
inline constexpr uint32_t kGameModeTable = 0x83A2D01C;
inline constexpr uint32_t kMatchStepTable = 0x83A2CFC8;

// Each dispatcher reads its halfword and jumps through table[value / 2].
inline constexpr uint32_t kMainModeDispatchWords[] = {
    0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x816B52DC, 0xA16B0000, 0x3D4083CD, 0x394A52CC,
    0xB16A0002, 0x3D6083CD, 0x396B52CC, 0xA16B0002, 0x7D6B0734, 0x556B083C, 0x3D4083A3, 0x394AD040,
};
inline constexpr uint32_t kGameModeDispatchWords[] = {
    0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083C1, 0x39400000, 0x994B3390, 0x3D6083CD, 0x816B52DC,
    0xA16B0004, 0x3D4083CD, 0x394A52CC, 0xB16A0002, 0x3D6083CD, 0x396B52CC, 0xA16B0002, 0x7D6B0734,
    0x556B083C, 0x3D4083A3, 0x394AD01C,
};
inline constexpr uint32_t kMatchStepDispatchWords[] = {
    0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x816B52DC, 0xA16B0008, 0x3D4083CD, 0x394A52CC,
    0xB16A0002, 0x3D6083CD, 0x396B52CC, 0xA16B0002, 0x7D6B0734, 0x556B083C, 0x3D4083A3, 0x394ACFC8,
};
inline constexpr uint32_t kMainModeTableWords[] = {0x82238EA0, kGameModeDispatch};
inline constexpr uint32_t kGameModeTableWords[] = {0x82238FD0, 0x82239088, kMatchStepDispatch};
// Step 4 shows the win counts, then starts the fight.
inline constexpr uint32_t kMatchStepTableWords[] = {0x822390C0, 0x82239150, 0x82239230};

// The scene draw picks the HD background from kHdBackground; 2 draws the Character Select screen texture.
inline constexpr uint32_t kHdBackgroundDraw = 0x8217A088;
inline constexpr uint32_t kHdBackgroundDrawWords[] = {
    0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083A2, 0x896B127F, 0x2C0B0000, 0x4082000C, 0x38600000,
    0x480000A4, 0x39600000, 0x99610050, 0x3D6083A2, 0x816B130C, 0x91610054, 0x81610054, 0x2F0B0002,
    0x419A0044, 0x81610054, 0x2F0B0003, 0x419A0048,
};

// Each background is one 4-vertex quad submitted through kQuadDraw; these are its two call sites.
inline constexpr uint32_t kQuadDraw = 0x823F75D8;
inline constexpr uint32_t kCharacterSelectBackgroundDraw = 0x82174F20;
inline constexpr uint32_t kScrollingBackgroundDraw = 0x82174C70;
inline constexpr uint32_t kCharacterSelectQuadCall = 0x821750CC;
inline constexpr uint32_t kScrollingQuadCall = 0x82174EFC;
inline constexpr uint32_t kCharacterSelectQuadReturn = 0x821750DC;
inline constexpr uint32_t kScrollingQuadReturn = 0x82174F0C;
inline constexpr uint32_t kQuadDrawWords[] = {0x7D8802A6, 0x496085D1, 0x9421FF90, 0x7C7F1B78,
                                              0x38600001, 0x7C9E2378, 0x7CBD2B78, 0x48002E2D};
inline constexpr uint32_t kCharacterSelectBackgroundDrawWords[] = {
    0x7D8802A6, 0x9181FFF8, 0x9421FEF0, 0x3D6083BE, 0x806B5AB0, 0x4BFFEA65, 0x48283619, 0x38600060,
};
inline constexpr uint32_t kScrollingBackgroundDrawWords[] = {
    0x7D8802A6, 0x9181FFF8, 0x9421FEF0, 0x38800000, 0x38600000, 0x48282A0D, 0x3D6083BE, 0x806B5ABC,
};
inline constexpr uint32_t kCharacterSelectQuadCallWords[] = {0x80A10090, 0x38800004, 0x38600006, 0x48282501};
inline constexpr uint32_t kScrollingQuadCallWords[] = {0x80A10094, 0x38800004, 0x38600006, 0x482826D1};

// For HD background 2 or 9 the scene draw also puts backgrounds\WorldMapLeft and WorldMapRight in the 96 units
// beside the Character Select background, through kQuadDraw from these two call sites.
inline constexpr uint32_t kSidePanelDraw = 0x82174870;
inline constexpr uint32_t kLeftSidePanelQuadCall = 0x82174A7C;
inline constexpr uint32_t kRightSidePanelQuadCall = 0x82174C4C;
inline constexpr uint32_t kLeftSidePanelQuadReturn = 0x82174A8C;
inline constexpr uint32_t kRightSidePanelQuadReturn = 0x82174C5C;
inline constexpr uint32_t kSidePanelDrawWords[] = {
    0x7D8802A6, 0x9181FFF8, 0x9421FE60, 0x3D6083A2, 0x816B130C, 0x91610190, 0x81610190,
    0x2F0B0002, 0x419A0014, 0x81610190, 0x2F0B0009, 0x419A0008, 0x48000028, 0x3D6083BE,
};
inline constexpr uint32_t kLeftSidePanelQuadCallWords[] = {0x80A100D0, 0x38800004, 0x38600006, 0x48282B51};
inline constexpr uint32_t kRightSidePanelQuadCallWords[] = {0x80A100D0, 0x38800004, 0x38600006, 0x48282981};

inline const FightRenderFunction kSidePanelFunctions[] = {
    {kSidePanelDraw, kSidePanelDrawWords},
    {kLeftSidePanelQuadCall, kLeftSidePanelQuadCallWords},
    {kRightSidePanelQuadCall, kRightSidePanelQuadCallWords},
};

inline bool SidePanelPathMatches(const uint8_t* base) {
  for (const FightRenderFunction& function : kSidePanelFunctions) {
    if (!MatchesWords(base, function)) {
      return false;
    }
  }
  return true;
}

enum class SidePanel { kNone, kLeft, kRight };

constexpr SidePanel SidePanelAt(uint32_t caller) {
  return caller == kLeftSidePanelQuadReturn    ? SidePanel::kLeft
         : caller == kRightSidePanelQuadReturn ? SidePanel::kRight
                                               : SidePanel::kNone;
}

// Fill leaves the side panels out: they stop at the background's own rectangle and would hide the filled edges.
constexpr bool SkipsSidePanel(bool character_select_fill, bool path_matches, uint32_t caller) {
  return character_select_fill && path_matches && SidePanelAt(caller) != SidePanel::kNone;
}

// The game's per-draw vertex scratch: reserve returns the space, commit advances past it.
inline constexpr uint32_t kVertexReserve = 0x821738A0;
inline constexpr uint32_t kVertexCommit = 0x821738F0;
inline constexpr uint32_t kVertexReserveWords[] = {0x7D8802A6, 0x9181FFF8, 0xFBE1FFF0, 0x9421FFA0,
                                                   0x3FE083E4, 0x39630003, 0x3D2083E4, 0x556A003A};
inline constexpr uint32_t kVertexCommitWords[] = {0x3D2083E4, 0x39630003, 0x556A003A, 0x8169D8FC,
                                                  0x7D6A5A14, 0x9169D8FC, 0x4E800020};

inline constexpr uint32_t kQuadPrimitive = 6;
inline constexpr uint32_t kQuadVertices = 4;
inline constexpr uint32_t kQuadVertexStride = 24;

inline const FightRenderFunction kPreFightFunctions[] = {
    {kMainModeDispatch, kMainModeDispatchWords},
    {kGameModeDispatch, kGameModeDispatchWords},
    {kMatchStepDispatch, kMatchStepDispatchWords},
    {kMainModeTable, kMainModeTableWords},
    {kGameModeTable, kGameModeTableWords},
    {kMatchStepTable, kMatchStepTableWords},
    {kHdBackgroundDraw, kHdBackgroundDrawWords},
    {kCharacterSelectBackgroundDraw, kCharacterSelectBackgroundDrawWords},
    {kScrollingBackgroundDraw, kScrollingBackgroundDrawWords},
    {kQuadDraw, kQuadDrawWords},
    {kCharacterSelectQuadCall, kCharacterSelectQuadCallWords},
    {kScrollingQuadCall, kScrollingQuadCallWords},
    {kVertexReserve, kVertexReserveWords},
    {kVertexCommit, kVertexCommitWords},
};

inline bool PreFightPathMatches(const uint8_t* base) {
  if (!IsFightMaskDrawer(base + kFightMaskDrawer)) {
    return false;
  }
  for (const FightRenderFunction& function : kPreFightFunctions) {
    if (!MatchesWords(base, function)) {
      return false;
    }
  }
  return true;
}

struct Span {
  double start;
  double end;
};

constexpr bool IsFillAspect(float aspect) {
  return aspect > 0.0f && aspect < 1.0f;
}

// The non-fight projection shows `canvas / aspect` units centred on the canvas, at the canvas's own scale.
constexpr Span NonFightVisibleX(float aspect) {
  const double width = double(kFightCanvasWidth) / double(aspect);
  const double left = -(width - double(kFightCanvasWidth)) / 2.0;
  return {left, left + width};
}

struct QuadVertex {
  float x;
  float y;
  float z;
  uint32_t color;
  float u;
  float v;
};

// The non-fight projection always spans the canvas height.
constexpr Span NonFightVisibleY() {
  return {0.0, 480.0};
}

using QuadVertices = std::array<QuadVertex, kQuadVertices>;

struct BackgroundPiece {
  const char* name;
  QuadVertices quad;
};

inline constexpr size_t kMaxBackgroundExtensions = 8;

struct BackgroundExtensions {
  std::array<BackgroundPiece, kMaxBackgroundExtensions> pieces;
  size_t count;
};

// How much of the texture, measured inward from each edge, the extensions sample.
inline constexpr float kEdgeStripFraction = 1.0f / 32.0f;

constexpr bool IsFiniteCoordinate(float value) {
  return value > -1e6f && value < 1e6f;
}

// The background as an axis-aligned rectangle. Index 0 is the low x (or y) side, 1 the high side.
struct BackgroundRect {
  float x[2];
  float y[2];
  float u[2];
  float v[2];
  int corner[2][2];
};

constexpr bool ReadBackgroundRect(const QuadVertices& quad, BackgroundRect& rect) {
  rect = {{quad[0].x, quad[0].x}, {quad[0].y, quad[0].y}, {}, {}, {{-1, -1}, {-1, -1}}};
  for (const QuadVertex& vertex : quad) {
    if (!IsFiniteCoordinate(vertex.x) || !IsFiniteCoordinate(vertex.y) || !IsFiniteCoordinate(vertex.u) ||
        !IsFiniteCoordinate(vertex.v)) {
      return false;
    }
    rect.x[0] = vertex.x < rect.x[0] ? vertex.x : rect.x[0];
    rect.x[1] = vertex.x > rect.x[1] ? vertex.x : rect.x[1];
    rect.y[0] = vertex.y < rect.y[0] ? vertex.y : rect.y[0];
    rect.y[1] = vertex.y > rect.y[1] ? vertex.y : rect.y[1];
  }
  if (!(rect.x[0] < rect.x[1]) || !(rect.y[0] < rect.y[1])) {
    return false;
  }
  for (int i = 0; i < int(kQuadVertices); ++i) {
    const int sx = quad[i].x == rect.x[0] ? 0 : (quad[i].x == rect.x[1] ? 1 : -1);
    const int sy = quad[i].y == rect.y[0] ? 0 : (quad[i].y == rect.y[1] ? 1 : -1);
    if (sx < 0 || sy < 0 || rect.corner[sx][sy] >= 0) {
      return false;
    }
    rect.corner[sx][sy] = i;
  }
  for (int side = 0; side < 2; ++side) {
    rect.u[side] = quad[rect.corner[side][0]].u;
    rect.v[side] = quad[rect.corner[0][side]].v;
    if (quad[rect.corner[side][1]].u != rect.u[side] || quad[rect.corner[1][side]].v != rect.v[side]) {
      return false;
    }
  }
  return rect.u[0] != rect.u[1] && rect.v[0] != rect.v[1];
}

// One column (or row) of the 3x3 layout: position and texture coordinate per side, and which background side its
// colour and depth come from.
struct EdgeBand {
  float position[2];
  float texture[2];
  int source[2];
};

constexpr EdgeBand CenterBand(const float (&position)[2], const float (&texture)[2]) {
  return {{position[0], position[1]}, {texture[0], texture[1]}, {0, 1}};
}

// The band past one edge: its inner side copies that edge of the background, and its outer side samples the texture
// a narrow strip inward (mirrored), so the seam matches and none of the central artwork is reused.
constexpr EdgeBand BandPast(int side, double screen, const float (&position)[2], const float (&texture)[2]) {
  const int other = 1 - side;
  EdgeBand band{};
  band.position[side] = float(screen);
  band.texture[side] = texture[side] + kEdgeStripFraction * (texture[other] - texture[side]);
  band.position[other] = position[side];
  band.texture[other] = texture[side];
  band.source[0] = side;
  band.source[1] = side;
  return band;
}

constexpr QuadVertices BuildPiece(const QuadVertices& center, const BackgroundRect& rect, const EdgeBand& column,
                                  const EdgeBand& row) {
  QuadVertices piece{};
  for (int sx = 0; sx < 2; ++sx) {
    for (int sy = 0; sy < 2; ++sy) {
      QuadVertex& vertex = piece[rect.corner[sx][sy]];
      vertex = center[rect.corner[column.source[sx]][row.source[sy]]];
      vertex.x = column.position[sx];
      vertex.u = column.texture[sx];
      vertex.y = row.position[sy];
      vertex.v = row.texture[sy];
    }
  }
  return piece;
}

// Fails closed unless `center` is an axis-aligned rectangle with one u per column and one v per row that leaves a
// gap at both sides of the screen. A top or bottom row is added only where the background leaves a gap.
constexpr bool BackgroundExtensionPieces(const QuadVertices& center, float aspect, BackgroundExtensions& out) {
  out = {};
  BackgroundRect rect{};
  if (!IsFillAspect(aspect) || !ReadBackgroundRect(center, rect)) {
    return false;
  }
  const Span visible_x = NonFightVisibleX(aspect);
  const Span visible_y = NonFightVisibleY();
  if (!(visible_x.start < rect.x[0]) || !(rect.x[1] < visible_x.end) || !(rect.y[0] < visible_y.end) ||
      !(visible_y.start < rect.y[1])) {
    return false;
  }
  const EdgeBand left = BandPast(0, visible_x.start, rect.x, rect.u);
  const EdgeBand middle_column = CenterBand(rect.x, rect.u);
  const EdgeBand right = BandPast(1, visible_x.end, rect.x, rect.u);
  const EdgeBand middle_row = CenterBand(rect.y, rect.v);
  auto add = [&](const char* name, const EdgeBand& column, const EdgeBand& row) {
    out.pieces[out.count++] = {name, BuildPiece(center, rect, column, row)};
  };
  add("left_extension", left, middle_row);
  add("right_extension", right, middle_row);
  if (visible_y.start < rect.y[0]) {
    const EdgeBand top = BandPast(0, visible_y.start, rect.y, rect.v);
    add("top_extension", middle_column, top);
    add("top_left_extension", left, top);
    add("top_right_extension", right, top);
  }
  if (rect.y[1] < visible_y.end) {
    const EdgeBand bottom = BandPast(1, visible_y.end, rect.y, rect.v);
    add("bottom_extension", middle_column, bottom);
    add("bottom_left_extension", left, bottom);
    add("bottom_right_extension", right, bottom);
  }
  return true;
}

struct TextureSize {
  uint32_t width;
  uint32_t height;
};

struct TextureBox {
  double u[2];
  double v[2];
};

constexpr bool Contains(const TextureBox& outer, const TextureBox& inner) {
  return outer.u[0] < inner.u[0] && inner.u[1] < outer.u[1] && outer.v[0] < inner.v[0] && inner.v[1] < outer.v[1];
}

// backgrounds\Character_select_screen (v grows down the image): the globe with its halo, and the part Fill never
// resizes or moves, which also keeps the light rays around the halo.
inline constexpr TextureSize kCharacterSelectArt{1920, 1024};
inline constexpr TextureBox kCharacterSelectGlobeHalo{{491.0 / 1920.0, 1408.0 / 1920.0},
                                                      {157.0 / 1024.0, 706.0 / 1024.0}};
inline constexpr TextureBox kCharacterSelectProtected{{0.2, 0.8}, {0.09375, 0.890625}};
static_assert(Contains(kCharacterSelectProtected, kCharacterSelectGlobeHalo));
// The texture range Fill stretches to the screen edges; stretched, the dark left-edge artwork and the bright last rows
// read as a border.
inline constexpr TextureBox kCharacterSelectSource{{0.05, 1.0}, {0.0, 0.9921875}};
static_assert(Contains(kCharacterSelectSource, kCharacterSelectProtected));

// Where the original quad shows texture coordinate `t` along one axis.
constexpr double OriginalPosition(const float (&position)[2], const float (&texture)[2], double t) {
  const double span = double(position[1]) - double(position[0]);
  return double(position[0]) + span * (t - double(texture[0])) / (double(texture[1]) - double(texture[0]));
}

// One axis of the Fill warp. Between the cuts it is the original quad's mapping. Past each cut it adds the gap
// between where the original quad shows the band's source end and the visible edge, times WarpEase(r), where r runs
// from 0 at the cut to 1 at that source end.
struct WarpAxis {
  float position[2];
  float texture[2];
  double cut[2];
  double end[2];
  double visible[2];
};

// Orders the cuts and source ends by where the original quad shows them; fails closed unless the original quad shows
// end, cut, cut, end strictly in that order inside itself, and itself inside the visible span.
constexpr bool MakeWarpAxis(const float (&position)[2], const float (&texture)[2], const double (&cut)[2],
                            const double (&source)[2], Span visible, WarpAxis& axis) {
  axis = {{position[0], position[1]},
          {texture[0], texture[1]},
          {cut[0], cut[1]},
          {source[0], source[1]},
          {visible.start, visible.end}};
  const auto at = [&](double t) { return OriginalPosition(position, texture, t); };
  if (at(cut[1]) < at(cut[0])) {
    std::swap(axis.cut[0], axis.cut[1]);
  }
  if (at(source[1]) < at(source[0])) {
    std::swap(axis.end[0], axis.end[1]);
  }
  return visible.start <= position[0] && position[0] <= at(axis.end[0]) && at(axis.end[0]) < at(axis.cut[0]) &&
         at(axis.cut[0]) < at(axis.cut[1]) && at(axis.cut[1]) < at(axis.end[1]) && at(axis.end[1]) <= position[1] &&
         position[1] <= visible.end;
}

inline constexpr double kWarpEaseLength = 0.5;

// The share of the gap absorbed at r: the stretch eases in as a smoothstep over the first kWarpEaseLength of the
// band, with no slope or curvature change at the cut, then stays constant so straight strokes there stay straight.
constexpr double WarpEase(double r) {
  constexpr double kStretch = 1.0 / (1.0 - kWarpEaseLength / 2.0);
  if (r >= kWarpEaseLength) {
    return kStretch * (r - kWarpEaseLength / 2.0);
  }
  const double x = r / kWarpEaseLength;
  return kStretch * kWarpEaseLength * x * x * x * (1.0 - x / 2.0);
}

constexpr double WarpPosition(const WarpAxis& axis, double t) {
  double position = OriginalPosition(axis.position, axis.texture, t);
  for (int side = 0; side < 2; ++side) {
    const double r = (t - axis.cut[side]) / (axis.end[side] - axis.cut[side]);
    if (r > 0.0) {
      position += (axis.visible[side] - OriginalPosition(axis.position, axis.texture, axis.end[side])) * WarpEase(r);
    }
  }
  return position;
}

// Each outer band: kWarpEaseSegments even steps across the ease, then one exact segment for its constant stretch.
inline constexpr int kWarpEaseSegments = 16;
inline constexpr int kWarpBandLines = kWarpEaseSegments + 2;
inline constexpr int kWarpCenter = kWarpBandLines - 1;
inline constexpr int kWarpLines = 2 * kWarpBandLines;
inline constexpr int kWarpStrips = kWarpLines - 1;
inline constexpr uint32_t kWarpStripVertices = 2 * kWarpLines;

// Grid lines along one axis, low to high position. Lines kWarpCenter and kWarpCenter + 1 are the cuts.
struct WarpLines {
  float position[kWarpLines];
  float texture[kWarpLines];
};

constexpr WarpLines BuildWarpLines(const WarpAxis& axis) {
  WarpLines lines{};
  for (int step = 0; step < kWarpBandLines; ++step) {
    const bool edge = step == kWarpBandLines - 1;
    const double r = kWarpEaseLength * step / kWarpEaseSegments;
    for (int side = 0; side < 2; ++side) {
      const int index = side == 0 ? kWarpCenter - step : kWarpCenter + 1 + step;
      const double end = axis.end[side];
      const double t = edge ? end : axis.cut[side] + (end - axis.cut[side]) * r;
      lines.texture[index] = float(t);
      lines.position[index] = float(edge ? axis.visible[side] : WarpPosition(axis, t));
    }
  }
  return lines;
}

// The warped background as triangle strips laid out like the game's quad: each strip spans one cell along the axis
// the quad's first two vertices differ on and every cell along the other axis, in the quad's vertex order.
struct WarpMesh {
  WarpAxis axes[2];
  WarpLines lines[2];
  std::array<std::array<QuadVertex, kWarpStripVertices>, kWarpStrips> strips;
};

// Fails closed unless `center` is the Character Select background quad over its 1920x1024 art, as an axis-aligned
// rectangle drawn as one strip quad. Canvas y grows up the screen.
constexpr bool CharacterSelectWarp(const QuadVertices& center, float aspect, TextureSize texture, WarpMesh& out) {
  BackgroundRect rect{};
  if (!IsFillAspect(aspect) || texture.width != kCharacterSelectArt.width ||
      texture.height != kCharacterSelectArt.height || !ReadBackgroundRect(center, rect) ||
      !MakeWarpAxis(rect.x, rect.u, kCharacterSelectProtected.u, kCharacterSelectSource.u, NonFightVisibleX(aspect),
                    out.axes[0]) ||
      !MakeWarpAxis(rect.y, rect.v, kCharacterSelectProtected.v, kCharacterSelectSource.v, NonFightVisibleY(),
                    out.axes[1])) {
    return false;
  }
  int side[kQuadVertices][2]{};
  for (int sx = 0; sx < 2; ++sx) {
    for (int sy = 0; sy < 2; ++sy) {
      side[rect.corner[sx][sy]][0] = sx;
      side[rect.corner[sx][sy]][1] = sy;
    }
  }
  const int across = side[0][0] != side[1][0] ? 0 : 1;
  const int along = 1 - across;
  if (side[1][along] != side[0][along] || side[2][across] != side[0][across] || side[2][along] == side[0][along]) {
    return false;
  }
  out.lines[0] = BuildWarpLines(out.axes[0]);
  out.lines[1] = BuildWarpLines(out.axes[1]);
  for (int strip = 0; strip < kWarpStrips; ++strip) {
    for (int step = 0; step < kWarpLines; ++step) {
      for (int k = 0; k < 2; ++k) {
        int line[2]{};
        line[across] = strip + side[k][across];
        line[along] = side[0][along] == 0 ? step : kWarpLines - 1 - step;
        QuadVertex& vertex = out.strips[strip][2 * step + k];
        vertex = center[rect.corner[line[0] > kWarpCenter][line[1] > kWarpCenter]];
        vertex.x = out.lines[0].position[line[0]];
        vertex.u = out.lines[0].texture[line[0]];
        vertex.y = out.lines[1].position[line[1]];
        vertex.v = out.lines[1].texture[line[1]];
      }
    }
  }
  return true;
}

// sub_82174F20 binds the game texture *(kCharacterSelectTexture) + 4 through sub_823F5AB8, which passes the D3D
// texture embedded at +28 of it to D3DDevice_SetTexture. SetTexture reads that texture's fetch constant at +28 and
// keeps the GPU's copy for sampler 0 in the device's fetch constant shadow.
inline constexpr uint32_t kCharacterSelectTexture = 0x83BE5AB0;
inline constexpr uint32_t kGameTextureD3D = 4 + 28;
inline constexpr uint32_t kD3DTextureFetch = 28;
inline constexpr uint32_t kDevicePointer = 0x83E5DA18;
inline constexpr uint32_t kDeviceTextures = 0x3100;
inline constexpr uint32_t kDeviceFetchConstants = 0x480;

inline constexpr uint32_t kTextureHeader = 0x82173998;
inline constexpr uint32_t kBindSampler0 = 0x823F8550;
inline constexpr uint32_t kBindTexture = 0x823F5AB8;
inline constexpr uint32_t kXdkSetTexture = 0x839A6E68;
inline constexpr uint32_t kTextureHeaderWords[] = {0x38630004, 0x4E800020};
inline constexpr uint32_t kBindSampler0Words[] = {0x7C641B78, 0x38600000, 0x4BFFD560};
inline constexpr uint32_t kBindTextureWords[] = {
    0x7D8802A6, 0x4960A0F1, 0x9421FF90, 0x7C9F2378, 0x7C7E1B78, 0x2B1F0000, 0x419A0040,
    0x817F0050, 0x38BF001C, 0x2B0B0000, 0x419A0034, 0x3FA083E6, 0x38C00000, 0x38800000,
    0x64C68000, 0x807DDA18, 0x495B1371, 0x807DDA18, 0x3CC04000, 0x80BF0050, 0x38800001,
};
inline constexpr uint32_t kXdkSetTextureWords[] = {
    0x7D8802A6, 0x48058D1D, 0x9421FF40, 0x39640C40, 0x7C7F1B78, 0x557D103A, 0x2B050000,
    0x7F9DF82E, 0x419A00E4, 0x83650030, 0x39640030, 0x80E50020, 0x5769653E, 0x8345002C,
    0x1D6B0018, 0x8325001C, 0x83050024, 0x82E50028, 0x54EA653E, 0x7D6BFA14, 0x39290200,
};

inline const FightRenderFunction kTextureBindFunctions[] = {
    {kTextureHeader, kTextureHeaderWords},
    {kBindSampler0, kBindSampler0Words},
    {kBindTexture, kBindTextureWords},
    {kXdkSetTexture, kXdkSetTextureWords},
};

inline bool TextureBindPathMatches(const uint8_t* base) {
  for (const FightRenderFunction& function : kTextureBindFunctions) {
    if (!MatchesWords(base, function)) {
      return false;
    }
  }
  return true;
}

using TextureFetch = std::array<uint32_t, 6>;

inline TextureFetch LoadTextureFetch(const uint8_t* host) {
  TextureFetch fetch{};
  for (uint32_t i = 0; i < fetch.size(); ++i) {
    fetch[i] = LoadBE32(host, 4 * i);
  }
  return fetch;
}

// A 2D texture fetch constant: type 2 in dword 0, dimension 1 in dword 5, width and height minus one in dword 2.
constexpr bool FetchTextureSize(const TextureFetch& fetch, TextureSize& size) {
  if ((fetch[0] & 3) != 2 || ((fetch[5] >> 9) & 3) != 1) {
    return false;
  }
  size = {(fetch[2] & 0x1FFF) + 1, ((fetch[2] >> 13) & 0x1FFF) + 1};
  return true;
}

// The size of the texture on sampler 0, provided it is the Character Select background's and the GPU's copy of its
// fetch constant still describes it.
inline bool LoadCharacterSelectTextureSize(const uint8_t* base, TextureSize& size) {
  const uint32_t holder = LoadBE32(base, kCharacterSelectTexture);
  const uint32_t device = LoadBE32(base, kDevicePointer);
  if (holder == 0 || device == 0) {
    return false;
  }
  const uint32_t d3d = holder + kGameTextureD3D;
  const uint8_t* device_host = GuestHost(base, device);
  if (LoadBE32(device_host, kDeviceTextures) != d3d) {
    return false;
  }
  const TextureFetch shadow = LoadTextureFetch(device_host + kDeviceFetchConstants);
  const TextureFetch header = LoadTextureFetch(GuestHost(base, d3d + kD3DTextureFetch));
  return shadow[2] == header[2] && FetchTextureSize(shadow, size);
}

void SetFightAreaFill(bool fill);

}  // namespace ssf2t
