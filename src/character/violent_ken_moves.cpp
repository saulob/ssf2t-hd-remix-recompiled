#include "character/violent_ken_moves.h"

#include <cstddef>

namespace ssf2t::character {
namespace {

constexpr uint8_t kForward = 2, kDown = 4, kDownForward = 6;
constexpr uint8_t kStepWindow = 15;

constexpr int16_t kRasetsuVelocity[3] = {0x680, 0x800, 0xA00};
constexpr uint16_t kRasetsuThreshold[3] = {0x4A0, 0x5F0, 0x740};

struct Path {
  uint8_t steps[6];
  uint8_t length;
};

constexpr uint8_t F = kForward, D = kDown, DF = kDownForward;

constexpr Path kDragonPaths[kRasetsuPaths] = {
    {{F, D, DF}, 3}, {{D, DF, D}, 3}, {{DF, D, DF}, 3}, {{F, DF, F}, 3}, {{F, D, F}, 3},
};

constexpr Path kShinkuPaths[kShinbuPaths] = {
    {{D, DF, F, D, DF, F}, 6}, {{D, DF, D, F}, 4}, {{D, DF, F, D, F}, 5}, {{D, DF, D, DF, F}, 5},
};

template <std::size_t N>
int Advance(const Path (&paths)[N], std::array<uint8_t, N>& steps, std::array<uint8_t, N>& timers, uint8_t nibble,
            bool button) {
  for (std::size_t path = 0; path < N; ++path) {
    uint8_t& step = steps[path];
    uint8_t& timer = timers[path];
    if (step != 0 && --timer == 0) step = 0;
    if (step < paths[path].length && nibble == paths[path].steps[step]) {
      ++step;
      timer = kStepWindow;
    }
    if (step == paths[path].length && button) return int(path);
  }
  return -1;
}

}  // namespace

int RasetsuRecognizer::Update(uint8_t nibble, uint16_t pad_current, uint16_t pad_previous) {
  const int strength = PressedKickStrength(pad_current, pad_previous);
  if (Advance(kDragonPaths, step_, timer_, nibble, strength >= 0) < 0) return -1;
  Reset();
  return strength;
}

bool ShinbuRecognizer::Update(uint8_t nibble, uint16_t pad_current, uint16_t pad_previous) {
  const bool kick = PressedKickStrength(pad_current, pad_previous) >= 0;
  if (Advance(kShinkuPaths, step_, timer_, nibble, kick) < 0) return false;
  Reset();
  return true;
}

bool InazumaInput(uint8_t nibble, uint16_t pad_current, uint16_t pad_previous) {
  return nibble == kForward && (uint16_t(pad_current & ~pad_previous) & kMediumKick) != 0;
}

RasetsuDash RasetsuDashFor(int strength, bool facing_flag) {
  if (strength < 0 || strength > 2) return {};
  const int16_t velocity = kRasetsuVelocity[strength];
  return {facing_flag ? velocity : int16_t(-velocity), facing_flag ? int16_t(kRasetsuDecelerationMagnitude) : int16_t(-kRasetsuDecelerationMagnitude),
          kRasetsuThreshold[strength]};
}

}  // namespace ssf2t::character
