#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "character/evil_ryu_sprites.h"

namespace ssf2t::character {

// The Character Select brackets are HD art with 1P or 2P drawn into the texture, one texture per blink colour.
struct CpuMarkerArt {
  std::string_view texture;
  uint16_t tile;
  uint16_t colour;
  int side;
  int label_top;
  int label_bottom;
};

inline constexpr std::array<CpuMarkerArt, 4> kCpuMarkerArt = {{
    {"1p_selector_white", 0x3361, 26, 0, 0, 56},
    {"1p_selector_red", 0x3361, 10, 0, 0, 56},
    {"2P_bracket_gray", 0x335A, 26, 1, 158, 216},
    {"2P_bracket_yellow", 0x335A, 10, 1, 158, 216},
}};

constexpr int CpuMarkerIndex(uint16_t tile, uint16_t colour) {
  for (size_t i = 0; i < kCpuMarkerArt.size(); ++i) {
    if (kCpuMarkerArt[i].tile == tile && kCpuMarkerArt[i].colour == colour) return int(i);
  }
  return -1;
}

constexpr bool CpuMarkerOnSide(int index, int cpu_side) {
  return index >= 0 && kCpuMarkerArt[size_t(index)].side == cpu_side;
}

std::string_view TextureFileName(const uint8_t* file, size_t size);
bool BuildCpuMarker(const uint8_t* file, size_t size, const CpuMarkerArt& art, EvilSpriteImage* out);

// The guest parses every texture file once; the brackets are kept so the labelled copies can be built from them.
void CaptureCpuMarkerTexture(const uint8_t* file, size_t size);
const EvilSpriteImage* LoadedCpuMarker(int index);

}  // namespace ssf2t::character
