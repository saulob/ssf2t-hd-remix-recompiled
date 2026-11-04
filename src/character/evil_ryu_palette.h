#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace ssf2t::character {

inline constexpr char kEvilRyuPaletteFile[] = "evil_ryu_palette.bin";
inline constexpr int kPaletteWords = 16;
inline constexpr int kPaletteColours = 10;

struct EvilRyuPalette {
  std::array<std::array<uint16_t, kPaletteWords>, kPaletteColours> ryu{};
  std::array<std::array<uint16_t, kPaletteWords>, kPaletteColours> evil{};
};

bool ParseEvilRyuPalette(const uint8_t* file, size_t size, EvilRyuPalette* out);
bool LoadEvilRyuPalette(const std::filesystem::path& file, EvilRyuPalette* out);

inline constexpr char kEvilRyuHdPaletteFile[] = "evil_ryu_hd_palette.bin";
inline constexpr int kHdPaletteBytes = 1024;
inline constexpr int kHdPaletteMirrorEntry = kPaletteColours;
inline constexpr int kHdPaletteEntries = kPaletteColours + 1;

// One 1024-byte palette per HD colour byte, then the second player's palette in a mirror match, in the guest's
// A,B,G,R texture palette byte order.
struct EvilRyuHdPalette {
  std::array<std::array<uint8_t, kHdPaletteBytes>, kHdPaletteEntries> colours{};
};

constexpr int HdPaletteEntry(int colour, int side, bool both_evil) {
  return both_evil && side == 1 ? kHdPaletteMirrorEntry : colour;
}

bool ParseEvilRyuHdPalette(const uint8_t* file, size_t size, EvilRyuHdPalette* out);
const EvilRyuHdPalette* LoadedEvilRyuHdPalette();

// Directory holding evil_ryu_palette.bin; the palette is loaded on first use.
void SetEvilRyuPaletteDirectory(const std::filesystem::path& directory);
const EvilRyuPalette* LoadedEvilRyuPalette();

// Index of the Ryu colour whose body words equal `words` (pens 1..14), or -1.
int MatchRyuColour(const EvilRyuPalette& palette, const uint16_t* words);

}  // namespace ssf2t::character
