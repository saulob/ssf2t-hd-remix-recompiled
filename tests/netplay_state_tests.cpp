// Deterministic state scope tests: the random generator and the fighter order a coin flip sets must travel with a
// snapshot, presentation-only state must not, and a replayed frame must end where a played one does. The game is
// modelled, not executed. Headless. Run: ssf2t_netplay_state_tests

#include <algorithm>
#include <cstring>

#include "netplay/guest_netplay.h"
#include "netplay/input_model.h"
#include "netplay_test_support.h"

using namespace ssf2t::netplay;
using namespace netplay_test;

namespace {

constexpr uint32_t kSetupFrame = 40;

// The game's generator: 31 words, an index, and the seed word kept beside them (0x84 bytes in all).
struct Lfg {
  uint32_t seed_word = 0;
  uint32_t words[31] = {};
  uint32_t index = 0;

  void Seed(uint32_t seed) {
    seed_word = seed;
    uint32_t x = seed ? seed : 1;
    for (uint32_t& w : words) {
      x = x * 1664525u + 1013904223u;
      w = x;
    }
    index = 0;
  }
  uint32_t Next() {
    const uint32_t next = (index + 1) % 31;
    const uint32_t other = (index + 24) % 31;
    words[next] += words[other];
    index = next;
    return words[next];
  }
};
static_assert(sizeof(Lfg) == kRngStateBytes);

struct Options {
  bool save_rng = true;
  bool save_side_order = true;
  bool presentation_draws = false;
  bool reseed_each_frame = false;
};

// A fight whose saved part is the game's (state, counter, side byte), whose coin flip draws from `rng` once at
// kSetupFrame, and whose unsaved parts are the generator, the fighter order, and presentation scratch.
class ToyFight : public Simulation {
 public:
  explicit ToyFight(Options options = {}, uint32_t seed = 12345) : options_(options) { rng_.Seed(seed); }

  bool Save(std::vector<uint8_t>& out) override {
    out.assign(sizeof(saved_), 0);
    std::memcpy(out.data(), &saved_, sizeof(saved_));
    if (options_.save_rng) Append(out, &rng_, sizeof(rng_));
    if (options_.save_side_order) Append(out, order_, sizeof(order_));
    Append(out, history_.words(), sizeof(InputWord) * kPlayers);
    return true;
  }

  bool Load(const std::vector<uint8_t>& in) override {
    const uint8_t* at = in.data();
    std::memcpy(&saved_, at, sizeof(saved_));
    at += sizeof(saved_);
    if (options_.save_rng) at = Take(&rng_, at, sizeof(rng_));
    if (options_.save_side_order) at = Take(order_, at, sizeof(order_));
    InputWord previous[kPlayers];
    std::memcpy(previous, at, sizeof(previous));
    history_.set_previous(previous);
    return true;
  }

  bool Advance(const InputWord (&inputs)[kPlayers], bool replay) override {
    if (options_.reseed_each_frame) rng_.Seed(saved_.counter + 1);
    if (saved_.counter == kSetupFrame) CoinFlip();
    for (int p = 0; p < kPlayers; ++p) {
      saved_.state = Mix(saved_.state, inputs[p], InputWord(MakeEdges(inputs[p], history_.previous(p)).pressed));
    }
    saved_.state = Mix(saved_.state, saved_.side, InputWord(order_[0]));
    if (options_.presentation_draws && !replay) rng_.Next();
    ++pace_;
    scratch_ += replay ? 1 : 2;
    ++saved_.counter;
    history_.Record(inputs);
    after[saved_.counter] = saved_.state;
    return true;
  }

  uint32_t Checksum() override { return uint32_t(saved_.state ^ saved_.side); }

  uint8_t side() const { return saved_.side; }
  uint64_t state() const { return saved_.state; }
  uint32_t pace() const { return pace_; }
  const Lfg& rng() const { return rng_; }
  uint32_t order() const { return order_[0]; }
  void set_order(uint32_t order) { order_[0] = order; }

  std::map<uint32_t, uint64_t> after;

 private:
  struct Saved {
    uint64_t state = 1;
    uint32_t counter = 0;
    uint32_t side = 1;
  };

  void CoinFlip() {
    order_[0] = 0;
    saved_.side = 1;
    if (((rng_.Next() * 2654435761u) >> 31) & 1) {
      order_[0] = 1;
      saved_.side = 2;
    }
  }
  static void Append(std::vector<uint8_t>& out, const void* from, size_t n) {
    const size_t at = out.size();
    out.resize(at + n);
    std::memcpy(out.data() + at, from, n);
  }
  static const uint8_t* Take(void* to, const uint8_t* from, size_t n) {
    std::memcpy(to, from, n);
    return from + n;
  }

