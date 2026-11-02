#include "online/fight_telemetry.h"

namespace ssf2t::online {

namespace {

constexpr const char* kRoster[kCharacterCount] = {
    "Ryu",   "E. Honda", "Blanka", "Guile",  "Ken",      "Chun-Li", "Zangief", "Dhalsim", "M. Bison",
    "Sagat", "Balrog",   "Vega",   "Cammy",  "T. Hawk",  "Fei Long", "Dee Jay", "Akuma",
};

}  // namespace

const char* CharacterName(int id) { return IsCharacterId(id) ? kRoster[id] : ""; }

const char* FightResultName(FightResult result) {
  switch (result) {
    case FightResult::kPlayer1: return "p1";
    case FightResult::kPlayer2: return "p2";
    case FightResult::kDraw: return "draw";
    default: return "";
  }
}

FightResult ResultOf(const FightSnapshot& snapshot) {
  if (!snapshot.valid) return FightResult::kNone;
  const bool first = snapshot.wins[0] > snapshot.wins_to_beat;
  const bool second = snapshot.wins[1] > snapshot.wins_to_beat;
  if (first && second) return FightResult::kDraw;
  if (first) return FightResult::kPlayer1;
  if (second) return FightResult::kPlayer2;
  return FightResult::kNone;
}

void FightReporter::Emit(const FightReport& report) {
  if (!sink_ || !sink_(report)) ++dropped_;
}

void FightReporter::Begin(uint32_t generation, uint32_t fight, const FightSnapshot& snapshot) {
  if (has_last_ && generation == last_generation_ && fight == last_fight_) return;
  if (active_) End();
  if (!snapshot.valid || !IsCharacterId(snapshot.character[0]) || !IsCharacterId(snapshot.character[1])) {
    ++skipped_;
    return;
  }
  has_last_ = true;
  last_generation_ = generation;
  last_fight_ = fight;
  active_ = true;
  generation_ = generation;
  fight_ = fight;
  character_[0] = snapshot.character[0];
  character_[1] = snapshot.character[1];
  candidate_ = FightResult::kNone;
  stable_ = 0;
  final_ = FightResult::kNone;
  ++starts_;
  FightReport report;
  report.kind = FightEventKind::kStart;
  report.generation = generation_;
  report.fight = fight_;
  report.p1 = character_[0];
  report.p2 = character_[1];
  Emit(report);
}

void FightReporter::Observe(const FightSnapshot& snapshot) {
  if (!active_ || final_ != FightResult::kNone) return;
  const bool same = snapshot.valid && snapshot.character[0] == character_[0] && snapshot.character[1] == character_[1];
  const FightResult result = same ? ResultOf(snapshot) : FightResult::kNone;
  if (result == FightResult::kNone || result != candidate_) {
    candidate_ = result;
    stable_ = result == FightResult::kNone ? 0 : 1;
  } else {
    ++stable_;
  }
  if (candidate_ != FightResult::kNone && stable_ >= kStableObservations) final_ = candidate_;
}

void FightReporter::End() {
  if (!active_) return;
  active_ = false;
  if (final_ == FightResult::kNone) return;
  ++completions_;
  FightReport report;
  report.kind = FightEventKind::kComplete;
  report.generation = generation_;
  report.fight = fight_;
  report.p1 = character_[0];
  report.p2 = character_[1];
  report.result = final_;
  Emit(report);
}

bool FightReportQueue::Push(const FightReport& report) {
  if (count_ == kCapacity) {
    ++dropped_;
    return false;
  }
  items_[(head_ + count_) % kCapacity] = report;
  ++count_;
  return true;
}

std::optional<FightReport> FightReportQueue::Pop() {
  if (!count_) return std::nullopt;
  const FightReport report = items_[head_];
  head_ = (head_ + 1) % kCapacity;
  --count_;
  return report;
}

}  // namespace ssf2t::online
