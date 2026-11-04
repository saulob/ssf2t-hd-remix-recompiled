#pragma once

#include <cstdint>

#include <rex/cvar.h>

namespace ssf2t::character {

inline constexpr char kEnableAkumaSetting[] = "ssf2t_enable_akuma";
inline constexpr bool kDefaultEnableAkuma = false;
inline constexpr char kEnableAkumaLabel[] = "Enable Akuma";
inline constexpr char kEnableAkumaHelp[] =
    "Unlocks the original Akuma on Character Select without the secret input sequence.";

REXCVAR_DECLARE(bool, ssf2t_enable_akuma);

bool AkumaEnabled();
// Whether Akuma can be picked now: the local option in Local and Arcade play, the negotiated result in an Online Match.
bool AkumaAvailableFor(bool online_select);
bool AkumaAvailable(uint8_t game_mode);

}  // namespace ssf2t::character
