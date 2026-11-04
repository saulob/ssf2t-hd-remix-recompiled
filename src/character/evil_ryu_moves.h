#pragma once

#include <array>
#include <cstdint>

namespace ssf2t::character {

// Fighter-relative direction nibbles of the guest input word.
inline constexpr uint8_t kNibbleBack = 1;
inline constexpr uint8_t kNibbleForward = 2;
inline constexpr uint8_t kNibbleDown = 4;
inline constexpr uint8_t kNibbleDownBack = 5;
inline constexpr uint8_t kNibbleDownForward = 6;

inline constexpr uint16_t kAllPunches = 0x70;
inline constexpr uint16_t kAllKicks = 0x700;

// Teleport variants: bit 0 kick, bit 1 backwards.
inline constexpr int kTeleportVariants = 4;
inline constexpr int kTeleportPaths = 10;

constexpr bool IsBackTeleport(int variant) { return (variant & 2) != 0; }
constexpr bool IsKickTeleport(int variant) { return (variant & 1) != 0; }

// The USF2 COMMAND_L623 and COMMAND_L421 motions followed by all three punches or all three kicks.
class TeleportRecognizer {
 public:
  // Returns the completed variant or -1.
  int Update(uint8_t nibble, uint16_t pad_current, uint16_t pad_previous);
  void Reset() { *this = TeleportRecognizer{}; }

 private:
  std::array<uint8_t, kTeleportPaths> step_{};
  std::array<uint8_t, kTeleportPaths> timer_{};
};

// LP, LP, Forward, LK, HP, or LP, LP, Forward+LK, HP, each within 27 frames of the previous one.
class RagingDemonRecognizer {
 public:
  bool Update(uint16_t input_current, uint16_t input_previous);
  void Reset() { *this = RagingDemonRecognizer{}; }

 private:
  uint8_t step_ = 0;
  uint8_t timer_ = 0;
};

struct DashParameters {
  int16_t velocity = 0;
  int16_t deceleration = 0;
  uint16_t threshold = 0;
};

// USF2 cActionControl17 teleport tables as {velocity, deceleration} and stop thresholds per variant, in 8.8 pixels.
DashParameters DashFor(int variant, bool facing_flag);

struct DashState {
  int32_t velocity = 0;
  int32_t deceleration = 0;
  uint16_t threshold = 0;

  // Advances one frame; `position` is the 16.16 x coordinate. Returns true once the dash is over.
  bool Step(int32_t& position, uint8_t wall_flag);
};

// The animation event byte of the teleport startup shakes the fighter: 1..16 ahead, 17 and up behind.
constexpr int16_t TeleportSlide(uint8_t event) { return event < 0x11 ? event : int16_t(-(event - 16)); }
constexpr bool TeleportBodyVisible(uint32_t frame) { return (frame & 1) != 0; }

// USF2 Shun Goku Satsu: E56 holds for 21 ticks, then a far teleport dash that whiffs at 3 pixels per tick.
inline constexpr uint8_t kDemonStartupFrames = 21;
inline constexpr int16_t kDemonDashVelocity = 0xB80;
inline constexpr int16_t kDemonDashDeceleration = 0x30;
inline constexpr int16_t kDemonWhiffVelocity = 0x300;
inline constexpr uint8_t kDemonDashTimeout = 0x7F;

constexpr bool DemonDashSpent(int16_t velocity) {
  return (velocity < 0 ? -int32_t(velocity) : int32_t(velocity)) <= kDemonWhiffVelocity;
}

enum class HostMoveKind : uint8_t { kNone, kTeleport, kRagingDemon, kSenpuKyaku };

inline constexpr uint32_t kSenpuTimeoutTicks = 240;
enum class HostMovePhase : uint8_t { kStartup, kDash, kTail, kRecovery };

inline constexpr int kTeleportTailFrames = 5;

struct HostMove {
  HostMoveKind kind = HostMoveKind::kNone;
  HostMovePhase phase = HostMovePhase::kStartup;
  uint8_t tail = 0;
  int8_t variant = 0;
  uint32_t ticks = 0;
  DashState dash;
  TeleportRecognizer teleport;
  RagingDemonRecognizer raging_demon;
};

enum class EvilRenderState : uint8_t {
  kNormal,
  kTeleportStartup,
  kTeleportGhost,
  kTeleportRecovery,
  kShunGokuSatsu,
  kUniquePose,
};

// A move ends with the fighter leaving the guest's special class, so outside it nothing is allowed to alter the body.
EvilRenderState RenderStateFor(const HostMove& move, bool in_special_class, bool unique_pose);

// USF2 keeps the fighter on screen through a ghost shader; alternate frames stand in for it, in the dash only.
constexpr bool BodySuppressed(EvilRenderState state, uint32_t ticks) {
  return state == EvilRenderState::kTeleportGhost && !TeleportBodyVisible(ticks);
}

class HostMoves {
 public:
  HostMove& For(int side) { return moves_[side]; }
  const HostMove& For(int side) const { return moves_[side]; }
  bool Active(int side) const { return side >= 0 && side < 2 && moves_[side].kind != HostMoveKind::kNone; }
  void Reset();

 private:
  std::array<HostMove, 2> moves_{};
};

HostMoves& GlobalHostMoves();

}  // namespace ssf2t::character
