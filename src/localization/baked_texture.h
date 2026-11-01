#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace ssf2t::localization {

// A game texture with text drawn into it, redrawn in Português Brasil with the same container, size and pixel format.
// `english_crc` pins the replacement to the stock texture; `deflated` inflates to `size` bytes, the whole texture file.
struct BakedTexture {
  const char* name;
  const char* english_text;
  const char* text;
  uint32_t english_crc;
  uint32_t size;
  const uint8_t* deflated;
  uint32_t deflated_size;
};

std::span<const BakedTexture> PortugueseBrazilBakedTextures();

// The texture name stored in an SG28XT03 file; empty when `data` is not one.
std::string_view BakedTextureName(const uint8_t* data, size_t size);

bool InflateBakedTexture(const BakedTexture& texture, std::vector<uint8_t>& out);

// Overwrites `data` with the Português Brasil texture when it is the stock texture of a translated row.
const BakedTexture* LocalizeBakedTexture(uint8_t* data, size_t size);

}  // namespace ssf2t::localization
