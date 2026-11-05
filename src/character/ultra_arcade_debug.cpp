#include "character/ultra_arcade_debug.h"

#include <algorithm>
#include <chrono>

namespace ssf2t::character {
namespace {

constexpr std::array<const char*, 17> kGuestNames = {"Ryu",     "EHonda", "Blanka", "Guile",  "Ken",    "ChunLi",
                                                     "Zangief", "Dhalsim", "Bison",  "Sagat",  "Balrog", "Vega",
                                                     "Cammy",   "THawk",  "FeiLong", "DeeJay", "Akuma"};

ArcadeReject RejectFor(ArcadeMode mode) {
  switch (mode) {
    case ArcadeMode::kOnline: return ArcadeReject::kOnline;
    case ArcadeMode::kLocalMatch: return ArcadeReject::kLocalMatch;
    case ArcadeMode::kOther: return ArcadeReject::kNotArcade;
    case ArcadeMode::kArcade: break;
  }
  return ArcadeReject::kNone;
}

}  // namespace

const char* ArcadeRejectName(ArcadeReject reason) {
  switch (reason) {
    case ArcadeReject::kNotArcade: return "not-arcade";
    case ArcadeReject::kOnline: return "online";
    case ArcadeReject::kLocalMatch: return "local-match";
    case ArcadeReject::kMenu: return "menu";
    case ArcadeReject::kNotReady: return "not-ready";
    case ArcadeReject::kPending: return "pending";
    case ArcadeReject::kAlreadyFinalBoss: return "already-final-boss";
    case ArcadeReject::kNone: break;
  }
  return "none";
}

ArcadeReject HostGate(const ArcadeStatus& status) {
  if (!status.ticking) return ArcadeReject::kMenu;
  if (status.mode != ArcadeMode::kArcade) return RejectFor(status.mode);
  if (!status.ready) return ArcadeReject::kNotReady;
  return ArcadeReject::kNone;
}

void ArcadeBridge::Publish(int64_t now_ms, ArcadeMode mode, bool ready) {
  mode_.store(uint8_t(mode), std::memory_order_relaxed);
  ready_.store(ready, std::memory_order_relaxed);
  last_tick_ms_.store(now_ms, std::memory_order_release);
}

ArcadeStatus ArcadeBridge::Status(int64_t now_ms) const {
  const int64_t last = last_tick_ms_.load(std::memory_order_acquire);
  ArcadeStatus status;
  status.ticking = last >= 0 && now_ms - last < kTickWindowMs;
  status.mode = ArcadeMode(mode_.load(std::memory_order_relaxed));
  status.ready = ready_.load(std::memory_order_relaxed);
  return status;
}

bool ArcadeBridge::Post(int64_t now_ms) {
  if (pending_.load(std::memory_order_acquire) && now_ms - posted_ms_.load(std::memory_order_relaxed) < kRequestWindowMs) {
    return false;
  }
  posted_ms_.store(now_ms, std::memory_order_relaxed);
  pending_.store(true, std::memory_order_release);
  return true;
}

bool ArcadeBridge::Take(int64_t now_ms) {
  if (!pending_.exchange(false, std::memory_order_acquire)) return false;
  return now_ms - posted_ms_.load(std::memory_order_relaxed) < kRequestWindowMs;
}

void ArcadeBridge::Reset() {
  last_tick_ms_.store(-1, std::memory_order_relaxed);
  mode_.store(uint8_t(ArcadeMode::kOther), std::memory_order_relaxed);
  ready_.store(false, std::memory_order_relaxed);
  pending_.store(false, std::memory_order_relaxed);
}

ArcadeBridge& GlobalArcadeBridge() {
  static ArcadeBridge bridge;
  return bridge;
}

int64_t ArcadeNowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::optional<FinalBossPlan> PlanFinalBoss(const LadderState& ladder) {
  if (ladder.index > kLadderFinalBossIndex) return std::nullopt;
  FinalBossPlan plan;
  plan.bonus_stages_done = std::max(ladder.bonus_stages_done, kBonusStagesTotal);
  return plan;
}

void FinalBossRequest::OnLadderChosen(ArcadeMode mode) {
  phase_ = FinalBossPhase::kIdle;
  run_ready_ = mode == ArcadeMode::kArcade;
}

void FinalBossRequest::OnFrame(ArcadeMode mode) {
  if (mode == ArcadeMode::kArcade) return;
  phase_ = FinalBossPhase::kIdle;
  run_ready_ = false;
}

ArcadeReject FinalBossRequest::Request(ArcadeMode mode, const LadderState& ladder) {
  if (mode != ArcadeMode::kArcade) return RejectFor(mode);
  if (!Ready(ladder)) return ArcadeReject::kNotReady;
  if (phase_ == FinalBossPhase::kPending) return ArcadeReject::kPending;
  if (ladder.index >= kLadderFinalBossIndex) return ArcadeReject::kAlreadyFinalBoss;
  phase_ = FinalBossPhase::kPending;
  return ArcadeReject::kNone;
}

FinalBossStep FinalBossRequest::OnNextOpponent(ArcadeMode mode, const LadderState& ladder) {
  FinalBossStep step;
  if (phase_ != FinalBossPhase::kPending) return step;
  const std::optional<FinalBossPlan> plan = PlanFinalBoss(ladder);
  if (mode != ArcadeMode::kArcade || !plan) {
    phase_ = FinalBossPhase::kIdle;
    step.kind = FinalBossStep::Kind::kDrop;
    step.reason = mode != ArcadeMode::kArcade ? RejectFor(mode) : ArcadeReject::kAlreadyFinalBoss;
    return step;
  }
  phase_ = FinalBossPhase::kApplied;
  flag_set_ = true;
  step.kind = FinalBossStep::Kind::kApply;
  step.plan = *plan;
  return step;
}

bool FinalBossRequest::ReleaseFlag() {
  const bool was_set = flag_set_;
  flag_set_ = false;
  return was_set;
}

bool FinalBossRequest::OnEnding() {
  const bool moved = phase_ == FinalBossPhase::kApplied;
  phase_ = FinalBossPhase::kIdle;
  run_ready_ = false;
  return moved;
}

FinalBossRequest& GlobalFinalBossRequest() {
  static FinalBossRequest request;
  return request;
}

const char* ArcadeFighterName(int guest_character, bool hidden, FighterIdentity identity) {
  if (hidden) return "Akuma";
  if (identity == FighterIdentity::kEvilRyu) return "EvilRyu";
  if (identity == FighterIdentity::kViolentKen) return "ViolentKen";
  if (guest_character < 0 || guest_character >= int(kGuestNames.size())) return "unknown";
  return kGuestNames[size_t(guest_character)];
}

}  // namespace ssf2t::character
