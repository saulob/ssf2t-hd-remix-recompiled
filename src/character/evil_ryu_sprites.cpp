#include "character/evil_ryu_sprites.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <mutex>

namespace ssf2t::character {
namespace {

constexpr char kMagic[] = "SSF2ERS2";
constexpr char kHudIconMagic[] = "SSF2ERI1";
constexpr size_t kMagicSize = 8;
constexpr size_t kHeaderSize = kMagicSize + 4;
constexpr size_t kRecordHeaderSize = 16;
constexpr uint32_t kMaxRecords = 8;
constexpr uint32_t kMaxDimension = 2048;
constexpr std::streamoff kMaxFileSize = 8 << 20;
constexpr int64_t kGuestNameWindowMilliseconds = 250;

uint32_t ReadLE32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }

int ArtIndex(uint32_t id) {
  switch (id) {
    case 224: return int(EvilArt::kBackTurned);
    case 225: return int(EvilArt::kPowerStance);
    case 1: return int(EvilArt::kLargePortrait);
    case 2: return int(EvilArt::kName);
    case 3: return int(EvilArt::kHudName);
    case 4: return int(EvilArt::kLosePortrait);
    default: return -1;
  }
}

std::mutex g_mutex;
std::filesystem::path g_directory;
EvilSprites g_sprites;
bool g_attempted = false;
bool g_loaded = false;
std::array<std::atomic<int64_t>, 2> g_name_drawn_ms{};
EvilHudIcon g_icon{};
bool g_icon_attempted = false;
bool g_icon_loaded = false;

bool LoadSprites() {
  std::ifstream in(g_directory / kEvilRyuSpriteFile, std::ios::binary | std::ios::ate);
  if (!in) return false;
  const std::streamoff size = in.tellg();
  if (size <= 0 || size > kMaxFileSize) return false;
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  in.seekg(0);
  return in.read(reinterpret_cast<char*>(bytes.data()), size) &&
         ParseEvilSprites(bytes.data(), bytes.size(), &g_sprites);
}

int64_t NowMilliseconds() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

}  // namespace

void WriteIndexedRows(uint8_t* target, uint32_t pitch, const EvilSpriteImage& image, bool mirrored) {
  const size_t width = size_t(image.width);
  for (size_t y = 0; y < size_t(image.height); ++y) {
    const uint8_t* source = image.indices.data() + y * width;
    uint8_t* row = target + y * pitch;
    if (mirrored) {
      std::reverse_copy(source, source + width, row);
    } else {
      std::memcpy(row, source, width);
    }
  }
}

void ClearPaletteColourKey(uint8_t* palette, size_t bytes) {
  for (size_t at = 0; at + 4 <= bytes; at += 4) {
    uint8_t* entry = palette + at;
    if (entry[1] == 0xFF && entry[2] == 0x00 && entry[3] == 0xFF) std::memset(entry, 0, 4);
  }
}

bool ParseEvilSprites(const uint8_t* file, size_t size, EvilSprites* out) {
  if (size < kHeaderSize || std::memcmp(file, kMagic, kMagicSize) != 0) return false;
  const uint32_t count = ReadLE32(file + kMagicSize);
  if (count > kMaxRecords) return false;
  size_t at = kHeaderSize;
  for (uint32_t i = 0; i < count; ++i) {
    if (size - at < kRecordHeaderSize) return false;
    const uint32_t id = ReadLE32(file + at);
    const uint32_t width = ReadLE32(file + at + 4);
    const uint32_t height = ReadLE32(file + at + 8);
    const bool has_palette = ReadLE32(file + at + 12) != 0;
    at += kRecordHeaderSize;
    if (width == 0 || height == 0 || width > kMaxDimension || height > kMaxDimension) return false;
    const size_t bytes = size_t(width) * height + (has_palette ? kEvilPaletteBytes : 0);
    if (size - at < bytes) return false;
    const int index = ArtIndex(id);
    if (index >= 0) {
      EvilSpriteImage& image = out->images[size_t(index)];
      image.width = int(width);
      image.height = int(height);
      image.has_palette = has_palette;
      const uint8_t* pixels = file + at;
      if (has_palette) {
        std::memcpy(image.palette.data(), pixels, kEvilPaletteBytes);
        pixels += kEvilPaletteBytes;
      }
      image.indices.assign(pixels, pixels + size_t(width) * height);
    }
    at += bytes;
  }
  // A package without the beaten portrait still loads; the large portrait stands in.
  for (int art = 0; art < kEvilArtCount; ++art) {
    if (EvilArt(art) != EvilArt::kLosePortrait && out->images[size_t(art)].indices.empty()) return false;
  }
  return true;
}

void SetEvilSpriteDirectory(const std::filesystem::path& directory) {
  std::lock_guard lock(g_mutex);
  g_directory = directory;
}

const EvilSpriteImage* LoadedEvilArt(EvilArt art) {
  std::lock_guard lock(g_mutex);
  if (!g_attempted) {
    g_attempted = true;
    g_loaded = LoadSprites();
  }
  return g_loaded ? &g_sprites.Get(art) : nullptr;
}

bool ParseEvilHudIcon(const uint8_t* file, size_t size, EvilHudIcon* out) {
  if (size != kMagicSize + kHudIconBytes || std::memcmp(file, kHudIconMagic, kMagicSize) != 0) return false;
  std::memcpy(out->data(), file + kMagicSize, kHudIconBytes);
  return true;
}

const EvilHudIcon* LoadedEvilHudIcon() {
  std::lock_guard lock(g_mutex);
  if (!g_icon_attempted) {
    g_icon_attempted = true;
    std::ifstream in(g_directory / kEvilRyuHudIconFile, std::ios::binary);
    std::vector<uint8_t> bytes(kMagicSize + kHudIconBytes);
    g_icon_loaded = in.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())) &&
                    in.peek() == std::ifstream::traits_type::eof() &&
                    ParseEvilHudIcon(bytes.data(), bytes.size(), &g_icon);
  }
  return g_icon_loaded ? &g_icon : nullptr;
}

void NoteGuestNameDrawn(int side) {
  if (side >= 0 && side < 2) g_name_drawn_ms[size_t(side)].store(NowMilliseconds(), std::memory_order_relaxed);
}

bool GuestNameDrawnRecently(int side) {
  if (side < 0 || side > 1) return false;
  const int64_t last = g_name_drawn_ms[size_t(side)].load(std::memory_order_relaxed);
  return last != 0 && NowMilliseconds() - last < kGuestNameWindowMilliseconds;
}

}  // namespace ssf2t::character
