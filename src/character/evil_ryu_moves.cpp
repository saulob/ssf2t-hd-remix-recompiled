#include "character/evil_ryu_moves.h"

namespace ssf2t::character {
namespace {

constexpr uint8_t kStepWindow = 15;
constexpr uint8_t kDemonWindow = 28;
constexpr uint16_t kDemonInputMask = 0x77F;
constexpr uint16_t kDemonSteps[] = {0x10, 0x10, 0x02, 0x100, 0x40};
constexpr uint16_t kDemonForwardKick = 0x102;
constexpr uint8_t kDemonForwardStep = 2;
constexpr uint8_t kDemonFinalStep = 4;

constexpr int16_t kDashVelocity[kTeleportVariants] = {0x8A0, 0x600, -0x8A0, -0x600};
constexpr int16_t kDashDeceleration[kTeleportVariants] = {0x24, 0x24, -0x24, -0x24};
constexpr uint16_t kDashThreshold[kTeleportVariants] = {0x240, 0x1B0, 0x240, 0x1B0};
constexpr uint8_t kShakunetsuRecovery[3] = {42, 50, 60};

constexpr uint8_t F = kNibbleForward, B = kNibbleBack, D = kNibbleDown, DF = kNibbleDownForward, DB = kNibbleDownBack;

struct Path {
  uint8_t steps[3];
  bool back;
};

constexpr Path kPaths[kTeleportPaths] = {
    {{F, D, DF}, false},  {{D, DF, D}, false},  {{DF, D, DF}, false}, {{F, DF, F}, false}, {{F, D, F}, false},
    {{B, D, DB}, true},   {{D, DB, D}, true},   {{B, DB, B}, true},   {{B, D, B}, true},   {{DB, D, DB}, true},
};

constexpr uint8_t kPathLength = 3;

bool AllNewlyChanged(uint16_t current, uint16_t previous, uint16_t mask) {
  return (~previous & current & mask) == mask || (~current & previous & mask) == mask;
}

int ButtonVariant(uint16_t current, uint16_t previous) {
  if (AllNewlyChanged(current, previous, kAllPunches)) return 0;
  if (AllNewlyChanged(current, previous, kAllKicks)) return 1;
  return -1;
}

}  // namespace

int TeleportRecognizer::Update(uint8_t nibble, uint16_t pad_current, uint16_t pad_previous) {
  const int button = ButtonVariant(pad_current, pad_previous);
  for (int path = 0; path < kTeleportPaths; ++path) {
    uint8_t& step = step_[path];
    uint8_t& timer = timer_[path];
    if (step != 0 && --timer == 0) step = 0;
    if (step < kPathLength && nibble == kPaths[path].steps[step]) {
      ++step;
      timer = kStepWindow;
    }
    if (step == kPathLength && button >= 0) {
      const int result = (kPaths[path].back ? 2 : 0) | button;
      Reset();
      return result;
    }
  }
  return -1;
}

bool RagingDemonRecognizer::Update(uint16_t input_current, uint16_t input_previous) {
  const uint16_t pressed = uint16_t(input_current & ~input_previous & kDemonInputMask);
  if (step_ == 0) {
    if ((pressed & kDemonSteps[0]) == 0) return false;
    step_ = 1;
    timer_ = kDemonWindow;
    return false;
  }
  if (--timer_ == 0) {
    Reset();
    return false;
  }
  if (step_ == kDemonForwardStep && pressed == kDemonForwardKick) {
    step_ = kDemonFinalStep;
    timer_ = kDemonWindow;
    return false;
  }
  if (pressed != kDemonSteps[step_]) return false;
  ++step_;
  timer_ = kDemonWindow;
  if (step_ < sizeof(kDemonSteps) / sizeof(kDemonSteps[0])) return false;
  Reset();
  return true;
}

DashParameters DashFor(int variant, bool facing_flag) {
  if (variant < 0 || variant >= kTeleportVariants) return {};
  DashParameters parameters{kDashVelocity[variant], kDashDeceleration[variant], kDashThreshold[variant]};
  if (!facing_flag) {
    parameters.velocity = int16_t(-parameters.velocity);
    parameters.deceleration = int16_t(-parameters.deceleration);
  }
  return parameters;
}

uint8_t ShakunetsuRecovery(uint8_t strength) {
  const size_t row = strength >> 1;
  return kShakunetsuRecovery[row < std::size(kShakunetsuRecovery) ? row : 0];
}

bool SenpuInput(uint8_t nibble, uint16_t pad_current, uint16_t pad_previous) {
  return nibble == kNibbleForward && (uint16_t(pad_current & ~pad_previous) & kMediumKick) != 0;
}

bool DashState::Step(int32_t& position, uint8_t wall_flag) {
  velocity = int16_t(velocity - deceleration);
  position += velocity * 256;
  const uint8_t direction = velocity < 0 ? 1 : 2;
  const int32_t magnitude = velocity < 0 ? -velocity : velocity;
  if (direction == wall_flag || magnitude < threshold) {
    velocity = 0;
    deceleration = 0;
    return true;
  }
  return false;
}

EvilRenderState RenderStateFor(const HostMove& move, bool in_special_class, bool unique_pose) {
  if (in_special_class && move.kind == HostMoveKind::kRagingDemon) return EvilRenderState::kShunGokuSatsu;
  if (in_special_class && move.kind == HostMoveKind::kRasetsu && move.phase == HostMovePhase::kDash) {
    return EvilRenderState::kTeleportGhost;
  }
  if (in_special_class && move.kind == HostMoveKind::kTeleport) {
    switch (move.phase) {
      case HostMovePhase::kStartup: return EvilRenderState::kTeleportStartup;
      case HostMovePhase::kDash: return EvilRenderState::kTeleportGhost;
      case HostMovePhase::kTail:
      case HostMovePhase::kRise:
      case HostMovePhase::kRecovery: return EvilRenderState::kTeleportRecovery;
    }
  }
  return unique_pose ? EvilRenderState::kUniquePose : EvilRenderState::kNormal;
}

void HostMoves::Reset() { moves_ = {}; }

HostMoves& GlobalHostMoves() {
  static HostMoves moves;
  return moves;
}

}  // namespace ssf2t::character
