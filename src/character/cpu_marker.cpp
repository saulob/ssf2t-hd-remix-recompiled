#include "character/cpu_marker.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

#include "character/akuma_portrait.h"

namespace ssf2t::character {
namespace {

constexpr char kMagic[] = "SG28XT03";
constexpr size_t kNameOffset = 20;
constexpr size_t kSizeFieldsBytes = 13;
constexpr size_t kWidthOffset = 4;
constexpr size_t kHeightOffset = 6;
constexpr int kBlockSize = 4;
constexpr int kBlockBytes = 16;

constexpr int kLabelLeft = 33;
constexpr int kLabelRight = 95;
constexpr int kLetterWidth = 18;
constexpr int kLetterGap = 4;
constexpr int kStroke = 7;
constexpr int kOpaque = 200;
constexpr int kVisible = 16;
constexpr size_t kPaletteColours = 256;

struct Rgba {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t a = 0;
};

struct Canvas {
  int width = 0;
  int height = 0;
  std::vector<Rgba> pixels;

  Rgba& At(int x, int y) { return pixels[size_t(y) * width + x]; }

  void Fill(int left, int top, int right, int bottom, Rgba colour) {
    for (int y = std::max(top, 0); y < std::min(bottom, height); ++y) {
      for (int x = std::max(left, 0); x < std::min(right, width); ++x) At(x, y) = colour;
    }
  }
};

size_t PaddedNameEnd(const uint8_t* file, size_t size) {
  const void* end = std::memchr(file + kNameOffset, '\0', size - kNameOffset);
  if (!end) return 0;
  const size_t after = size_t(static_cast<const uint8_t*>(end) - file) + 1;
  return (after + 3) & ~size_t(3);
}

bool Decode(const uint8_t* file, size_t size, Canvas& canvas) {
  if (TextureFileName(file, size).empty()) return false;
  const size_t fields = PaddedNameEnd(file, size);
  if (fields == 0 || fields + kSizeFieldsBytes > size) return false;
  canvas.width = file[fields + kWidthOffset] << 8 | file[fields + kWidthOffset + 1];
  canvas.height = file[fields + kHeightOffset] << 8 | file[fields + kHeightOffset + 1];
  if (canvas.width <= 0 || canvas.height <= 0 || canvas.width % kBlockSize != 0 || canvas.height % kBlockSize != 0) {
    return false;
  }
  const size_t blocks = size_t(canvas.width / kBlockSize) * (canvas.height / kBlockSize);
  const uint8_t* data = file + fields + kSizeFieldsBytes;
  if (size_t(file + size - data) < blocks * kBlockBytes) return false;
  canvas.pixels.assign(size_t(canvas.width) * canvas.height, {});
  const int stride = canvas.width * int(sizeof(Rgba));
  for (int by = 0; by < canvas.height / kBlockSize; ++by) {
    for (int bx = 0; bx < canvas.width / kBlockSize; ++bx) {
      DecodeDxt5Block(data, reinterpret_cast<uint8_t*>(&canvas.At(bx * kBlockSize, by * kBlockSize)), stride);
      data += kBlockBytes;
    }
  }
  return true;
}

uint32_t Key(const Rgba& c) { return uint32_t(c.r) << 16 | uint32_t(c.g) << 8 | c.b; }

// The two commonest solid colours of the old label: its fill and its outline, when it has one.
std::array<Rgba, 2> LabelColours(Canvas& canvas, const CpuMarkerArt& art) {
  std::map<uint32_t, int> counts;
  std::map<uint32_t, Rgba> colours;
  for (int y = art.label_top; y < art.label_bottom; ++y) {
    for (int x = kLabelLeft; x < kLabelRight; ++x) {
      const Rgba& pixel = canvas.At(x, y);
      if (pixel.a < kOpaque) continue;
      Rgba solid = pixel;
      solid.a = 255;
      ++counts[Key(solid)];
      colours[Key(solid)] = solid;
    }
  }
  std::vector<std::pair<int, uint32_t>> order;
  for (const auto& [key, count] : counts) order.push_back({count, key});
  std::sort(order.rbegin(), order.rend());
  std::array<Rgba, 2> result{};
  if (!order.empty()) result[0] = colours[order[0].second];
  result[1] = result[0];
  for (const auto& [count, key] : order) {
    const Rgba& other = colours[key];
    const int distance = std::abs(other.r - result[0].r) + std::abs(other.g - result[0].g) + std::abs(other.b - result[0].b);
    if (distance > 96) {
      result[1] = other;
      break;
    }
  }
  return result;
}

struct Bar {
  int left, top, right, bottom;
};

void DrawLetter(Canvas& canvas, int x, int top, int height, char letter, int grow, Rgba colour) {
  const int w = kLetterWidth;
  const int s = kStroke;
  const int middle = height / 2;
  std::vector<Bar> bars;
  if (letter == 'C') {
    bars = {{0, 0, s, height}, {0, 0, w, s}, {0, height - s, w, height}};
  } else if (letter == 'P') {
    bars = {{0, 0, s, height}, {0, 0, w, s}, {w - s, 0, w, middle + s / 2}, {0, middle - s / 2, w, middle + s / 2}};
  } else {
    bars = {{0, 0, s, height}, {w - s, 0, w, height}, {0, height - s, w, height}};
  }
  for (const Bar& bar : bars) {
    canvas.Fill(x + bar.left - grow, top + bar.top - grow, x + bar.right + grow, top + bar.bottom + grow, colour);
  }
}

void DrawCpuLabel(Canvas& canvas, const CpuMarkerArt& art) {
  const std::array<Rgba, 2> colours = LabelColours(canvas, art);
  canvas.Fill(kLabelLeft, art.label_top, kLabelRight, art.label_bottom, Rgba{});
  const int top = art.label_top + 1;
  const int height = art.label_bottom - art.label_top - 2;
  for (const int grow : {1, 0}) {
    int x = kLabelLeft + 1;
    for (const char letter : {'C', 'P', 'U'}) {
      DrawLetter(canvas, x, top, height, letter, grow, colours[grow == 1 ? 1 : 0]);
      x += kLetterWidth + kLetterGap;
    }
  }
}

uint8_t PaletteIndex(const Rgba& pixel, std::vector<Rgba>& palette, std::map<uint32_t, uint8_t>& known) {
  if (pixel.a < kVisible) return 0;
  const uint32_t key = uint32_t(pixel.a >> 4) << 24 | uint32_t(pixel.r >> 2) << 16 | uint32_t(pixel.g >> 2) << 8 |
                       uint32_t(pixel.b >> 2);
  if (const auto found = known.find(key); found != known.end()) return found->second;
  if (palette.size() < kPaletteColours) {
    palette.push_back(pixel);
    const uint8_t index = uint8_t(palette.size() - 1);
    known[key] = index;
    return index;
  }
  size_t best = 1;
  int best_distance = 1 << 30;
  for (size_t i = 1; i < palette.size(); ++i) {
    const Rgba& c = palette[i];
    const int distance =
        std::abs(c.r - pixel.r) + std::abs(c.g - pixel.g) + std::abs(c.b - pixel.b) + std::abs(c.a - pixel.a);
    if (distance < best_distance) {
      best_distance = distance;
      best = i;
    }
  }
  return uint8_t(best);
}

void ToIndexed(const Canvas& canvas, EvilSpriteImage* out) {
  std::vector<Rgba> palette(1);
  std::map<uint32_t, uint8_t> known;
  out->width = canvas.width;
  out->height = canvas.height;
  out->has_palette = true;
  out->indices.resize(canvas.pixels.size());
  for (size_t i = 0; i < canvas.pixels.size(); ++i) out->indices[i] = PaletteIndex(canvas.pixels[i], palette, known);
  out->palette.fill(0);
  for (size_t i = 0; i < palette.size(); ++i) {
    uint8_t* entry = out->palette.data() + i * 4;
    entry[0] = palette[i].a;
    entry[1] = palette[i].b;
    entry[2] = palette[i].g;
    entry[3] = palette[i].r;
  }
}

std::array<EvilSpriteImage, kCpuMarkerArt.size()> g_markers;
std::array<bool, kCpuMarkerArt.size()> g_ready{};

}  // namespace

std::string_view TextureFileName(const uint8_t* file, size_t size) {
  if (size <= kNameOffset || std::memcmp(file, kMagic, 8) != 0) return {};
  const char* name = reinterpret_cast<const char*>(file) + kNameOffset;
  const void* end = std::memchr(name, '\0', size - kNameOffset);
  return end ? std::string_view(name, size_t(static_cast<const char*>(end) - name)) : std::string_view();
}

bool BuildCpuMarker(const uint8_t* file, size_t size, const CpuMarkerArt& art, EvilSpriteImage* out) {
  Canvas canvas;
  if (!Decode(file, size, canvas) || canvas.width <= kLabelRight || canvas.height < art.label_bottom) return false;
  DrawCpuLabel(canvas, art);
  ToIndexed(canvas, out);
  return true;
}

void CaptureCpuMarkerTexture(const uint8_t* file, size_t size) {
  const std::string_view name = TextureFileName(file, size);
  if (name.empty()) return;
  for (size_t i = 0; i < kCpuMarkerArt.size(); ++i) {
    if (name == kCpuMarkerArt[i].texture) g_ready[i] = BuildCpuMarker(file, size, kCpuMarkerArt[i], &g_markers[i]);
  }
}

const EvilSpriteImage* LoadedCpuMarker(int index) {
  return index >= 0 && size_t(index) < g_ready.size() && g_ready[size_t(index)] ? &g_markers[size_t(index)] : nullptr;
}

}  // namespace ssf2t::character
