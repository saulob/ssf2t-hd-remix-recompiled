#pragma once

#include <cstdint>
#include <functional>

#include "guest_memory.h"

namespace ssf2t {

// The game's wrapper around XamShowAchievementsUI: `li r4,0` then a tail branch to the import thunk.
constexpr uint32_t kShowAchievementsWrapper = 0x839C3D20;
constexpr uint32_t kShowAchievementsWrapperWords[2] = {0x38800000, 0x48057BE0};

// `code` is the guest memory at kShowAchievementsWrapper.
inline bool IsShowAchievementsWrapper(const uint8_t* code) {
  return LoadBE32(code, 0) == kShowAchievementsWrapperWords[0] && LoadBE32(code, 4) == kShowAchievementsWrapperWords[1];
}

// Called on a guest thread; the handler hops to the UI thread. Set before the guest starts.
void SetAchievementsOpener(std::function<void()> open);

}  // namespace ssf2t
