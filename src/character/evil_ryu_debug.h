#pragma once

// DEBUG_SAULO: temporary Evil Ryu and Violent Ken move hotkeys. Remove this module, evil_ryu_debug_keys.cpp and the
// marked calls in ultra_moves_hook.cpp during final review.

#include <atomic>
#include <cstdint>

namespace ssf2t::character {

enum class DebugKey : uint8_t { kNone, kF4, kF5, kF6, kF7, kF8 };

enum class DebugFighter : uint8_t { kNone, kEvilRyu, kViolentKen };

enum class DebugMove : uint8_t {
  kNone,
  kTeleportForwardFar,
  kTeleportBackwardFar,
  kTeleportForwardNear,
  kTeleportBackwardNear,
  kSenpuKyaku,
  kRagingDemon,
  kShakunetsu,
  kRasetsuLight,
  kRasetsuMedium,
  kRasetsuHeavy,
  kInazuma,
  kShinbu,
  kShoryukenHeavy,
  kAirTatsumaki,
};

enum class DebugReject : uint8_t {
  kNone,
  kUltraOff,
  kNotInFight,
  kOnline,
  kWrongFighter,
  kFighterNotReady,
  kIncompatibleState,
  kNotAirborne,
};

constexpr DebugMove EvilDebugMoveFor(DebugKey key, bool shift, bool ctrl) {
  if (ctrl) return DebugMove::kNone;
  switch (key) {
    case DebugKey::kF4: return shift ? DebugMove::kNone : DebugMove::kShakunetsu;
    case DebugKey::kF5: return shift ? DebugMove::kTeleportForwardNear : DebugMove::kTeleportForwardFar;
    case DebugKey::kF6: return shift ? DebugMove::kTeleportBackwardNear : DebugMove::kTeleportBackwardFar;
    case DebugKey::kF7: return shift ? DebugMove::kNone : DebugMove::kSenpuKyaku;
    case DebugKey::kF8: return shift ? DebugMove::kNone : DebugMove::kRagingDemon;
    case DebugKey::kNone: break;
  }
  return DebugMove::kNone;
}

constexpr DebugMove ViolentDebugMoveFor(DebugKey key, bool shift, bool ctrl) {
  if (key == DebugKey::kF4) {
    if (shift && ctrl) return DebugMove::kNone;
    return ctrl ? DebugMove::kRasetsuHeavy : shift ? DebugMove::kRasetsuMedium : DebugMove::kRasetsuLight;
  }
  if (shift || ctrl) return DebugMove::kNone;
  switch (key) {
    case DebugKey::kF5: return DebugMove::kInazuma;
    case DebugKey::kF6: return DebugMove::kShinbu;
    case DebugKey::kF7: return DebugMove::kShoryukenHeavy;
    case DebugKey::kF8: return DebugMove::kAirTatsumaki;
    default: return DebugMove::kNone;
  }
}

constexpr DebugMove DebugMoveFor(DebugFighter fighter, DebugKey key, bool shift, bool ctrl) {
  if (fighter == DebugFighter::kEvilRyu) return EvilDebugMoveFor(key, shift, ctrl);
  if (fighter == DebugFighter::kViolentKen) return ViolentDebugMoveFor(key, shift, ctrl);
  return DebugMove::kNone;
}

// A held key repeats key-down events; only the first one is a press.
constexpr DebugMove DebugMoveForPress(DebugFighter fighter, DebugKey key, bool shift, bool ctrl, bool key_was_down) {
  return key_was_down ? DebugMove::kNone : DebugMoveFor(fighter, key, shift, ctrl);
}

constexpr bool IsViolentDebugMove(DebugMove move) { return move >= DebugMove::kRasetsuLight; }

// Air Tatsumaki is the only debug move that needs the fighter in the air; the others start from the ground.
constexpr DebugReject StartRejection(DebugMove move, bool host_active, bool airborne, bool guest_refuses) {
  if (move == DebugMove::kAirTatsumaki) {
    if (!airborne) return DebugReject::kNotAirborne;
  } else if (airborne) {
    return DebugReject::kIncompatibleState;
  }
  return host_active || guest_refuses ? DebugReject::kIncompatibleState : DebugReject::kNone;
}

constexpr int RasetsuStrengthFor(DebugMove move) {
  switch (move) {
    case DebugMove::kRasetsuLight: return 0;
    case DebugMove::kRasetsuMedium: return 1;
    case DebugMove::kRasetsuHeavy: return 2;
    default: return -1;
  }
}

constexpr bool IsTeleportMove(DebugMove move) {
  return move == DebugMove::kTeleportForwardFar || move == DebugMove::kTeleportBackwardFar ||
         move == DebugMove::kTeleportForwardNear || move == DebugMove::kTeleportBackwardNear;
}

// The variant the teleport recogniser reports for the same motion: bit 0 is the kick, bit 1 is backwards.
constexpr int TeleportVariantFor(DebugMove move) {
  switch (move) {
    case DebugMove::kTeleportForwardFar: return 0;
    case DebugMove::kTeleportForwardNear: return 1;
    case DebugMove::kTeleportBackwardFar: return 2;
    case DebugMove::kTeleportBackwardNear: return 3;
    default: return -1;
  }
}

const char* DebugMoveName(DebugMove move);
const char* DebugRejectName(DebugReject reason);
const char* DebugKeyLabel(DebugMove move);

// What the guest thread last saw of the fight. The host key handler reads it without touching guest memory.
struct DebugFightStatus {
  bool ticking = false;
  bool online = false;
  DebugFighter p1 = DebugFighter::kNone;
};

DebugReject HostGate(bool ultra_enabled, bool select_active, const DebugFightStatus& status);

constexpr int64_t kFightTickWindowMs = 250;
constexpr int64_t kRequestWindowMs = 500;

class DebugBridge {
 public:
  void PublishFight(int64_t now_ms, bool online, DebugFighter p1);
  DebugFightStatus Status(int64_t now_ms) const;

  void Post(DebugMove move, int64_t now_ms);
  // A request nobody picked up in time is dropped instead of firing in a later fight.
  DebugMove Take(int64_t now_ms);
  void Reset();

 private:
  std::atomic<int64_t> last_tick_ms_{-1};
  std::atomic<bool> online_{false};
  std::atomic<uint8_t> p1_{0};
  std::atomic<int64_t> posted_ms_{0};
  std::atomic<uint8_t> pending_{0};
};

DebugBridge& GlobalDebugBridge();
int64_t DebugNowMs();

}  // namespace ssf2t::character
