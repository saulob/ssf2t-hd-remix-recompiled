#include "character/evil_ryu_palette.h"

#include <cstring>
#include <fstream>
#include <mutex>

namespace ssf2t::character {
namespace {

constexpr char kMagic[] = "SSF2ERP1";
constexpr size_t kMagicSize = 8;
constexpr size_t kHeaderSize = kMagicSize + 4;
constexpr char kHdMagic[] = "SSF2ERH1";
constexpr size_t kRecordBytes = 2 * kPaletteWords * 2;

uint16_t ReadLE16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

std::mutex g_mutex;
EvilRyuHdPalette g_hd_palette;
bool g_hd_attempted = false;
bool g_hd_loaded = false;
std::filesystem::path g_directory;
EvilRyuPalette g_palette;
bool g_attempted = false;
bool g_loaded = false;

}  // namespace

bool ParseEvilRyuPalette(const uint8_t* file, size_t size, EvilRyuPalette* out) {
  if (size != kHeaderSize + kRecordBytes * kPaletteColours || std::memcmp(file, kMagic, kMagicSize) != 0) {
    return false;
  }
  const uint32_t count = file[kMagicSize] | (file[kMagicSize + 1] << 8) | (file[kMagicSize + 2] << 16) |
                         (uint32_t(file[kMagicSize + 3]) << 24);
  if (count != uint32_t(kPaletteColours)) return false;
  const uint8_t* at = file + kHeaderSize;
  for (int colour = 0; colour < kPaletteColours; ++colour) {
    for (int word = 0; word < kPaletteWords; ++word, at += 2) out->ryu[colour][word] = ReadLE16(at);
    for (int word = 0; word < kPaletteWords; ++word, at += 2) out->evil[colour][word] = ReadLE16(at);
  }
  return true;
}

bool LoadEvilRyuPalette(const std::filesystem::path& file, EvilRyuPalette* out) {
  std::ifstream in(file, std::ios::binary | std::ios::ate);
  if (!in) return false;
  const std::streamoff size = in.tellg();
  if (size <= 0 || size > 4096) return false;
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  in.seekg(0);
  if (!in.read(reinterpret_cast<char*>(bytes.data()), size)) return false;
  return ParseEvilRyuPalette(bytes.data(), bytes.size(), out);
}

bool ParseEvilRyuHdPalette(const uint8_t* file, size_t size, EvilRyuHdPalette* out) {
  if (size != kHeaderSize + size_t(kHdPaletteBytes) * kHdPaletteEntries || std::memcmp(file, kHdMagic, kMagicSize) != 0) {
    return false;
  }
  const uint32_t count = file[kMagicSize] | (file[kMagicSize + 1] << 8) | (file[kMagicSize + 2] << 16) |
                         (uint32_t(file[kMagicSize + 3]) << 24);
  if (count != uint32_t(kHdPaletteEntries)) return false;
  for (int entry = 0; entry < kHdPaletteEntries; ++entry) {
    std::memcpy(out->colours[entry].data(), file + kHeaderSize + size_t(entry) * kHdPaletteBytes, kHdPaletteBytes);
  }
  return true;
}

void SetEvilRyuPaletteDirectory(const std::filesystem::path& directory) {
  std::lock_guard lock(g_mutex);
  g_directory = directory;
}

const EvilRyuPalette* LoadedEvilRyuPalette() {
  std::lock_guard lock(g_mutex);
  if (!g_attempted) {
    g_attempted = true;
    g_loaded = LoadEvilRyuPalette(g_directory / kEvilRyuPaletteFile, &g_palette);
  }
  return g_loaded ? &g_palette : nullptr;
}

const EvilRyuHdPalette* LoadedEvilRyuHdPalette() {
  std::lock_guard lock(g_mutex);
  if (!g_hd_attempted) {
    g_hd_attempted = true;
    std::ifstream in(g_directory / kEvilRyuHdPaletteFile, std::ios::binary | std::ios::ate);
    const std::streamoff size = in ? std::streamoff(in.tellg()) : 0;
    if (size > 0 && size <= 16384) {
      std::vector<uint8_t> bytes(static_cast<size_t>(size));
      in.seekg(0);
      g_hd_loaded = in.read(reinterpret_cast<char*>(bytes.data()), size) &&
                    ParseEvilRyuHdPalette(bytes.data(), bytes.size(), &g_hd_palette);
    }
  }
  return g_hd_loaded ? &g_hd_palette : nullptr;
}

int MatchRyuColour(const EvilRyuPalette& palette, const uint16_t* words) {
  for (int colour = 0; colour < kPaletteColours; ++colour) {
    bool same = true;
    for (int word = 1; word < kPaletteWords - 1 && same; ++word) same = palette.ryu[colour][word] == words[word];
    if (same) return colour;
  }
  return -1;
}

}  // namespace ssf2t::character
