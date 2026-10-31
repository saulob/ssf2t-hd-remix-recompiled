// Rollback core tests: prediction, late-input correction, the rollback window, replay without effects, frame
// wraparound, checksum desync and the redundancy window, on a toy simulation. Pure logic, no sockets or game files.
// Run: ssf2t_netplay_rollback_tests

#include <algorithm>
#include <deque>

#include "netplay/rollback_core.h"
#include "netplay_test_support.h"

using namespace ssf2t::netplay;
using namespace netplay_test;

namespace {

using Peer = PeerT<ToySim>;

bool ConfirmedHistoryMatches(const Peer& a, const Peer& b, uint32_t delay) {
  const uint32_t frames = std::min(a.core.confirmed_frame() - a.core.config().start_frame,
                                   b.core.confirmed_frame() - b.core.config().start_frame);
  if (frames < 30) return false;
  for (uint32_t g = 1; g <= frames; ++g) {
    const auto ea = a.sim.after.find(g);
    const auto eb = b.sim.after.find(g);
    if (ea == a.sim.after.end() || eb == b.sim.after.end()) return false;
    if (ea->second != eb->second || ea->second != Reference(g, delay)) return false;
  }
  return true;
}

void RunScenario(const char* name, Network net, uint32_t delay, Frame start, uint64_t seed, bool expect_rollbacks) {
  std::printf("  %s\n", name);
  Peer a(0, delay, 8, start), b(1, delay, 8, start);
  CHECK(a.core.Start());
  CHECK(b.core.Start());
  RunPair(a, b, 600, net, seed);
  CHECK(ConfirmedHistoryMatches(a, b, delay));
  CHECK(!a.core.desync() && !b.core.desync());
  CHECK(!a.core.faulted() && !b.core.faulted());
  CHECK(a.core.stats().max_depth <= 8 && b.core.stats().max_depth <= 8);
  CHECK(a.sim.effects == a.core.stats().frames);
  CHECK(b.sim.effects == b.core.stats().frames);
  if (expect_rollbacks) CHECK(a.core.stats().rollbacks + b.core.stats().rollbacks > 0);
  CHECK(FrameDiff(a.core.current_frame(), a.core.confirmed_frame()) <= 8);
}

void TestEquivalenceUnderNetworkFaults() {
  RunScenario("clean link", {0, 0, 0, 0, false}, 1, 0, 1, false);
  RunScenario("latency 3", {3, 0, 0, 0, false}, 1, 0, 2, true);
  RunScenario("latency, jitter, loss, duplicates, reordering", {2, 4, 20, 15, false}, 1, 0, 3, true);
  RunScenario("heavy loss", {4, 3, 55, 10, false}, 2, 0, 4, true);
  RunScenario("zero input delay", {2, 2, 10, 0, false}, 0, 0, 5, true);
  RunScenario("frame counter wraparound", {2, 3, 15, 10, false}, 1, 0xFFFFFFF0u, 6, true);
  RunScenario("start frame offset", {3, 1, 5, 5, false}, 2, 0x12345678u, 7, true);
}

void TestWindowStallsWithoutPeer() {
  ToySim sim;
  RollbackCore core(sim, RollbackConfig{0, 1, 8, 30, 0});
  CHECK(core.Start());
  uint32_t advanced = 0;
  for (int i = 0; i < 100; ++i) {
    if (core.Tick(1) == TickStatus::kAdvanced) ++advanced;
  }
  CHECK(advanced == 9);
  CHECK(core.stats().stalls == 91);
  CHECK(FrameDiff(core.current_frame(), core.confirmed_frame()) == 8);
  CHECK(core.stats().rollbacks == 0);
}

void TestLateInputRollsBackOnlyWhenDifferent() {
  ToySim sim;
  RollbackCore core(sim, RollbackConfig{0, 1, 8, 30, 0});
  CHECK(core.Start());
  for (int i = 0; i < 6; ++i) CHECK(core.Tick(InputWord(i + 1)) == TickStatus::kAdvanced);
  CHECK(core.current_frame() == 6);

  const InputWord same[] = {0, 0, 0};
  core.OnRemoteInputs(1, same, 3);
  CHECK(core.Tick(9) == TickStatus::kAdvanced);
  CHECK(core.stats().rollbacks == 0);
  CHECK(core.confirmed_frame() >= 4);

  const InputWord different[] = {0x40, 0x41};
  core.OnRemoteInputs(4, different, 2);
  const uint32_t replayed_before = sim.replayed;
  const uint32_t effects_before = sim.effects;
  CHECK(core.Tick(10) == TickStatus::kAdvanced);
  CHECK(core.stats().rollbacks == 1);
  CHECK(core.stats().resimulated == 3);
  CHECK(sim.replayed - replayed_before == 3);
  CHECK(sim.effects - effects_before == 1);
  CHECK(core.confirmed_frame() >= 6);
}

void TestStaleDuplicateAndFarInputsAreIgnored() {
  ToySim sim;
  RollbackCore core(sim, RollbackConfig{0, 1, 8, 30, 100});
  CHECK(core.Start());
  const Frame wanted = core.remote_wanted();
  const InputWord word[] = {5};
  core.OnRemoteInputs(wanted + kMaxRemoteAhead + 10, word, 1);
  CHECK(core.remote_wanted() == wanted);
  core.OnRemoteInputs(wanted - 50, word, 1);
  CHECK(core.remote_wanted() == wanted);
  core.OnRemoteInputs(wanted, word, 1);
  CHECK(core.remote_wanted() == wanted + 1);
  core.OnRemoteInputs(wanted, word, 1);
  CHECK(core.remote_wanted() == wanted + 1);
  const InputWord changed[] = {6};
  core.OnRemoteInputs(wanted, changed, 1);
  CHECK(core.remote_wanted() == wanted + 1);
  CHECK(core.stats().rollbacks == 0);
}

void TestOutOfOrderBatchesFillTheGap() {
  ToySim sim;
  RollbackCore core(sim, RollbackConfig{1, 1, 8, 30, 0});
  CHECK(core.Start());
  const InputWord later[] = {3, 4};
  core.OnRemoteInputs(3, later, 2);
  CHECK(core.remote_wanted() == 1);
  const InputWord earlier[] = {1, 2};
  core.OnRemoteInputs(1, earlier, 2);
  CHECK(core.remote_wanted() == 5);
}

void TestPeerWantedBoundsTheLocalWindow() {
  ToySim sim;
  RollbackCore core(sim, RollbackConfig{0, 1, 8, 30, 0});
  CHECK(core.Start());
  uint32_t advanced = 0;
  for (int i = 0; i < 200; ++i) {
    const InputWord zero[] = {0};
    core.OnRemoteInputs(core.remote_wanted(), zero, 1);
    if (core.Tick(2) == TickStatus::kAdvanced) ++advanced;
  }
  CHECK(advanced == kWindow);
  Frame first = 0;
  InputWord words[kWindow];
  CHECK(core.CollectLocalInputs(first, words, kWindow) == kWindow);
  CHECK(first == 1);
  core.OnPeerWanted(first + 10);
  CHECK(core.CollectLocalInputs(first, words, kWindow) == kWindow - 10);
  core.OnPeerWanted(core.local_next() + 5);
  CHECK(core.CollectLocalInputs(first, words, kWindow) == kWindow - 10);
  CHECK(core.Tick(2) == TickStatus::kAdvanced);
}

void TestDesyncIsDetectedAtAChecksumPoint() {
  Peer a(0, 1, 8, 0, 10), b(1, 1, 8, 0, 10);
  b.sim.corrupt_at = 55;
  CHECK(a.core.Start());
  CHECK(b.core.Start());
  RunPair(a, b, 400, {1, 1, 5, 5, false}, 11);
  CHECK(a.core.desync().has_value());
  CHECK(b.core.desync().has_value());
  if (a.core.desync()) CHECK(a.core.desync()->frame > 55 && a.core.desync()->frame % 10 == 0);
  CHECK(!a.core.faulted());
  // Only the first mismatch is kept as the desync; the count shows it was not a one-off.
  CHECK(a.core.stats().mismatches > 1 && a.core.stats().mismatches < a.core.stats().checksums);
}

void TestMatchingRunsReportNoDesync() {
  Peer a(0, 1, 8, 0, 10), b(1, 1, 8, 0, 10);
  CHECK(a.core.Start());
  CHECK(b.core.Start());
  RunPair(a, b, 400, {2, 2, 10, 10, false}, 12);
  CHECK(!a.core.desync() && !b.core.desync());
  CHECK(a.core.stats().checksums > 10 && a.core.stats().mismatches == 0 && b.core.stats().mismatches == 0);
}

void TestSimulationFailuresFault() {
  {
    ToySim sim;
    sim.fail_save = true;
    RollbackCore core(sim, RollbackConfig{});
    CHECK(!core.Start());
    CHECK(core.faulted());
    CHECK(core.Tick(0) == TickStatus::kFault);
  }
  {
    ToySim sim;
    RollbackCore core(sim, RollbackConfig{0, 1, 8, 30, 0});
    CHECK(core.Start());
    for (int i = 0; i < 4; ++i) core.Tick(1);
    sim.fail_load = true;
    const InputWord different[] = {0x7FF};
    core.OnRemoteInputs(1, different, 1);
    CHECK(core.Tick(1) == TickStatus::kFault);
    CHECK(core.faulted());
    CHECK(core.Tick(1) == TickStatus::kFault);
  }
}

void TestLongRunStaysConsistent() {
  Peer a(0, 1, 8, 0), b(1, 1, 8, 0);
  CHECK(a.core.Start());
  CHECK(b.core.Start());
  RunPair(a, b, 2000, {3, 2, 10, 5, false}, 13);
  CHECK(a.core.current_frame() > 1000);
  CHECK(ConfirmedHistoryMatches(a, b, 1));
}

void TestDisconnectedPeerStallsWithoutRunaway() {
  Peer a(0, 1, 8, 0), b(1, 1, 8, 0);
  CHECK(a.core.Start());
  CHECK(b.core.Start());
  RunPair(a, b, 100, {1, 0, 0, 0, false}, 14);
  const Frame before = a.core.current_frame();
  for (int i = 0; i < 200; ++i) a.core.Tick(1);
  CHECK(FrameDiff(a.core.current_frame(), before) <= 9);
  CHECK(!a.core.faulted());
}

}  // namespace

int main() {
  std::printf("netplay rollback tests\n");
  TestEquivalenceUnderNetworkFaults();
  TestWindowStallsWithoutPeer();
  TestLateInputRollsBackOnlyWhenDifferent();
  TestStaleDuplicateAndFarInputsAreIgnored();
  TestOutOfOrderBatchesFillTheGap();
  TestPeerWantedBoundsTheLocalWindow();
  TestDesyncIsDetectedAtAChecksumPoint();
  TestMatchingRunsReportNoDesync();
  TestSimulationFailuresFault();
  TestLongRunStaysConsistent();
  TestDisconnectedPeerStallsWithoutRunaway();
  std::printf("%d checks, %d failures\n", netplay_test::g_checks, netplay_test::g_failures);
  return netplay_test::g_failures == 0 ? 0 : 1;
}
