#pragma once

#include <cstdint>

#include "character/fighter_identity.h"
#include "online/secret_characters.h"

namespace ssf2t::character {

// The game mode byte of an Online Match, the only mode whose Character Select both players run together.
inline constexpr uint8_t kOnlineSelectMode = 3;

constexpr bool OnlineSelectMode(uint8_t game_mode) { return game_mode == kOnlineSelectMode; }

// Local and Arcade play follow the player's own option. An Online Match offers a secret character only when both
// players' options and builds agree on it, never because this player turned it on.
constexpr bool SecretAvailable(bool online_select, bool local_option, bool negotiated) {
  return online_select ? negotiated : local_option;
}

// The same rule the matching chooser applies to the guest's own unlock query: online, an unavailable Akuma is locked
// whatever the player's save says, because the peers' saves differ.
constexpr uint32_t AkumaUnlockFor(bool online_select, bool available, uint32_t original) {
  if (available) return 1u;
  return online_select ? 0u : original;
}

// A fighter the match did not negotiate. The Ultra fighters ride on guest ids, so the guest character alone says nothing.
enum class SelectionVerdict : uint8_t { kAccepted, kAkuma, kEvilRyu, kViolentKen };

constexpr SelectionVerdict ValidateSecretSelection(int guest_character, FighterIdentity requested,
                                                   const online::SecretSet& negotiated) {
  if (guest_character == kAkumaGuestCharacter && !negotiated.akuma) return SelectionVerdict::kAkuma;
  if (requested == FighterIdentity::kEvilRyu && guest_character == kRyuGuestCharacter && !negotiated.evil_ryu) {
    return SelectionVerdict::kEvilRyu;
  }
  if (requested == FighterIdentity::kViolentKen && guest_character == kKenGuestCharacter && !negotiated.violent_ken) {
    return SelectionVerdict::kViolentKen;
  }
  return SelectionVerdict::kAccepted;
}

constexpr const char* SelectionVerdictName(SelectionVerdict verdict) {
  if (verdict == SelectionVerdict::kAkuma) return "Akuma";
  if (verdict == SelectionVerdict::kEvilRyu) return "EvilRyu";
  return verdict == SelectionVerdict::kViolentKen ? "ViolentKen" : "none";
}

// The identity a fight report names for a side: the Ryu carrier is Evil Ryu only when the match negotiated him.
constexpr bool ReportsEvilRyu(int guest_character, FighterIdentity requested, bool evil_ryu_negotiated) {
  return evil_ryu_negotiated && requested == FighterIdentity::kEvilRyu && guest_character == kRyuGuestCharacter;
}

constexpr bool ReportsViolentKen(int guest_character, FighterIdentity requested, bool violent_ken_negotiated) {
  return violent_ken_negotiated && requested == FighterIdentity::kViolentKen && guest_character == kKenGuestCharacter;
}

}  // namespace ssf2t::character
