// Packet protocol tests: exact byte layout, round trips and rejection of every malformed shape. Pure logic.
// Run: ssf2t_netplay_packet_tests

#include <algorithm>
#include <cstring>
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
}

void TestEveryTruncationAndExtensionIsRejected() {
  Input input;
  input.words = {1, 2, 3};
  const Body bodies[] = {Hello{}, HelloAck{}, input, Checksum{1, 2}, Bye{}};
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
  for (int type : {0, 6, 7, 255}) {
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
  std::printf("%d checks, %d failures\n", netplay_test::g_checks, netplay_test::g_failures);
  return netplay_test::g_failures == 0 ? 0 : 1;
}
