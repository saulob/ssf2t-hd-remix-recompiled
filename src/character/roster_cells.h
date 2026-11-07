#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "character/akuma_portrait.h"
#include "character/evil_ryu_assets.h"
#include "character/ultra_portraits.h"

namespace ssf2t::character {

enum class RosterArt : uint8_t { kAkuma, kEvilRyu, kViolentKen, kEvilRyuName, kViolentKenName };
inline constexpr int kRosterArtCount = 5;

// The arguments of the guest's roster cell quad: arcade units, lower left corner, y up.
struct ArcadeQuad {
  float x = 0;
  float y = 0;
  float width = 0;
  float height = 0;
};

inline constexpr float kCanvasPerArcadeX = 1.583f;
inline constexpr float kCanvasPerArcadeY = 2.0f;
inline constexpr float kCanvasOrigin = 16.0f;
inline constexpr float kCanvasLeft = -640.0f / 7.0f;
inline constexpr float kPicturePerCanvasX = 14.0f / 9.0f;
inline constexpr float kPicturePerCanvasY = 1.5f;
inline constexpr float kPictureWidth = 1280.0f;
inline constexpr float kPictureHeight = 720.0f;
inline constexpr ViewRect kGuestPicture = {0, 0, kPictureWidth, kPictureHeight};

inline ArcadeQuad PictureToArcade(const ViewRect& picture) {
  const float canvas_x = picture.x / kPicturePerCanvasX + kCanvasLeft;
  const float canvas_y = (kPictureHeight - picture.y - picture.height) / kPicturePerCanvasY;
  return {(canvas_x - kCanvasOrigin) / kCanvasPerArcadeX, (canvas_y - kCanvasOrigin) / kCanvasPerArcadeY,
          picture.width / kPicturePerCanvasX / kCanvasPerArcadeX, picture.height / kPicturePerCanvasY / kCanvasPerArcadeY};
}

inline constexpr float kNamePlateTopY = 40.0f;

inline ViewRect NamePlatePicture(int side, int art_width, int art_height) {
  const ViewRect portrait = LargePortraitPicture(side);
  const float width = portrait.width * 0.9f;
  return {portrait.x + (portrait.width - width) * 0.5f, kNamePlateTopY, width,
          width * float(art_height) / float(art_width)};
}

// The guest texture holds the art in its top left corner, so the quad grows by what the padding added.
struct GuestCellImage {
  int width = 0;
  int height = 0;
  int art_width = 0;
  int art_height = 0;
  std::vector<uint8_t> argb;
};

inline ViewRect PaddedPicture(const ViewRect& picture, const GuestCellImage& image) {
  return {picture.x, picture.y, picture.width * float(image.width) / float(image.art_width),
          picture.height * float(image.height) / float(image.art_height)};
}

inline int NextPowerOfTwo(int value) {
  int power = 1;
  while (power < value) power *= 2;
  return power;
}

inline GuestCellImage ToGuestCellImage(const PortraitImage& image) {
  GuestCellImage out;
  out.width = NextPowerOfTwo(image.width);
  out.height = NextPowerOfTwo(image.height);
  out.art_width = image.width;
  out.art_height = image.height;
  out.argb.assign(size_t(out.width) * out.height * 4, 0);
  for (int y = 0; y < image.height; ++y) {
    const uint8_t* source = image.rgba.data() + size_t(y) * image.width * 4;
    uint8_t* target = out.argb.data() + size_t(y) * out.width * 4;
    for (int x = 0; x < image.width; ++x, source += 4, target += 4) {
      target[0] = source[3];
      target[1] = source[0];
      target[2] = source[1];
      target[3] = source[2];
    }
  }
  return out;
}

// What one run of the native roster pass adds. Lifetime and fade need no input here: the cells live inside that pass,
// which runs only while the native grid exists, and the guest's full screen fade quad is drawn after it.
struct RosterCellInputs {
  bool akuma = false;
  bool evil_ryu = false;
  bool violent_ken = false;
  uint8_t live1 = 0;
  uint8_t live2 = 0;
  std::array<bool, 2> on_evil_ryu{};
  std::array<bool, 2> on_violent_ken{};
  std::array<bool, 2> guest_name_drawn{};
};

struct RosterCellPlan {
  bool akuma = false;
  bool evil_ryu = false;
  bool violent_ken = false;
  std::array<std::optional<RosterArt>, 2> names{};

  bool any() const { return akuma || evil_ryu || violent_ken || names[0].has_value() || names[1].has_value(); }
};

inline RosterCellPlan PlanRosterCells(const RosterCellInputs& in) {
  RosterCellPlan plan;
  plan.akuma = AkumaPortraitVisible(in.akuma, true, in.live1, in.live2);
  plan.evil_ryu = in.evil_ryu;
  plan.violent_ken = in.violent_ken;
  if (!in.evil_ryu && !in.violent_ken) return plan;
  for (size_t side = 0; side < 2; ++side) {
    if (in.guest_name_drawn[side]) continue;
    if (in.on_violent_ken[side]) {
      plan.names[side] = RosterArt::kViolentKenName;
    } else if (in.on_evil_ryu[side]) {
      plan.names[side] = RosterArt::kEvilRyuName;
    }
  }
  return plan;
}

}  // namespace ssf2t::character
