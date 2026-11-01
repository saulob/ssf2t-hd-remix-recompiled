// Match setup on the gameplay socket: the STUN exchange that learns the socket's mapped endpoint, the peer's
// candidates, and the punch whose acknowledgement alone proves a path. In memory: no socket is opened.
// Run: ssf2t_online_rendezvous_tests

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "online/rendezvous.h"

using namespace ssf2t::online;
using ssf2t::netplay::Endpoint;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

struct Sent {
  Endpoint to;
  std::vector<uint8_t> data;
};

class FakeTransport final : public ssf2t::netplay::Transport {
 public:
  bool Send(const Endpoint& to, const uint8_t* data, size_t size) override {
    sent.push_back({to, std::vector<uint8_t>(data, data + size)});
    return true;
  }
  size_t Receive(uint8_t*, size_t, Endpoint&) override { return 0; }
  std::vector<Sent> sent;
};

constexpr Endpoint kStunA{0x0A000001, 3478};
constexpr Endpoint kStunB{0x0A000002, 19302};
constexpr Endpoint kWilliam{0xB371953F, 54114};  // 179.113.149.63:54114, a mapping of local port 7001
constexpr Endpoint kSaulo{0xB11241E5, 57219};    // 177.18.65.229:57219

std::vector<uint8_t> StunAnswer(const std::vector<uint8_t>& request, const Endpoint& mapped) {
  std::vector<uint8_t> reply(32);
  reply[0] = 0x01, reply[1] = 0x01, reply[3] = 0x0c;
  std::memcpy(reply.data() + 4, request.data() + 4, 16);
  const uint16_t port = mapped.port ^ 0x2112;
  const uint32_t ip = mapped.address ^ 0x2112a442u;
  const uint8_t attribute[12] = {0x00, 0x20, 0x00, 0x08, 0x00, 0x01, uint8_t(port >> 8), uint8_t(port),
                                 uint8_t(ip >> 24), uint8_t(ip >> 16), uint8_t(ip >> 8), uint8_t(ip)};
  std::memcpy(reply.data() + 20, attribute, sizeof attribute);
  return reply;
}

struct Fixture {
  std::vector<std::string> logs;
  Rendezvous rendezvous{[this](const std::string& line) { logs.push_back(line); }};
  FakeTransport out;

  int Count(const std::string& part) const {
    int n = 0;
    for (const std::string& line : logs) n += line.find(part) != std::string::npos ? 1 : 0;
    return n;
  }
  bool Feed(const Endpoint& from, const std::vector<uint8_t>& data) {
    return rendezvous.Take(from, data.data(), data.size(), out);
  }
  int SentTo(const Endpoint& to) const {
    int n = 0;
    for (const Sent& s : out.sent) n += s.to == to ? 1 : 0;
    return n;
  }
};

void TestStunLearnsTheMappedEndpointWithItsOwnPort() {
  Fixture f;
  f.rendezvous.StartStun({kStunA, kStunB}, 7001, 1000, f.out);
  CHECK(f.rendezvous.stun_stage() == Rendezvous::Stage::kRunning && f.rendezvous.active());
  CHECK(f.out.sent.size() == 2 && f.out.sent[0].to == kStunA && f.out.sent[1].to == kStunB);
  CHECK(f.out.sent[0].data.size() == 20 && f.out.sent[0].data != f.out.sent[1].data);  // one transaction each

  CHECK(f.Feed(kStunA, StunAnswer(f.out.sent[0].data, kWilliam)));
  CHECK(f.rendezvous.stun_stage() == Rendezvous::Stage::kDone);
  CHECK(f.rendezvous.mapped() == kWilliam);  // 54114, not the local 7001
  CHECK(f.Count("stun mapped endpoint=179.113.149.63:54114 local_port=7001") == 1);

  CHECK(f.Feed(kStunB, StunAnswer(f.out.sent[1].data, kWilliam)));
  CHECK(f.Count("stun second endpoint=179.113.149.63:54114 agrees=true") == 1);
  CHECK(f.rendezvous.mapped() == kWilliam);

  // A port the NAT keeps is reported as it is.
  Fixture kept;
  kept.rendezvous.StartStun({kStunA}, 7001, 0, kept.out);
  CHECK(kept.Feed(kStunA, StunAnswer(kept.out.sent[0].data, {kSaulo.address, 7001})));
  CHECK(kept.rendezvous.mapped() == (Endpoint{kSaulo.address, 7001}));
}

