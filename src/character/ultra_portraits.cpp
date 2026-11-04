#include "character/ultra_portraits.h"

#include <cstring>
#include <fstream>
#include <vector>

namespace ssf2t::character {
namespace {

constexpr char kMagic[] = "SSF2UPT1";
constexpr size_t kMagicSize = 8;
constexpr size_t kHeaderSize = kMagicSize + 4;
constexpr size_t kRecordHeaderSize = 12;
constexpr uint32_t kMaxRecords = 16;
constexpr size_t kPixelBytes = size_t(kPortraitSize) * kPortraitSize * 4;
constexpr std::streamoff kMaxFileSize = 1 << 20;

uint32_t ReadLE32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }

}  // namespace

PortraitImage ParseUltraPortrait(const uint8_t* file, size_t size, UltraPortrait which) {
  PortraitImage image;
  if (size < kHeaderSize || std::memcmp(file, kMagic, kMagicSize) != 0) {
    image.error = "decode";
    return image;
  }
  const uint32_t count = ReadLE32(file + kMagicSize);
  if (count > kMaxRecords) {
    image.error = "decode";
    return image;
  }
  size_t at = kHeaderSize;
  for (uint32_t i = 0; i < count; ++i) {
    if (size - at < kRecordHeaderSize) break;
    const uint32_t id = ReadLE32(file + at);
    const uint32_t width = ReadLE32(file + at + 4);
    const uint32_t height = ReadLE32(file + at + 8);
    at += kRecordHeaderSize;
    if (width != uint32_t(kPortraitSize) || height != uint32_t(kPortraitSize) || size - at < kPixelBytes) break;
    if (id == uint32_t(which)) {
      image.width = kPortraitSize;
      image.height = kPortraitSize;
      image.rgba.assign(file + at, file + at + kPixelBytes);
      return image;
    }
    at += kPixelBytes;
  }
  image.error = "find entry";
  return image;
}

PortraitImage LoadUltraPortrait(const std::filesystem::path& file, UltraPortrait which) {
  PortraitImage failed;
  std::ifstream in(file, std::ios::binary | std::ios::ate);
  if (!in) {
    failed.error = "open file";
    return failed;
  }
  const std::streamoff size = in.tellg();
  if (size <= 0 || size > kMaxFileSize) {
    failed.error = "read file";
    return failed;
  }
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  in.seekg(0);
  if (!in.read(reinterpret_cast<char*>(bytes.data()), size)) {
    failed.error = "read file";
    return failed;
  }
  return ParseUltraPortrait(bytes.data(), bytes.size(), which);
}

void ApplyRosterFrame(PortraitImage& art, const PortraitImage& frame) {
  if (art.error || frame.error || art.width != frame.width || art.height != frame.height ||
      art.rgba.size() != frame.rgba.size() || art.rgba.size() != size_t(art.width) * art.height * 4) {
    return;
  }
  for (int y = 0; y < art.height; ++y) {
    for (int x = 0; x < art.width; ++x) {
      const bool band = x < kUltraFrameBand || y < kUltraFrameBand || x >= art.width - kUltraFrameBand ||
                        y >= art.height - kUltraFrameBand;
      if (!band) continue;
      const size_t at = (size_t(y) * art.width + x) * 4;
      std::memcpy(&art.rgba[at], &frame.rgba[at], 4);
    }
  }
}

}  // namespace ssf2t::character
