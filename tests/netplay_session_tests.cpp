// Session and transport tests: handshake, rejection, intruders, loss, timeouts and a real loopback UDP match.
// Headless: the UDP part binds 127.0.0.1 only. Run: ssf2t_netplay_session_tests

#include <algorithm>
#include <chrono>
#include <deque>
#include <functional>
#include <thread>

#include "netplay/determinism.h"
#include "netplay/frame_zero.h"
#include "netplay/guest_netplay.h"
#include "netplay/session.h"
#include "netplay/udp_transport.h"
#include "netplay_test_support.h"

using namespace ssf2t::netplay;
using namespace netplay_test;

namespace {

struct Datagram {
  Endpoint from;
  Endpoint to;
  std::vector<uint8_t> data;
  uint64_t due = 0;
};

class Hub {
 public:
  uint64_t now = 0;
  uint32_t latency = 0;
  uint32_t jitter = 0;
  uint32_t loss_percent = 0;
  uint32_t duplicate_percent = 0;
  bool cut = false;
  Lcg rng{5};
  std::deque<Datagram> queue;
  uint32_t dropped_by_rule = 0;
  std::function<bool(const Datagram&)> drop_rule;

  void Enqueue(const Endpoint& from, const Endpoint& to, const uint8_t* data, size_t size) {
    Datagram d{from, to, std::vector<uint8_t>(data, data + size), 0};
    if (cut || rng.Chance(loss_percent) || (drop_rule && drop_rule(d))) {
      ++dropped_by_rule;
      return;
    }
    d.due = now + latency + rng.Below(jitter + 1);
    queue.push_back(d);
    if (rng.Chance(duplicate_percent)) {
      d.due = now + latency + rng.Below(jitter + 1);
      queue.push_back(d);
    }
  }
};

class Node final : public Transport {
 public:
  Node(Hub& hub, Endpoint self) : hub_(hub), self_(self) {}
  bool Send(const Endpoint& to, const uint8_t* data, size_t size) override {
    hub_.Enqueue(self_, to, data, size);
    return true;
  }
  size_t Receive(uint8_t* buffer, size_t capacity, Endpoint& from) override {
    for (auto it = hub_.queue.begin(); it != hub_.queue.end(); ++it) {
      if (!(it->to == self_) || it->due > hub_.now) continue;
      const size_t n = std::min(capacity, it->data.size());
      std::memcpy(buffer, it->data.data(), n);
      from = it->from;
      hub_.queue.erase(it);
      return n;
    }
    return 0;
  }
  const Endpoint& self() const { return self_; }

 private:
  Hub& hub_;
  Endpoint self_;
};

const Endpoint kHostAddr{0x0A000001, 7000};
const Endpoint kJoinAddr{0x0A000002, 7001};
const Endpoint kThirdAddr{0x0A000003, 7002};

SessionConfig HostConfig(GuestId guest = GuestId::kBase013) {
  SessionConfig c;
  c.role = Role::kHost;
  c.guest = guest;
  c.session_id = 0xABCD1234;
  c.input_delay = 2;
  return c;
}

SessionConfig JoinConfig(GuestId guest = GuestId::kBase013) {
  SessionConfig c;
  c.role = Role::kJoin;
  c.guest = guest;
  c.peer = kHostAddr;
  c.nonce = 0x55AA;
  return c;
}

struct Rig {
  Hub hub;
  Node host_node{hub, kHostAddr};
  Node join_node{hub, kJoinAddr};
  ToySim host_sim, join_sim;
  Session host;
  Session join;
  uint32_t host_accepted = 0, join_accepted = 0;
  explicit Rig(SessionConfig hc = HostConfig(), SessionConfig jc = JoinConfig())
      : host(hc, host_node), join(jc, join_node) {}

