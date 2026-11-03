#include "character/akuma_portrait.h"

#include <algorithm>
#include <cstring>
#include <fstream>

#include "updater/third_party/miniz/miniz.h"

namespace ssf2t::character {
namespace {

constexpr char kMagic[] = "SG28XT03";
constexpr size_t kDataOffset = 0x2D;
constexpr size_t kDataSize = size_t(kPortraitSize) * kPortraitSize;
constexpr char kPortraitPath[] = "TEXTURES/PORTRAITS/AKUMA_SMALL.TEXTURE.X360";
constexpr std::streamoff kTableOffset = 0x800;
constexpr size_t kEntrySize = 16;
constexpr size_t kMaxEntries = 1 << 16;

uint32_t ReadLE32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }

void Rgb565(uint16_t color, uint8_t out[3]) {
  const int r = (color >> 11) & 31;
  const int g = (color >> 5) & 63;
  const int b = color & 31;
  out[0] = uint8_t((r << 3) | (r >> 2));
  out[1] = uint8_t((g << 2) | (g >> 4));
  out[2] = uint8_t((b << 3) | (b >> 2));
}

void DecodeAlpha(const uint8_t* block, uint8_t alpha[8]) {
  const int a0 = block[0];
  const int a1 = block[1];
  alpha[0] = uint8_t(a0);
  alpha[1] = uint8_t(a1);
  if (a0 > a1) {
    for (int i = 1; i < 7; ++i) alpha[i + 1] = uint8_t(((7 - i) * a0 + i * a1) / 7);
    return;
  }
  for (int i = 1; i < 5; ++i) alpha[i + 1] = uint8_t(((5 - i) * a0 + i * a1) / 5);
  alpha[6] = 0;
  alpha[7] = 255;
}

// Xbox 360 stores every 16-bit word of a DXT5 block byte-swapped.
void DecodeBlock(const uint8_t* source, uint8_t* rgba, int stride) {
  uint8_t block[16];
  for (int i = 0; i < 16; i += 2) {
    block[i] = source[i + 1];
    block[i + 1] = source[i];
  }
  uint8_t alpha[8];
  DecodeAlpha(block, alpha);
  uint64_t alpha_bits = 0;
  for (int i = 0; i < 6; ++i) alpha_bits |= uint64_t(block[2 + i]) << (8 * i);
  uint8_t palette[4][3];
  Rgb565(uint16_t(block[8] | (block[9] << 8)), palette[0]);
  Rgb565(uint16_t(block[10] | (block[11] << 8)), palette[1]);
  for (int c = 0; c < 3; ++c) {
    palette[2][c] = uint8_t((2 * palette[0][c] + palette[1][c]) / 3);
    palette[3][c] = uint8_t((palette[0][c] + 2 * palette[1][c]) / 3);
  }
  const uint32_t indices = ReadLE32(block + 12);
  for (int i = 0; i < 16; ++i) {
    uint8_t* pixel = rgba + (i / 4) * stride + (i % 4) * 4;
    std::memcpy(pixel, palette[(indices >> (2 * i)) & 3], 3);
    pixel[3] = alpha[(alpha_bits >> (3 * i)) & 7];
  }
}

bool ReadEntry(std::ifstream& in, uint32_t key, uint32_t& offset, uint32_t& packed, uint32_t& size) {
  uint8_t header[8];
  in.seekg(0);
  if (!in.read(reinterpret_cast<char*>(header), sizeof(header))) return false;
  const uint32_t count = ReadLE32(header + 4);
  if (count == 0 || count > kMaxEntries) return false;
  std::vector<uint8_t> table(size_t(count) * kEntrySize);
  in.seekg(kTableOffset);
  if (!in.read(reinterpret_cast<char*>(table.data()), std::streamsize(table.size()))) return false;
  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t* entry = table.data() + size_t(i) * kEntrySize;
    if (ReadLE32(entry) != key) continue;
    offset = ReadLE32(entry + 4);
    packed = ReadLE32(entry + 8);
    size = ReadLE32(entry + 12);
    return true;
  }
  return false;
}

}  // namespace

PortraitImage DecodePortraitTexture(const uint8_t* file, size_t size) {
  PortraitImage image;
  if (size < kDataOffset + kDataSize || std::memcmp(file, kMagic, 8) != 0) {
    image.error = "decode";
    return image;
  }
  const int stride = kPortraitSize * 4;
  image.width = image.height = kPortraitSize;
  image.rgba.assign(size_t(stride) * kPortraitSize, 0);
  const uint8_t* data = file + kDataOffset;
  for (int by = 0; by < kPortraitSize / 4; ++by) {
    for (int bx = 0; bx < kPortraitSize / 4; ++bx) {
      DecodeBlock(data + (size_t(by) * (kPortraitSize / 4) + bx) * 16,
                  image.rgba.data() + size_t(by) * 4 * stride + size_t(bx) * 16, stride);
    }
  }
  return image;
}

PortraitImage LoadAkumaPortrait(const std::filesystem::path& archive) {
  PortraitImage failed;
  std::ifstream in(archive, std::ios::binary);
  if (!in) {
    failed.error = "open archive";
    return failed;
  }
  uint32_t offset = 0;
  uint32_t packed = 0;
  uint32_t size = 0;
  const uint32_t key = uint32_t(mz_crc32(0, reinterpret_cast<const unsigned char*>(kPortraitPath), sizeof(kPortraitPath) - 1));
  if (!ReadEntry(in, key, offset, packed, size) || size > (1u << 20) || packed > (1u << 20)) {
    failed.error = "find entry";
    return failed;
  }
  std::vector<uint8_t> stored(packed);
  in.seekg(offset);
  if (!in.read(reinterpret_cast<char*>(stored.data()), std::streamsize(packed))) {
    failed.error = "read entry";
    return failed;
  }
  if (packed == size) return DecodePortraitTexture(stored.data(), stored.size());
  std::vector<uint8_t> file(size);
  mz_ulong inflated = size;
  if (mz_uncompress(file.data(), &inflated, stored.data(), mz_ulong(packed)) != MZ_OK || inflated != size) {
    failed.error = "decompress";
    return failed;
  }
  return DecodePortraitTexture(file.data(), file.size());
}

}  // namespace ssf2t::character
