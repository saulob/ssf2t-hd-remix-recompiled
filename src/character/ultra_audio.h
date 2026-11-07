#pragma once

#include "character/violent_ken_audio.h"

namespace ssf2t::character {

// The next Hadoken voice on `side` plays the Shun Goku Satsu call. False while the Evil Ryu clips are not installed.
bool MarkRagingDemonVoice(int side);

// The next Hadoken voice on `side` plays this host move's call. False while the Violent Ken clips are not installed.
bool MarkViolentMoveVoice(int side, ViolentMoveVoice voice);

}  // namespace ssf2t::character