  // Marks both sides fight-ready from the start, as the pre-ready-barrier tests assume: connection alone used to
  // mean the match could run.
  void Start() {
    host.Start(hub.now);
    join.Start(hub.now);
    host.SetLocalReady(true, hub.now);
    join.SetLocalReady(true, hub.now);
  }
  void Step(uint64_t ms = 16) {
    hub.now += ms;
    if (!host.core() && host.ReadyToBeginFight()) host.BeginFight(host_sim, hub.now);
    if (!join.core() && join.ReadyToBeginFight()) join.BeginFight(join_sim, hub.now);
    if (host.RunFrame(Sample(0, host_accepted), hub.now) == TickStatus::kAdvanced) ++host_accepted;
    if (join.RunFrame(Sample(1, join_accepted), hub.now) == TickStatus::kAdvanced) ++join_accepted;
  }
  void Run(int steps) {
    for (int i = 0; i < steps; ++i) Step();
  }
};

bool HistoriesMatch(const Rig& r, uint32_t delay) {
  const RollbackCore* a = r.host.core();
  const RollbackCore* b = r.join.core();
  if (!a || !b) return false;
  const uint32_t frames = std::min(a->confirmed_frame(), b->confirmed_frame());
  if (frames < 30) return false;
  for (uint32_t g = 1; g <= frames; ++g) {
    const auto ea = r.host_sim.after.find(g);
    const auto eb = r.join_sim.after.find(g);
    if (ea == r.host_sim.after.end() || eb == r.join_sim.after.end()) return false;
    if (ea->second != eb->second || ea->second != Reference(g, delay)) return false;
  }
  return true;
}

void TestHandshakeAndMatch() {
  Rig rig;
  rig.hub.latency = 30;
  rig.hub.jitter = 25;
  rig.hub.loss_percent = 15;
  rig.hub.duplicate_percent = 10;
  rig.Start();
  CHECK(rig.host.state() == SessionState::kWaiting);
  CHECK(rig.join.state() == SessionState::kConnecting);
  rig.Run(800);
  CHECK(rig.host.state() == SessionState::kRunning);
  CHECK(rig.join.state() == SessionState::kRunning);
  CHECK(rig.host.local_player() == 0 && rig.join.local_player() == 1);
  CHECK(rig.host.session_id() == 0xABCD1234 && rig.join.session_id() == 0xABCD1234);
  CHECK(rig.host.core() && rig.host.core()->config().input_delay == 2);
  CHECK(rig.join.core() && rig.join.core()->config().input_delay == 2);
  CHECK(HistoriesMatch(rig, 2));
  CHECK(rig.host.core()->stats().rollbacks + rig.join.core()->stats().rollbacks > 0);
  CHECK(!rig.host.core()->desync() && !rig.join.core()->desync());
}

void TestGuestMismatchIsRejected() {
  Rig rig(HostConfig(GuestId::kBase013), JoinConfig(GuestId::kTitleUpdate023));
  rig.Start();
  rig.Run(20);
  CHECK(rig.join.state() == SessionState::kRejected);
  CHECK(rig.join.reject_reason() == RejectReason::kGuestMismatch);
  CHECK(rig.host.state() == SessionState::kWaiting);
  CHECK(rig.host.core() == nullptr && rig.join.core() == nullptr);
  CHECK(rig.join.RunFrame(0, rig.hub.now) == TickStatus::kStalled);
}

void TestBuildMismatchIsRejected() {
  Hub hub;
  Node host_node(hub, kHostAddr), peer(hub, kJoinAddr);
  Session host(HostConfig(), host_node);
  host.Start(0);
  uint8_t buffer[kMaxPacketSize];
  Hello hello;
  hello.build = kNetplayBuild + 1;
  hello.nonce = 4;
  const size_t n = Encode({0, 0, 0}, hello, buffer, sizeof buffer);
  peer.Send(kHostAddr, buffer, n);
  hub.now = 10;
  host.Poll(hub.now);
  CHECK(host.state() == SessionState::kWaiting);
  Endpoint from;
  const size_t got = peer.Receive(buffer, sizeof buffer, from);
  const auto reply = Decode(buffer, got);
  const HelloAck* ack = reply ? std::get_if<HelloAck>(&reply->body) : nullptr;
  CHECK(ack && ack->reject == RejectReason::kBuildMismatch && ack->nonce == 4);
}

void TestIntrudersAndGarbageAreIgnored() {
  Rig rig;
  rig.Start();
  rig.Run(40);
  CHECK(rig.host.state() == SessionState::kRunning);

  Node third(rig.hub, kThirdAddr);
  Lcg rng(77);
  uint8_t buffer[kMaxPacketSize];
  for (int i = 0; i < 300; ++i) {
    const size_t n = rng.Below(100);
    for (size_t j = 0; j < n; ++j) buffer[j] = uint8_t(rng.Next());
    third.Send(kHostAddr, buffer, n);
    third.Send(kJoinAddr, buffer, n);
  }
  Input poison;
  poison.first = rig.host.core()->remote_wanted();
  poison.next_wanted = 1000000;
  poison.words.assign(kMaxInputsPerPacket, 0x7FF);
  size_t n = Encode({0xABCD1234, 1, 1}, poison, buffer, sizeof buffer);
  third.Send(kHostAddr, buffer, n);
  n = Encode({0xDEAD, 1, 1}, poison, buffer, sizeof buffer);
  rig.hub.queue.push_back({kJoinAddr, kHostAddr, std::vector<uint8_t>(buffer, buffer + n), 0});
  n = Encode({0xABCD1234, 1, 1}, Bye{ByeReason::kQuit}, buffer, sizeof buffer);
  third.Send(kHostAddr, buffer, n);
  third.Send(kJoinAddr, buffer, n);

  rig.Run(400);
  CHECK(rig.host.state() == SessionState::kRunning);
  CHECK(rig.join.state() == SessionState::kRunning);
  CHECK(rig.host.packets_dropped() > 0);
  CHECK(HistoriesMatch(rig, 2));
}

void TestBusyHostRejectsASecondJoiner() {
  Rig rig;
  rig.Start();
  rig.Run(40);
  Node third(rig.hub, kThirdAddr);
  uint8_t buffer[kMaxPacketSize];
  Hello hello;
  hello.nonce = 999;
  size_t n = Encode({0, 0, 0}, hello, buffer, sizeof buffer);
  third.Send(kHostAddr, buffer, n);
  rig.Run(5);
  Endpoint from;
  const size_t got = third.Receive(buffer, sizeof buffer, from);
  const auto reply = Decode(buffer, got);
  const HelloAck* ack = reply ? std::get_if<HelloAck>(&reply->body) : nullptr;
  CHECK(ack && ack->reject == RejectReason::kBusy);
  CHECK(rig.host.state() == SessionState::kRunning);
  CHECK(rig.host.local_player() == 0);
}

void TestLostHelloAckIsResent() {
  Rig rig;
  bool dropped = false;
  rig.hub.drop_rule = [&](const Datagram& d) {
    const auto p = Decode(d.data.data(), d.data.size());
    if (!dropped && p && std::holds_alternative<HelloAck>(p->body)) {
      dropped = true;
      return true;
    }
    return false;
  };
  rig.Start();
  rig.Run(60);
  CHECK(dropped);
  CHECK(rig.host.state() == SessionState::kRunning);
  CHECK(rig.join.state() == SessionState::kRunning);
  CHECK(rig.join.session_id() == rig.host.session_id());
}

void TestTimeouts() {
  {
    Rig rig;
    rig.hub.cut = true;
    rig.Start();
    rig.hub.now = 20000;
    rig.join.Poll(rig.hub.now);
    CHECK(rig.join.state() == SessionState::kTimedOut);
    CHECK(rig.host.state() == SessionState::kWaiting);
  }
  {
    Rig rig;
    rig.Start();
    rig.Run(50);
    CHECK(rig.host.state() == SessionState::kRunning);
    rig.hub.cut = true;
    rig.Run(400);
    CHECK(rig.host.state() == SessionState::kTimedOut);
    CHECK(rig.join.state() == SessionState::kTimedOut);
    CHECK(rig.host.RunFrame(0, rig.hub.now) == TickStatus::kStalled);
  }
}

void TestByeEndsThePeer() {
  Rig rig;
  rig.Start();
  rig.Run(50);
  rig.host.Stop(rig.hub.now);
  CHECK(rig.host.state() == SessionState::kClosed);
  rig.Run(5);
  CHECK(rig.join.state() == SessionState::kClosed);
  CHECK(rig.join.peer_bye() && *rig.join.peer_bye() == ByeReason::kQuit);
}

void TestKeepaliveHoldsAnIdleMatch() {
  Rig rig;
  rig.Start();
  rig.Run(50);
  for (int i = 0; i < 100; ++i) {
    rig.hub.now += 100;
    rig.host.Poll(rig.hub.now);
    rig.join.Poll(rig.hub.now);
  }
  CHECK(rig.host.state() == SessionState::kRunning);
  CHECK(rig.join.state() == SessionState::kRunning);
}

void TestDesyncIsReportedWithoutEndingTheSession() {
  Rig rig;
  rig.join_sim.corrupt_at = 70;
  rig.Start();
  rig.Run(500);
  CHECK(rig.host.core() && rig.host.core()->desync().has_value());
  CHECK(rig.join.core() && rig.join.core()->desync().has_value());
  CHECK(rig.host.state() == SessionState::kRunning);
}

void TestRealLoopbackMatch() {
  UdpTransport host_socket, join_socket;
  CHECK(host_socket.Open(0, true));
  CHECK(join_socket.Open(0, true));
  if (!host_socket.is_open() || !join_socket.is_open()) return;
  CHECK(host_socket.bound_port() != 0 && join_socket.bound_port() != 0);

  ToySim host_sim, join_sim;
  SessionConfig hc = HostConfig();
  SessionConfig jc = JoinConfig();
  jc.peer = Endpoint{0x7F000001, host_socket.bound_port()};
  Session host(hc, host_socket);
  Session join(jc, join_socket);

  const auto t0 = std::chrono::steady_clock::now();
  const auto ms = [&] {
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
  };
  host.Start(ms());
  join.Start(ms());
  host.SetLocalReady(true, ms());
  join.SetLocalReady(true, ms());
  uint32_t host_accepted = 0, join_accepted = 0;
  while (ms() < 5000 && (host_accepted < 240 || join_accepted < 240)) {
    if (!host.core() && host.ReadyToBeginFight()) host.BeginFight(host_sim, ms());
    if (!join.core() && join.ReadyToBeginFight()) join.BeginFight(join_sim, ms());
    if (host.RunFrame(Sample(0, host_accepted), ms()) == TickStatus::kAdvanced) ++host_accepted;
    if (join.RunFrame(Sample(1, join_accepted), ms()) == TickStatus::kAdvanced) ++join_accepted;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  CHECK(host.state() == SessionState::kRunning && join.state() == SessionState::kRunning);
  CHECK(host_accepted >= 240 && join_accepted >= 240);
  if (host.core() && join.core()) {
    const uint32_t frames = std::min(host.core()->confirmed_frame(), join.core()->confirmed_frame());
    CHECK(frames >= 200);
    bool same = frames >= 200;
    for (uint32_t g = 1; g <= frames && same; ++g) {
      same = host_sim.after.count(g) && join_sim.after.count(g) && host_sim.after[g] == join_sim.after[g] &&
             host_sim.after[g] == Reference(g, 2);
    }
    CHECK(same);
  }
  host.Stop(ms());
  for (int i = 0; i < 50 && join.state() == SessionState::kRunning; ++i) {
    join.Poll(ms());
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  CHECK(join.state() == SessionState::kClosed);
  host_socket.Close();
  host_socket.Close();
  CHECK(!host_socket.is_open());
  uint8_t byte;
  Endpoint from;
  CHECK(host_socket.Receive(&byte, 1, from) == 0);
  CHECK(!host_socket.Send(Endpoint{0x7F000001, 9}, &byte, 1));
}

void TestNonBlockingReceiveReturnsAtOnce() {
  UdpTransport socket;
  CHECK(socket.Open(0, true));
  uint8_t buffer[16];
  Endpoint from;
  const auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < 1000; ++i) CHECK(socket.Receive(buffer, sizeof buffer, from) == 0);
  CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2));
}

