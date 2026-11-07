#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "character/character_moves.h"
#include "character/fighter_identity.h"

namespace ssf2t::character {

struct QuoteSet {
  FighterIdentity identity;
  int carrier;
  std::span<const std::string_view> ids;
  std::span<const uint8_t> weights;
};

inline constexpr size_t kMaxQuotes = 5;
static_assert(kEvilRyuQuotes <= int(kMaxQuotes) && kViolentKenQuotes <= int(kMaxQuotes));

inline constexpr std::array<QuoteSet, 2> kQuoteSets = {{
    {FighterIdentity::kEvilRyu, kRyuGuestCharacter, kEvilRyuQuoteIds, kEvilRyuQuoteWeights},
    {FighterIdentity::kViolentKen, kKenGuestCharacter, kViolentKenQuoteIds, kViolentKenQuoteWeights},
}};

// The winner dialog is called with the carrier's guest character; the quotes follow the fighter the side chose.
constexpr const QuoteSet* QuoteSetFor(int carrier, FighterIdentity identity) {
  for (const QuoteSet& set : kQuoteSets) {
    if (carrier == set.carrier && identity == set.identity) return &set;
  }
  return nullptr;
}

// The guest's taunt row has six slots and picks one by value % 6. Slots take the quotes in turn, which is the closest
// six-way split of USF2's sixteen weighted entries.
inline constexpr size_t kTauntSlots = 6;

constexpr size_t QuoteForSlot(size_t slot, size_t quote_count) { return slot % quote_count; }

}  // namespace ssf2t::character
