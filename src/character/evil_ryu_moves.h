#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "character/violent_ken_moves.h"

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

// USF2 cActionControl18 teleport tables as {velocity, deceleration} and stop thresholds per variant, in 8.8 pixels.
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

// USF2 Shun Goku Satsu: E56 holds for 21 ticks, then a far teleport dash that whiffs at 4 pixels per tick.
inline constexpr uint8_t kDemonStartupFrames = 21;
inline constexpr int16_t kDemonDashVelocity = 0x8A0;
inline constexpr int16_t kDemonDashDeceleration = 0x24;
inline constexpr int16_t kDemonWhiffVelocity = 0x400;
inline constexpr uint8_t kDemonDashTimeout = 0x7F;

constexpr bool DemonDashSpent(int16_t velocity) {
  return (velocity < 0 ? -int32_t(velocity) : int32_t(velocity)) <= kDemonWhiffVelocity;
}

// The guest stores a special's strength as 0, 2 or 4.
constexpr uint8_t TatsuSpins(uint8_t strength) { return uint8_t(strength >> 1); }
uint8_t ShakunetsuRecovery(uint8_t strength);

// The guest's half circle runs back to forward; USF2 Evil Ryu's runs forward to back, so its tracker sees him mirrored.
constexpr uint16_t MirrorDirections(uint16_t input) {
  return uint16_t((input & ~3u) | ((input & 1u) << 1) | ((input >> 1) & 1u));
}

// Senpu Kyaku is forward + medium kick: a hop from frame 1 until frame kSenpuHoldFrame, which holds until the landing.
bool SenpuInput(uint8_t nibble, uint16_t pad_current, uint16_t pad_previous);
inline constexpr int16_t kSenpuHopVelocityX = 0x480;
inline constexpr int16_t kSenpuHopDecelerationX = 0x20;
inline constexpr int16_t kSenpuHopVelocityY = 0x300;
inline constexpr int16_t kSenpuHopDecelerationY = 0x50;
inline constexpr uint32_t kSenpuHoldFrame = 6;

enum class HostMoveKind : uint8_t { kNone, kTeleport, kRagingDemon, kSenpuKyaku, kRasetsu, kInazuma, kShinbu };

inline constexpr uint32_t kSenpuTimeoutTicks = 240;
enum class HostMovePhase : uint8_t { kStartup, kDash, kTail, kRecovery, kRise, kFreeze, kRiseStartup, kLand };

inline constexpr int kTeleportTailFrames = 5;

// The standing height is the last height the fighter held for a while; a lowest-ever height is polluted by landings that
// dip below the floor and by an earlier fight.
struct GroundHeight {
  int32_t y = 0;
  int32_t last = 0;
  uint8_t still = 0;
  bool known = false;
};

inline constexpr uint8_t kGroundStillTicks = 8;
inline constexpr int32_t kAirborneMargin = 4 << 16;

constexpr void ObserveGround(GroundHeight& ground, int32_t y, bool frozen) {
  if (!ground.known) {
    ground = {y, y, 0, true};
    return;
  }
  if (frozen) return;
  if (y != ground.last) {
    ground.last = y;
    ground.still = 0;
  } else if (ground.still < kGroundStillTicks) {
    ++ground.still;
  }
  if (ground.still >= kGroundStillTicks) ground.y = y;
}

constexpr bool AboveGround(const GroundHeight& ground, int32_t y) { return y > ground.y + kAirborneMargin; }

struct HostMove {
  HostMoveKind kind = HostMoveKind::kNone;
  HostMovePhase phase = HostMovePhase::kStartup;
  uint8_t tail = 0;
  int8_t variant = 0;
  uint32_t ticks = 0;
  DashState dash;
  TeleportRecognizer teleport;
  RagingDemonRecognizer raging_demon;
  RasetsuRecognizer rasetsu;
  ShinbuRecognizer shinbu;
  int32_t rise_velocity = 0;
  int32_t ground_y = 0;
  GroundHeight ground;
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

static_assert(std::is_trivially_copyable_v<HostMove>, "host moves ride in the rollback blob as raw bytes");

class HostMoves {
 public:
  HostMove& For(int side) { return moves_[side]; }
  const HostMove& For(int side) const { return moves_[side]; }
  bool Active(int side) const { return side >= 0 && side < 2 && moves_[side].kind != HostMoveKind::kNone; }
  void Reset();
  void Save(uint8_t* out) const { std::memcpy(out, moves_.data(), sizeof(moves_)); }
  void Load(const uint8_t* in) { std::memcpy(moves_.data(), in, sizeof(moves_)); }
  static constexpr size_t kBytes = 2 * sizeof(HostMove);

 private:
  std::array<HostMove, 2> moves_{};
};

HostMoves& GlobalHostMoves();

}  // namespace ssf2t::character