// Connecting alone must not start the fight: it only starts once both sides report ready, in whichever order.
void TestReadyBarrierOrdering(bool host_ready_first) {
  Rig rig;
  rig.host.Start(rig.hub.now);
  rig.join.Start(rig.hub.now);
  for (int i = 0; i < 20; ++i) {
    rig.hub.now += 16;
    rig.host.Poll(rig.hub.now);
    rig.join.Poll(rig.hub.now);
  }
  CHECK(rig.host.state() == SessionState::kConnected && rig.join.state() == SessionState::kConnected);
  CHECK(!rig.host.core() && !rig.join.core());

  auto& first = host_ready_first ? rig.host : rig.join;
  auto& second = host_ready_first ? rig.join : rig.host;
  first.SetLocalReady(true, rig.hub.now);
  for (int i = 0; i < 10; ++i) {
    rig.hub.now += 16;
    rig.host.Poll(rig.hub.now);
    rig.join.Poll(rig.hub.now);
  }
  CHECK(!rig.host.ReadyToBeginFight() && !rig.join.ReadyToBeginFight());
  second.SetLocalReady(true, rig.hub.now);
  for (int i = 0; i < 40 && !(rig.host.ReadyToBeginFight() && rig.join.ReadyToBeginFight()); ++i) {
    rig.hub.now += 16;
    rig.host.Poll(rig.hub.now);
    rig.join.Poll(rig.hub.now);
  }
  CHECK(rig.host.ReadyToBeginFight() && rig.join.ReadyToBeginFight());
  CHECK(rig.host.BeginFight(rig.host_sim, rig.hub.now));
  CHECK(rig.join.BeginFight(rig.join_sim, rig.hub.now));
  CHECK(rig.host.state() == SessionState::kRunning && rig.join.state() == SessionState::kRunning);
  rig.Run(60);
  CHECK(HistoriesMatch(rig, 2));
}

