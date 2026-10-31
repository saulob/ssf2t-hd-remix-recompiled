#pragma once

#include <cstdint>
#include <string>

namespace ssf2t {

// One accepted invite, from the port's Accept until the game joins it (JoinLobbyFromInvite) or declines it.
class InviteLifecycle {
 public:
  enum class Gate { kNoInvite, kFirst, kRepeat };
  // Accepted until the game's lobby teardown starts and ends, then the game joins it and the port's join runs.
  enum class Phase { kNone, kAccepted, kTeardown, kReady, kJoinRequested, kJoining };

  bool Deliver(const std::string& session) { return Deliver(session, generation_ + 1); }

  // False for the invite already waiting for the game; a different session replaces it.
  bool Deliver(const std::string& session, uint32_t generation) {
    if (pending_ && session == session_) return false;
    generation_ = generation;
    session_ = session;
    pending_ = true;
    phase_ = Phase::kAccepted;
    gate_generation_ = 0;
    repeats_ = 0;
    return true;
  }

  // The game's own leave of its current lobby: true once per accepted invite.
  bool BeginTeardown() {
    if (phase_ != Phase::kAccepted) return false;
    phase_ = Phase::kTeardown;
    return true;
  }

  // The game is back in the Online Match zone, or joins without having left anything.
  bool CompleteTeardown() {
    if (phase_ != Phase::kTeardown) return false;
    phase_ = Phase::kReady;
    return true;
  }

  // The port's remote join for the accepted session: true once per accepted invite.
  bool BeginJoining(const std::string& session) {
    if (phase_ != Phase::kJoinRequested || session != session_) return false;
    phase_ = Phase::kJoining;
    return true;
  }

  // The game's accept box calls RequestOnlineSignIn(1) on every frame it is open and ignores the result.
  Gate ConfirmGate() {
    if (!pending_) return Gate::kNoInvite;
    if (gate_generation_ == generation_) {
      ++repeats_;
      return Gate::kRepeat;
    }
    gate_generation_ = generation_;
    return Gate::kFirst;
  }

  // False when nothing was waiting.
  bool Consume() {
    const bool was = pending_;
    pending_ = false;
    if (was) phase_ = Phase::kJoinRequested;
    return was;
  }

  // The game declined the accepted invite: nothing is joined.
  bool Cancel() {
    const bool was = pending_;
    pending_ = false;
    phase_ = Phase::kNone;
    return was;
  }

  uint32_t generation() const { return generation_; }
  const std::string& session() const { return session_; }
  bool pending() const { return pending_; }
  Phase phase() const { return phase_; }
  uint32_t repeats() const { return repeats_; }

 private:
  uint32_t generation_ = 0;
  std::string session_;
  bool pending_ = false;
  Phase phase_ = Phase::kNone;
  uint32_t gate_generation_ = 0;
  uint32_t repeats_ = 0;
};

}  // namespace ssf2t
