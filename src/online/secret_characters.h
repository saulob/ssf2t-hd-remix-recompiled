#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace ssf2t::online {

// The extra fighters an online match can negotiate. Violent Ken joins here once he is playable online.
enum class SecretCharacter : uint8_t { kAkuma = 0, kEvilRyu = 1 };
inline constexpr int kSecretCharacterCount = 2;

constexpr SecretCharacter SecretCharacterAt(int index) { return SecretCharacter(index); }
const char* SecretKey(SecretCharacter character);   // the wire key: "akuma", "evil_ryu"
const char* SecretLabel(SecretCharacter character);  // "Akuma", "Evil Ryu"

// One flag per secret character.
struct SecretSet {
  bool akuma = false;
  bool evil_ryu = false;

  bool Has(SecretCharacter character) const { return character == SecretCharacter::kAkuma ? akuma : evil_ryu; }
  void Set(SecretCharacter character, bool value) { (character == SecretCharacter::kAkuma ? akuma : evil_ryu) = value; }
  bool Any() const { return akuma || evil_ryu; }
  uint8_t Bits() const { return uint8_t((akuma ? 1u : 0u) | (evil_ryu ? 2u : 0u)); }
  static SecretSet FromBits(uint8_t bits) { return {(bits & 1u) != 0, (bits & 2u) != 0}; }
  friend bool operator==(const SecretSet& a, const SecretSet& b) { return a.akuma == b.akuma && a.evil_ryu == b.evil_ryu; }
};

// What one client advertises: what its build understands, and what its player turned on.
struct SecretCapabilities {
  SecretSet supports;
  SecretSet enabled;

  // A client cannot enable what it does not understand.
  SecretCapabilities Normalized() const {
    return {supports, {enabled.akuma && supports.akuma, enabled.evil_ryu && supports.evil_ryu}};
  }
  friend bool operator==(const SecretCapabilities& a, const SecretCapabilities& b) {
    return a.supports == b.supports && a.enabled == b.enabled;
  }
};

// A character is available in a match only when both clients understand it and both players turned it on. Identical on
// both sides, whichever of them computes it.
SecretSet EffectiveSecrets(const SecretCapabilities& a, const SecretCapabilities& b);

// What the Online page says about one secret character.
enum class SecretStatus {
  kAvailable,
  kDisabledLocally,
  kDisabledByOpponent,
  kUnsupportedByOpponent,
  kUnsupportedByService,
  kWaiting,
};

// `peer` is empty while the opponent's capabilities are not known yet.
SecretStatus SecretStatusOf(SecretCharacter character, const SecretCapabilities& local,
                            const std::optional<SecretCapabilities>& peer, bool service_supported = true);

// The room's secret characters as a client knows them. `effective` is the AND of both sides, or empty when no match.
struct MatchSecrets {
  SecretCapabilities peer;
  SecretSet effective;
};

// The Worker's answer is checked against the client's own AND of the two sides; the result can only be smaller.
SecretSet VerifiedSecrets(const SecretCapabilities& local, const MatchSecrets& room);

}  // namespace ssf2t::online
