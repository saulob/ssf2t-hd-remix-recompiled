#include <cstring>
#include <string_view>

#include "localization/arcade_quotes.h"

namespace ssf2t::localization {

size_t PatchArcadeQuotes(uint8_t* pool, size_t size) {
  const std::string_view view(reinterpret_cast<const char*>(pool), size);
  size_t written = 0;
  for (const ArcadeQuoteLine& line : ArcadeQuoteLines()) {
    const std::string_view english = line.english;
    const size_t at = view.find(english);
    if (at == std::string_view::npos || view.find(english, at + 1) != std::string_view::npos) continue;
    if (std::strlen(line.portuguese) != english.size()) continue;
    std::memcpy(pool + at, line.portuguese, english.size());
    ++written;
  }
  return written;
}

}  // namespace ssf2t::localization
