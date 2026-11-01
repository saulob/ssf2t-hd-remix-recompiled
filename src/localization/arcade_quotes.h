#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace ssf2t::localization {

// A line of the arcade win quotes at its Base013 image address (Title Update addresses add a fixed shift).
struct ArcadeQuoteLine {
  uint32_t base013_address;
  const char* english;
  const char* portuguese;
};

std::span<const ArcadeQuoteLine> ArcadeQuoteLines();

// Overwrites each English line found exactly once in the pool with its Portuguese line of the same length. Returns the
// number of lines written.
size_t PatchArcadeQuotes(uint8_t* pool, size_t size);

}  // namespace ssf2t::localization
