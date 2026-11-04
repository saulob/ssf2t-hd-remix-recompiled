#include "online/fight_telemetry.h"

namespace ssf2t::online {

namespace {

constexpr const char* kRoster[kCharacterCount] = {
    "Ryu",   "E. Honda", "Blanka", "Guile",  "Ken",      "Chun-Li", "Zangief", "Dhalsim", "M. Bison",
    "Sagat", "Balrog",   "Vega",   "Cammy",  "T. Hawk",  "Fei Long", "Dee Jay", "Akuma", "Evil Ryu",
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

void FightReporter::Begin(uint32_t generation, uint32_t fight) {
  if (has_last_ && generation == last_generation_ && fight == last_fight_) return;
  if (active_) End(FightSnapshot{}, false);
  has_last_ = true;
  last_generation_ = generation;
  last_fight_ = fight;
  active_ = true;
  started_ = false;
  confirmed_result_ = false;
  result_ = FightResult::kNone;
  generation_ = generation;
  fight_ = fight;
  character_[0] = character_[1] = -1;
  candidate_ = FightResult::kNone;
  candidate_frame_ = 0;
  final_ = FightResult::kNone;
}

bool FightReporter::TryStart(const FightSnapshot& snapshot, bool identity_ready) {
  if (started_) return true;
  if (!identity_ready || !snapshot.valid || !IsCharacterId(snapshot.character[0]) ||
      !IsCharacterId(snapshot.character[1])) {
    return false;
  }
  started_ = true;
  character_[0] = snapshot.character[0];
  character_[1] = snapshot.character[1];
  ++starts_;
  FightReport report;
  report.kind = FightEventKind::kStart;
  report.generation = generation_;
  report.fight = fight_;
  report.p1 = character_[0];
  report.p2 = character_[1];
  Emit(report);
  return true;
}

void FightReporter::Observe(const FightSnapshot& snapshot, bool identity_ready, uint32_t frame, uint32_t confirmed) {
  if (!active_ || !TryStart(snapshot, identity_ready) || final_ != FightResult::kNone) return;
  const FightResult result = ResultOf(snapshot);
  if (result != candidate_) {
    candidate_ = result;
    candidate_frame_ = frame;
    final_ = FightResult::kNone;
  }
  if (candidate_ != FightResult::kNone && confirmed >= candidate_frame_) {
    final_ = candidate_;
    confirmed_result_ = true;
  }
}

void FightReporter::End(const FightSnapshot& last, bool identity_ready) {
  if (!active_) return;
  active_ = false;
  if (!TryStart(last, identity_ready)) {
    ++skipped_;
    return;
  }
  const FightResult result = final_ != FightResult::kNone ? final_ : candidate_;
  if (result == FightResult::kNone) return;
  result_ = result;
  ++completions_;
  FightReport report;
  report.kind = FightEventKind::kComplete;
  report.generation = generation_;
  report.fight = fight_;
  report.p1 = character_[0];
  report.p2 = character_[1];
  report.result = result;
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
