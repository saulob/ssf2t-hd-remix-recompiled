#pragma once

// DEBUG_SAULO: temporary Evil Ryu move hotkeys. Remove this module, evil_ryu_debug_keys.cpp and the marked calls in
// ultra_moves_hook.cpp during final review.

#include <atomic>
#include <cstdint>

namespace ssf2t::character {

enum class DebugKey : uint8_t { kNone, kF5, kF6, kF7, kF8 };

enum class DebugMove : uint8_t {
  kNone,
  kTeleportForwardFar,
  kTeleportBackwardFar,
  kTeleportForwardNear,
  kTeleportBackwardNear,
  kSenpuKyaku,
  kRagingDemon,
};

enum class DebugReject : uint8_t {
  kNone,
  kUltraOff,
  kNotInFight,
  kOnline,
  kNotEvilRyu,
  kFighterNotReady,
  kIncompatibleState,
};

constexpr DebugMove DebugMoveFor(DebugKey key, bool shift) {
  switch (key) {
    case DebugKey::kF5: return shift ? DebugMove::kTeleportForwardNear : DebugMove::kTeleportForwardFar;
    case DebugKey::kF6: return shift ? DebugMove::kTeleportBackwardNear : DebugMove::kTeleportBackwardFar;
    case DebugKey::kF7: return shift ? DebugMove::kNone : DebugMove::kSenpuKyaku;
    case DebugKey::kF8: return shift ? DebugMove::kNone : DebugMove::kRagingDemon;
    case DebugKey::kNone: break;
  }
  return DebugMove::kNone;
}

// A held key repeats key-down events; only the first one is a press.
constexpr DebugMove DebugMoveForPress(DebugKey key, bool shift, bool key_was_down) {
  return key_was_down ? DebugMove::kNone : DebugMoveFor(key, shift);
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
  bool p1_evil = false;
};

DebugReject HostGate(bool ultra_enabled, bool select_active, const DebugFightStatus& status);

constexpr int64_t kFightTickWindowMs = 250;
constexpr int64_t kRequestWindowMs = 500;

class DebugBridge {
 public:
  void PublishFight(int64_t now_ms, bool online, bool p1_evil);
  DebugFightStatus Status(int64_t now_ms) const;

  void Post(DebugMove move, int64_t now_ms);
  // A request nobody picked up in time is dropped instead of firing in a later fight.
  DebugMove Take(int64_t now_ms);
  void Reset();

 private:
  std::atomic<int64_t> last_tick_ms_{-1};
  std::atomic<bool> online_{false};
  std::atomic<bool> p1_evil_{false};
  std::atomic<int64_t> posted_ms_{0};
  std::atomic<uint8_t> pending_{0};
};

DebugBridge& GlobalDebugBridge();
int64_t DebugNowMs();

}  // namespace ssf2t::character
