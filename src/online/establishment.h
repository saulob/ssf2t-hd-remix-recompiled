#pragma once

#include <algorithm>
#include <string>

namespace ssf2t::online {

// One Player Match's network establishment, from the match starting to a usable transport, on a single absolute clock.
inline constexpr double kEstablishmentBudgetSeconds = 15.0;
// Once a transport is selected the game's lobby handshake needs time of its own, inside the same absolute budget.
inline constexpr double kHandshakeBudgetSeconds = 10.0;

class Establishment {
 public:
  enum class Phase { kIdle, kConnecting, kSelected, kFailed };

  void Begin(double now) {
    *this = {};
    phase_ = Phase::kConnecting;
    begin_ = now;
  }
  void Reset() { *this = {}; }

  // Only the first selection counts: a repeated one never moves the deadline.
  bool Select(double now) {
    if (phase_ != Phase::kConnecting) return false;
    phase_ = Phase::kSelected;
    selected_ = now;
    return true;
  }
  bool Fail(double now, std::string reason) {
    if (phase_ == Phase::kIdle || phase_ == Phase::kFailed) return false;
    phase_ = Phase::kFailed;
    failed_ = now;
    reason_ = std::move(reason);
    return true;
  }

  Phase phase() const { return phase_; }
  const std::string& reason() const { return reason_; }
  double Elapsed(double now) const { return phase_ == Phase::kIdle ? 0.0 : std::max(0.0, now - begin_); }
  double AbsoluteDeadline() const { return begin_ + kEstablishmentBudgetSeconds; }
  double HandshakeDeadline() const {
    return phase_ == Phase::kSelected ? std::min(selected_ + kHandshakeBudgetSeconds, AbsoluteDeadline())
                                      : AbsoluteDeadline();
  }
  double AbsoluteRemaining(double now) const { return std::max(0.0, AbsoluteDeadline() - now); }
  double HandshakeRemaining(double now) const { return std::max(0.0, HandshakeDeadline() - now); }

  // Waiting for a transport past the absolute budget.
  bool ConnectingExpired(double now) const { return phase_ == Phase::kConnecting && now >= AbsoluteDeadline(); }
  // The game's own lobby wait may go on until this passes (or the establishment failed).
  bool GuestWaitExpired(double now) const {
    return phase_ == Phase::kFailed || ((phase_ == Phase::kConnecting || phase_ == Phase::kSelected) && now >= HandshakeDeadline());
  }

 private:
  Phase phase_ = Phase::kIdle;
  double begin_ = 0;
  double selected_ = 0;
  double failed_ = 0;
  std::string reason_;
};

}  // namespace ssf2t::online
