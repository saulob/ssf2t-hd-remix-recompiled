// Input contract tests: what a resimulated frame must be given (current and previous words per player), and that a
// snapshot carries every piece of input state the step reads. The guest is modelled, not executed. Headless.
// Run: ssf2t_netplay_input_tests

#include <algorithm>
#include <cstring>

#include "netplay/input_model.h"
#include "netplay/rollback_core.h"
#include "netplay_test_support.h"

using namespace ssf2t::netplay;
using namespace netplay_test;

namespace {

constexpr InputWord kButton = 0x0010;
constexpr InputWord kOther = 0x0100;

// A guest with the three kinds of input state the game has: what its own save covers (`state`, the decoded words),
// what the set-input callback rewrites (`table`), and what the step shifts and nobody restores unless asked
// (`ring`, `repeat`). `save_extras` says whether the snapshot carries the last group.
class EdgeGuest : public Simulation {
 public:
  explicit EdgeGuest(bool save_extras) : save_extras_(save_extras) {}

  bool Save(std::vector<uint8_t>& out) override {
    out.resize(sizeof(saved_) + (save_extras_ ? sizeof(extras_) : 0) + sizeof(InputWord) * kPlayers);
    uint8_t* at = out.data();
    std::memcpy(at, &saved_, sizeof(saved_));
    at += sizeof(saved_);
    if (save_extras_) {
      std::memcpy(at, &extras_, sizeof(extras_));
      at += sizeof(extras_);
    }
    std::memcpy(at, history_.words(), sizeof(InputWord) * kPlayers);
    return true;
  }

  bool Load(const std::vector<uint8_t>& in) override {
    const uint8_t* at = in.data();
    std::memcpy(&saved_, at, sizeof(saved_));
    at += sizeof(saved_);
    if (save_extras_) {
      std::memcpy(&extras_, at, sizeof(extras_));
      at += sizeof(extras_);
    }
    InputWord previous[kPlayers];
    std::memcpy(previous, at, sizeof(previous));
    history_.set_previous(previous);
    return true;
  }

  bool Advance(const InputWord (&inputs)[kPlayers], bool replay) override {
    for (int p = 0; p < kPlayers; ++p) SetInput(p, inputs[p], history_.previous(p));
    Step();
    history_.Record(inputs);
    after[saved_.counter] = saved_.state;
    effects += replay ? 0 : 1;
    return true;
  }

  uint32_t Checksum() override { return uint32_t(saved_.state ^ (saved_.state >> 32)); }

  void SetInput(int player, InputWord current, InputWord previous) {
    extras_.table[player] = MakeEdges(current, previous);
    for (int q = 0; q < kPlayers; ++q) {
      saved_.decoded_previous[q] = saved_.decoded_current[q];
      saved_.decoded_current[q] = InputWord(extras_.table[q].current);
    }
  }

  void Step() {
    for (int p = 0; p < kPlayers; ++p) {
      const uint64_t edges = uint64_t(extras_.table[p].pressed) | uint64_t(extras_.table[p].released) << 16;
      saved_.state = Mix(saved_.state, InputWord(edges ^ (edges >> 16)), InputWord(extras_.ring[p][2] + extras_.repeat[p]));
      saved_.state = Mix(saved_.state, saved_.decoded_current[p], saved_.decoded_previous[p]);
      for (int i = 3; i > 0; --i) extras_.ring[p][i] = extras_.ring[p][i - 1];
      extras_.ring[p][0] = saved_.decoded_current[p];
      extras_.repeat[p] = saved_.decoded_current[p] ? extras_.repeat[p] + 1 : 0;
    }
    ++saved_.counter;
  }

  InputEdges Table(int player) const { return extras_.table[player]; }
  uint64_t state() const { return saved_.state; }

  uint32_t effects = 0;
  std::map<uint32_t, uint64_t> after;

 private:
  struct Saved {
    uint64_t state = 1;
    uint32_t counter = 0;
    InputWord decoded_current[kPlayers] = {};
    InputWord decoded_previous[kPlayers] = {};
  };
  struct Extras {
    InputEdges table[kPlayers] = {};
    InputWord ring[kPlayers][4] = {};
    uint32_t repeat[kPlayers] = {};
  };

