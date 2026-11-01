#include "localization/baked_texture.h"

#include <cstring>
#include <string>

#include "localization/game_text.h"
#include "updater/third_party/miniz/miniz.h"

namespace ssf2t::localization {
namespace {

constexpr char kMagic[] = "SG28XT03";
constexpr size_t kNameOffset = 20;

}  // namespace

std::string_view BakedTextureName(const uint8_t* data, size_t size) {
  if (size <= kNameOffset || std::memcmp(data, kMagic, 8) != 0) return {};
  const char* name = reinterpret_cast<const char*>(data) + kNameOffset;
  const void* end = std::memchr(name, '\0', size - kNameOffset);
  return end ? std::string_view(name, size_t(static_cast<const char*>(end) - name)) : std::string_view();
}

bool InflateBakedTexture(const BakedTexture& texture, std::vector<uint8_t>& out) {
  out.assign(texture.size, 0);
  mz_ulong length = texture.size;
  return mz_uncompress(out.data(), &length, texture.deflated, texture.deflated_size) == MZ_OK && length == texture.size;
}

const BakedTexture* LocalizeBakedTexture(uint8_t* data, size_t size) {
  const std::string_view name = BakedTextureName(data, size);
  if (name.empty()) return nullptr;
  for (const BakedTexture& row : PortugueseBrazilBakedTextures()) {
    if (name != row.name || size != row.size) continue;
    if (Crc32(std::string_view(reinterpret_cast<const char*>(data), size)) != row.english_crc) return nullptr;
    std::vector<uint8_t> replacement;
    if (!InflateBakedTexture(row, replacement)) return nullptr;
    std::memcpy(data, replacement.data(), size);
    return &row;
  }
  return nullptr;
}

}  // namespace ssf2t::localization
