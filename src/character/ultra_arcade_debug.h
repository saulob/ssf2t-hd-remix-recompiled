#pragma once

// DEBUG_SAULO: temporary F1 Arcade final-boss shortcut. Remove this module, ultra_arcade_debug_hook.cpp,
// ultra_arcade_debug_keys.cpp and the marked calls in ssf2t_hd_remix_app.h during final review.
//
// F1 moves the guest's own Arcade ladder to its last slot, M. Bison, through the same state the game's built-in
// "Bison only" start code sets. The fight, the winner and the ending are the guest's own.

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>

#include "character/fighter_identity.h"

namespace ssf2t::character {

// The ladder is 16 halfwords per run: eight random opponents, Balrog, Vega, Sagat, then M. Bison, then 0x20. The guest
// reads slot [index] as the next opponent and ends the run when it reads 0x20.
inline constexpr uint16_t kLadderFinalBossIndex = 11;
inline constexpr uint16_t kLadderBisonId = 8;
inline constexpr uint16_t kLadderEndId = 0x20;
// The bonus stages come after fights 3, 6 and 9; three done means none is left.
inline constexpr uint8_t kBonusStagesTotal = 3;

enum class ArcadeMode : uint8_t { kArcade, kLocalMatch, kOnline, kOther };

// The guest's game-mode byte: 0 Arcade, 1 Local Match, 3 Online. An online session role wins over the byte.
constexpr ArcadeMode ArcadeModeOf(uint8_t game_mode, bool online_session) {
  if (online_session || game_mode == 3) return ArcadeMode::kOnline;
  if (game_mode == 0) return ArcadeMode::kArcade;
  if (game_mode == 1) return ArcadeMode::kLocalMatch;
  return ArcadeMode::kOther;
}

enum class ArcadeReject : uint8_t {
  kNone,
  kNotArcade,
  kOnline,
  kLocalMatch,
  kMenu,
  kNotReady,
  kPending,
  kAlreadyFinalBoss,
};

const char* ArcadeRejectName(ArcadeReject reason);

// What the guest thread last saw of the game. The key handler reads it without touching guest memory.
struct ArcadeStatus {
  bool ticking = false;
  ArcadeMode mode = ArcadeMode::kOther;
  // An Arcade run has chosen its ladder and has not ended.
  bool ready = false;
};

ArcadeReject HostGate(const ArcadeStatus& status);

// A held key repeats key-down events; only the first one is a press.
constexpr bool IsFirstPress(bool key_was_down) { return !key_was_down; }

constexpr int64_t kTickWindowMs = 250;
constexpr int64_t kRequestWindowMs = 500;

class ArcadeBridge {
 public:
  void Publish(int64_t now_ms, ArcadeMode mode, bool ready);
  ArcadeStatus Status(int64_t now_ms) const;

  // False when a request is already waiting for the guest thread.
  bool Post(int64_t now_ms);
  // A request nobody picked up in time is dropped instead of firing in a later run.
  bool Take(int64_t now_ms);
  void Reset();

 private:
  std::atomic<int64_t> last_tick_ms_{-1};
  std::atomic<uint8_t> mode_{uint8_t(ArcadeMode::kOther)};
  std::atomic<bool> ready_{false};
  std::atomic<int64_t> posted_ms_{0};
  std::atomic<bool> pending_{false};
};

ArcadeBridge& GlobalArcadeBridge();
int64_t ArcadeNowMs();

// The fields of the guest's ladder this shortcut reads and writes.
struct LadderState {
  uint16_t index = 0;
  uint8_t bonus_stages_done = 0;
  // The player's choice is confirmed and the guest has built the ladder; the guest clears this with its arena.
  bool chosen = false;
};

// Every write the shortcut makes, and no other: fighters, identities, the winner, the ending word and the screens
// stay the guest's.
enum class ArcadeWrite : uint8_t { kLadderIndex, kBonusStagesDone, kBossOnlyFlag, kAkumaOnlyFlag };

inline constexpr std::array<ArcadeWrite, 4> kPlanWrites = {ArcadeWrite::kLadderIndex, ArcadeWrite::kBonusStagesDone,
                                                           ArcadeWrite::kBossOnlyFlag, ArcadeWrite::kAkumaOnlyFlag};

struct FinalBossPlan {
  uint16_t ladder_index = kLadderFinalBossIndex;
  uint8_t bonus_stages_done = kBonusStagesTotal;
  // The game's "Bison only" code: the Akuma rules answer no and the fight is the normal M. Bison.
  uint8_t boss_only = 1;
  uint8_t akuma_only = 0;
};

// The ladder only moves forward: nothing to do once it is past the last slot, which is the ending. At the last slot
// itself the plan keeps the Akuma rules off for the fight that is next.
std::optional<FinalBossPlan> PlanFinalBoss(const LadderState& ladder);

// The guest side of the plan; the hook writes guest memory, the tests record.
template <typename Writer>
void ApplyFinalBossPlan(Writer& writer, const FinalBossPlan& plan) {
  writer.Write(ArcadeWrite::kLadderIndex, plan.ladder_index);
  writer.Write(ArcadeWrite::kBonusStagesDone, plan.bonus_stages_done);
  writer.Write(ArcadeWrite::kBossOnlyFlag, plan.boss_only);
  writer.Write(ArcadeWrite::kAkumaOnlyFlag, plan.akuma_only);
}

enum class FinalBossPhase : uint8_t { kIdle, kPending, kApplied };

struct FinalBossStep {
  enum class Kind : uint8_t { kNone, kApply, kDrop };
  Kind kind = Kind::kNone;
  ArcadeReject reason = ArcadeReject::kNone;
  FinalBossPlan plan;
};

// The guest thread's view of one request, from F1 to the ending.
class FinalBossRequest {
 public:
  FinalBossPhase phase() const { return phase_; }
  bool Ready(const LadderState& ladder) const { return run_ready_ && ladder.chosen; }

  // A new run chose its ladder: nothing from an earlier one carries over.
  void OnLadderChosen(ArcadeMode mode);
  // True once after the shortcut set the guest's Bison-only flag: the caller clears it when the run is over, so the
  // flag never reaches a run that did not ask for it.
  bool ReleaseFlag();
  // Every frame; a run that stopped being Arcade is over.
  void OnFrame(ArcadeMode mode);

  // The key was pressed. kNone when the request now waits for the next opponent.
  ArcadeReject Request(ArcadeMode mode, const LadderState& ladder);
  // The guest is about to pick the next opponent.
  FinalBossStep OnNextOpponent(ArcadeMode mode, const LadderState& ladder);
  // The guest chose its ending. True when this run was moved to the final boss.
  bool OnEnding();

 private:
  FinalBossPhase phase_ = FinalBossPhase::kIdle;
  bool run_ready_ = false;
  bool flag_set_ = false;
};

FinalBossRequest& GlobalFinalBossRequest();

// The name in the log lines: the chosen fighter, never the carrier it plays on.
const char* ArcadeFighterName(int guest_character, bool hidden, FighterIdentity identity);

}  // namespace ssf2t::character
