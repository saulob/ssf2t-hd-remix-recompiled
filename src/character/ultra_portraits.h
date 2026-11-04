#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

#include "character/akuma_navigation.h"
#include "character/akuma_portrait.h"

namespace ssf2t::character {

enum class UltraPortrait : uint8_t { kEvilRyu = 0, kViolentKen = 1 };

inline constexpr int kUltraPortraitCount = 2;
inline constexpr char kUltraPortraitFile[] = "ultra_portraits.bin";
inline constexpr int kUltraFrameBand = 6;

// Roster grid: 32 arcade px per cell, arcade -> canvas (19/12, 2), canvas -> 1280x720 picture (14/9, 3/2).
// Rows are counted down from Akuma's cell; the roster rows are 2 and 3. Columns are centred on 3.5.
inline constexpr float kRosterColumnPitch = 32.0f * (19.0f / 12.0f) * (14.0f / 9.0f);
inline constexpr float kRosterRowPitch = 32.0f * 2.0f * 1.5f;
inline constexpr float kRosterCenterColumn = 3.5f;

// The column right of the grid's last: eight columns in the arcade grid, six in the versus grid.
constexpr float UltraColumn(SelectLayout layout) { return layout == SelectLayout::kArcade ? 8.0f : 7.0f; }

constexpr float UltraCenterX(SelectLayout layout) {
  return kPortraitCenterX + (UltraColumn(layout) - kRosterCenterColumn) * kRosterColumnPitch;
}

inline constexpr float kUltraCenterX = UltraCenterX(SelectLayout::kArcade);
inline constexpr float kUltraCellWidth = kPortraitWidth;
inline constexpr float kUltraCellHeight = kPortraitHeight;

inline constexpr int UltraPortraitRow(UltraPortrait which) { return which == UltraPortrait::kEvilRyu ? 2 : 3; }

inline constexpr float UltraPortraitCenterY(UltraPortrait which) {
  return kPortraitCenterY + float(UltraPortraitRow(which)) * kRosterRowPitch;
}

inline ViewRect UltraPortraitRect(const ViewRect& view, float guest_width, UltraPortrait which,
                                  SelectLayout layout = SelectLayout::kArcade) {
  const float scale = view.height / 720.0f;
  const float width = kUltraCellWidth * scale;
  const float height = kUltraCellHeight * scale;
  const float center_x = view.x + ((guest_width - 1280.0f) * 0.5f + UltraCenterX(layout)) * scale;
  const float center_y = view.y + UltraPortraitCenterY(which) * scale;
  return {center_x - width * 0.5f, center_y - height * 0.5f, width, height};
}

inline bool UltraPortraitVisible(bool enabled, bool select_active, bool renderer_ready, bool texture_ready) {
  return enabled && select_active && renderer_ready && texture_ready;
}

PortraitImage ParseUltraPortrait(const uint8_t* file, size_t size, UltraPortrait which);
PortraitImage LoadUltraPortrait(const std::filesystem::path& file, UltraPortrait which);

// Copies the original roster frame over the outer band of the art.
void ApplyRosterFrame(PortraitImage& art, const PortraitImage& frame);

}  // namespace ssf2t::character