void TestFixedOwnership() {
  Rig rig;
  rig.Start();
  rig.Run(60);
  CHECK(rig.host.local_player() == 0);
  CHECK(rig.join.local_player() == 1);
}

void TestLeavingFightStopsTakeover() {
  Rig rig;
  rig.Start();
  rig.Run(60);
  CHECK(rig.host.state() == SessionState::kRunning && rig.join.state() == SessionState::kRunning);
  rig.host.SetLocalReady(false, rig.hub.now);
  CHECK(rig.host.state() == SessionState::kConnected);
  CHECK(!rig.host.core());
  rig.Run(20);
  CHECK(rig.join.state() == SessionState::kConnected);
  CHECK(!rig.join.core());

  // Both sides can renegotiate and start a second fight on the same connection.
  rig.host.SetLocalReady(true, rig.hub.now);
  rig.Run(60);
  CHECK(rig.host.state() == SessionState::kRunning && rig.join.state() == SessionState::kRunning);
}

// The runtime drops readiness once the guest leaves the fight loop. The next fight then starts at frame 0 on a new
// core instead of resuming the old one, which rolled the guest back into the previous fight's snapshots.
void TestNextFightStartsOnAFreshCore() {
  Rig rig;
  rig.Start();
  rig.Run(120);
  CHECK(rig.host.core() && rig.host.core()->current_frame() > 60);
  rig.host.SetLocalReady(false, rig.hub.now);
  rig.join.SetLocalReady(false, rig.hub.now);
  rig.Run(20);
  CHECK(rig.host.state() == SessionState::kConnected && rig.join.state() == SessionState::kConnected);
  CHECK(!rig.host.core() && !rig.join.core());
  CHECK(!rig.host.local_ready() && !rig.host.remote_ready() && !rig.join.remote_ready());
  CHECK(!rig.host.ReadyToBeginFight() && !rig.join.ReadyToBeginFight());

  const uint32_t loads_before = rig.host_sim.loads + rig.join_sim.loads;
  rig.host.SetLocalReady(true, rig.hub.now);
  rig.join.SetLocalReady(true, rig.hub.now);
  for (int i = 0; i < 40 && !(rig.host.core() && rig.join.core()); ++i) rig.Step();
  CHECK(rig.host.core() && rig.join.core());
  if (!rig.host.core() || !rig.join.core()) return;
  CHECK(rig.host.core()->current_frame() < 10 && rig.join.core()->current_frame() < 10);
  CHECK(rig.host_sim.loads + rig.join_sim.loads == loads_before);
}

void TestRoundTripIsMeasuredFromAcks() {
  Rig rig;
  rig.hub.latency = 40;
  rig.Start();
  CHECK(!rig.host.rtt_ms() && !rig.join.rtt_ms());
  rig.Run(300);
  CHECK(rig.host.state() == SessionState::kRunning);
  const auto host_rtt = rig.host.rtt_ms();
  const auto join_rtt = rig.join.rtt_ms();
  CHECK(host_rtt && *host_rtt >= 80 && *host_rtt <= 120);
  CHECK(join_rtt && *join_rtt >= 80 && *join_rtt <= 120);

  rig.hub.cut = true;
  rig.Run(150);
  CHECK(!rig.host.rtt_ms());
}

constexpr uint32_t kGlobalsAddress = 0x83E0E7E0;

SessionConfig Verified(SessionConfig c) {
  c.verify_frame_zero = true;
  return c;
}

std::vector<uint8_t> FrameZeroBlob() {
  std::vector<uint8_t> blob(kSimBlobSize);
  for (size_t i = 0; i < blob.size(); ++i) blob[i] = uint8_t(i * 131 + 7);
  return blob;
}

// One peer's guest at frame 0: its simulation blob and setup words, and how its load treats an adopted state.
struct FrameZeroSide {
  std::vector<uint8_t> blob = FrameZeroBlob();
  std::vector<uint32_t> setup = {3, 0x00020000, 0, 7};
  bool offered = false;
  bool adopted = false;
  std::optional<size_t> load_keeps;  // a byte the guest's load leaves as it was: the adopted state does not take
  std::vector<std::string> report;

  bool Reported(const std::string& text) const {
    return std::any_of(report.begin(), report.end(),
                       [&text](const std::string& line) { return line.find(text) != std::string::npos; });
  }
};