  bool save_extras_;
  Saved saved_;
  Extras extras_;
  InputHistory history_;
};

struct ExtrasSim final : EdgeGuest {
  ExtrasSim() : EdgeGuest(true) {}
};

// Runs one frame from `snapshot` with the given words and returns the state after it.
uint64_t FrameFrom(EdgeGuest& guest, const std::vector<uint8_t>& snapshot, const InputWord (&inputs)[kPlayers],
                   bool replay) {
  guest.Load(snapshot);
  guest.Advance(inputs, replay);
  return guest.state();
}

void Warm(EdgeGuest& guest) {
  const InputWord steady[kPlayers] = {kButton, 0};
  for (int i = 0; i < 5; ++i) guest.Advance(steady, false);
}

void TestSameStateAndInputsGiveTheSameResult() {
  EdgeGuest guest(true);
  Warm(guest);
  std::vector<uint8_t> a;
  guest.Save(a);
  const InputWord in[kPlayers] = {kButton, kOther};
  const uint64_t first = FrameFrom(guest, a, in, true);
  std::vector<uint8_t> b1;
  guest.Save(b1);
  const uint64_t second = FrameFrom(guest, a, in, false);
  std::vector<uint8_t> b2;
  guest.Save(b2);
  CHECK(first == second);
  CHECK(b1 == b2);
}

void TestChangingTheCurrentWordChangesTheResult() {
  EdgeGuest guest(true);
  Warm(guest);
  std::vector<uint8_t> a;
  guest.Save(a);
  const InputWord none[kPlayers] = {0, 0};
  const InputWord some[kPlayers] = {kButton, 0};
  const InputWord other[kPlayers] = {kOther, 0};
  const uint64_t r0 = FrameFrom(guest, a, none, false);
  const uint64_t r1 = FrameFrom(guest, a, some, false);
  const uint64_t r2 = FrameFrom(guest, a, other, false);
  CHECK(r0 != r1 && r1 != r2 && r0 != r2);
}

void TestEdgeDerivation() {
  const InputEdges pressed = MakeEdges(kButton, 0);
  CHECK(pressed.current == kButton && pressed.previous == 0 && pressed.pressed == kButton && pressed.released == 0);
  const InputEdges held = MakeEdges(kButton, kButton);
  CHECK(held.pressed == 0 && held.released == 0 && held.current == kButton && held.previous == kButton);
  const InputEdges released = MakeEdges(0, kButton);
  CHECK(released.pressed == 0 && released.released == kButton && released.current == 0);
  const InputEdges mixed = MakeEdges(kButton | kOther, kButton | 0x0001);
  CHECK(mixed.pressed == kOther && mixed.released == 0x0001);
  const InputEdges all = MakeEdges(0xFFFF, 0x0000);
  CHECK(all.pressed == 0xFFFF && all.released == 0);
}

void TestPressedHeldReleasedGiveDifferentFrames() {
  EdgeGuest guest(true);
  Warm(guest);
  std::vector<uint8_t> a;
  guest.Save(a);

  const auto frame = [&](InputWord current, InputWord previous) {
    guest.Load(a);
    guest.SetInput(0, current, previous);
    guest.SetInput(1, 0, 0);
    guest.Step();
    return guest.state();
  };
  const uint64_t pressed = frame(kButton, 0);
  const uint64_t held = frame(kButton, kButton);
  const uint64_t released = frame(0, kButton);
  const uint64_t idle = frame(0, 0);
  CHECK(pressed != held && held != released && released != idle && pressed != released && pressed != idle);

  CHECK(frame(kButton, 0) == pressed);
  CHECK(frame(kButton, kButton) == held);
  CHECK(frame(0, kButton) == released);
}

void TestPlayersAreIndependent() {
  EdgeGuest guest(true);
  std::vector<uint8_t> a;
  guest.Save(a);
  const InputWord only_p0[kPlayers] = {kButton, 0};
  const InputWord only_p1[kPlayers] = {0, kButton};
  const InputWord both[kPlayers] = {kButton, kButton};
  const uint64_t r0 = FrameFrom(guest, a, only_p0, false);
  const InputEdges p0_edges = guest.Table(0), p1_edges = guest.Table(1);
  CHECK(p0_edges.pressed == kButton && p1_edges.pressed == 0 && p1_edges.current == 0);
  const uint64_t r1 = FrameFrom(guest, a, only_p1, false);
  CHECK(guest.Table(0).pressed == 0 && guest.Table(1).pressed == kButton);
  const uint64_t r2 = FrameFrom(guest, a, both, false);
  CHECK(guest.Table(0).pressed == kButton && guest.Table(1).pressed == kButton);
  CHECK(r0 != r1 && r1 != r2 && r0 != r2);
}

void TestPreviousWordsTravelWithTheSnapshot() {
  EdgeGuest guest(true);
  const InputWord first[kPlayers] = {kButton, kOther};
  guest.Advance(first, false);
  std::vector<uint8_t> a;
  guest.Save(a);
  const InputWord next[kPlayers] = {kButton, kOther};
  guest.Advance(next, false);
  CHECK(guest.Table(0).pressed == 0 && guest.Table(1).pressed == 0);
  guest.Load(a);
  guest.Advance(next, false);
  CHECK(guest.Table(0).pressed == 0 && guest.Table(0).previous == kButton);
  guest.Load(a);
  const InputWord changed[kPlayers] = {0, kOther};
  guest.Advance(changed, false);
  CHECK(guest.Table(0).released == kButton);
}

void TestRestoreCarriesEveryInputState() {
  EdgeGuest with(true);
  Warm(with);
  std::vector<uint8_t> a, a_after_restore;
  with.Save(a);
  const InputWord in[kPlayers] = {kButton, 0};
  with.Advance(in, true);
  with.Load(a);
  with.Save(a_after_restore);
  CHECK(a == a_after_restore);
  CHECK(a.size() > 0);
}

void TestNoStaleInputStateLeaksBetweenPasses() {
  const InputWord in[kPlayers] = {kButton, kOther};
  EdgeGuest complete(true);
  Warm(complete);
  std::vector<uint8_t> a, b1, b2;
  complete.Save(a);
  complete.Advance(in, true);
  complete.Save(b1);
  complete.Load(a);
  complete.Advance(in, false);
  complete.Save(b2);
  CHECK(b1 == b2);

  EdgeGuest partial(false);
  Warm(partial);
  partial.Save(a);
  partial.Advance(in, true);
  partial.Save(b1);
  partial.Load(a);
  partial.Advance(in, false);
  partial.Save(b2);
  CHECK(b1 != b2);
}

using EdgePeer = PeerT<ExtrasSim>;

uint64_t EdgeReference(uint32_t frames, uint32_t delay) {
  EdgeGuest guest(true);
  for (uint32_t g = 0; g < frames; ++g) {
    const InputWord in[kPlayers] = {g < delay ? InputWord(0) : Sample(0, g - delay),
                                    g < delay ? InputWord(0) : Sample(1, g - delay)};
    guest.Advance(in, false);
  }
  return guest.state();
}

void RunWrapCase(Frame start, const Network& net, uint64_t seed) {
  EdgePeer a(0, 1, 8, start), b(1, 1, 8, start);
  CHECK(a.core.Start());
  CHECK(b.core.Start());
  RunPair(a, b, 500, net, seed);
  const uint32_t frames = std::min(a.core.confirmed_frame() - start, b.core.confirmed_frame() - start);
  CHECK(frames > 100);
  bool same = true;
  for (uint32_t g = 1; g <= frames && same; ++g) {
    same = a.sim.after.count(g) && b.sim.after.count(g) && a.sim.after[g] == b.sim.after[g];
  }
  CHECK(same);
  CHECK(a.sim.after[frames] == EdgeReference(frames, 1));
  CHECK(!a.core.desync() && !b.core.desync());
  CHECK(a.core.stats().rollbacks + b.core.stats().rollbacks > 0);
}

void TestEdgeSensitiveSimulationSurvivesRollbackAndWrap() {
  RunWrapCase(0, {2, 3, 15, 10, false}, 21);
  RunWrapCase(0xFFFFFFF0u, {2, 3, 15, 10, false}, 22);
  RunWrapCase(0xFFFFFF00u, {4, 2, 25, 5, false}, 23);
}

}  // namespace

int main() {
  std::printf("netplay input tests\n");
  TestSameStateAndInputsGiveTheSameResult();
  TestChangingTheCurrentWordChangesTheResult();
  TestEdgeDerivation();
  TestPressedHeldReleasedGiveDifferentFrames();
  TestPlayersAreIndependent();
  TestPreviousWordsTravelWithTheSnapshot();
  TestRestoreCarriesEveryInputState();
  TestNoStaleInputStateLeaksBetweenPasses();
  TestEdgeSensitiveSimulationSurvivesRollbackAndWrap();
  std::printf("%d checks, %d failures\n", netplay_test::g_checks, netplay_test::g_failures);
  return netplay_test::g_failures == 0 ? 0 : 1;
}
