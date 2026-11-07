#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "launcher_pages.h"

namespace ssf2t {

struct EmbeddedFile {
  const char* name;
  const uint8_t* data;
  size_t size;
};

// Square RGBA, white with the artwork as alpha, so one image takes every sidebar colour. Empty on failure.
std::vector<uint8_t> RasterizeIcon(const uint8_t* svg, size_t size, float optical_scale, int pixels);

// The page's embedded icon at that many pixels square.
std::vector<uint8_t> RenderPageIcon(LauncherPage page, int pixels);

}  // namespace ssf2t