// One frame of each guest as the runtime drives it: offer frame 0 once the barrier passed, take the host's state when
// asked to, begin only once both frame-0 states agree, then play.
void StepVerified(Rig& rig, FrameZeroSide& host, FrameZeroSide& join, uint64_t ms = 16) {
  rig.hub.now += ms;
  const auto side = [&rig](Session& s, FrameZeroSide& f, ToySim& sim, uint32_t& accepted, int player) {
    if (!s.core() && s.ReadyToBeginFight()) {
      if (!f.offered) {
        s.OfferFrameZero(FrameZeroView(f.blob), kGlobalsAddress, f.setup);
        f.offered = true;
      }
      if (!f.adopted && s.FrameZeroAdoptionReady()) {
        std::vector<uint8_t> blob = f.blob;
        if (s.AdoptFrameZero(blob)) {
          if (f.load_keeps) blob[*f.load_keeps] = f.blob[*f.load_keeps];
          f.blob = blob;
          f.adopted = true;
          s.ReofferFrameZero(FrameZeroView(f.blob));
        }
      }
      if (s.frame_zero() == FrameZeroVerdict::kMatch) {
        const std::vector<uint8_t> view = FrameZeroView(f.blob);
        sim.state = Fnv1a64(view.data(), view.size());
        sim.counter = 0;
        sim.after.clear();
        s.BeginFight(sim, rig.hub.now);
      }
    }
    if (s.RunFrame(Sample(player, accepted), rig.hub.now) == TickStatus::kAdvanced) ++accepted;
    for (std::string& line : s.TakeFrameZeroReport()) f.report.push_back(std::move(line));
  };
  side(rig.host, host, rig.host_sim, rig.host_accepted, 0);
  side(rig.join, join, rig.join_sim, rig.join_accepted, 1);
}

void RunVerified(Rig& rig, FrameZeroSide& host, FrameZeroSide& join, int steps) {
  for (int i = 0; i < steps; ++i) StepVerified(rig, host, join);
}

void ConnectAndReady(Rig& rig) {
  rig.Start();
  for (int i = 0; i < 40 && !(rig.host.ReadyToBeginFight() && rig.join.ReadyToBeginFight()); ++i) {
    rig.hub.now += 16;
    rig.host.Poll(rig.hub.now);
    rig.join.Poll(rig.hub.now);
  }
}

// Each toy sim starts the fight from its own frame-0 state, so their histories agree only if those states did.
bool SimsAgree(const Rig& r) {
  const RollbackCore* a = r.host.core();
  const RollbackCore* b = r.join.core();
  if (!a || !b) return false;
  const uint32_t frames = std::min(a->confirmed_frame(), b->confirmed_frame());
  if (frames < 30) return false;
  for (uint32_t g = 1; g <= frames; ++g) {
    const auto ea = r.host_sim.after.find(g);
    const auto eb = r.join_sim.after.find(g);
    if (ea == r.host_sim.after.end() || eb == r.join_sim.after.end() || ea->second != eb->second) return false;
  }
  return true;
}

bool BothRunning(const Rig& rig) {
  return rig.host.state() == SessionState::kRunning && rig.join.state() == SessionState::kRunning;
}

void TestFrameZeroMustMatchBeforeRollbackStarts() {
  Rig rig(Verified(HostConfig()), Verified(JoinConfig()));
  FrameZeroSide host, join;
  ConnectAndReady(rig);
  CHECK(rig.host.ReadyToBeginFight() && rig.join.ReadyToBeginFight());
  CHECK(!rig.host.BeginFight(rig.host_sim, rig.hub.now));
  CHECK(!rig.host.core() && rig.host.frame_zero() == FrameZeroVerdict::kPending);

  RunVerified(rig, host, join, 600);
  CHECK(BothRunning(rig));
  CHECK(host.Reported("DEBUG_SAULO netplay frame0 state match fight=0"));
  CHECK(join.Reported("DEBUG_SAULO netplay frame0 state match fight=0"));
  CHECK(host.Reported("DEBUG_SAULO netplay setup remote fight=0 round=0 words=00000003,00020000,00000000,00000007"));
  CHECK(!host.adopted && !join.adopted && !join.Reported("round=1"));
  CHECK(SimsAgree(rig));
  const RollbackCore* core = rig.host.core();
  CHECK(core && core->stats().checksums > 10 && core->stats().mismatches == 0 && !core->desync());
}

// What a previous fight left behind differs between the peers (it ended at a different frame on each): the joiner
// takes the host's bytes, offers the result again, and the fight starts from the same state on both. Bytes outside
// the compared view (the input table's menu repeat counters) stay the joiner's own.
void TestCarriedStateIsTakenFromTheHost() {
  Rig rig(Verified(HostConfig()), Verified(JoinConfig()));
  FrameZeroSide host, join;
  const size_t rng = kGuestStateSize + kInputTableStateBytes + kInputRingBytes;
  const size_t side = rng + kRngStateBytes;
  const size_t repeat_counter = kGuestStateSize + 28;
  host.blob[0x30] = 0x01;
  join.blob[0x30] = 0x00;
  host.blob[0x5C21] = 0x0C;
  join.blob[0x5C21] = 0x01;
  host.blob[kArenaSaveSize + 0x3EE] = 0xFF;
  join.blob[kArenaSaveSize + 0x3EE] = 0x00;
  host.blob[rng + 4] = 0x11;
  join.blob[rng + 4] = 0x22;
  host.blob[side] = 0x05;
  join.blob[side] = 0x07;
  host.blob[repeat_counter] = 0x55;
  join.blob[repeat_counter] = 0x66;
  const std::vector<uint8_t> host_before = host.blob;
  ConnectAndReady(rig);
  RunVerified(rig, host, join, 600);
  CHECK(BothRunning(rig));
  CHECK(join.adopted && !host.adopted);
  CHECK(host.blob == host_before);
  CHECK(FrameZeroView(join.blob) == FrameZeroView(host.blob));
  CHECK(join.blob[repeat_counter] == 0x66);
  CHECK(join.blob[rng + 4] == 0x11 && join.blob[side] == 0x05);
  CHECK(host.Reported("frame0 state MISMATCH fight=0 regions=arena,globals,rng,side"));
  CHECK(host.Reported("frame0 state match fight=0 round=1") && join.Reported("frame0 state match fight=0 round=1"));
  CHECK(rig.host.frame_zero_refusal() == FrameZeroRefusal::kNone);
  CHECK(SimsAgree(rig));
  const RollbackCore* core = rig.host.core();
  CHECK(core && core->stats().mismatches == 0 && !core->desync());
}

