#include "character/ultra_navigation.h"

namespace ssf2t::character {

UltraMove UltraNavigation::Resolve(bool enabled, int side, uint8_t live, PadDirection direction) {
  if (side < 0 || side > 1) return {};
  if (!enabled) {
    Set(side, false);
    return {};
  }
  if (OnEvilRyu(side)) {
    if (direction != PadDirection::kLeft) return {UltraMoveAction::kHold, kEvilRyuCarrierLive};
    Set(side, false);
    return {UltraMoveAction::kLeave, kEvilRyuNeighbourLive};
  }
  if (direction == PadDirection::kRight && live == kEvilRyuNeighbourLive) {
    Set(side, true);
    return {UltraMoveAction::kEnter, kEvilRyuCarrierLive};
  }
  return {};
}

bool UltraNavigation::OnEvilRyu(int side) const {
  return side >= 0 && side < 2 && (mask_.load(std::memory_order_relaxed) & (1u << side)) != 0;
}

void UltraNavigation::Reset() {
  Set(0, false);
  Set(1, false);
}

void UltraNavigation::Set(int side, bool selected) {
  const uint8_t bit = uint8_t(1u << side);
  if (selected) {
    mask_.fetch_or(bit, std::memory_order_relaxed);
  } else {
    mask_.fetch_and(uint8_t(~bit), std::memory_order_relaxed);
  }
  SetRequestedIdentity(side, selected ? FighterIdentity::kEvilRyu : FighterIdentity::kGuest);
}

bool SelectSession::Touch(int64_t now_ms) {
  const bool started = last_ms_ == 0 || now_ms - last_ms_ >= kSelectSessionGapMilliseconds;
  if (started) ++id_;
  last_ms_ = now_ms;
  return started;
}

SelectSession& GlobalSelectSession() {
  static SelectSession session;
  return session;
}

UltraNavigation& GlobalUltraNavigation() {
  static UltraNavigation navigation;
  return navigation;
}

}  // namespace ssf2t::character
