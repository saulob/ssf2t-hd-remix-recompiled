#pragma once

#include <functional>

#include "input_owner.h"
#include "online/protocol.h"

namespace ssf2t {

// The game's Invite Player opens this instead of the Xbox Guide.
void SetInvitePickerOpener(std::function<void()> open);

// Hands an accepted invite to the game: XN_LIVE_INVITE_ACCEPTED, then the game's own accepted-invite path reads the
// session and joins it as Quick Match would. `accepted_by` is the device that pressed Accept; it picks the match's
// input owner.
void DeliverAcceptedInvite(const online::PlayerMatchAd& ad, InputSource accepted_by);

}  // namespace ssf2t
