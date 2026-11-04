#include "online/secret_characters.h"

namespace ssf2t::online {

const char* SecretKey(SecretCharacter character) {
  return character == SecretCharacter::kAkuma ? "akuma" : "evil_ryu";
}

const char* SecretLabel(SecretCharacter character) {
  return character == SecretCharacter::kAkuma ? "Akuma" : "Evil Ryu";
}

SecretSet EffectiveSecrets(const SecretCapabilities& a, const SecretCapabilities& b) {
  const SecretCapabilities left = a.Normalized();
  const SecretCapabilities right = b.Normalized();
  SecretSet out;
  for (int i = 0; i < kSecretCharacterCount; ++i) {
    const SecretCharacter character = SecretCharacterAt(i);
    out.Set(character, left.supports.Has(character) && right.supports.Has(character) && left.enabled.Has(character) &&
                           right.enabled.Has(character));
  }
  return out;
}

SecretStatus SecretStatusOf(SecretCharacter character, const SecretCapabilities& local,
                            const std::optional<SecretCapabilities>& peer, bool service_supported) {
  const SecretCapabilities mine = local.Normalized();
  if (!mine.enabled.Has(character)) return SecretStatus::kDisabledLocally;
  if (!service_supported) return SecretStatus::kUnsupportedByService;
  if (!peer) return SecretStatus::kWaiting;
  const SecretCapabilities theirs = peer->Normalized();
  if (!theirs.supports.Has(character)) return SecretStatus::kUnsupportedByOpponent;
  if (!theirs.enabled.Has(character)) return SecretStatus::kDisabledByOpponent;
  return SecretStatus::kAvailable;
}

SecretSet VerifiedSecrets(const SecretCapabilities& local, const MatchSecrets& room) {
  const SecretSet own = EffectiveSecrets(local, room.peer);
  return {room.effective.akuma && own.akuma, room.effective.evil_ryu && own.evil_ryu};
}

}  // namespace ssf2t::online
