#pragma once

#include <bit>
#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>

#include "guest_memory.h"

namespace ssf2t {

inline constexpr char kFightAreaSetting[] = "ssf2t_fight_area";
inline constexpr char kFightAreaOriginal[] = "original";
inline constexpr char kFightAreaFill[] = "fill_16_9";
inline constexpr const char* kDefaultFightArea = kFightAreaOriginal;
inline constexpr char kFightAreaLabel[] = "Fight Area";
inline constexpr char kFightAreaHelp[] =
    "Original keeps the game's original fight framing. Fill 16:9 expands the fight area to use the full 16:9 frame.";

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

void SetFightAreaFill(bool fill);

}  // namespace ssf2t
