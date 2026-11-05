#pragma once

#include <array>
#include <cstdint>

namespace ssf2t::character {

inline constexpr uint16_t kLightKick = 0x100;
inline constexpr uint16_t kMediumKick = 0x200;
inline constexpr uint16_t kHeavyKick = 0x400;

inline constexpr int kRasetsuPaths = 5;
inline constexpr int kShinbuPaths = 4;

// 0 light, 1 medium, 2 heavy, -1 for no newly pressed kick. The heaviest wins, as USF2's strength check does.
constexpr int PressedKickStrength(uint16_t current, uint16_t previous) {
  const uint16_t pressed = uint16_t(current & ~previous);
  if (pressed & kHeavyKick) return 2;
  if (pressed & kMediumKick) return 1;
  return (pressed & kLightKick) ? 0 : -1;
}

// COMMAND_L623 then a kick: Rasetsukyaku.
class RasetsuRecognizer {
 public:
  int Update(uint8_t nibble, uint16_t pad_current, uint16_t pad_previous);
  void Reset() { *this = RasetsuRecognizer{}; }

 private:
  std::array<uint8_t, kRasetsuPaths> step_{};
  std::array<uint8_t, kRasetsuPaths> timer_{};
};

// COMMAND_L236236 then a kick: Shinbu Messatsu.
class ShinbuRecognizer {
 public:
  bool Update(uint8_t nibble, uint16_t pad_current, uint16_t pad_previous);
  void Reset() { *this = ShinbuRecognizer{}; }

 private:
  std::array<uint8_t, kShinbuPaths> step_{};
  std::array<uint8_t, kShinbuPaths> timer_{};
};

// Inazuma Kick: forward held, medium kick newly pressed.
bool InazumaInput(uint8_t nibble, uint16_t pad_current, uint16_t pad_previous);

struct RasetsuDash {
  int16_t velocity = 0;
  int16_t deceleration = 0;
  uint16_t threshold = 0;
};

// USF2 cActionControl19 Rasetsukyaku tables 0xb9c4c0 and 0xb9c4d8, in 8.8 pixels per tick.
RasetsuDash RasetsuDashFor(int strength, bool facing_flag);

// Guest Ken move ids at fighter +0x88. Violent Ken has no Shoryureppa and no kick follow-up specials.
inline constexpr uint8_t kShoryureppaMoveId = 6;
inline constexpr uint8_t kFirstStepKickMoveId = 8;
inline constexpr uint8_t kLastStepKickMoveId = 12;
constexpr bool ViolentMoveRemoved(uint8_t move_id) {
  return move_id == kShoryureppaMoveId || (move_id >= kFirstStepKickMoveId && move_id <= kLastStepKickMoveId);
}

// CPU Ken special ids at fighter +0x186, table 83A2B528.
inline constexpr uint8_t kCpuStepKickSpecial = 8;
inline constexpr uint8_t kCpuShoryureppaSpecial = 10;
constexpr bool ViolentCpuSpecialRemoved(uint8_t special) {
  return special == kCpuStepKickSpecial || special == kCpuShoryureppaSpecial;
}

// USF2 table 0xb9c47c {vx, ax, vy, ay}: only vx differs from Ken (4, 8, 11).
inline constexpr uint8_t kShoryukenAction = 0x4E;
inline constexpr uint8_t kShoryukenSubs = 3;
inline constexpr int16_t kViolentShoryukenVelocity[kShoryukenSubs] = {0x400, 0x900, 0xC00};

// USF2 constructor table 0xb9c420, Ken 0xb9a418 (3.1875, 2.125, vy 7, 7.25, 7.125, gravity 0.3125).
inline constexpr int16_t kViolentWalkForward = 0x400;
inline constexpr int16_t kViolentWalkBack = 0x300;

struct JumpRow {
  int16_t vx = 0;
  int16_t ax = 0;
  int16_t vy = 0;
  int16_t ay = 0;
};

inline constexpr JumpRow kViolentJumpForward = {0x380, 5, 0xB00, 0x90};
inline constexpr JumpRow kViolentJumpBack = {-0x398, -5, 0xB40, 0x90};
inline constexpr JumpRow kViolentJumpNeutral = {0, 0, 0xB20, 0x90};

// The row for a jump the guest has just started; `forward_speed` is its vx with the facing removed. The result is in
// the same facing-relative form, because the guest stores vx and ax with the facing applied.
constexpr JumpRow ViolentJumpRow(int32_t forward_speed) {
  if (forward_speed == 0) return kViolentJumpNeutral;
  return forward_speed > 0 ? kViolentJumpForward : kViolentJumpBack;
}

// USF2 table 0xb9c470. The class sets this speed, zero vy and zero gravity, where Ken adds to his jump speed.
inline constexpr uint8_t kAirTatsuAction = 0x50;
inline constexpr uint8_t kAirTatsuFirstSub = 5;
inline constexpr uint8_t kAirTatsuSubs = 3;
inline constexpr int16_t kViolentAirTatsuVelocity[kAirTatsuSubs] = {0x280, 0x320, 0x3C0};

constexpr bool IsViolentAirTatsu(uint8_t action, uint8_t sub) {
  return action == kAirTatsuAction && sub >= kAirTatsuFirstSub && sub < kAirTatsuFirstSub + kAirTatsuSubs;
}

// USF2 cActionControl19 Shinbu Messatsu: the rush plays out its animation, stopping at zero speed if that comes first,
// and the rising rush follows whether or not it connected. Nothing in the handler reads a hit.
inline constexpr int16_t kShinbuRushVelocity = 0x600;
inline constexpr int16_t kShinbuRushDeceleration = 0x28;
inline constexpr int16_t kShinbuRiseVelocityX = 0xC00;
inline constexpr int16_t kShinbuRiseVelocityY = 0xA00;
inline constexpr int16_t kShinbuRiseDecelerationX = 0x78;
inline constexpr int16_t kShinbuRiseDecelerationY = 0x60;

inline constexpr int32_t kRasetsuDecelerationMagnitude = 0x24;

inline constexpr uint32_t kUltraMoveTimeoutTicks = 240;

// A speed that decays to zero, moving `position` (16.16) in the fighter's facing direction.
constexpr int32_t DecayTowardZero(int32_t speed, int32_t deceleration) {
  return speed > deceleration ? speed - deceleration : 0;
}

}  // namespace ssf2t::character