void TestStunRetriesThenGivesUp() {
  Fixture f;
  f.rendezvous.StartStun({kStunA, kStunB}, 7001, 0, f.out);
  f.rendezvous.Tick(100, f.out);
  CHECK(f.out.sent.size() == 2);
  f.rendezvous.Tick(260, f.out);
  CHECK(f.out.sent.size() == 4);
  // Another transaction's answer, and an answer that is not STUN at all, change nothing.
  std::vector<uint8_t> foreign = f.out.sent[0].data;
  foreign[10] ^= 0xff;
  CHECK(f.Feed(kStunA, StunAnswer(foreign, kWilliam)));
  CHECK(f.rendezvous.stun_stage() == Rendezvous::Stage::kRunning);
  f.rendezvous.Tick(2600, f.out);
  CHECK(f.rendezvous.stun_stage() == Rendezvous::Stage::kFailed && !f.rendezvous.active());
  CHECK(f.rendezvous.mapped() == Endpoint{});
  CHECK(f.Count("stun mapped endpoint=none reason=no_answer local_port=7001") == 1);

  Fixture none;
  none.rendezvous.StartStun({}, 7001, 0, none.out);
  CHECK(none.rendezvous.stun_stage() == Rendezvous::Stage::kFailed && none.out.sent.empty());
}

void TestSecondStunMappingThatDiffers() {
  Fixture f;
  f.rendezvous.StartStun({kStunA, kStunB}, 7001, 0, f.out);
  f.Feed(kStunA, StunAnswer(f.out.sent[0].data, kWilliam));
  f.Feed(kStunB, StunAnswer(f.out.sent[1].data, {kWilliam.address, 54200}));
  CHECK(f.Count("agrees=false") == 1 && f.rendezvous.mapped() == kWilliam);
}

PeerInfo Peer(const std::string& ip, uint16_t port, const std::string& public_ip = {}, uint16_t public_port = 0) {
  PeerInfo peer;
  peer.match = "m1";
  peer.role = "host";
  peer.ip = ip;
  peer.port = port;
  peer.public_ip = public_ip;
  peer.public_port = public_port;
  return peer;
}

void TestCandidates() {
  // The STUN endpoint first; the observed address with the local port only after it, as a fallback.
  auto c = PeerCandidates(Peer("2804:14d::1", 7001, "179.113.149.63", 54114), {}, kSaulo);
  CHECK(c.size() == 1 && c[0].type == CandidateType::kStun && c[0].endpoint == kWilliam);
  c = PeerCandidates(Peer("179.113.149.63", 7001, "179.113.149.63", 54114), {}, kSaulo);
  CHECK(c.size() == 2 && c[0].type == CandidateType::kStun && c[0].endpoint == kWilliam &&
        c[1].type == CandidateType::kObserved && c[1].endpoint == (Endpoint{kWilliam.address, 7001}));
  // A NAT that keeps the port makes both the same endpoint: offered once, as the STUN one.
  c = PeerCandidates(Peer("179.113.149.63", 54114, "179.113.149.63", 54114), {}, kSaulo);
  CHECK(c.size() == 1 && c[0].type == CandidateType::kStun);

  // IPv4-mapped IPv6 is the same IPv4 address.
  c = PeerCandidates(Peer("::ffff:179.113.149.63", 7001, "::ffff:179.113.149.63", 54114), {}, kSaulo);
  CHECK(c.size() == 2 && c[0].type == CandidateType::kStun && c[0].endpoint == kWilliam);

  // No STUN endpoint: only then the observed guess, and only when it is IPv4.
  c = PeerCandidates(Peer("179.113.149.63", 7001), {}, kSaulo);
  CHECK(c.size() == 1 && c[0].type == CandidateType::kObserved && c[0].endpoint == (Endpoint{kWilliam.address, 7001}));
  CHECK(PeerCandidates(Peer("2804:14d::1", 7001), {}, kSaulo).empty());
  CHECK(PeerCandidates(Peer("not-an-ip", 7001), {}, kSaulo).empty());
  CHECK(PeerCandidates(Peer("", 7001), {}, kSaulo).empty());
  c = PeerCandidates(Peer("2804:14d::1", 7001, "garbage", 54114), {}, kSaulo);
  CHECK(c.empty());
  c = PeerCandidates(Peer("179.113.149.63", 0, "179.113.149.63", 0), {}, kSaulo);  // no usable port anywhere
  CHECK(c.empty());

  // Same network: LAN endpoints first, then the STUN one.
  PeerInfo lan = Peer("179.113.149.63", 7002, "179.113.149.63", 54114);
  lan.same_network = true;
  lan.lan = {"127.0.0.1", "192.168.1.9"};
  c = PeerCandidates(lan, {"127.0.0.1", "192.168.1.5"}, kWilliam);
  CHECK(c.size() == 3 && c[0].type == CandidateType::kLan && c[0].endpoint == (Endpoint{0xC0A80109, 7002}) &&
        c[1].type == CandidateType::kStun && c[2].type == CandidateType::kObserved);
  // The same public address counts as the same network even when the WebSocket addresses differ.
  lan.same_network = false;
  c = PeerCandidates(lan, {"192.168.1.5"}, {kWilliam.address, 61000});
  CHECK(c.size() == 3 && c[0].type == CandidateType::kLan);
  c = PeerCandidates(lan, {"192.168.1.5"}, kSaulo);
  CHECK(c.size() == 2 && c[0].type == CandidateType::kStun);  // different networks: no LAN guesses
  // Same PC (its LAN address is ours): loopback, at the other instance's own port.
  lan.same_network = true;
  c = PeerCandidates(lan, {"192.168.1.9"}, kWilliam);
  CHECK(!c.empty() && c[0].endpoint == (Endpoint{0x7F000001, 7002}));
}

