#include "online/secret_view.h"

#include "localization/ui_text.h"

namespace ssf2t::online {

std::string SecretStatusText(SecretStatus status) {
  switch (status) {
    case SecretStatus::kAvailable: return Tr("Available");
    case SecretStatus::kDisabledLocally: return Tr("Disabled locally");
    case SecretStatus::kDisabledByOpponent: return Tr("Disabled by opponent");
    case SecretStatus::kUnsupportedByOpponent: return Tr("Unsupported by opponent");
    case SecretStatus::kUnsupportedByService: return Tr("Unsupported by the service");
    case SecretStatus::kWaiting: return Tr("Waiting for opponent");
  }
  return {};
}

std::vector<SecretRowView> DescribeSecretCharacters(const Snapshot& state, const SecretCapabilities& edited) {
  std::vector<SecretRowView> rows;
  for (int i = 0; i < kSecretCharacterCount; ++i) {
    const SecretCharacter character = SecretCharacterAt(i);
    SecretRowView row;
    row.character = character;
    row.label = Tr(SecretLabel(character));
    if (!state.secret.in_match) {
      row.status = edited.enabled.Has(character) ? Tr("Enabled locally") : Tr("Off");
    } else {
      const bool lost = state.match && state.match->peer_lost;
      const SecretStatus status = lost && edited.enabled.Has(character)
                                      ? SecretStatus::kWaiting
                                      : SecretStatusOf(character, state.secret.local, state.secret.peer,
                                                       state.secret.service_supported);
      row.status = SecretStatusText(status);
      row.available = status == SecretStatus::kAvailable && state.secret.effective.Has(character);
    }
    rows.push_back(std::move(row));
  }
  return rows;
}

}  // namespace ssf2t::online