// Setup words are read outside the snapshot: a difference there cannot be taken from the host, so nothing is.
void TestSetupMismatchRefusesTheFight() {
  Rig rig(Verified(HostConfig()), Verified(JoinConfig()));
  FrameZeroSide host, join;
  join.setup[3] = 8;
  host.blob[0x30] = 0x01;
  ConnectAndReady(rig);
  RunVerified(rig, host, join, 200);
  CHECK(rig.host.state() == SessionState::kConnected && rig.join.state() == SessionState::kConnected);
  CHECK(!rig.host.core() && !rig.join.core() && !join.adopted);
  CHECK(rig.host.frame_zero() == FrameZeroVerdict::kMismatch && rig.join.frame_zero() == FrameZeroVerdict::kMismatch);
  CHECK(rig.host.frame_zero_refusal() == FrameZeroRefusal::kSetup);
  CHECK(rig.join.frame_zero_refusal() == FrameZeroRefusal::kSetup);
  CHECK(host.Reported("setup MISMATCH fight=0 word=3 local=00000007 remote=00000008"));
  CHECK(join.Reported("setup MISMATCH fight=0 word=3 local=00000008 remote=00000007"));
  CHECK(std::string(RefusalName(FrameZeroRefusal::kSetup)) == "setup_mismatch");
  CHECK(rig.host_sim.counter == 0 && rig.join_sim.counter == 0);
}

// A difference the joiner's load does not remove is refused exactly as before, with the remaining bytes reported.
void TestStateStillDifferingAfterAgreeIsRefused() {
  Rig rig(Verified(HostConfig()), Verified(JoinConfig()));
  FrameZeroSide host, join;
  host.blob[0x3F0] = 0x01;
  join.blob[0x3F0] = 0x02;
  join.load_keeps = 0x3F0;
  ConnectAndReady(rig);
  RunVerified(rig, host, join, 300);
  CHECK(join.adopted);
  CHECK(!rig.host.core() && !rig.join.core());
  CHECK(rig.host.frame_zero() == FrameZeroVerdict::kMismatch && rig.join.frame_zero() == FrameZeroVerdict::kMismatch);
  CHECK(rig.host.frame_zero_refusal() == FrameZeroRefusal::kState);
  CHECK(std::string(RefusalName(rig.join.frame_zero_refusal())) == "frame0_mismatch");
  CHECK(host.Reported("frame0 state MISMATCH fight=0 round=1 regions=arena"));
  CHECK(join.Reported("frame0 diff at=arena+0x3F0 checked size=1 local=02 remote=01"));
  CHECK(host.Reported("frame0 diff at=arena+0x3F0 checked size=1 local=01 remote=02"));
  CHECK(!rig.host.BeginFight(rig.host_sim, rig.hub.now));
}

// Peers that met at different points differ far beyond what a fight leaves behind: nothing is taken.
void TestStateTooDifferentIsRefused() {
  Rig rig(Verified(HostConfig()), Verified(JoinConfig()));
  FrameZeroSide host, join;
  for (size_t block = 0; block <= kMaxAdoptedBlocks; ++block) join.blob[block * kStateBlockBytes] ^= 0xFF;
  ConnectAndReady(rig);
  RunVerified(rig, host, join, 300);
  CHECK(!join.adopted && !rig.host.core() && !rig.join.core());
  CHECK(rig.host.frame_zero_refusal() == FrameZeroRefusal::kTooDifferent);
  CHECK(rig.join.frame_zero_refusal() == FrameZeroRefusal::kTooDifferent);
  CHECK(host.Reported("frame0 blocks block_bytes=256 differing=65"));
}

void TestFrameZeroNamesGlobalsAndTableBytes() {
  Rig rig(Verified(HostConfig()), Verified(JoinConfig()));
  FrameZeroSide host, join;
  const size_t globals = kArenaSaveSize + 0x40;
  const size_t current_p2 = kGuestStateSize + kInputEntryStride + kInputCurrentOffset;
  for (size_t i = 0; i < 4; ++i) {
    host.blob[globals + i] = 0x10;
    join.blob[globals + i] = uint8_t(0x20 + i);
  }
  host.blob[current_p2 + 3] = 0x01;
  join.blob[current_p2 + 3] = 0x10;
  host.blob[0x2000] = 0xAA;
  join.blob[0x2000] = 0xBB;
  ConnectAndReady(rig);
  RunVerified(rig, host, join, 300);
  CHECK(host.Reported("regions=arena,globals,table"));
  CHECK(join.Reported("frame0 diff at=arena+0x2000 unchecked size=1 local=BB remote=AA"));
  CHECK(join.Reported("frame0 diff at=globals+0x40 @83E0E820 size=4 local=20212223 remote=10101010"));
  CHECK(join.Reported("frame0 diff at=input_table+0xA7 size=1 local=10 remote=01"));
  CHECK(BothRunning(rig) && join.adopted);
}