void TestPunchPacketFormat() {
  const std::vector<uint8_t> bytes = EncodePunch({PunchKind::kAck, 2, 0x0123456789abcdefull});
  CHECK(bytes.size() == 14 && std::memcmp(bytes.data(), "SFPN", 4) == 0);
  const auto packet = DecodePunch(bytes.data(), bytes.size());
  CHECK(packet && packet->kind == PunchKind::kAck && packet->side == 2 && packet->key == 0x0123456789abcdefull);
  CHECK(!DecodePunch(bytes.data(), 13));
  std::vector<uint8_t> bad = bytes;
  bad[4] = 9;
  CHECK(!DecodePunch(bad.data(), bad.size()));
  bad = bytes;
  bad[0] = 'X';
  CHECK(!DecodePunch(bad.data(), bad.size()));
  CHECK(PunchKey("m1") != PunchKey("m2") && PunchKey("m1") == PunchKey("m1") && PunchKey("") != 0);
}

void TestOnlyAnAcknowledgementSelects() {
  Fixture f;
  const uint64_t key = PunchKey("m1");
  f.rendezvous.StartPunch(key, 1, {{CandidateType::kStun, kWilliam}}, 0, f.out);
  CHECK(f.rendezvous.punch_stage() == Rendezvous::Stage::kRunning && f.SentTo(kWilliam) == 1);
  for (uint64_t t = 100; t <= 500; t += 100) f.rendezvous.Tick(t, f.out);
  CHECK(f.SentTo(kWilliam) == 6);
  CHECK(f.Count("udp punch tx endpoint=179.113.149.63:54114 type=stun") == 1);
  CHECK(f.rendezvous.punch_stage() == Rendezvous::Stage::kRunning);

  // Its punch: answered, and no proof for this side.
  f.out.sent.clear();
  CHECK(f.Feed(kWilliam, EncodePunch({PunchKind::kPunch, 2, key})));
  CHECK(f.out.sent.size() == 1 && f.out.sent[0].to == kWilliam);
  const auto ack = DecodePunch(f.out.sent[0].data.data(), f.out.sent[0].data.size());
  CHECK(ack && ack->kind == PunchKind::kAck && ack->side == 1 && ack->key == key);
  CHECK(f.rendezvous.punch_stage() == Rendezvous::Stage::kRunning);

  // Stale (another match), its own side reflected back, or garbage: consumed, never selected or answered.
  f.out.sent.clear();
  CHECK(f.Feed(kWilliam, EncodePunch({PunchKind::kAck, 2, PunchKey("m0")})));
  CHECK(f.Feed(kWilliam, EncodePunch({PunchKind::kAck, 1, key})));
  CHECK(f.Feed(kWilliam, EncodePunch({PunchKind::kPunch, 2, PunchKey("m0")})));
  CHECK(f.out.sent.empty() && f.rendezvous.punch_stage() == Rendezvous::Stage::kRunning);

  // Game traffic is not the tap's.
  const std::vector<uint8_t> game = {'S', 'F', 'R', 'T', 1, 2, 3};
  CHECK(!f.Feed(kWilliam, game));
  CHECK(!f.Feed(kWilliam, {0}));

  CHECK(f.Feed(kWilliam, EncodePunch({PunchKind::kAck, 2, key})));
  CHECK(f.rendezvous.punch_stage() == Rendezvous::Stage::kDone && !f.rendezvous.active());
  CHECK(f.rendezvous.selected().endpoint == kWilliam && f.rendezvous.selected().type == CandidateType::kStun);
  CHECK(f.Count("udp candidate selected endpoint=179.113.149.63:54114 type=stun") == 1);

  // Once selected it stops punching but keeps answering, so the peer can finish too.
  f.out.sent.clear();
  f.rendezvous.Tick(600, f.out);
  CHECK(f.out.sent.empty());
  CHECK(f.Feed(kWilliam, EncodePunch({PunchKind::kPunch, 2, key})) && f.out.sent.size() == 1);
}

