#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

#include "character/evil_ryu_actions.h"

namespace ssf2t::character {

inline constexpr char kEvilRyuSpriteFile[] = "evil_ryu_sprites.bin";
inline constexpr int kEvilPaletteBytes = 1024;

enum class EvilArt : uint8_t { kBackTurned, kPowerStance, kLargePortrait, kName, kHudName };
inline constexpr int kEvilArtCount = 5;

// 8 bit palette indices, width * height, top row first; index 0 is transparent. Fight sprites have no palette of their
// own and use the fighter's.
struct EvilSpriteImage {
  int width = 0;
  int height = 0;
  bool has_palette = false;
  std::array<uint8_t, kEvilPaletteBytes> palette{};
  std::vector<uint8_t> indices;
};

struct EvilSprites {
  std::array<EvilSpriteImage, kEvilArtCount> images;
  const EvilSpriteImage& Get(EvilArt art) const { return images[size_t(art)]; }
};

constexpr EvilArt ArtFor(EvilPose pose) {
  return pose == EvilPose::kBackTurned ? EvilArt::kBackTurned : EvilArt::kPowerStance;
}

// Guest indexed textures are locked at a pitch of their own, not their width. Large portraits are stored facing left
// because the guest mirrors them for P1; USF2's art faces right, hence `mirrored`.
void WriteIndexedRows(uint8_t* target, uint32_t pitch, const EvilSpriteImage& image, bool mirrored);

// Clears each A,R,G,B entry whose colour is the 0xFF00FF key, as the guest does to every palette it loads. Without this
// the key is drawn as opaque magenta.
void ClearPaletteColourKey(uint8_t* palette, size_t bytes);

bool ParseEvilSprites(const uint8_t* file, size_t size, EvilSprites* out);
void SetEvilSpriteDirectory(const std::filesystem::path& directory);
const EvilSpriteImage* LoadedEvilArt(EvilArt art);

inline constexpr char kEvilRyuHudIconFile[] = "evil_ryu_hud_icon.bin";
inline constexpr char kEvilRyuHudName[] = "Evil Ryu";
inline constexpr int kHudIconSize = 128;
inline constexpr size_t kHudIconBytes = size_t(kHudIconSize) * kHudIconSize * 4;

// In the byte order of the native HUD_Icon textures: A,R,G,B, top row first, facing left.
using EvilHudIcon = std::array<uint8_t, kHudIconBytes>;

bool ParseEvilHudIcon(const uint8_t* file, size_t size, EvilHudIcon* out);
const EvilHudIcon* LoadedEvilHudIcon();

// Screen set element names are looked up by the CRC-32 of the upper case name.
constexpr uint32_t ElementHash(std::string_view name) {
  uint32_t crc = 0xFFFFFFFFu;
  for (const char raw : name) {
    const char c = raw >= 'a' && raw <= 'z' ? char(raw - 'a' + 'A') : raw;
    crc ^= uint8_t(c);
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
  }
  return ~crc;
}

// A portrait element holds one texture cell. Pointing the element at the Evil Ryu cell changes that element alone, so
// the other side's Ryu keeps his icon. Returns the value for the element; `saved` keeps its own cell for the way back.
constexpr uint32_t RoutePortraitCell(bool evil, uint32_t current, uint32_t evil_cell, uint32_t& saved) {
  if (evil) {
    if (current != evil_cell) saved = current;
    return evil_cell;
  }
  return current == evil_cell ? saved : current;
}

// The guest drew the Evil Ryu name for `side`; the host fallback stays out of the way while that keeps happening.
void NoteGuestNameDrawn(int side);
bool GuestNameDrawnRecently(int side);

}  // namespace ssf2t::character
