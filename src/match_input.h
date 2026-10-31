#pragma once

#include <cstdint>

namespace ssf2t {

// The local user that owns the current online match (input_owner.h); -1 before any was set.
int32_t MatchInputUser();
// The user whose pad drives the local fighter: the match owner while the match lasts, else `active_user`.
int32_t MatchFightUser(int32_t active_user);
// The match really ended; the last owner still answers the error popup that follows.
void EndMatchInput(const char* reason);

}  // namespace ssf2t