// The acknowledgement selects the address it came from, which a NAT may give another port than the advertised one.
void TestPeerReflexiveCandidate() {
  Fixture f;
  const uint64_t key = PunchKey("m1");
  f.rendezvous.StartPunch(key, 2, {{CandidateType::kStun, kSaulo}}, 0, f.out);
  const Endpoint other_port{kSaulo.address, 61234};
  f.Feed(other_port, EncodePunch({PunchKind::kPunch, 1, key}));
  CHECK(f.Count("peer candidate received type=peer endpoint=177.18.65.229:61234") == 1);
  f.out.sent.clear();
  f.rendezvous.Tick(100, f.out);
  CHECK(f.SentTo(other_port) == 1 && f.SentTo(kSaulo) == 1);
  f.Feed(other_port, EncodePunch({PunchKind::kAck, 1, key}));
  CHECK(f.rendezvous.selected().endpoint == other_port && f.rendezvous.selected().type == CandidateType::kPeer);
}

void TestPunchTimesOutAndStopForgetsTheMatch() {
  Fixture f;
  const uint64_t key = PunchKey("m1");
  f.rendezvous.StartPunch(key, 1, {{CandidateType::kStun, kWilliam}, {CandidateType::kStun, kWilliam}}, 0, f.out);
  f.rendezvous.Tick(9900, f.out);
  CHECK(f.rendezvous.punch_stage() == Rendezvous::Stage::kRunning);
  f.rendezvous.Tick(10000, f.out);
  CHECK(f.rendezvous.punch_stage() == Rendezvous::Stage::kFailed);
  CHECK(f.Count("udp rendezvous failed reason=punch_timeout candidates=1 peer_heard=false") == 1);
  const size_t sent = f.out.sent.size();
  f.rendezvous.Tick(10100, f.out);
  CHECK(f.out.sent.size() == sent);  // bounded: nothing more after the timeout

  Fixture none;
  none.rendezvous.StartPunch(key, 1, {}, 0, none.out);
  CHECK(none.rendezvous.punch_stage() == Rendezvous::Stage::kFailed);
  CHECK(none.Count("reason=no_candidates") == 1);

  Fixture stopped;
  stopped.rendezvous.StartPunch(key, 1, {{CandidateType::kStun, kWilliam}}, 0, stopped.out);
  stopped.rendezvous.Stop();
  stopped.out.sent.clear();
  CHECK(stopped.Feed(kWilliam, EncodePunch({PunchKind::kPunch, 2, key})));  // still not the game's
  CHECK(stopped.out.sent.empty() && stopped.rendezvous.punch_stage() == Rendezvous::Stage::kIdle);
  stopped.rendezvous.Tick(200, stopped.out);
  CHECK(stopped.out.sent.empty());
}

}  // namespace

int main() {
  std::printf("online rendezvous tests\n");
  TestStunLearnsTheMappedEndpointWithItsOwnPort();
  TestStunRetriesThenGivesUp();
  TestSecondStunMappingThatDiffers();
  TestCandidates();
  TestPunchPacketFormat();
  TestOnlyAnAcknowledgementSelects();
  TestPeerReflexiveCandidate();
  TestPunchTimesOutAndStopForgetsTheMatch();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
