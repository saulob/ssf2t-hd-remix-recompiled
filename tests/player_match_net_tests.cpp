#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "guest_memory.h"
#include "netplay/lobby_mux.h"
#include "netplay/udp_transport.h"
#include "netplay_setting.h"
#include "player_match.h"
#include "player_match_guest.h"
#include "player_match_net.h"

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(expr)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(expr)) {                                                  \
      ++g_failures;                                                 \
      std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    }                                                               \
  } while (0)

using namespace ssf2t;
namespace np = ssf2t::netplay;

constexpr char kHostCode[] = "AAAA-2222";
constexpr char kJoinCode[] = "BBBB-3333";

std::optional<online::ActivePeer> Peer(const char* match, const char* player, bool host) {
  return online::ActivePeer{match, player, host};
}

class FakeLink final : public LobbyLink {
 public:
  bool Send(const np::LobbyDatagram& d) override {
    sent.push_back(d);
    return true;
  }
  bool Take(np::LobbyDatagram& d) override {
    if (inbox.empty()) return false;
    d = inbox.front();
    inbox.pop_front();
    return true;
  }
  std::vector<np::LobbyDatagram> sent;
  std::deque<np::LobbyDatagram> inbox;
};

std::string ReadSource(const char* name) {
  std::ifstream in(std::string(SSF2T_SOURCE_DIR) + "/src/" + name, std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

void TestActiveMatchCreatesARoute() {
  CHECK(!RouteFor(std::nullopt));
  CHECK(!RouteFor(Peer("", kJoinCode, true)));
  const auto host_view = RouteFor(Peer("m1", kJoinCode, true));
  const auto join_view = RouteFor(Peer("m1", kHostCode, false));
  CHECK(host_view && join_view);
  CHECK(host_view->address == SyntheticAddress(VirtualXuid(kJoinCode)));
  CHECK(join_view->address == SyntheticAddress(VirtualXuid(kHostCode)));
  CHECK(host_view->tag == join_view->tag && host_view->tag == SessionTag("m1"));
  CHECK(host_view->host && !join_view->host);
  CHECK(RouteFor(Peer("m2", kJoinCode, true))->tag != host_view->tag);
}

void TestXnAddrMapsBothWays() {
  uint8_t xnaddr[kXnAddrBytes];
  const uint64_t xuid = VirtualXuid(kHostCode);
  WriteXnAddr(xnaddr, xuid);
  CHECK(XnAddrXuid(xnaddr) == xuid);
  const uint32_t address = LoadBE32(xnaddr, 0);
  CHECK(address == SyntheticAddress(xuid) && LoadBE32(xnaddr, 4) == address && IsSyntheticAddress(address));
  CHECK((address & 0xFF) != 0 && (address & 0xFF) != 255 && ((address >> 8) & 0xFF) != 0);
  CHECK(xnaddr[10] == uint8_t(xuid >> 40) && xnaddr[15] == uint8_t(xuid));
  uint8_t reverse[kXnAddrBytes];
  const auto route = RouteFor(Peer("m1", kHostCode, false));
  WriteXnAddr(reverse, route->xuid);
  CHECK(std::memcmp(reverse, xnaddr, kXnAddrBytes) == 0);
  uint8_t foreign[kXnAddrBytes] = {};
  StoreBE32(foreign, 0, 0x7F000001);
  CHECK(!XnAddrXuid(foreign));
  CHECK(!IsSyntheticAddress(0x7F000001) && !IsSyntheticAddress(0xC0A80105));
}

void TestQosReturnsTheAdvertisedPeer() {
  online::PlayerMatchAd ad;
  ad.player = kHostCode;
  ad.qos = std::string(224, 'a');
  uint8_t host[kXnAddrBytes], stranger[kXnAddrBytes];
  WriteXnAddr(host, VirtualXuid(kHostCode));
  WriteXnAddr(stranger, VirtualXuid("CCCC-4444"));
  const QosTarget found = QosTargetFor(host, {ad});
  CHECK(found.reachable && found.data.size() == 112 && found.rtt_ms == kUnmeasuredRttMs);
  CHECK(!QosTargetFor(stranger, {ad}).reachable);
  CHECK(!QosTargetFor(host, {}).reachable);

  const std::vector<QosTarget> targets = {found, QosTargetFor(stranger, {ad})};
  constexpr uint32_t kAddress = 0x40002000;
  std::vector<uint8_t> out(QosResultBytes(targets), 0xCC);
  CHECK(out.size() == kQosHeaderBytes + 2 * kQosInfoBytes + 112);
  WriteQosResult(out.data(), kAddress, targets);
  CHECK(LoadBE32(out.data(), 0) == 2 && LoadBE32(out.data(), 4) == 0);
  const uint8_t* first = out.data() + kQosHeaderBytes;
  CHECK(first[0] == (kQosComplete | kQosTargetContacted | kQosDataReceived));
  CHECK(LoadBE16(first, 2) == 1 && LoadBE16(first, 4) == 1 && LoadBE16(first, 6) == 112);
  CHECK(LoadBE32(first, 8) == kAddress + kQosHeaderBytes + 2 * kQosInfoBytes);
  CHECK(LoadBE16(first, 12) == kUnmeasuredRttMs && LoadBE32(first, 16) == 0 && LoadBE32(first, 20) == 0);
  CHECK(out[kQosHeaderBytes + 2 * kQosInfoBytes] == 0xAA);
  const uint8_t* second = first + kQosInfoBytes;
  CHECK(second[0] == (kQosComplete | kQosTargetDisabled) && LoadBE16(second, 6) == 0);
  // The game's consumer keeps a result only with COMPLETE and DATA_RECEIVED, without TARGET_DISABLED, and 112 bytes.
  const auto kept = [](const uint8_t* info) {
    return (info[0] & 0x01) && (info[0] & 0x08) && !(info[0] & 0x04) && LoadBE16(info, 6) == 112;
  };
  CHECK(kept(first) && !kept(second));
}

void TestShimRoutesOnlyToThePeer() {
  VdpShim shim;
  FakeLink link;
  const uint32_t lobby = shim.Create();
  const uint32_t other = shim.Create();
  CHECK(lobby != other && shim.Owns(lobby) && !shim.Owns(0x1234));
  shim.Bind(lobby, 1000);
  shim.Bind(other, 1001);
  const auto route = RouteFor(Peer("m1", kHostCode, false));
  const uint8_t payload[] = {0x00, 0x03, 'h', 'i', '!'};
  CHECK(!shim.SendTo(lobby, route->address, 1000, payload, sizeof payload, std::nullopt, link));
  CHECK(!shim.SendTo(lobby, 0xC0A80105, 1000, payload, sizeof payload, route, link));
  CHECK(link.sent.empty());
  CHECK(shim.SendTo(lobby, route->address, 1000, payload, sizeof payload, route, link));
  CHECK(link.sent.size() == 1 && link.sent[0].from_port == 1000 && link.sent[0].to_port == 1000);
  CHECK(link.sent[0].data == std::vector<uint8_t>(payload, payload + sizeof payload));

  link.inbox.push_back({2000, 1001, {9, 9}});
  link.inbox.push_back({2000, 1000, {1, 2, 3}});
  link.inbox.push_back({2000, 4444, {7}});
  const auto got = shim.RecvFrom(lobby, route, link);
  CHECK(got && got->from_address == route->address && got->from_port == 2000);
  CHECK(got && got->data == (std::vector<uint8_t>{1, 2, 3}));
  CHECK(!shim.RecvFrom(lobby, route, link));
  CHECK(shim.queued(other) == 1);
  const auto other_got = shim.RecvFrom(other, route, link);
  CHECK(other_got && other_got->data == (std::vector<uint8_t>{9, 9}));
  CHECK(!shim.RecvFrom(lobby, std::nullopt, link));
}

void TestShimForgetsAStaleSession() {
  VdpShim shim;
  FakeLink link;
  const uint32_t socket = shim.Create();
  shim.Bind(socket, 1000);
  const auto first = RouteFor(Peer("m1", kHostCode, false));
  link.inbox.push_back({1000, 1000, {1}});
  link.inbox.push_back({1000, 1000, {2}});
  CHECK(shim.RecvFrom(socket, first, link).has_value());
  CHECK(shim.queued(socket) == 1);
  const auto second = RouteFor(Peer("m2", kHostCode, false));
  CHECK(!shim.RecvFrom(socket, second, link));
  CHECK(shim.queued(socket) == 0);
  shim.Close(socket);
  CHECK(!shim.Owns(socket));
  CHECK(!shim.SendTo(socket, second->address, 1000, nullptr, 0, second, link));
}

void TestLobbyDatagramFormat() {
  const np::LobbyDatagram d{1000, 1001, {1, 2, 3}};
  const std::vector<uint8_t> bytes = np::EncodeLobby(0xA1B2C3D4, d);
  CHECK(bytes.size() == np::kLobbyHeaderBytes + 3 && bytes[0] == 'S' && bytes[3] == 'B');
  const auto decoded = np::DecodeLobby(bytes.data(), bytes.size());
  CHECK(decoded && decoded->first == 0xA1B2C3D4 && decoded->second.from_port == 1000 &&
        decoded->second.to_port == 1001 && decoded->second.data == d.data);
  const uint8_t gameplay[] = {'S', 'F', 'N', 'P', 1, 2, 3, 4, 5, 6, 7, 8, 9};
  CHECK(!np::DecodeLobby(gameplay, sizeof gameplay));
  CHECK(!np::DecodeLobby(bytes.data(), 5));
}

void TestMuxCarriesLobbyAndGameplayOverLoopback() {
  np::UdpTransport a_socket, b_socket;
  CHECK(a_socket.Open(0, true) && b_socket.Open(0, true));
  np::LobbyMux a(a_socket), b(b_socket);
  const uint32_t loopback = 0x7F000001;
  const uint32_t tag = SessionTag("m1");
  a.SetRoute(tag, {loopback, b_socket.bound_port()});
  b.SetRoute(tag, {loopback, a_socket.bound_port()});
  CHECK(a.SendLobby({1000, 1000, {0, 2, 'o', 'k'}}));
  const uint8_t game[] = {'G', 'A', 'M', 'E'};
  CHECK(a.Send({loopback, b_socket.bound_port()}, game, sizeof game));
  np::LobbyDatagram got;
  bool lobby_arrived = false;
  for (int i = 0; i < 200 && !lobby_arrived; ++i) lobby_arrived = b.TakeLobby(got);
  CHECK(lobby_arrived && got.data == (std::vector<uint8_t>{0, 2, 'o', 'k'}) && got.to_port == 1000);
  uint8_t buffer[64];
  np::Endpoint from;
  size_t size = 0;
  for (int i = 0; i < 200 && !size; ++i) size = b.Receive(buffer, sizeof buffer, from);
  CHECK(size == sizeof game && std::memcmp(buffer, game, sizeof game) == 0 && from.port == a_socket.bound_port());
  CHECK(b.SendLobby({1000, 1000, {5}}));
  bool reply = false;
  for (int i = 0; i < 200 && !reply; ++i) reply = a.TakeLobby(got);
  CHECK(reply && got.data == std::vector<uint8_t>{5});
  CHECK(a.lobby_sent() == 1 && a.lobby_received() == 1 && b.lobby_received() == 1);

  np::UdpTransport c_socket;
  CHECK(c_socket.Open(0, true));
  np::LobbyMux c(c_socket);
  c.SetRoute(SessionTag("other"), {loopback, b_socket.bound_port()});
  CHECK(c.SendLobby({1000, 1000, {6}}));
  bool crossed = false;
  for (int i = 0; i < 100; ++i) crossed = crossed || b.TakeLobby(got);
  CHECK(!crossed && b.lobby_rejected() >= 1);

  b.SetRoute(0, {});
  CHECK(!b.SendLobby({1000, 1000, {7}}));
}

np::LobbyDatagram Datagram(uint8_t first, size_t size = 9) {
  np::LobbyDatagram datagram;
  datagram.from_port = 1000;
  datagram.to_port = 1000;
  datagram.data.assign(size, first);
  return datagram;
}

// The game's first lobby datagrams are sent before the transport is selected; they wait for it instead of being lost.
void TestLobbyHoldKeepsEarlyDatagramsForTheirMatchOnly() {
  np::LobbyHold hold;
  CHECK(!hold.Hold(0, Datagram(1)));
  CHECK(!hold.Hold(7, Datagram(1, np::kMaxLobbyPayload + 1)));
  CHECK(hold.Hold(7, Datagram(1)) && hold.Hold(7, Datagram(2)) && hold.size() == 2);
  const std::vector<np::LobbyDatagram> taken = hold.Take(7);
  CHECK(taken.size() == 2 && taken[0].data[0] == 1 && taken[1].data[0] == 2 && hold.size() == 0);
  CHECK(hold.Take(7).empty());

  // Another match's datagrams are never delivered to this one.
  CHECK(hold.Hold(7, Datagram(3)));
  CHECK(hold.Hold(8, Datagram(4)) && hold.size() == 1);
  CHECK(hold.Take(7).empty());
  CHECK(hold.Hold(9, Datagram(5)));
  CHECK(hold.Take(8).empty() && hold.size() == 0);

  // Bounded: a stalled transport cannot grow it.
  for (size_t i = 0; i < np::kMaxHeldLobby; ++i) CHECK(hold.Hold(9, Datagram(uint8_t(i))));
  CHECK(!hold.Hold(9, Datagram(0)) && hold.size() == np::kMaxHeldLobby);
  hold.Clear();
  CHECK(hold.size() == 0 && hold.Take(9).empty());

  // Flushed through a mux, they reach the peer as the game sent them.
  np::UdpTransport sender, receiver;
  CHECK(sender.Open(58161, true) && receiver.Open(58162, true));
  np::LobbyMux mux(sender);
  mux.SetRoute(9, np::Endpoint{0x7F000001, 58162});
  CHECK(hold.Hold(9, Datagram(0x42)));
  for (const np::LobbyDatagram& held : hold.Take(9)) CHECK(mux.SendLobby(held));
  uint8_t buffer[64];
  np::Endpoint from;
  size_t got = 0;
  for (int i = 0; i < 50 && got == 0; ++i) {
    got = receiver.Receive(buffer, sizeof buffer, from);
    if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  const auto decoded = np::DecodeLobby(buffer, got);
  CHECK(decoded && decoded->first == 9 && decoded->second.data == Datagram(0x42).data);
}

void TestPortsAndEndpointsFollowTheAssignment() {
  CHECK(kNetplayHostPort == 7001 && kNetplayJoinPort == 7002);
  online::PeerInfo peer;
  peer.ip = "203.0.113.9";
  peer.port = 7001;
  CHECK(online::ChoosePeerAddress({"192.168.1.5"}, peer) == "203.0.113.9");
  peer.same_network = true;
  peer.lan = {"127.0.0.1", "192.168.1.9"};
  CHECK(online::ChoosePeerAddress({"192.168.1.5"}, peer) == "192.168.1.9");
  CHECK(online::ChoosePeerAddress({"192.168.1.9"}, peer) == "127.0.0.1");
}

void TestHooksCoverBothImagesAndOnlyPlayerMatch() {
  const std::string hook = ReadSource("player_match_net_hook.cpp");
  for (const char* name : {"base013, 839C1D48", "base013, 839C1DA8", "base013, 839C1EE8", "base013, 839C1FB8",
                           "base013, 839C1F80", "tu023, 839C24A8", "tu023, 839C2508", "tu023, 839C2648",
                           "tu023, 839C2718", "tu023, 839C26E0"}) {
    CHECK(Has(hook, std::string("SSF2T_NET_HOOK(") + name));
  }
  CHECK(Has(hook, "protocol != ssf2t::kVdpProtocol || !LiveNow()"));
  CHECK(!Has(hook, "token"));
  const std::string netplay = ReadSource("netplay_hook.cpp");
  CHECK(Has(netplay, "return session_ && online_driven_ && base[G::kGameMode] == kOnlineGameMode;"));
  CHECK(Has(netplay, "REX_HOOK_RAW(base013_sub_8241DEC8)") && Has(netplay, "REX_HOOK_RAW(tu023_sub_8241E618)"));
  CHECK(Has(netplay, "(!online_driven_ || base[G::kGameMode] == kOnlineGameMode)"));
  const std::string menu = ReadSource("player_match_hook.cpp");
  CHECK(Has(menu, "ssf2t::OnlineGate::kServiceUnavailable"));
  CHECK(Base013PlayerMatch::kNetSyncStart == 0x8241DEC8 && TitleUpdate023PlayerMatch::kNetSyncStart == 0x8241E618);
  CHECK(Base013PlayerMatch::kQosLookup != TitleUpdate023PlayerMatch::kQosLookup);
}

}  // namespace

int main() {
  TestActiveMatchCreatesARoute();
  TestXnAddrMapsBothWays();
  TestQosReturnsTheAdvertisedPeer();
  TestShimRoutesOnlyToThePeer();
  TestShimForgetsAStaleSession();
  TestLobbyDatagramFormat();
  TestMuxCarriesLobbyAndGameplayOverLoopback();
  TestLobbyHoldKeepsEarlyDatagramsForTheirMatchOnly();
  TestPortsAndEndpointsFollowTheAssignment();
  TestHooksCoverBothImagesAndOnlyPlayerMatch();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
