#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace ssf2t::online {

// The game's internal character ids (its own name table, 0 to 16); the Worker has the same list.
inline constexpr int kCharacterCount = 17;
inline constexpr int kAkumaCharacter = 16;

constexpr bool IsCharacterId(int id) { return id >= 0 && id < kCharacterCount; }
// Empty outside the roster.
const char* CharacterName(int id);

enum class FightResult { kNone, kPlayer1, kPlayer2, kDraw };

// "p1", "p2" or "draw"; empty for kNone.
const char* FightResultName(FightResult result);

// What the fight holds at one frame: the two fighters' characters and round wins, and the round wins a fighter has to
// exceed for the fight to be over.
struct FightSnapshot {
  bool valid = false;
  int character[2] = {-1, -1};
  int wins[2] = {0, 0};
  int wins_to_beat = 0;
};

// kNone while neither fighter has exceeded `wins_to_beat`; kDraw when both have.
FightResult ResultOf(const FightSnapshot& snapshot);

enum class FightEventKind { kStart, kComplete };

struct FightReport {
  FightEventKind kind = FightEventKind::kStart;
  uint32_t generation = 0;  // the online match's count in this process
  uint32_t fight = 0;       // the fight's index within the match
  int p1 = -1;
  int p2 = -1;
  FightResult result = FightResult::kNone;
};

// Only an online match's fight is reported: Local Match, Arcade, Training and the rest never are.
constexpr bool FightReportable(bool online_driven, uint8_t game_mode, uint8_t online_mode) {
  return online_driven && game_mode == online_mode;
}

// Turns what the fight runtime sees into at most one start and one completion per fight. It reads and decides; it never
// writes the game and never waits: the sink answers false when it could not take a report, which is only counted.
class FightReporter {
 public:
  using Sink = std::function<bool(const FightReport&)>;

  // A result must hold this many consecutive observations before it is final: a rollback rewrites at most a few frames.
  static constexpr int kStableObservations = 20;

  explicit FightReporter(Sink sink) : sink_(std::move(sink)) {}

  // The fight became authoritative. The same (generation, fight) again, as after a retry, reports nothing.
  void Begin(uint32_t generation, uint32_t fight, const FightSnapshot& snapshot);
  // Every frame of the running fight.
  void Observe(const FightSnapshot& snapshot);
  // The fight is over or was cut short; a completion is reported only for a result that became final.
  void End();

  bool active() const { return active_; }
  uint32_t starts() const { return starts_; }
  uint32_t completions() const { return completions_; }
  uint32_t dropped() const { return dropped_; }
  uint32_t skipped() const { return skipped_; }

 private:
  void Emit(const FightReport& report);

  Sink sink_;
  bool active_ = false;
  bool has_last_ = false;
  uint32_t last_generation_ = 0;
  uint32_t last_fight_ = 0;
  uint32_t generation_ = 0;
  uint32_t fight_ = 0;
  int character_[2] = {-1, -1};
  FightResult candidate_ = FightResult::kNone;
  int stable_ = 0;
  FightResult final_ = FightResult::kNone;
  uint32_t starts_ = 0;
  uint32_t completions_ = 0;
  uint32_t dropped_ = 0;
  uint32_t skipped_ = 0;
};

// Reports waiting for the Update tick, so the fight's thread never touches the network or the service's lock.
class FightReportQueue {
 public:
  static constexpr size_t kCapacity = 32;

  // False (and counted) when full: the newest report is the one dropped.
  bool Push(const FightReport& report);
  std::optional<FightReport> Pop();
  size_t size() const { return count_; }
  uint32_t dropped() const { return dropped_; }

 private:
  FightReport items_[kCapacity];
  size_t head_ = 0;
  size_t count_ = 0;
  uint32_t dropped_ = 0;
};

}  // namespace ssf2t::online
