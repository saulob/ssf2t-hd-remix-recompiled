#include "character/evil_ryu_assets.h"

#include <cstring>
#include <fstream>
#include <vector>

namespace ssf2t::character {
namespace {

constexpr char kMagic[] = "SSF2ERA1";
constexpr size_t kMagicSize = 8;
constexpr size_t kHeaderSize = kMagicSize + 4;
constexpr size_t kRecordHeaderSize = 12;
constexpr uint32_t kMaxRecords = 16;
constexpr uint32_t kMaxDimension = 2048;
constexpr std::streamoff kMaxFileSize = 16 << 20;

uint32_t ReadLE32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }

}  // namespace

PortraitImage ParseEvilRyuAsset(const uint8_t* file, size_t size, EvilRyuAsset which) {
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
    if (width == 0 || height == 0 || width > kMaxDimension || height > kMaxDimension) break;
    const size_t bytes = size_t(width) * height * 4;
    if (size - at < bytes) break;
    if (id == uint32_t(which)) {
      image.width = int(width);
      image.height = int(height);
      image.rgba.assign(file + at, file + at + bytes);
      return image;
    }
    at += bytes;
  }
  image.error = "find entry";
  return image;
}

PortraitImage LoadEvilRyuAsset(const std::filesystem::path& file, EvilRyuAsset which) {
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
  return ParseEvilRyuAsset(bytes.data(), bytes.size(), which);
}

}  // namespace ssf2t::character
