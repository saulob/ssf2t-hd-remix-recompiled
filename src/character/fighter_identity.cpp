#include "character/fighter_identity.h"

#include <array>

namespace ssf2t::character {
namespace {

constexpr int kSides = 2;

std::array<FighterIdentityInfo, kSides> g_identities;
std::array<FighterIdentity, kSides> g_requested;

bool ValidSide(int side) { return side >= 0 && side < kSides; }

}  // namespace

const char* IdentityName(FighterIdentity identity) {
  switch (identity) {
    case FighterIdentity::kRyu: return "Ryu";
    case FighterIdentity::kEvilRyu: return "Evil Ryu";
    case FighterIdentity::kKen: return "Ken";
    case FighterIdentity::kViolentKen: return "Violent Ken";
    default: return "guest";
  }
}

FighterIdentityInfo ResolveFighterIdentity(int guest_character, bool hidden, bool ultra_fighters_enabled,
                                           FighterIdentity requested) {
  FighterIdentityInfo info;
  info.guest_character = guest_character;
  info.hidden = hidden;
  info.ultra_fighters = ultra_fighters_enabled;
  if (guest_character == kRyuGuestCharacter) info.identity = FighterIdentity::kRyu;
  if (guest_character == kKenGuestCharacter) info.identity = FighterIdentity::kKen;
  if (!ultra_fighters_enabled || hidden) return info;

  if (requested == FighterIdentity::kEvilRyu && guest_character == kRyuGuestCharacter) {
    info.identity = FighterIdentity::kEvilRyu;
  } else if (requested == FighterIdentity::kViolentKen && guest_character == kKenGuestCharacter) {
    info.identity = FighterIdentity::kViolentKen;
  }
  return info;
}

FighterIdentity StartedFighterIdentity(bool started, int guest_character, bool hidden, bool ultra_fighters_enabled,
                                       FighterIdentity requested) {
  if (!started) return FighterIdentity::kGuest;
  return ResolveFighterIdentity(guest_character, hidden, ultra_fighters_enabled, requested).identity;
}

void SetRequestedIdentity(int side, FighterIdentity identity) {
  if (ValidSide(side)) g_requested[side] = identity;
}

FighterIdentity RequestedIdentity(int side) {
  return ValidSide(side) ? g_requested[side] : FighterIdentity::kGuest;
}

FighterIdentityInfo IdentityForSide(int side) {
  return ValidSide(side) ? g_identities[side] : FighterIdentityInfo{};
}

void SetIdentityForSide(int side, const FighterIdentityInfo& info) {
  if (ValidSide(side)) g_identities[side] = info;
}

void ResetFightIdentities() { g_identities = {}; }

void ResetIdentities() {
  g_identities = {};
  g_requested = {};
}

}  // namespace ssf2t::character
