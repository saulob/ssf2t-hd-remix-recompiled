#pragma once

#include <cstdint>
#include <string>

#include "netplay/guest_netplay.h"

namespace ssf2t::netplay {

// An online fight sends what the game's own NetSync sends (rollback callback command 3): the controller table entry
// of the active user, the controller that chose Online Match. The fighter entries are what the rollback writes, and
// the network player index (host 0, joiner 1) never selects a device.
inline constexpr uint32_t kLocalUsers = 4;

constexpr uint32_t LocalInputUser(uint32_t active_user) { return active_user < kLocalUsers ? active_user : 0; }

constexpr uint32_t ControllerWordAddress(uint32_t controller_table, uint32_t user) {
  return controller_table + user * kInputEntryStride + kInputCurrentOffset;
}

constexpr InputWord ControllerWord(uint32_t raw) { return InputWord(raw & 0xFFFF); }

// The pad as the first frames of an online fight log it: raw buttons, the left stick as -1/0/1 per axis, and the
// fight word the game mapped it to.
struct PadSample {
  uint16_t buttons = 0;
  int8_t stick_x = 0;
  int8_t stick_y = 0;
  InputWord word = 0;

  bool operator==(const PadSample&) const = default;
};

inline constexpr int16_t kStickDeadzone = 7849;  // XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE

constexpr int8_t StickDirection(int16_t axis) { return axis > kStickDeadzone ? 1 : axis < -kStickDeadzone ? -1 : 0; }

// One line per change during the first kFrames frames of a fight, nothing after.
class InputChangeLog {
 public:
  static constexpr uint32_t kFrames = 120;

  bool ShouldLog(uint32_t frame, const PadSample& sample) {
    if (frame >= kFrames || (seen_ && sample == last_)) return false;
    seen_ = true;
    last_ = sample;
    return true;
  }
  void Reset() { *this = {}; }

 private:
  bool seen_ = false;
  PadSample last_{};
};

inline std::string DescribeFightBits(InputWord word) {
  static constexpr struct {
    InputWord bit;
    const char* name;
  } kButtons[] = {{0x0010, "LP"}, {0x0020, "MP"}, {0x0040, "HP"}, {0x0100, "LK"}, {0x0200, "MK"}, {0x0400, "HK"}};
  std::string text;
  for (const auto& button : kButtons) {
    if (!(word & button.bit)) continue;
    if (!text.empty()) text += '+';
    text += button.name;
  }
  if (const InputWord dirs = word & 0x000F) {
    if (!text.empty()) text += '+';
    text += "dir" + std::to_string(dirs);
  }
  return text.empty() ? "none" : text;
}

}  // namespace ssf2t::netplay
