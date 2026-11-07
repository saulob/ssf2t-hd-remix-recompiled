#include "character/ultra_navigation.h"

namespace ssf2t::character {

namespace {

constexpr int kViolentKenMaskShift = 2;

}  // namespace

UltraMove UltraNavigation::Resolve(bool evil_ryu_available, bool violent_ken_available, int side, uint8_t live,
                                   PadDirection direction) {
  if (side < 0 || side > 1) return {};
  if ((OnEvilRyu(side) && !evil_ryu_available) || (OnViolentKen(side) && !violent_ken_available)) {
    Set(side, FighterIdentity::kGuest);
  }
  if (OnEvilRyu(side)) {
    if (direction == PadDirection::kLeft) {
      Set(side, FighterIdentity::kGuest);
      return {UltraMoveAction::kLeave, kEvilRyuNeighbourLive};
    }
    if (direction == PadDirection::kDown && violent_ken_available) {
      Set(side, FighterIdentity::kViolentKen);
      return {UltraMoveAction::kSwitch, kViolentKenCarrierLive};
    }
    return {UltraMoveAction::kHold, kEvilRyuCarrierLive};
  }
  if (OnViolentKen(side)) {
    if (direction == PadDirection::kLeft) {
      Set(side, FighterIdentity::kGuest);
      return {UltraMoveAction::kLeave, kViolentKenNeighbourLive};
    }
    if (direction == PadDirection::kUp && evil_ryu_available) {
      Set(side, FighterIdentity::kEvilRyu);
      return {UltraMoveAction::kSwitch, kEvilRyuCarrierLive};
    }
    return {UltraMoveAction::kHold, kViolentKenCarrierLive};
  }
  if (direction != PadDirection::kRight) return {};
  if (live == kEvilRyuNeighbourLive && evil_ryu_available) {
    Set(side, FighterIdentity::kEvilRyu);
    return {UltraMoveAction::kEnter, kEvilRyuCarrierLive};
  }
  if (live == kViolentKenNeighbourLive && violent_ken_available) {
    Set(side, FighterIdentity::kViolentKen);
    return {UltraMoveAction::kEnter, kViolentKenCarrierLive};
  }
  return {};
}

bool UltraNavigation::OnEvilRyu(int side) const {
  return side >= 0 && side < 2 && (mask_.load(std::memory_order_relaxed) & (1u << side)) != 0;
}

bool UltraNavigation::OnViolentKen(int side) const {
  return side >= 0 && side < 2 &&
         (mask_.load(std::memory_order_relaxed) & (1u << (side + kViolentKenMaskShift))) != 0;
}

void UltraNavigation::Leave(int side) {
  if (side >= 0 && side < 2) Set(side, FighterIdentity::kGuest);
}

void UltraNavigation::Reset() {
  Set(0, FighterIdentity::kGuest);
  Set(1, FighterIdentity::kGuest);
}

void UltraNavigation::Restore(uint8_t mask) {
  for (int side = 0; side < 2; ++side) {
    const bool evil = (mask & (1u << side)) != 0;
    const bool violent = (mask & (1u << (side + kViolentKenMaskShift))) != 0;
    Set(side, evil ? FighterIdentity::kEvilRyu : violent ? FighterIdentity::kViolentKen : FighterIdentity::kGuest);
  }
}

void UltraNavigation::Set(int side, FighterIdentity identity) {
  const uint8_t evil = uint8_t(1u << side);
  const uint8_t violent = uint8_t(1u << (side + kViolentKenMaskShift));
  mask_.fetch_and(uint8_t(~(evil | violent)), std::memory_order_relaxed);
  if (identity == FighterIdentity::kEvilRyu) mask_.fetch_or(evil, std::memory_order_relaxed);
  if (identity == FighterIdentity::kViolentKen) mask_.fetch_or(violent, std::memory_order_relaxed);
  SetRequestedIdentity(side, identity);
}

uint8_t UltraMaskKeptByDefender(uint8_t mask, const SelectEntry& entry) {
  if (!entry.arcade) return 0;
  uint8_t kept = 0;
  for (int side = 0; side < 2; ++side) {
    const SelectEntrySide& self = entry.sides[size_t(side)];
    if (!entry.sides[size_t(1 - side)].joined || self.joined || !self.played || self.hidden) continue;
    const uint8_t evil = uint8_t(1u << side);
    const uint8_t violent = uint8_t(1u << (side + kViolentKenMaskShift));
    if (entry.evil_ryu_available && self.guest_character == kRyuGuestCharacter) kept |= mask & evil;
    if (entry.violent_ken_available && self.guest_character == kKenGuestCharacter) kept |= mask & violent;
  }
  return kept;
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
