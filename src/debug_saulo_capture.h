#pragma once

// DEBUG_SAULO: temporary helpers for the F9 victory-screen capture. Remove with debug_saulo_f9.* and the capture
// block in fight_area_hook.cpp.

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "fight_area.h"

namespace ssf2t::debug_saulo {

inline constexpr int64_t kCaptureNanos = 1'500'000'000;
inline constexpr size_t kMaxQuadLines = 64;
inline constexpr size_t kMaxSnapshots = 16;

constexpr bool CaptureExpired(int64_t now_nanos, int64_t begin_nanos) {
  return now_nanos - begin_nanos >= kCaptureNanos;
}

struct QuadExtent {
  float min_x, max_x, min_y, max_y, min_u, max_u, min_v, max_v;
};

constexpr QuadExtent ExtentOf(const QuadVertices& quad) {
  QuadExtent e{quad[0].x, quad[0].x, quad[0].y, quad[0].y, quad[0].u, quad[0].u, quad[0].v, quad[0].v};
  for (const QuadVertex& v : quad) {
    e.min_x = v.x < e.min_x ? v.x : e.min_x;
    e.max_x = v.x > e.max_x ? v.x : e.max_x;
    e.min_y = v.y < e.min_y ? v.y : e.min_y;
    e.max_y = v.y > e.max_y ? v.y : e.max_y;
    e.min_u = v.u < e.min_u ? v.u : e.min_u;
    e.max_u = v.u > e.max_u ? v.u : e.max_u;
    e.min_v = v.v < e.min_v ? v.v : e.min_v;
    e.max_v = v.v > e.max_v ? v.v : e.max_v;
  }
  return e;
}

constexpr bool IsFiniteQuad(const QuadVertices& quad) {
  for (const QuadVertex& v : quad) {
    if (!IsFiniteCoordinate(v.x) || !IsFiniteCoordinate(v.y) || !IsFiniteCoordinate(v.u) || !IsFiniteCoordinate(v.v)) {
      return false;
    }
  }
  return true;
}

// Guest pointers the capture dereferences: aligned and inside the mapped main heaps, never the physical range.
constexpr bool PlausibleGuestPointer(uint32_t address) {
  return address >= 0x40000000u && address < 0xC0000000u && (address & 3u) == 0;
}

constexpr bool InGuestImage(uint32_t address) {
  return address >= 0x82000000u && address < 0x84000000u;
}

constexpr uint64_t Mix(uint64_t hash, uint64_t value) {
  hash ^= value;
  return hash * 0x100000001B3ull;
}

constexpr int32_t Rounded(float value) {
  return static_cast<int32_t>(value < 0.0f ? value - 0.5f : value + 0.5f);
}

constexpr uint64_t QuadKey(uint32_t caller, uint32_t texture, const QuadExtent& e) {
  uint64_t hash = 0xCBF29CE484222325ull;
  for (const uint64_t part :
       {uint64_t(caller), uint64_t(texture), uint64_t(uint32_t(Rounded(e.min_x))), uint64_t(uint32_t(Rounded(e.max_x))),
        uint64_t(uint32_t(Rounded(e.min_y))), uint64_t(uint32_t(Rounded(e.max_y)))}) {
    hash = Mix(hash, part);
  }
  return hash;
}

// A bounded set of distinct keys; Add reports whether the key is new and fits.
template <size_t N>
class UniqueKeys {
 public:
  bool Add(uint64_t key) {
    for (size_t i = 0; i < count_; ++i) {
      if (keys_[i] == key) {
        return false;
      }
    }
    if (count_ == N) {
      return false;
    }
    keys_[count_++] = key;
    return true;
  }
  size_t size() const { return count_; }
  void Clear() { count_ = 0; }

 private:
  std::array<uint64_t, N> keys_{};
  size_t count_ = 0;
};

enum class CallerKind { kOther, kCharacterSelect, kScrolling, kLeftSidePanel, kRightSidePanel };

template <typename G>
constexpr CallerKind ClassifyCaller(uint32_t caller) {
  return caller == G::kCharacterSelectQuadReturn ? CallerKind::kCharacterSelect
         : caller == G::kScrollingQuadReturn      ? CallerKind::kScrolling
         : caller == G::kLeftSidePanelQuadReturn  ? CallerKind::kLeftSidePanel
         : caller == G::kRightSidePanelQuadReturn ? CallerKind::kRightSidePanel
                                                  : CallerKind::kOther;
}

constexpr const char* CallerKindName(CallerKind kind) {
  switch (kind) {
    case CallerKind::kCharacterSelect:
      return "CHAR_SELECT";
    case CallerKind::kScrolling:
      return "SCROLLING";
    case CallerKind::kLeftSidePanel:
      return "SIDE_LEFT";
    case CallerKind::kRightSidePanel:
      return "SIDE_RIGHT";
    default:
      return "OTHER";
  }
}

// The game's background texture holders sit side by side: Character Select, WorldMapLeft, WorldMapRight, scrolling.
inline constexpr size_t kBackgroundHolders = 4;
inline constexpr const char* kBackgroundHolderNames[kBackgroundHolders] = {"char_select", "world_left", "world_right",
                                                                           "scrolling"};

constexpr const char* TextureTag(uint32_t d3d, const std::array<uint32_t, kBackgroundHolders>& holders) {
  for (size_t i = 0; i < kBackgroundHolders; ++i) {
    if (holders[i] != 0 && d3d == holders[i] + kGameTextureD3D) {
      return kBackgroundHolderNames[i];
    }
  }
  return "other";
}

}  // namespace ssf2t::debug_saulo
