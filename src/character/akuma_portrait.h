#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace ssf2t::character {

struct PortraitImage {
  const char* error = nullptr;
  int width = 0;
  int height = 0;
  std::vector<uint8_t> rgba;
};

inline constexpr int kPortraitSize = 128;

PortraitImage DecodePortraitTexture(const uint8_t* file, size_t size);
PortraitImage LoadAkumaPortrait(const std::filesystem::path& archive);

inline constexpr uint8_t kAkumaLiveSelect = 32;

inline bool GuestShowsAkuma(uint8_t live1, uint8_t live2) {
  return live1 == kAkumaLiveSelect || live2 == kAkumaLiveSelect;
}

inline bool AkumaPortraitVisible(bool enabled, bool select_active, uint8_t live1, uint8_t live2) {
  return enabled && select_active && !GuestShowsAkuma(live1, live2);
}

struct ViewRect {
  float x = 0;
  float y = 0;
  float width = 0;
  float height = 0;
};

inline ViewRect GuestView(float display_width, float display_height, float guest_aspect, bool letterbox) {
  if (!letterbox || display_width <= 0 || display_height <= 0) return {0, 0, display_width, display_height};
  const float width = display_height * guest_aspect < display_width ? display_height * guest_aspect : display_width;
  const float height = width / guest_aspect;
  return {(display_width - width) * 0.5f, (display_height - height) * 0.5f, width, height};
}

// Fixed cell in the 1280x720 guest picture, matching the guest's Akuma cell.
inline constexpr float kPortraitCenterX = 640.0f;
inline constexpr float kPortraitCenterY = 360.0f;
inline constexpr float kPortraitWidth = 80.0f;
inline constexpr float kPortraitHeight = 92.0f;

inline ViewRect AkumaPortraitRect(const ViewRect& view, float guest_width) {
  const float scale = view.height / 720.0f;
  const float width = kPortraitWidth * scale;
  const float height = kPortraitHeight * scale;
  const float center_x = view.x + ((guest_width - 1280.0f) * 0.5f + kPortraitCenterX) * scale;
  const float center_y = view.y + kPortraitCenterY * scale;
  return {center_x - width * 0.5f, center_y - height * 0.5f, width, height};
}

}  // namespace ssf2t::character
