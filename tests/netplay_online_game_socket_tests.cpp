// OnlineGameSocket: the one UDP socket online play binds early and keeps for every match, and the relay that carries a
// match over the online service instead. Headless; 127.0.0.1 only, fixed test ports; the service is an in-process
// stand-in that routes frames the way the hub does.
// Run: ssf2t_netplay_online_game_socket_tests

#include <chrono>
#include <cstring>
#include <deque>
#include <string>
#include <thread>
#include <vector>

#include "netplay/lobby_mux.h"
#include "netplay/online_game_socket.h"
#include "netplay/packet.h"
#include "netplay/relay_frame.h"
#include "netplay/session.h"
#include "netplay/udp_transport.h"
#include "netplay_test_support.h"

using namespace ssf2t::netplay;
using namespace netplay_test;

namespace {

constexpr uint16_t kPortA = 58101;
constexpr uint16_t kPortB = 58102;
constexpr uint16_t kPeerPort = 58109;
constexpr uint32_t kLoopback = 0x7F000001;

void WaitForLoopback() { std::this_thread::sleep_for(std::chrono::milliseconds(20)); }

// Consumes datagrams that start with "TAP", answering each from the same socket.
class EchoTap final : public DatagramTap {
 public:
  bool Take(const Endpoint& from, const uint8_t* data, size_t size, Transport& reply) override {
    if (size < 3 || std::memcmp(data, "TAP", 3) != 0) return false;
    ++taken;
    reply.Send(from, data, size);
    return true;
  }
  int taken = 0;
};

void Send(UdpTransport& from, uint16_t to_port, const char* text) {
  from.Send({kLoopback, to_port}, reinterpret_cast<const uint8_t*>(text), std::strlen(text));
}

std::string ReceiveText(Transport& transport, Endpoint& from) {
  uint8_t buffer[256];
  const size_t size = transport.Receive(buffer, sizeof buffer, from);
  return std::string(reinterpret_cast<const char*>(buffer), size);
}

void TestOpenTakesTheFirstFreePortAndNeverRebinds() {
  OnlineGameSocket first;
  CHECK(!first.is_open() && first.bound_port() == 0);
  CHECK(first.Open({kPortA, kPortB}, true));
  CHECK(first.is_open() && first.bound_port() == kPortA);

  // A second instance on this PC gets the other port.
  OnlineGameSocket second;
  CHECK(second.Open({kPortA, kPortB}, true) && second.bound_port() == kPortB);

  // Opening again changes nothing, whatever ports it names: a new bind would be a new NAT mapping.
  CHECK(first.Open({kPortB}, true) && first.bound_port() == kPortA);
  UdpTransport contender;
  CHECK(!contender.Open(kPortA, true));

  OnlineGameSocket third;
  CHECK(!third.Open({kPortA, kPortB}, true) && !third.is_open() && third.bound_port() == 0);
}

void TestTapSeesDatagramsFirstAndRepliesFromTheSameSocket() {
  OnlineGameSocket socket;
  CHECK(socket.Open({kPortA}, true));
  EchoTap tap;
  socket.SetTap(&tap);
  UdpTransport peer;
  CHECK(peer.Open(kPeerPort, true));

  Send(peer, kPortA, "TAP1");
  Send(peer, kPortA, "GAME1");
  WaitForLoopback();
  Endpoint from;
  CHECK(ReceiveText(socket, from) == "GAME1" && from == (Endpoint{kLoopback, kPeerPort}));
  CHECK(tap.taken == 1);
  WaitForLoopback();
  CHECK(ReceiveText(peer, from) == "TAP1" && from == (Endpoint{kLoopback, kPortA}));

  // What Use sends leaves from the bound port too.
  socket.Use([](Transport& out) { out.Send({kLoopback, kPeerPort}, reinterpret_cast<const uint8_t*>("USE"), 3); });
  WaitForLoopback();
  CHECK(ReceiveText(peer, from) == "USE" && from == (Endpoint{kLoopback, kPortA}));
  socket.SetTap(nullptr);
}

void TestPumpKeepsGameDatagramsInOrderForTheSession() {
  OnlineGameSocket socket;
  CHECK(socket.Open({kPortA}, true));
  EchoTap tap;
  socket.SetTap(&tap);
  UdpTransport peer;
  CHECK(peer.Open(kPeerPort, true));
  Send(peer, kPortA, "GAME1");
  Send(peer, kPortA, "TAP2");
  Send(peer, kPortA, "GAME2");
  WaitForLoopback();
  socket.Pump();
  CHECK(tap.taken == 1);
  Endpoint from;
  CHECK(ReceiveText(socket, from) == "GAME1");
  CHECK(ReceiveText(socket, from) == "GAME2");
  CHECK(ReceiveText(socket, from).empty());
  CHECK(socket.bound_port() == kPortA);
  socket.SetTap(nullptr);
}

constexpr uint32_t kToken = 0xC0FFEE01;

std::vector<uint8_t> Bytes(const char* text) { return {text, text + std::strlen(text)}; }

// What the relay hands the online service's connection.
struct Outbox {
  std::vector<std::vector<uint8_t>> frames;
  bool refuse = false;
  RelaySend Sender() {
    return [this](std::vector<uint8_t>&& frame) {
      if (refuse) return false;
      frames.push_back(std::move(frame));
      return true;
    };
  }
};

void TestRelayFramesEncodeAndDecode() {
  const std::vector<uint8_t> payload = Bytes("lobby!");
  const std::vector<uint8_t> frame = EncodeRelayFrame(0xDEADBEEF, 7, payload.data(), payload.size());
  CHECK(frame.size() == kRelayHeaderBytes + payload.size());
  CHECK(frame[0] == 'S' && frame[1] == 'R' && frame[2] == kRelayVersion && frame[3] == kRelayData);
  RelayFrameError error = RelayFrameError::kBadLength;
  const auto decoded = DecodeRelayFrame(frame.data(), frame.size(), &error);
  CHECK(decoded && decoded->token == 0xDEADBEEF && decoded->seq == 7 && decoded->payload == payload);
  CHECK(error == RelayFrameError::kNone);

  const auto reason = [&](std::vector<uint8_t> bytes, size_t size) {
    RelayFrameError why = RelayFrameError::kNone;
    CHECK(!DecodeRelayFrame(bytes.data(), size, &why));
    return std::string(RelayFrameErrorName(why));
  };
  std::vector<uint8_t> bad = frame;
  bad[0] = '{';
  CHECK(reason(bad, bad.size()) == "bad_magic");
  bad = frame;
  bad[2] = 2;
  CHECK(reason(bad, bad.size()) == "bad_version");
  bad = frame;
  bad[3] = 9;
  CHECK(reason(bad, bad.size()) == "bad_type");
  CHECK(reason(frame, 3) == "truncated");
  CHECK(reason(frame, kRelayHeaderBytes - 1) == "truncated");
  CHECK(reason(frame, frame.size() - 1) == "bad_length");
  std::vector<uint8_t> longer = frame;
  longer.push_back(0);
  CHECK(reason(longer, longer.size()) == "bad_length");

  const std::vector<uint8_t> largest(kRelayMaxPayload, 0x5A);
  const std::vector<uint8_t> full = EncodeRelayFrame(1, 1, largest.data(), largest.size());
  CHECK(DecodeRelayFrame(full.data(), full.size()));
  const std::vector<uint8_t> over(kRelayMaxPayload + 1, 0x5A);
  CHECK(EncodeRelayFrame(1, 1, over.data(), over.size()).empty());
  CHECK(EncodeRelayFrame(1, 1, over.data(), 0).empty());
  std::vector<uint8_t> forged(kRelayHeaderBytes + kRelayMaxPayload + 1, 0);
  std::memcpy(forged.data(), full.data(), kRelayHeaderBytes);
  forged[12] = uint8_t((kRelayMaxPayload + 1) >> 8);
  forged[13] = uint8_t(kRelayMaxPayload + 1);
  CHECK(reason(forged, forged.size()) == "oversized");
}

void TestRelayQueuesUntilSelectedAndSendsOnlyToTheRelayPeer() {
  OnlineGameSocket socket;
  CHECK(socket.Open({kPortA}, true));
  UdpTransport peer;
  CHECK(peer.Open(kPeerPort, true));
  Outbox outbox;
  const std::vector<uint8_t> hello = Bytes("HELLO");
  const std::vector<uint8_t> frame = EncodeRelayFrame(kToken, 1, hello.data(), hello.size());

  CHECK(!socket.DeliverRelay(frame.data(), frame.size()));  // no relay yet
  socket.OpenRelay(kToken, outbox.Sender());
  CHECK(socket.DeliverRelay(frame.data(), frame.size()));
  Endpoint from;
  CHECK(ReceiveText(socket, from).empty());  // ready, not selected: nothing reaches the game yet
  CHECK(socket.Send(kRelayPeer, hello.data(), hello.size()));  // not selected: plain UDP, harmless loopback
  CHECK(outbox.frames.empty());

  socket.SelectRelay();
  CHECK(socket.relay_selected());
  CHECK(ReceiveText(socket, from) == "HELLO" && from == kRelayPeer);
  CHECK(socket.Send(kRelayPeer, hello.data(), hello.size()));
  CHECK(socket.Send(kRelayPeer, hello.data(), 2));
  CHECK(outbox.frames.size() == 2);
  const auto sent = DecodeRelayFrame(outbox.frames[0].data(), outbox.frames[0].size());
  const auto second = DecodeRelayFrame(outbox.frames[1].data(), outbox.frames[1].size());
  CHECK(sent && sent->token == kToken && sent->seq == 1 && sent->payload == hello);
  CHECK(second && second->seq == 2 && second->payload.size() == 2);

  // Anything else still goes out as UDP, and UDP from elsewhere still comes in.
  CHECK(socket.Send({kLoopback, kPeerPort}, hello.data(), hello.size()));
  WaitForLoopback();
  CHECK(ReceiveText(peer, from) == "HELLO" && from == (Endpoint{kLoopback, kPortA}));
  CHECK(outbox.frames.size() == 2);

  const MatchTraffic traffic = socket.traffic();
  CHECK(traffic.relay_tx_packets == 2 && traffic.relay_tx_bytes == 7 && traffic.relay_rx_packets == 1);
  CHECK(traffic.relay_rx_bytes == 5 && traffic.udp_tx_packets == 2);
  CHECK(traffic.relay_tx_sizes == (std::vector<size_t>{5, 2}) && traffic.relay_rx_sizes == (std::vector<size_t>{5}));
  socket.ResetTraffic();
  CHECK(socket.traffic().relay_tx_packets == 0 && socket.traffic().relay_tx_sizes.empty());
}

void TestRelayRejectsOtherTokensAndForgetsEverythingOnClose() {
  OnlineGameSocket socket;
  CHECK(socket.Open({kPortA}, true));
  Outbox outbox;
  socket.OpenRelay(kToken, outbox.Sender());
  socket.SelectRelay();
  const std::vector<uint8_t> data = Bytes("OLD");
  const std::vector<uint8_t> stale = EncodeRelayFrame(kToken ^ 1, 1, data.data(), data.size());
  CHECK(!socket.DeliverRelay(stale.data(), stale.size()));
  const uint8_t junk[3] = {1, 2, 3};
  CHECK(!socket.DeliverRelay(junk, sizeof junk));
  CHECK(socket.traffic().relay_rejected == 2);
  Endpoint from;
  CHECK(ReceiveText(socket, from).empty());

  const std::vector<uint8_t> ours = EncodeRelayFrame(kToken, 1, data.data(), data.size());
  CHECK(socket.DeliverRelay(ours.data(), ours.size()));
  socket.CloseRelay();
  CHECK(!socket.relay_selected());
  CHECK(ReceiveText(socket, from).empty());  // the queued frame went with the relay
  CHECK(!socket.DeliverRelay(ours.data(), ours.size()));
  CHECK(socket.Send(kRelayPeer, data.data(), data.size()));  // UDP to loopback now, never the old relay
  CHECK(outbox.frames.empty());

  // The next match's relay never sees the previous one's frames.
  socket.OpenRelay(kToken + 1, outbox.Sender());
  socket.SelectRelay();
  CHECK(!socket.DeliverRelay(ours.data(), ours.size()));
  CHECK(ReceiveText(socket, from).empty());
}

void TestRelayFailsOnOverflowAndRefusedSends() {
  OnlineGameSocket socket;
  CHECK(socket.Open({kPortA}, true));
  Outbox outbox;
  socket.OpenRelay(kToken, outbox.Sender());
  socket.SelectRelay();
  const std::vector<uint8_t> data = Bytes("X");
  int accepted = 0;
  for (uint32_t i = 0; i < 300; ++i) {
    const std::vector<uint8_t> frame = EncodeRelayFrame(kToken, i, data.data(), data.size());
    accepted += socket.DeliverRelay(frame.data(), frame.size()) ? 1 : 0;
  }
  CHECK(accepted == 256);
  CHECK(socket.relay_failure() == "receive_overflow");
  Endpoint from;
  CHECK(ReceiveText(socket, from).empty());
  CHECK(!socket.Send(kRelayPeer, data.data(), data.size()));

  socket.OpenRelay(kToken, outbox.Sender());
  socket.SelectRelay();
  CHECK(socket.relay_failure().empty());
  outbox.refuse = true;
  CHECK(!socket.Send(kRelayPeer, data.data(), data.size()));
  CHECK(socket.relay_failure() == "send_refused");
}

// The hub's routing in miniature: a frame from one end of the match goes to the other end, unchanged.
struct ServiceStandIn {
  std::deque<std::vector<uint8_t>> to_host, to_join;
  RelaySend From(bool host) {
    return [this, host](std::vector<uint8_t>&& frame) {
      (host ? to_join : to_host).push_back(std::move(frame));
      return true;
    };
  }
  void Deliver(OnlineGameSocket& host, OnlineGameSocket& join) {
    for (; !to_join.empty(); to_join.pop_front()) join.DeliverRelay(to_join.front().data(), to_join.front().size());
    for (; !to_host.empty(); to_host.pop_front()) host.DeliverRelay(to_host.front().data(), to_host.front().size());
  }
};

// Real logs: "player_match lobby packet tx bytes=9". Through the relay the joiner's lobby gets exactly that datagram,
// from the relay peer, and the rollback session's own packets share the same path.
void TestLobbyPacketAndRollbackSessionCrossTheRelay() {
  OnlineGameSocket host_socket, join_socket;
  CHECK(host_socket.Open({kPortA}, true) && join_socket.Open({kPortB}, true));
  ServiceStandIn service;
  host_socket.OpenRelay(kToken, service.From(true));
  join_socket.OpenRelay(kToken, service.From(false));
  host_socket.SelectRelay();
  join_socket.SelectRelay();
  LobbyMux host_mux(host_socket), join_mux(join_socket);
  constexpr uint32_t kTag = 0x5A5A0001;
  host_mux.SetRoute(kTag, kRelayPeer);
  join_mux.SetRoute(kTag, kRelayPeer);

  LobbyDatagram packet;
  packet.from_port = 1000;
  packet.to_port = 1000;
  packet.data = {7, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};
  CHECK(join_mux.SendLobby(packet));
  CHECK(service.to_host.size() == 1);
  const auto frame = DecodeRelayFrame(service.to_host.front().data(), service.to_host.front().size());
  CHECK(frame && frame->payload.size() == kLobbyHeaderBytes + 9);
  service.Deliver(host_socket, join_socket);
  LobbyDatagram received;
  CHECK(host_mux.TakeLobby(received));
  CHECK(received.from_port == 1000 && received.to_port == 1000 && received.data == packet.data);
  CHECK(host_mux.lobby_received() == 1 && host_mux.lobby_rejected() == 0);
  LobbyDatagram answer = packet;
  answer.data.assign(1042, 6);  // the host's player set, the largest lobby datagram seen
  CHECK(host_mux.SendLobby(answer));
  service.Deliver(host_socket, join_socket);
  CHECK(join_mux.TakeLobby(received) && received.data.size() == 1042 && received.data[0] == 6);

  SessionConfig host_config;
  host_config.role = Role::kHost;
  host_config.peer = kRelayPeer;
  host_config.session_id = 0x1234;
  SessionConfig join_config;
  join_config.role = Role::kJoin;
  join_config.peer = kRelayPeer;
  join_config.nonce = 0x99;
  Session host(host_config, host_mux), join(join_config, join_mux);
  host.Start(0);
  join.Start(0);
  for (uint64_t now = 0; now < 2000 && !(host.state() == SessionState::kConnected &&
                                         join.state() == SessionState::kConnected);
       now += 16) {
    host.Poll(now);
    join.Poll(now);
    service.Deliver(host_socket, join_socket);
  }
  CHECK(host.state() == SessionState::kConnected && join.state() == SessionState::kConnected);
  const MatchTraffic traffic = join_socket.traffic();
  CHECK(traffic.relay_tx_packets >= 2 && traffic.relay_rx_packets >= 2 && traffic.udp_tx_packets == 0);
  CHECK(host_socket.traffic().udp_tx_packets == 0);
}

}  // namespace

int main() {
  std::printf("netplay online game socket tests\n");
  TestOpenTakesTheFirstFreePortAndNeverRebinds();
  TestTapSeesDatagramsFirstAndRepliesFromTheSameSocket();
  TestPumpKeepsGameDatagramsInOrderForTheSession();
  TestRelayFramesEncodeAndDecode();
  TestRelayQueuesUntilSelectedAndSendsOnlyToTheRelayPeer();
  TestRelayRejectsOtherTokensAndForgetsEverythingOnClose();
  TestRelayFailsOnOverflowAndRefusedSends();
  TestLobbyPacketAndRollbackSessionCrossTheRelay();
  std::printf("%d checks, %d failures\n", netplay_test::g_checks, netplay_test::g_failures);
  return netplay_test::g_failures == 0 ? 0 : 1;
}