  Options options_;
  Saved saved_;
  Lfg rng_;
  uint32_t order_[4] = {};
  uint32_t pace_ = 0;
  uint32_t scratch_ = 0;
  InputHistory history_;
};

struct Pass {
  std::vector<uint8_t> blob;
  uint64_t state;
  uint8_t side;
};

Pass RunPass(ToyFight& fight, const std::vector<uint8_t>& from, const InputWord (&in)[kPlayers], bool replay) {
  fight.Load(from);
  fight.Advance(in, replay);
  Pass pass;
  fight.Save(pass.blob);
  pass.state = fight.state();
  pass.side = fight.side();
  return pass;
}

// Positions a fight one frame before its coin flip, with a warm input history.
std::vector<uint8_t> BeforeSetup(ToyFight& fight) {
  const InputWord warm[kPlayers] = {0x0010, 0};
  while (true) {
    std::vector<uint8_t> blob;
    fight.Save(blob);
    if (fight.after.size() >= kSetupFrame) return blob;
    fight.Advance(warm, false);
  }
}

void TestSnapshotRestoresTheRandomGeneratorAndFighterOrder() {
  ToyFight fight;
  const std::vector<uint8_t> a = BeforeSetup(fight);
  const InputWord none[kPlayers] = {0, 0};
  fight.Advance(none, false);
  CHECK(fight.rng().index != 0 || fight.order() != 0 || fight.side() != 1);
  std::vector<uint8_t> restored;
  fight.Load(a);
  fight.Save(restored);
  CHECK(a == restored);
}

void TestBothPassesBeginFromIdenticalState() {
  ToyFight fight;
  const std::vector<uint8_t> a = BeforeSetup(fight);
  const InputWord none[kPlayers] = {0, 0};
  fight.Load(a);
  std::vector<uint8_t> first_start, second_start;
  fight.Save(first_start);
  fight.Advance(none, true);
  fight.Load(a);
  fight.Save(second_start);
  CHECK(first_start == second_start);
}

void TestReplayedAndPlayedFramesEndTheSame() {
  for (const InputWord word : {InputWord(0), InputWord(0x0010)}) {
    for (uint32_t seed = 1; seed <= 40; ++seed) {
      ToyFight fight(Options{}, seed);
      const std::vector<uint8_t> a = BeforeSetup(fight);
      const InputWord in[kPlayers] = {word, InputWord(word ? 0x0100 : 0)};
      const Pass b1 = RunPass(fight, a, in, true);
      const Pass b2 = RunPass(fight, a, in, false);
      CHECK(b1.blob == b2.blob);
    }
  }
}

void TestBothSidesOccurAndAreStable() {
  bool saw[3] = {};
  for (uint32_t seed = 1; seed <= 200 && !(saw[1] && saw[2]); ++seed) {
    ToyFight fight(Options{}, seed);
    const std::vector<uint8_t> a = BeforeSetup(fight);
    const InputWord none[kPlayers] = {0, 0};
    const Pass b1 = RunPass(fight, a, none, true);
    const Pass b2 = RunPass(fight, a, none, false);
    CHECK(b1.side == b2.side && (b1.side == 1 || b1.side == 2));
    saw[b1.side] = true;
  }
  CHECK(saw[1] && saw[2]);
}

void TestUnsavedGeneratorReproducesTheDivergence() {
  Options options;
  options.save_rng = false;
  uint32_t diverged = 0, equal = 0;
  for (uint32_t seed = 1; seed <= 60; ++seed) {
    ToyFight fight(options, seed);
    const std::vector<uint8_t> a = BeforeSetup(fight);
    const InputWord none[kPlayers] = {0, 0};
    const Pass b1 = RunPass(fight, a, none, true);
    const Pass b2 = RunPass(fight, a, none, false);
    (b1.side != b2.side ? diverged : equal) += 1;
  }
  CHECK(diverged > 0 && equal > 0);
}

void TestFighterOrderIsRestored() {
  for (const bool save : {true, false}) {
    Options options;
    options.save_side_order = save;
    ToyFight fight(options, 7);
    const std::vector<uint8_t> a = BeforeSetup(fight);
    const InputWord none[kPlayers] = {0, 0};
    fight.Advance(none, false);
    const uint32_t flipped = fight.order();
    std::vector<uint8_t> after_flip;
    fight.Save(after_flip);
    fight.set_order(flipped + 5);
    fight.Load(after_flip);
    CHECK((fight.order() == flipped) == save);
    (void)a;
  }
}

void TestPresentationOnlyStateIsNotStored() {
  ToyFight fight;
  const std::vector<uint8_t> a = BeforeSetup(fight);
  const InputWord none[kPlayers] = {0, 0};
  const uint32_t pace_before = fight.pace();
  const Pass b1 = RunPass(fight, a, none, true);
  const Pass b2 = RunPass(fight, a, none, false);
  CHECK(fight.pace() == pace_before + 2);
  CHECK(b1.blob == b2.blob);
  CHECK(a.size() == 16 + kRngStateBytes + sizeof(uint32_t) * 4 + sizeof(InputWord) * kPlayers);
}

void TestGatedDrawsNeedAPerFrameReseed() {
  Options draws;
  draws.presentation_draws = true;
  ToyFight fight(draws, 9);
  const std::vector<uint8_t> a = BeforeSetup(fight);
  const InputWord none[kPlayers] = {0, 0};
  const Pass b1 = RunPass(fight, a, none, true);
  const Pass b2 = RunPass(fight, a, none, false);
  CHECK(b1.side == b2.side);
  CHECK(b1.blob != b2.blob);

  Options reseeded = draws;
  reseeded.reseed_each_frame = true;
  ToyFight fixed(reseeded, 9);
  const std::vector<uint8_t> c = BeforeSetup(fixed);
  const Pass r1 = RunPass(fixed, c, none, true);
  const Pass r2 = RunPass(fixed, c, none, false);
  CHECK(r1.side == r2.side);
  const InputWord next[kPlayers] = {0x0010, 0};
  const Pass n1 = RunPass(fixed, r1.blob, next, false);
  const Pass n2 = RunPass(fixed, r2.blob, next, false);
  CHECK(n1.state == n2.state && n1.side == n2.side);
}

using FightPeer = PeerT<ToyFight>;

uint64_t FightReference(uint32_t frames, uint32_t delay) {
  ToyFight fight;
  for (uint32_t g = 0; g < frames; ++g) {
    const InputWord in[kPlayers] = {g < delay ? InputWord(0) : Sample(0, g - delay),
                                    g < delay ? InputWord(0) : Sample(1, g - delay)};
    fight.Advance(in, false);
  }
  return fight.state();
}

bool HistoryFollowsReference(FightPeer& peer, uint32_t frames) {
  for (uint32_t g = 1; g <= frames; ++g) {
    if (!peer.sim.after.count(g)) return false;
  }
  return peer.sim.after[frames] == FightReference(frames, 1);
}

void TestRollbackAcrossTheCoinFlipConverges() {
  for (uint64_t seed = 31; seed < 36; ++seed) {
    FightPeer a(0, 1, 8, 0), b(1, 1, 8, 0);
    CHECK(a.core.Start());
    CHECK(b.core.Start());
    RunPair(a, b, 400, {2, 3, 15, 10, false}, seed);
    const uint32_t frames = std::min(a.core.confirmed_frame(), b.core.confirmed_frame());
    CHECK(frames > kSetupFrame + 20);
    CHECK(HistoryFollowsReference(a, frames) && HistoryFollowsReference(b, frames));
    CHECK(!a.core.desync() && !b.core.desync());
    CHECK(a.core.stats().rollbacks + b.core.stats().rollbacks > 0);
  }
}

struct UnsavedRngFight : ToyFight {
  UnsavedRngFight() : ToyFight(Options{false, true, false, false}) {}
};

void TestUnsavedGeneratorBreaksRollbackAcrossTheFlip() {
  uint32_t broken = 0;
  for (uint64_t seed = 31; seed < 71; ++seed) {
    PeerT<UnsavedRngFight> a(0, 1, 8, 0), b(1, 1, 8, 0);
    CHECK(a.core.Start());
    CHECK(b.core.Start());
    RunPair(a, b, 400, {3, 4, 30, 10, false}, seed);
    const uint32_t frames = std::min(a.core.confirmed_frame(), b.core.confirmed_frame());
    bool match = frames > kSetupFrame + 20;
    for (uint32_t g = 1; g <= frames && match; ++g) {
      match = a.sim.after.count(g) && b.sim.after.count(g) && a.sim.after[g] == b.sim.after[g] &&
              a.sim.after[g] == FightReference(g, 1);
    }
    if (!match || a.core.desync() || b.core.desync()) ++broken;
  }
  CHECK(broken > 0);
}

void TestStaticMappings() {
  CHECK(kRngStateBytes == 33 * 4 && kSideOrderBytes == 16);
  CHECK(TitleUpdate023Netplay::kRngState + 4 == 0x83C33E58);
  CHECK(Base013Netplay::kRngState + 4 == 0x83C33940);
  CHECK(TitleUpdate023Netplay::kSideOrder == 0x83C19CCC && Base013Netplay::kSideOrder == 0x83C19C4C);
  CHECK(TitleUpdate023Netplay::kPaceCounter == 0x83E38474 && Base013Netplay::kPaceCounter == 0x83E37F5C);
  CHECK(TitleUpdate023Netplay::kSideSetup == 0x82318558 && Base013Netplay::kSideSetup == 0x82318618);
  CHECK(TitleUpdate023Netplay::kRandStep == 0x823E23C8 && Base013Netplay::kRandStep == 0x823E1CF0);
}

}  // namespace

int main() {
  std::printf("netplay state tests\n");
  TestSnapshotRestoresTheRandomGeneratorAndFighterOrder();
  TestBothPassesBeginFromIdenticalState();
  TestReplayedAndPlayedFramesEndTheSame();
  TestBothSidesOccurAndAreStable();
  TestUnsavedGeneratorReproducesTheDivergence();
  TestFighterOrderIsRestored();
  TestPresentationOnlyStateIsNotStored();
  TestGatedDrawsNeedAPerFrameReseed();
  TestRollbackAcrossTheCoinFlipConverges();
  TestUnsavedGeneratorBreaksRollbackAcrossTheFlip();
  TestStaticMappings();
  std::printf("%d checks, %d failures\n", netplay_test::g_checks, netplay_test::g_failures);
  return netplay_test::g_failures == 0 ? 0 : 1;
}
