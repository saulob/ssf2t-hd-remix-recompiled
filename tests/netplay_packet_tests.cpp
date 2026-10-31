// Packet protocol tests: exact byte layout, round trips and rejection of every malformed shape. Pure logic.
// Run: ssf2t_netplay_packet_tests

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "netplay/frame.h"
#include "netplay/packet.h"
#include "netplay/udp_transport.h"
#include "netplay_test_support.h"

using namespace ssf2t::netplay;
using namespace netplay_test;

namespace {

std::vector<uint8_t> Encoded(const Header& header, const Body& body) {
  uint8_t buffer[kMaxPacketSize];
  const size_t n = Encode(header, body, buffer, sizeof buffer);
  return std::vector<uint8_t>(buffer, buffer + n);
}

void TestHelloLayout() {
  const Hello hello{GuestId::kTitleUpdate023, 0x0102, 0xAABBCCDD};
  const std::vector<uint8_t> bytes = Encoded({0x01020304, 0x0A0B, 0x0C0D}, hello);
  const uint8_t expected[] = {0x53, 0x46, 0x52, 0x54, 0x01, 0x01, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04,
                              0x0A, 0x0B, 0x0C, 0x0D, 0x02, 0x00, 0x01, 0x02, 0xAA, 0xBB, 0xCC, 0xDD};
  CHECK(bytes.size() == sizeof expected);
  CHECK(bytes.size() == sizeof expected && std::memcmp(bytes.data(), expected, sizeof expected) == 0);
  CHECK(kHeaderSize == 16);
}

void TestInputLayout() {
  Input input;
  input.next_wanted = 0x11223344;
  input.first = 0x55667788;
  input.words = {0x0102, 0x0304};
  const std::vector<uint8_t> bytes = Encoded({7, 1, 2}, input);
  const uint8_t body[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x02, 0x00, 0x01, 0x02, 0x03, 0x04};
  CHECK(bytes.size() == kHeaderSize + sizeof body);
  CHECK(bytes.size() == kHeaderSize + sizeof body && std::memcmp(bytes.data() + kHeaderSize, body, sizeof body) == 0);
}

void TestRoundTrips() {
  const Header header{0xDEADBEEF, 65535, 1};
  {
    const auto p = Decode(Encoded(header, Hello{GuestId::kBase013, kNetplayBuild, 77}).data(), kHeaderSize + 8);
    CHECK(p && p->header.session == 0xDEADBEEF && p->header.seq == 65535 && p->header.ack == 1);
    const Hello* h = p ? std::get_if<Hello>(&p->body) : nullptr;
    CHECK(h && h->guest == GuestId::kBase013 && h->build == kNetplayBuild && h->nonce == 77);
  }
  {
    HelloAck ack;
    ack.reject = RejectReason::kBusy;
    ack.joiner_slot = 1;
    ack.input_delay = 3;
    ack.nonce = 9;
    ack.start_frame = 0xFFFFFFF0;
    const auto bytes = Encoded(header, ack);
    const auto p = Decode(bytes.data(), bytes.size());
    const HelloAck* a = p ? std::get_if<HelloAck>(&p->body) : nullptr;
    CHECK(a && a->reject == RejectReason::kBusy && a->input_delay == 3 && a->nonce == 9);
    CHECK(a && a->start_frame == 0xFFFFFFF0);
  }
  {
    Input input;
    input.next_wanted = 5;
    input.first = 0xFFFFFFFE;
    for (uint16_t i = 0; i < kMaxInputsPerPacket; ++i) input.words.push_back(uint16_t(i * 257));
    const auto bytes = Encoded(header, input);
    CHECK(bytes.size() <= kMaxPacketSize);
    const auto p = Decode(bytes.data(), bytes.size());
    const Input* in = p ? std::get_if<Input>(&p->body) : nullptr;
    CHECK(in && in->first == 0xFFFFFFFE && in->words == input.words && in->next_wanted == 5);
  }
  {
    const auto bytes = Encoded(header, Input{});
    const auto p = Decode(bytes.data(), bytes.size());
    const Input* in = p ? std::get_if<Input>(&p->body) : nullptr;
    CHECK(in && in->words.empty());
  }
  {
    const auto bytes = Encoded(header, Checksum{123456, 0xCAFEF00D});
    const auto p = Decode(bytes.data(), bytes.size());
    const Checksum* c = p ? std::get_if<Checksum>(&p->body) : nullptr;
    CHECK(c && c->frame == 123456 && c->value == 0xCAFEF00D);
  }
  {
    const auto bytes = Encoded(header, Bye{ByeReason::kDesync});
    const auto p = Decode(bytes.data(), bytes.size());
    const Bye* b = p ? std::get_if<Bye>(&p->body) : nullptr;
    CHECK(b && b->reason == ByeReason::kDesync);
  }
  {
    const auto bytes = Encoded(header, Ready{true, true, 0xABCD1234});
    const auto p = Decode(bytes.data(), bytes.size());
    const Ready* r = p ? std::get_if<Ready>(&p->body) : nullptr;
    CHECK(r && r->ready && r->has_start_frame && r->start_frame == 0xABCD1234);
  }
  {
    const auto bytes = Encoded(header, Ready{false, false, 0});
    const auto p = Decode(bytes.data(), bytes.size());
    const Ready* r = p ? std::get_if<Ready>(&p->body) : nullptr;
    CHECK(r && !r->ready && !r->has_start_frame);
  }
}

void TestEveryTruncationAndExtensionIsRejected() {
  Input input;
  input.words = {1, 2, 3};
  const Body bodies[] = {Hello{}, HelloAck{}, input, Checksum{1, 2}, Bye{}, Ready{}, StateDigest{},
                         StateBlocks{1, 2, {3, 4}}, StateBytes{1, 2, {3, 4, 5}}};
  for (const Body& body : bodies) {
    const std::vector<uint8_t> bytes = Encoded({1, 2, 3}, body);
    CHECK(Decode(bytes.data(), bytes.size()).has_value());
    for (size_t n = 0; n < bytes.size(); ++n) CHECK(!Decode(bytes.data(), n).has_value());
    std::vector<uint8_t> longer = bytes;
    longer.push_back(0);
    CHECK(!Decode(longer.data(), longer.size()).has_value());
  }
  CHECK(!Decode(nullptr, 20).has_value());
}

void TestHeaderFieldsAreValidated() {
  std::vector<uint8_t> good = Encoded({1, 2, 3}, Checksum{1, 2});
  CHECK(Decode(good.data(), good.size()).has_value());
  std::vector<uint8_t> bad = good;
  bad[0] ^= 1;
  CHECK(!Decode(bad.data(), bad.size()).has_value());
  bad = good;
  bad[4] = kProtocolVersion + 1;
  CHECK(!Decode(bad.data(), bad.size()).has_value());
  for (int type : {0, 7, 8, 255}) {
    bad = good;
    bad[5] = uint8_t(type);
    CHECK(!Decode(bad.data(), bad.size()).has_value());
  }
  bad = good;
  bad[6] = 0xFF;
  CHECK(Decode(bad.data(), bad.size()).has_value());
}

void TestBodyFieldsAreValidated() {
  std::vector<uint8_t> hello = Encoded({}, Hello{});
  for (int guest : {0, 3, 255}) {
    std::vector<uint8_t> bad = hello;
    bad[kHeaderSize] = uint8_t(guest);
    CHECK(!Decode(bad.data(), bad.size()).has_value());
  }
  std::vector<uint8_t> ack = Encoded({}, HelloAck{});
  std::vector<uint8_t> bad = ack;
  bad[kHeaderSize] = 4;
  CHECK(!Decode(bad.data(), bad.size()).has_value());
  bad = ack;
  bad[kHeaderSize + 1] = 2;
  CHECK(!Decode(bad.data(), bad.size()).has_value());
  std::vector<uint8_t> bye = Encoded({}, Bye{});
  bye[kHeaderSize] = 3;
  CHECK(!Decode(bye.data(), bye.size()).has_value());

  std::vector<uint8_t> ready = Encoded({}, Ready{});
  bad = ready;
  bad[kHeaderSize] = 2;
  CHECK(!Decode(bad.data(), bad.size()).has_value());
  bad = ready;
  bad[kHeaderSize + 1] = 2;
  CHECK(!Decode(bad.data(), bad.size()).has_value());

  Input input;
  input.words = {1, 2};
  std::vector<uint8_t> in = Encoded({}, input);
  in[kHeaderSize + 8] = 3;
  CHECK(!Decode(in.data(), in.size()).has_value());
  in[kHeaderSize + 8] = uint8_t(kMaxInputsPerPacket + 1);
  CHECK(!Decode(in.data(), in.size()).has_value());
  in[kHeaderSize + 8] = 1;
  CHECK(!Decode(in.data(), in.size()).has_value());
}

void TestEncodeRefusesWhatDoesNotFit() {
  Input input;
  input.words.assign(kMaxInputsPerPacket + 1, 1);
  uint8_t buffer[kMaxPacketSize];
  CHECK(Encode({}, input, buffer, sizeof buffer) == 0);
  CHECK(Encode({}, Checksum{}, buffer, kHeaderSize + 7) == 0);
  CHECK(Encode({}, Checksum{}, buffer, kHeaderSize + 8) == kHeaderSize + 8);
  CHECK(Encode({}, Bye{}, buffer, 3) == 0);
}

void TestRandomBytesNeverCrashAndReencode() {
  Lcg rng(99);
  size_t accepted = 0;
  for (int i = 0; i < 200000; ++i) {
    uint8_t buffer[64];
    const size_t n = rng.Below(sizeof buffer + 1);
    for (size_t j = 0; j < n; ++j) buffer[j] = uint8_t(rng.Next());
    if (n >= 8 && rng.Chance(80)) {
      const uint8_t head[] = {0x53, 0x46, 0x52, 0x54, kProtocolVersion, uint8_t(1 + rng.Below(5))};
      std::memcpy(buffer, head, std::min(sizeof head, n));
    }
    const auto p = Decode(buffer, n);
    if (!p) continue;
    ++accepted;
    const auto again = Encoded(p->header, p->body);
    CHECK(Decode(again.data(), again.size()).has_value());
  }
  CHECK(accepted > 0);
}

void TestFrameArithmeticWraps() {
  CHECK(FrameDiff(5, 3) == 2);
  CHECK(FrameDiff(3, 5) == -2);
  CHECK(FrameDiff(2, 0xFFFFFFFEu) == 4);
  CHECK(FrameBefore(0xFFFFFFFEu, 2));
  CHECK(!FrameBefore(2, 0xFFFFFFFEu));
  CHECK(SeqDiff(1, 65535) == 2);
  CHECK(SeqDiff(65535, 1) == -2);
}

void TestParseIPv4() {
  uint32_t a = 0;
  CHECK(ParseIPv4("127.0.0.1", a) && a == 0x7F000001);
  CHECK(ParseIPv4("192.168.1.20", a) && a == 0xC0A80114);
  CHECK(ParseIPv4("localhost", a) && a == 0x7F000001);
  CHECK(ParseIPv4("0.0.0.0", a) && a == 0);
  CHECK(ParseIPv4("255.255.255.255", a) && a == 0xFFFFFFFF);
  for (const char* bad : {"", "1.2.3", "1.2.3.4.5", "256.1.1.1", "1.2.3.", ".1.2.3", "1..2.3", "a.b.c.d", "1.2.3.4 ",
                          "0001.2.3.4", "1.2.3.-4", "example.com"}) {
    CHECK(!ParseIPv4(bad, a));
  }
}

// What the signaling service may legitimately hand back for a peer address (real Internet test 84b7d2f1: a
// CF-Connecting-IP of "bad_peer_address" with no logged value turned out to plausibly be an IPv6 literal, which the
// IPv4-only transport cannot use).
void TestParsePeerAddress() {
  uint32_t a = 0;
  CHECK(ParsePeerAddress("177.18.65.229", a) == PeerAddressKind::kIPv4 && a == 0xB11241E5);
  CHECK(ParsePeerAddress("  179.113.149.63 ", a) == PeerAddressKind::kIPv4 && a == 0xB371953F);
  CHECK(ParsePeerAddress("", a) == PeerAddressKind::kEmpty);
  CHECK(ParsePeerAddress("   ", a) == PeerAddressKind::kEmpty);

  CHECK(ParsePeerAddress("::ffff:179.113.149.63", a) == PeerAddressKind::kIPv4MappedIPv6 && a == 0xB371953F);
  CHECK(ParsePeerAddress("::FFFF:10.0.0.1", a) == PeerAddressKind::kIPv4MappedIPv6 && a == 0x0A000001);
  CHECK(ParsePeerAddress("[::ffff:179.113.149.63]", a) == PeerAddressKind::kIPv4MappedIPv6 && a == 0xB371953F);

  CHECK(ParsePeerAddress("179.113.149.63:54114", a) == PeerAddressKind::kHostPort && a == 0xB371953F);
  CHECK(ParsePeerAddress("[179.113.149.63]", a) == PeerAddressKind::kIPv4 && a == 0xB371953F);

  CHECK(ParsePeerAddress("2804:14d::1", a) == PeerAddressKind::kIPv6);
  CHECK(ParsePeerAddress("::1", a) == PeerAddressKind::kIPv6);
  CHECK(ParsePeerAddress("[2804:14d::1]", a) == PeerAddressKind::kIPv6);
  CHECK(ParsePeerAddress("[2804:14d::1]:7001", a) == PeerAddressKind::kIPv6);
  CHECK(ParsePeerAddress("fe80::1%eth0", a) == PeerAddressKind::kIPv6);

  CHECK(ParsePeerAddress("example.com:7001", a) == PeerAddressKind::kInvalid);
  CHECK(ParsePeerAddress("not-an-address", a) == PeerAddressKind::kInvalid);
  CHECK(ParsePeerAddress("[", a) == PeerAddressKind::kInvalid);
  CHECK(ParsePeerAddress("[]", a) == PeerAddressKind::kEmpty);

  for (const PeerAddressKind kind : {PeerAddressKind::kEmpty, PeerAddressKind::kInvalid, PeerAddressKind::kIPv6}) {
    CHECK(!UsableAddress(kind));
  }
  for (const PeerAddressKind kind : {PeerAddressKind::kIPv4, PeerAddressKind::kIPv4MappedIPv6, PeerAddressKind::kHostPort}) {
    CHECK(UsableAddress(kind));
  }
  CHECK(std::string(PeerAddressKindName(PeerAddressKind::kIPv6)) == "ipv6");
  CHECK(std::string(PeerAddressKindName(PeerAddressKind::kIPv4MappedIPv6)) == "ipv4_mapped_ipv6");
}

// The frame-0 handshake: one digest per peer, then block hashes and raw bytes only while the states disagree.
void TestFrameZeroBodies() {
  StateDigest digest;
  digest.fight = 0x01020304;
  for (size_t i = 0; i < kStateRegions; ++i) digest.regions[i] = 0xA0000000u + uint32_t(i);
  std::vector<uint8_t> bytes = Encoded({1, 2, 3}, digest);
  CHECK(bytes.size() == kHeaderSize + 6 + 4 * kStateRegions);
  CHECK(bytes[5] == uint8_t(PacketType::kStateDigest));
  auto p = Decode(bytes.data(), bytes.size());
  const StateDigest* d = p ? std::get_if<StateDigest>(&p->body) : nullptr;
  CHECK(d && d->fight == 0x01020304 && d->regions == digest.regions && d->round == 0 && d->setup.empty());

  digest.round = 1;
  digest.setup.assign(kMaxSetupWords, 0);
  for (size_t i = 0; i < kMaxSetupWords; ++i) digest.setup[i] = 0x10000000u * uint32_t(i % 16) + uint32_t(i);
  bytes = Encoded({1, 2, 3}, digest);
  CHECK(bytes.size() == kHeaderSize + 6 + 4 * kStateRegions + 4 * kMaxSetupWords && bytes.size() <= kMaxPacketSize);
  p = Decode(bytes.data(), bytes.size());
  d = p ? std::get_if<StateDigest>(&p->body) : nullptr;
  CHECK(d && d->round == 1 && d->setup == digest.setup && d->regions == digest.regions);

  StateBlocks blocks{7, 900, std::vector<uint32_t>(kMaxBlockHashesPerPacket, 0xDEADBEEF), 1};
  bytes = Encoded({1, 2, 3}, blocks);
  CHECK(bytes.size() <= kMaxPacketSize);
  p = Decode(bytes.data(), bytes.size());
  const StateBlocks* b = p ? std::get_if<StateBlocks>(&p->body) : nullptr;
  CHECK(b && b->fight == 7 && b->first == 900 && b->hashes == blocks.hashes && b->round == 1);

  StateBytes raw{7, 0x123456, std::vector<uint8_t>(kMaxStateBytesPerPacket, 0x5A), 1};
  bytes = Encoded({1, 2, 3}, raw);
  CHECK(bytes.size() <= kMaxPacketSize);
  p = Decode(bytes.data(), bytes.size());
  const StateBytes* r = p ? std::get_if<StateBytes>(&p->body) : nullptr;
  CHECK(r && r->fight == 7 && r->offset == 0x123456 && r->bytes == raw.bytes && r->round == 1);

  uint8_t buffer[kMaxPacketSize];
  CHECK(Encode({}, StateBlocks{1, 0, {}}, buffer, sizeof buffer) == 0);
  CHECK(Encode({}, StateBlocks{1, 0, std::vector<uint32_t>(kMaxBlockHashesPerPacket + 1)}, buffer, sizeof buffer) == 0);
  CHECK(Encode({}, StateBytes{1, 0, {}}, buffer, sizeof buffer) == 0);
  CHECK(Encode({}, StateBytes{1, 0, std::vector<uint8_t>(kMaxStateBytesPerPacket + 1)}, buffer, sizeof buffer) == 0);
  CHECK(Encode({}, StateBytes{1, 0, {1}, 2}, buffer, sizeof buffer) == 0);
  StateDigest too_many;
  too_many.setup.assign(kMaxSetupWords + 1, 1);
  CHECK(Encode({}, too_many, buffer, sizeof buffer) == 0);

  // Rounds above 1 and setup counts that disagree with the length are refused.
  std::vector<uint8_t> bad = Encoded({}, StateDigest{});
  bad[kHeaderSize + 4] = 2;
  CHECK(!Decode(bad.data(), bad.size()).has_value());
  bad = Encoded({}, StateDigest{});
  bad[kHeaderSize + 5] = 1;
  CHECK(!Decode(bad.data(), bad.size()).has_value());
  bad = Encoded({}, StateBlocks{1, 0, {5}});
  bad[kHeaderSize + 7] = 2;
  CHECK(!Decode(bad.data(), bad.size()).has_value());
  bad = Encoded({}, StateBytes{1, 0, {5}});
  bad[kHeaderSize + 9] = 2;
  CHECK(!Decode(bad.data(), bad.size()).has_value());
  // A build without the setup agreement would leave its peer waiting at frame 0, so it is refused at Hello.
  CHECK(kNetplayBuild == 3);
}

// The header's reserved word carries the sender's fight count, so a late packet of an earlier fight is recognised.
void TestHeaderCarriesTheFight() {
  const Header header{0x11223344, 5, 6, 0xBEEF};
  const std::vector<uint8_t> bytes = Encoded(header, Checksum{1, 2});
  CHECK(bytes[6] == 0xBE && bytes[7] == 0xEF);
  const auto p = Decode(bytes.data(), bytes.size());
  CHECK(p && p->header.fight == 0xBEEF && p->header.session == 0x11223344 && p->header.seq == 5 && p->header.ack == 6);
}

}  // namespace

int main() {
  std::printf("netplay packet tests\n");
  TestHelloLayout();
  TestInputLayout();
  TestRoundTrips();
  TestEveryTruncationAndExtensionIsRejected();
  TestHeaderFieldsAreValidated();
  TestBodyFieldsAreValidated();
  TestEncodeRefusesWhatDoesNotFit();
  TestRandomBytesNeverCrashAndReencode();
  TestFrameArithmeticWraps();
  TestParseIPv4();
  TestParsePeerAddress();
  TestFrameZeroBodies();
  TestHeaderCarriesTheFight();
  std::printf("%d checks, %d failures\n", netplay_test::g_checks, netplay_test::g_failures);
  return netplay_test::g_failures == 0 ? 0 : 1;
}
