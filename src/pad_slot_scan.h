#pragma once

#include <array>

namespace ssf2t {

// Which XInput slots to read each frame. Reading an empty slot is slow, so empty slots are retried once a second;
// a slot that answered is read every frame until it stops answering.
class PadSlotScan {
 public:
  static constexpr int kSlots = 4;
  static constexpr double kRescanSeconds = 1.0;

  // `read(slot)` reads one slot and returns true when a controller answered.
  template <typename Read>
  void Poll(double now, Read&& read) {
    const bool rescan = now >= next_rescan_;
    if (rescan) {
      next_rescan_ = now + kRescanSeconds;
    }
    for (int slot = 0; slot < kSlots; ++slot) {
      if (connected_[slot] || rescan) {
        connected_[slot] = read(slot);
      }
    }
  }

  bool connected(int slot) const { return connected_[slot]; }

  void Reset() {
    connected_ = {};
    next_rescan_ = 0.0;
  }

 private:
  std::array<bool, kSlots> connected_{};
  double next_rescan_ = 0.0;
};

}  // namespace ssf2t
