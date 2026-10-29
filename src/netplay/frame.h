#pragma once

#include <cstdint>

namespace ssf2t::netplay {

using Frame = uint32_t;

// Signed distance a - b that survives wraparound of the 32-bit counter.
constexpr int32_t FrameDiff(Frame a, Frame b) { return static_cast<int32_t>(a - b); }
constexpr bool FrameBefore(Frame a, Frame b) { return FrameDiff(a, b) < 0; }

constexpr int32_t SeqDiff(uint16_t a, uint16_t b) { return static_cast<int16_t>(static_cast<uint16_t>(a - b)); }

}  // namespace ssf2t::netplay
