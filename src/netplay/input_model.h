#pragma once

#include <cstdint>

#include "netplay/rollback_core.h"

namespace ssf2t::netplay {

// What the game's set-input callback stores for a player from the words it is given.
struct InputEdges {
  uint32_t current = 0;
  uint32_t previous = 0;
  uint32_t pressed = 0;
  uint32_t released = 0;
  friend bool operator==(const InputEdges&, const InputEdges&) = default;
};

constexpr InputEdges MakeEdges(InputWord current, InputWord previous) {
  return {current, previous, uint32_t(~uint32_t(previous) & current), uint32_t(previous & ~uint32_t(current))};
}

// The only input history the host owns: each player's word of the last simulated frame. It travels with every
// snapshot, so a restore hands the next frame the same previous words it had the first time.
class InputHistory {
 public:
  InputWord previous(int player) const { return previous_[player]; }
  void set_previous(const InputWord (&previous)[kPlayers]) {
    for (int p = 0; p < kPlayers; ++p) previous_[p] = previous[p];
  }
  InputEdges Edges(int player, InputWord current) const { return MakeEdges(current, previous_[player]); }
  void Record(const InputWord (&inputs)[kPlayers]) { set_previous(inputs); }

  const InputWord (&words() const)[kPlayers] { return previous_; }

 private:
  InputWord previous_[kPlayers] = {};
};

}  // namespace ssf2t::netplay
