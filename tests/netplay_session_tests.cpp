// Session and transport tests: handshake, rejection, intruders, loss, timeouts and a real loopback UDP match.
// Headless: the UDP part binds 127.0.0.1 only. Run: ssf2t_netplay_session_tests

#include <algorithm>
#include <chrono>
#include <deque>
#include <functional>
#include <thread>

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
      : host(hc, host_node, host_sim), join(jc, join_node, join_sim) {}

  void Start() {
    host.Start(hub.now);
    join.Start(hub.now);
  }
  void Step(uint64_t ms = 16) {
    hub.now += ms;
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
  ToySim sim;
  Session host(HostConfig(), host_node, sim);
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
  Session host(hc, host_socket, host_sim);
  Session join(jc, join_socket, join_sim);

  const auto t0 = std::chrono::steady_clock::now();
  const auto ms = [&] {
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
  };
  host.Start(ms());
  join.Start(ms());
  uint32_t host_accepted = 0, join_accepted = 0;
  while (ms() < 5000 && (host_accepted < 240 || join_accepted < 240)) {
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
  std::printf("%d checks, %d failures\n", netplay_test::g_checks, netplay_test::g_failures);
  return netplay_test::g_failures == 0 ? 0 : 1;
}