// Only what the set-input command writes for the two fighters counts in the input table: the menu repeat counters
// and the unused entries change with every real frame a peer waits.
void TestFrameZeroViewKeepsOnlyTheInputCommandFields() {
  std::vector<uint8_t> a(kSimBlobSize, 0x11), b(kSimBlobSize, 0x11);
  for (const uint32_t peer_local : {28u, 30u, 156u + 28u, 156u + 30u, 2 * 156u + 8u, 3 * 156u + 8u, 100u}) {
    b[kGuestStateSize + peer_local] = 0x99;
  }
  CHECK(FrameZeroView(a) == FrameZeroView(b));
  for (const uint32_t command : {0u, 8u, 12u, 16u, 20u, 156u + 8u, 156u + 20u}) {
    std::vector<uint8_t> c = a;
    c[kGuestStateSize + command] = 0x99;
    CHECK(FrameZeroView(a) != FrameZeroView(c));
  }
  CHECK(FrameZeroView(a).size() == kSimBlobSize);
  CHECK(DescribeStateOffset(0x3F0, 0) == "arena+0x3F0 checked");
  CHECK(DescribeStateOffset(1700, 0) == "arena+0x6A4 unchecked");
  CHECK(DescribeStateOffset(kArenaSaveSize, kGlobalsAddress) == "globals+0x0 @83E0E7E0");
  CHECK(DescribeSetupWords({3, 0x00020000}) == "00000003,00020000");
}

// A real divergence after an agreed frame 0 is still reported by the game's checksum.
void TestRealDesyncAfterAgreedFrameZeroIsDetected() {
  Rig rig(Verified(HostConfig()), Verified(JoinConfig()));
  FrameZeroSide host, join;
  rig.join_sim.corrupt_at = 100;
  ConnectAndReady(rig);
  RunVerified(rig, host, join, 400);
  CHECK(host.Reported("frame0 state match"));
  const RollbackCore* core = rig.host.core();
  CHECK(core && core->desync().has_value() && core->stats().mismatches > 0);
  CHECK(rig.host.state() == SessionState::kRunning);
}

void EndFightOnBoth(Rig& rig, FrameZeroSide& host, FrameZeroSide& join) {
  rig.host.SetLocalReady(false, rig.hub.now);
  RunVerified(rig, host, join, 10);
  rig.join.SetLocalReady(false, rig.hub.now);
  RunVerified(rig, host, join, 200);
}

void ReadyAgain(Rig& rig, FrameZeroSide& host, FrameZeroSide& join, FrameZeroSide next_host, FrameZeroSide next_join) {
  host = std::move(next_host);
  join = std::move(next_join);
  rig.host.SetLocalReady(true, rig.hub.now);
  rig.join.SetLocalReady(true, rig.hub.now);
}

// A fight ending drops only the rollback core: the connection stays through Results and the quote screens, however
// long they last, and each next fight agrees on its own frame 0 (fight 1 with what fight 0 left behind on each peer,
// fight 2 with other setup words on both), and only leaving ends the session.
void TestFightEndKeepsTheMatchForTheNextFight() {
  Rig rig(Verified(HostConfig()), Verified(JoinConfig()));
  FrameZeroSide host, join;
  ConnectAndReady(rig);
  RunVerified(rig, host, join, 200);
  CHECK(BothRunning(rig) && rig.host.fight_index() == 0 && rig.join.fight_index() == 0);

  EndFightOnBoth(rig, host, join);
  RunVerified(rig, host, join, 800);
  CHECK(rig.host.state() == SessionState::kConnected && rig.join.state() == SessionState::kConnected);
  CHECK(!rig.host.core() && !rig.join.core());
  CHECK(rig.host.frame_zero() == FrameZeroVerdict::kPending && rig.join.frame_zero() == FrameZeroVerdict::kPending);
  CHECK(!rig.host.peer_bye() && !rig.join.peer_bye());
  CHECK(rig.host.fight_index() == 1 && rig.join.fight_index() == 1);

  FrameZeroSide second_host, second_join;
  second_join.blob[0x30] = 0x42;
  second_join.blob[kGuestStateSize + kInputTableStateBytes + kInputRingBytes + kRngStateBytes] = 0x09;
  ReadyAgain(rig, host, join, second_host, second_join);
  RunVerified(rig, host, join, 300);
  CHECK(host.Reported("frame0 state MISMATCH fight=1 regions=arena,side"));
  CHECK(host.Reported("frame0 state match fight=1 round=1") && join.Reported("frame0 state match fight=1 round=1"));
  CHECK(join.adopted && BothRunning(rig) && rig.host.fight_index() == 1);
  CHECK(rig.host.core() && rig.host.core()->current_frame() < 300 && rig.host.core()->stats().mismatches == 0);
  CHECK(SimsAgree(rig));

  EndFightOnBoth(rig, host, join);
  FrameZeroSide third_host, third_join;
  third_host.setup[3] = third_join.setup[3] = 11;
  ReadyAgain(rig, host, join, third_host, third_join);
  RunVerified(rig, host, join, 200);
  CHECK(host.Reported("frame0 state match fight=2") && join.Reported("frame0 state match fight=2"));
  CHECK(!host.Reported("round=1") && BothRunning(rig) && rig.join.fight_index() == 2);

  rig.host.Stop(rig.hub.now);
  RunVerified(rig, host, join, 10);
  CHECK(rig.join.state() == SessionState::kClosed && rig.join.peer_bye() == ByeReason::kQuit);
}

std::vector<uint8_t> EncodedPacket(const Header& header, const Body& body) {
  uint8_t buffer[kMaxPacketSize];
  return std::vector<uint8_t>(buffer, buffer + Encode(header, body, buffer, sizeof buffer));
}

