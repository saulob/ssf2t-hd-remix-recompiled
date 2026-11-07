#pragma once

#include <cstdint>

namespace ssf2t::character {

// Port-side identity. The guest fighter keeps its original character id; this layer lets the port distinguish
// variants that the original roster does not represent.
enum class FighterIdentity : uint8_t {
  kGuest = 0,
  kRyu = 1,
  kEvilRyu = 2,
  kKen = 3,
  kViolentKen = 4,
};

struct FighterIdentityInfo {
  FighterIdentity identity = FighterIdentity::kGuest;
  int guest_character = -1;
  bool hidden = false;
  bool ultra_fighters = false;
};

const char* IdentityName(FighterIdentity identity);

// The guest ids this build understands.
inline constexpr int kRyuGuestCharacter = 0;
inline constexpr int kKenGuestCharacter = 4;
inline constexpr int kAkumaGuestCharacter = 16;

FighterIdentityInfo ResolveFighterIdentity(int guest_character, bool hidden, bool ultra_fighters_enabled,
                                           FighterIdentity requested);

// Ryu is guest character 0, so a fighter the guest has not started yet reads as Ryu: it has no identity until then.
FighterIdentity StartedFighterIdentity(bool started, int guest_character, bool hidden, bool ultra_fighters_enabled,
                                       FighterIdentity requested);

// The HUD reads the guest's own per-side character word, which outlives the fighter's round reset, so the name and
// portrait do not flicker back to Ryu between rounds.
constexpr bool HudShowsEvilRyu(bool ultra_fighters_enabled, FighterIdentity requested, int hud_character) {
  return ultra_fighters_enabled && requested == FighterIdentity::kEvilRyu && hud_character == kRyuGuestCharacter;
}

constexpr bool HudShowsViolentKen(bool ultra_fighters_enabled, FighterIdentity requested, int hud_character) {
  return ultra_fighters_enabled && requested == FighterIdentity::kViolentKen && hud_character == kKenGuestCharacter;
}

// Set by the Character Select integration when the player explicitly chooses an Ultra fighter. kGuest means the
// normal guest selection is used.
void SetRequestedIdentity(int side, FighterIdentity identity);
FighterIdentity RequestedIdentity(int side);

FighterIdentityInfo IdentityForSide(int side);
void SetIdentityForSide(int side, const FighterIdentityInfo& info);
void ResetFightIdentities();
void ResetIdentities();

}  // namespace ssf2t::character
