#pragma once

#include <cstdint>

#include "character/fighter_identity.h"
#include "character/select_mode.h"
#include "online/secret_characters.h"

namespace ssf2t::character {

// Local and Arcade play follow the player's own option. An Online Match offers a secret character only when both
// players' options and builds agree on it, never because this player turned it on.
constexpr bool SecretAvailable(bool online_select, bool local_option, bool negotiated) {
  return online_select ? negotiated : local_option;
}

struct SecretCellInputs {
  bool online_select = false;
  bool developer_netplay = false;
  bool akuma_option = false;
  bool ultra_option = false;
  bool evil_ryu_packages = false;
  bool violent_ken_packages = false;
  bool violent_ken_gameplay = false;
  online::SecretSet negotiated{};
};

struct SecretCells {
  bool akuma = false;
  bool evil_ryu = false;
  bool violent_ken = false;

  constexpr bool any() const { return akuma || evil_ryu || violent_ken; }
};

// The one answer to which secret cells exist right now: drawing, navigation and confirm all ask it, so a cell is either
// usable or not there. The HD style is deliberately no input: it decides how a pick is committed, never whether it can
// be made. The developer Netplay Test agrees on fights through frame zero guest state, which has no Ultra identity.
constexpr SecretCells ResolveSecretCells(const SecretCellInputs& in) {
  const bool online = in.online_select;
  const bool ultra_shared = !in.developer_netplay;
  SecretCells cells;
  cells.akuma = SecretAvailable(online, in.akuma_option, online && in.negotiated.akuma);
  cells.evil_ryu =
      ultra_shared && SecretAvailable(online, in.ultra_option && in.evil_ryu_packages, online && in.negotiated.evil_ryu);
  cells.violent_ken = ultra_shared && in.violent_ken_gameplay &&
                      SecretAvailable(online, in.ultra_option && in.violent_ken_packages,
                                      online && in.negotiated.violent_ken);
  return cells;
}

SecretCells SecretCellsFor(uint8_t game_mode);

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
