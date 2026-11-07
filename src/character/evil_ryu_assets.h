#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

#include "character/akuma_portrait.h"

namespace ssf2t::character {

enum class EvilRyuAsset : uint32_t { kLargePortrait = 1, kNameGraphic = 2 };

inline constexpr char kEvilRyuAssetFile[] = "evil_ryu_assets.bin";
inline constexpr char kViolentKenAssetFile[] = "violent_ken_assets.bin";

PortraitImage ParseEvilRyuAsset(const uint8_t* file, size_t size, EvilRyuAsset which);
PortraitImage LoadEvilRyuAsset(const std::filesystem::path& file, EvilRyuAsset which);

// Arcade units of the guest large-portrait quad (mode 0 HD grid) mapped to the 1280x720 guest picture.
inline constexpr float kArcadeToPictureX = (19.0f / 12.0f) * (14.0f / 9.0f);
inline constexpr float kArcadeToPictureY = 3.0f;
inline constexpr float kLargePortraitArcadeWidth = 96.0f;
inline constexpr float kLargePortraitArcadeHeight = 96.0f;
inline constexpr float kLargePortraitArcadeBottomY = 112.0f;
inline constexpr float kLargePortraitArcadeX[2] = {0.0f, 288.0f};

inline ViewRect LargePortraitPicture(int side) {
  const float x = 640.0f + (kLargePortraitArcadeX[side] - 192.0f) * kArcadeToPictureX;
  const float y = 360.0f - (kLargePortraitArcadeBottomY + kLargePortraitArcadeHeight - 112.0f) * kArcadeToPictureY;
  return {x, y, kLargePortraitArcadeWidth * kArcadeToPictureX, kLargePortraitArcadeHeight * kArcadeToPictureY};
}

inline ViewRect ToView(const ViewRect& view, float guest_width, const ViewRect& picture) {
  const float scale = view.height / 720.0f;
  return {view.x + ((guest_width - 1280.0f) * 0.5f + picture.x) * scale, view.y + picture.y * scale,
          picture.width * scale, picture.height * scale};
}

}  // namespace ssf2t::character
