#pragma once

#include <string>
#include <vector>

#include "online/online_service.h"

namespace ssf2t::online {

struct SecretRowView {
  SecretCharacter character = SecretCharacter::kAkuma;
  std::string label;
  std::string status;
  bool available = false;  // both players have it on and can pick it
};

// The Online page's "Secret characters" rows. Without a match they show the player's own options (`edited`, which
// follows the launcher's unsaved choices); inside one they show what the match negotiated and why.
std::vector<SecretRowView> DescribeSecretCharacters(const Snapshot& state, const SecretCapabilities& edited);

std::string SecretStatusText(SecretStatus status);

}  // namespace ssf2t::online
