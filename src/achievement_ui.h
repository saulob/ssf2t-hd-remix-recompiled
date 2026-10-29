#pragma once

#include <cstdint>
#include <functional>

#include "guest_memory.h"

namespace ssf2t {

// The game's wrapper around XamShowAchievementsUI: `li r4,0` then a tail branch to the import thunk.
struct Base013Achievements {
  static constexpr uint32_t kShowAchievementsWrapper = 0x839C3D20;
  static constexpr uint32_t kShowAchievementsWrapperWords[2] = {0x38800000, 0x48057BE0};
};

struct TitleUpdate023Achievements {
  static constexpr uint32_t kShowAchievementsWrapper = 0x839C4480;
  static constexpr uint32_t kShowAchievementsWrapperWords[2] = {0x38800000, 0x48057C68};
};

// `code` is the guest memory at G::kShowAchievementsWrapper.
template <typename G>
inline bool IsShowAchievementsWrapper(const uint8_t* code) {
  return LoadBE32(code, 0) == G::kShowAchievementsWrapperWords[0] &&
         LoadBE32(code, 4) == G::kShowAchievementsWrapperWords[1];
}

// Called on a guest thread; the handler hops to the UI thread. Set before the guest starts.
void SetAchievementsOpener(std::function<void()> open);

}  // namespace ssf2t