// Inputs and checksums carry the count of fights their sender began: a late one from an earlier fight is dropped,
// so it can neither feed the new fight's prediction nor flag it as desynced.
void TestPacketsOfAnEarlierFightAreDropped() {
  Rig rig(Verified(HostConfig()), Verified(JoinConfig()));
  FrameZeroSide host, join;
  ConnectAndReady(rig);
  RunVerified(rig, host, join, 200);
  EndFightOnBoth(rig, host, join);
  ReadyAgain(rig, host, join, FrameZeroSide{}, FrameZeroSide{});
  RunVerified(rig, host, join, 300);
  CHECK(BothRunning(rig) && rig.join.fight_index() == 1);
  const Frame checked = (rig.join.core()->confirmed_frame() / 30 + 2) * 30;

  const uint32_t dropped = rig.join.packets_dropped();
  const Header stale{rig.host.session_id(), 0, 0, 1};
  for (const Body& body : {Body{Checksum{checked, 0xBAD0BAD0}}, Body{Input{0, 0, {0xFFFF, 0xFFFF}}}}) {
    const std::vector<uint8_t> bytes = EncodedPacket(stale, body);
    rig.hub.Enqueue(kHostAddr, kJoinAddr, bytes.data(), bytes.size());
  }
  RunVerified(rig, host, join, 120);
  CHECK(rig.join.packets_dropped() == dropped + 2);
  CHECK(!rig.join.core()->desync() && rig.join.core()->stats().mismatches == 0);
  CHECK(SimsAgree(rig));
}

// A new Player Match gets a new session id, so whatever the previous match still has in flight on the same ports is
// dropped, and the new match agrees on its own frame 0.
void TestNewMatchIgnoresThePreviousMatch() {
  CHECK(MatchSessionId(1, 1000) != MatchSessionId(2, 1000));
  CHECK(MatchSessionId(2, 1000) != MatchSessionId(2, 9000));
  CHECK(MatchSessionId(0x9E3779B9u, 0) != 0);

  SessionConfig first_host = Verified(HostConfig());
  first_host.session_id = MatchSessionId(1, 1000);
  Rig rig(first_host, Verified(JoinConfig()));
  FrameZeroSide host, join;
  ConnectAndReady(rig);
  RunVerified(rig, host, join, 200);
  CHECK(BothRunning(rig));
  const uint32_t old_session = rig.host.session_id();

  SessionConfig next_host = Verified(HostConfig());
  next_host.session_id = MatchSessionId(2, rig.hub.now);
  SessionConfig next_join = Verified(JoinConfig());
  next_join.nonce = 0x77AA;
  Session host2(next_host, rig.host_node);
  Session join2(next_join, rig.join_node);
  rig.hub.queue.clear();
  host2.Start(rig.hub.now);
  join2.Start(rig.hub.now);
  host2.SetLocalReady(true, rig.hub.now);
  join2.SetLocalReady(true, rig.hub.now);
  for (int i = 0; i < 40 && !(host2.ReadyToBeginFight() && join2.ReadyToBeginFight()); ++i) {
    rig.hub.now += 16;
    host2.Poll(rig.hub.now);
    join2.Poll(rig.hub.now);
  }
  CHECK(host2.session_id() != old_session && join2.session_id() == host2.session_id());

  StateDigest stale;
  stale.regions = StateRegionHashes(FrameZeroView(FrameZeroBlob()));
  stale.setup = FrameZeroSide{}.setup;
  const std::vector<uint8_t> bytes = EncodedPacket(Header{old_session, 1, 0, 0}, stale);
  const uint32_t dropped = host2.packets_dropped();
  rig.hub.Enqueue(kJoinAddr, kHostAddr, bytes.data(), bytes.size());
  host2.OfferFrameZero(FrameZeroView(FrameZeroBlob()), kGlobalsAddress, FrameZeroSide{}.setup);
  rig.hub.now += 16;
  host2.Poll(rig.hub.now);
  CHECK(host2.packets_dropped() == dropped + 1);
  CHECK(host2.frame_zero() == FrameZeroVerdict::kPending);

  join2.OfferFrameZero(FrameZeroView(FrameZeroBlob()), kGlobalsAddress, FrameZeroSide{}.setup);
  for (int i = 0; i < 20; ++i) {
    rig.hub.now += 16;
    host2.Poll(rig.hub.now);
    join2.Poll(rig.hub.now);
  }
  CHECK(host2.frame_zero() == FrameZeroVerdict::kMatch && join2.frame_zero() == FrameZeroVerdict::kMatch);
}

}  // namespace

int main() {
  std::printf("netplay session tests\n");
  TestHandshakeAndMatch();
  TestGuestMismatchIsRejected();
  TestBuildMismatchIsRejected();
  TestIntrudersAndGarbageAreIgnored();
  TestBusyHostRejectsASecondJoiner();
  TestLostHelloAckIsResent();
  TestTimeouts();
  TestByeEndsThePeer();
  TestKeepaliveHoldsAnIdleMatch();
  TestDesyncIsReportedWithoutEndingTheSession();
  TestRealLoopbackMatch();
  TestNonBlockingReceiveReturnsAtOnce();
  TestReadyBarrierOrdering(true);
  TestReadyBarrierOrdering(false);
  TestFixedOwnership();
  TestLeavingFightStopsTakeover();
  TestNextFightStartsOnAFreshCore();
  TestRoundTripIsMeasuredFromAcks();
  TestFrameZeroMustMatchBeforeRollbackStarts();
  TestCarriedStateIsTakenFromTheHost();
  TestSetupMismatchRefusesTheFight();
  TestStateStillDifferingAfterAgreeIsRefused();
  TestStateTooDifferentIsRefused();
  TestFrameZeroNamesGlobalsAndTableBytes();
  TestFrameZeroViewKeepsOnlyTheInputCommandFields();
  TestRealDesyncAfterAgreedFrameZeroIsDetected();
  TestFightEndKeepsTheMatchForTheNextFight();
  TestPacketsOfAnEarlierFightAreDropped();
  TestNewMatchIgnoresThePreviousMatch();
  std::printf("%d checks, %d failures\n", netplay_test::g_checks, netplay_test::g_failures);
  return netplay_test::g_failures == 0 ? 0 : 1;
}
