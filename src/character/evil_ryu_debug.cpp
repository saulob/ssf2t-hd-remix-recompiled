#include "character/evil_ryu_debug.h"

#include <chrono>

namespace ssf2t::character {

const char* DebugMoveName(DebugMove move) {
  switch (move) {
    case DebugMove::kTeleportForwardFar: return "teleport-forward-far";
    case DebugMove::kTeleportBackwardFar: return "teleport-backward-far";
    case DebugMove::kTeleportForwardNear: return "teleport-forward-near";
    case DebugMove::kTeleportBackwardNear: return "teleport-backward-near";
    case DebugMove::kSenpuKyaku: return "senpu-kyaku";
    case DebugMove::kRagingDemon: return "raging-demon";
    case DebugMove::kShakunetsu: return "shakunetsu";
    case DebugMove::kRasetsuLight: return "rasetsukyaku-lk";
    case DebugMove::kRasetsuMedium: return "rasetsukyaku-mk";
    case DebugMove::kRasetsuHeavy: return "rasetsukyaku-hk";
    case DebugMove::kInazuma: return "inazuma-kick";
    case DebugMove::kShinbu: return "shinbu-messatsu";
    case DebugMove::kShoryukenHeavy: return "shoryuken-hp";
    case DebugMove::kAirTatsumaki: return "air-tatsumaki";
    case DebugMove::kNone: break;
  }
  return "none";
}

const char* DebugRejectName(DebugReject reason) {
  switch (reason) {
    case DebugReject::kUltraOff: return "ultra-off";
    case DebugReject::kNotInFight: return "not-in-fight";
    case DebugReject::kOnline: return "online";
    case DebugReject::kWrongFighter: return "wrong-fighter";
    case DebugReject::kNotAirborne: return "not-airborne";
    case DebugReject::kFighterNotReady: return "fighter-not-ready";
    case DebugReject::kIncompatibleState: return "incompatible-state";
    case DebugReject::kNone: break;
  }
  return "none";
}

const char* DebugKeyLabel(DebugMove move) {
  switch (move) {
    case DebugMove::kShakunetsu:
    case DebugMove::kRasetsuLight: return "F4";
    case DebugMove::kRasetsuMedium: return "Shift+F4";
    case DebugMove::kRasetsuHeavy: return "Ctrl+F4";
    case DebugMove::kInazuma:
    case DebugMove::kTeleportForwardFar: return "F5";
    case DebugMove::kTeleportForwardNear: return "Shift+F5";
    case DebugMove::kShinbu:
    case DebugMove::kTeleportBackwardFar: return "F6";
    case DebugMove::kTeleportBackwardNear: return "Shift+F6";
    case DebugMove::kShoryukenHeavy:
    case DebugMove::kSenpuKyaku: return "F7";
    case DebugMove::kAirTatsumaki:
    case DebugMove::kRagingDemon: return "F8";
    case DebugMove::kNone: break;
  }
  return "none";
}

DebugReject HostGate(bool ultra_enabled, bool select_active, const DebugFightStatus& status) {
  if (!ultra_enabled) return DebugReject::kUltraOff;
  if (select_active || !status.ticking) return DebugReject::kNotInFight;
  if (status.online) return DebugReject::kOnline;
  if (status.p1 == DebugFighter::kNone) return DebugReject::kWrongFighter;
  return DebugReject::kNone;
}

void DebugBridge::PublishFight(int64_t now_ms, bool online, DebugFighter p1) {
  online_.store(online, std::memory_order_relaxed);
  p1_.store(uint8_t(p1), std::memory_order_relaxed);
  last_tick_ms_.store(now_ms, std::memory_order_release);
}

DebugFightStatus DebugBridge::Status(int64_t now_ms) const {
  const int64_t last = last_tick_ms_.load(std::memory_order_acquire);
  DebugFightStatus status;
  status.ticking = last >= 0 && now_ms - last < kFightTickWindowMs;
  status.online = online_.load(std::memory_order_relaxed);
  status.p1 = DebugFighter(p1_.load(std::memory_order_relaxed));
  return status;
}

void DebugBridge::Post(DebugMove move, int64_t now_ms) {
  posted_ms_.store(now_ms, std::memory_order_relaxed);
  pending_.store(uint8_t(move), std::memory_order_release);
}

DebugMove DebugBridge::Take(int64_t now_ms) {
  const uint8_t taken = pending_.exchange(0, std::memory_order_acquire);
  if (taken == 0 || now_ms - posted_ms_.load(std::memory_order_relaxed) >= kRequestWindowMs) return DebugMove::kNone;
  return DebugMove(taken);
}

void DebugBridge::Reset() {
  last_tick_ms_.store(-1, std::memory_order_relaxed);
  online_.store(false, std::memory_order_relaxed);
  p1_.store(0, std::memory_order_relaxed);
  pending_.store(0, std::memory_order_relaxed);
}

DebugBridge& GlobalDebugBridge() {
  static DebugBridge bridge;
  return bridge;
}

int64_t DebugNowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

}  // namespace ssf2t::character
