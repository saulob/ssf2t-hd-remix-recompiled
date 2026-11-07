#include "character/akuma_select.h"

namespace ssf2t::character {

uint32_t AkumaUnlockResult(bool enabled, uint32_t original) { return enabled ? 1u : original; }

}  // namespace ssf2t::character
