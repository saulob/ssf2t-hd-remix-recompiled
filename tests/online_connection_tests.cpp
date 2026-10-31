// The launcher's Connection test, on Dragon's model: what each line and verdict says for every combination of facts, the
// STUN client against a local fake server, and the whole test against fake services that fail in each way. Headless:
// loopback sockets only; UPnP is not probed here (it needs a router).
// Run: ssf2t_online_connection_tests

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "online/connection_test.h"
#include "online/net_addr.h"
#include "online/protocol.h"
#include "online/stun.h"
#include "online/upnp.h"

using namespace ssf2t::online;

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

using Mark = ConnectionReport::Mark;
using Router = ConnectionFacts::Router;

// A STUN answer proves this PC has a UDP endpoint, never that a given peer can reach it.
const std::string kAvailable = "UDP endpoint available. Direct peer connectivity will be verified when joining a match.";
const std::string kAvailableThroughRouter =
    "UDP endpoint available through your router. Direct peer connectivity will be verified when joining a match.";

const ConnectionReport::Line* Find(const ConnectionReport& report, const std::string& label) {
  for (const ConnectionReport::Line& line : report.lines) {
    if (line.label == label) return &line;
  }
  return nullptr;
}

bool Detail(const ConnectionReport& report, const std::string& part) {
  for (const std::string& line : report.details) {
    if (line.find(part) != std::string::npos) return true;
  }
  return false;
}

ConnectionFacts Good() {
  ConnectionFacts facts;
  facts.lobby_ok = true;
  facts.lobby_latency_ms = 42;
  facts.udp_ok = true;
  facts.second_answer = true;
  facts.same_mapping = true;
  return facts;
}

void TestReadyOnlyWhenTheServiceAndDirectConnectionsAreVerified() {
  const ConnectionReport ready = SummarizeConnection(Good());
  CHECK(ready.ready && ready.verdict == "Ready for online play");
  CHECK(Find(ready, "Online service") && Find(ready, "Online service")->mark == Mark::kOk &&
        Find(ready, "Online service")->text == "Connected \xC2\xB7 42 ms");
  CHECK(Find(ready, "Direct connections") && Find(ready, "Direct connections")->text == kAvailable);
  CHECK(ready.latency_ms == 42);

  // Only one STUN server answering proves nothing about this network's NAT type (task 84b7d2f1: this is exactly
  // what read as "Ready for online play" for both real players before this fix, from a single STUN answer alone).
  ConnectionFacts one = Good();
  one.second_answer = false;
  one.same_mapping = false;
  const ConnectionReport untested = SummarizeConnection(one);
  CHECK(Find(untested, "Direct connections")->mark == Mark::kNote &&
        Find(untested, "Direct connections")->text ==
            "UDP endpoint available, but your network's NAT type could not be confirmed (only one STUN server "
            "answered). Direct peer connectivity will be verified when joining a match.");
  CHECK(!untested.ready && untested.verdict == "Probably ready: your network's NAT type could not be fully tested");

  ConnectionFacts own = Good();
  own.second_answer = false;
  own.public_address = true;
  CHECK(Find(SummarizeConnection(own), "Direct connections")->text == kAvailable);
}

void TestServiceProblems() {
  ConnectionFacts down = Good();
  down.lobby_ok = false;
  const ConnectionReport report = SummarizeConnection(down);
  CHECK(!report.ready && report.verdict == "Not ready: cannot reach the online service");
  CHECK(Find(report, "Online service")->mark == Mark::kProblem &&
        Find(report, "Online service")->text == "Cannot reach the online service. Check your internet connection.");

  ConnectionFacts refused = down;
  refused.lobby_refusal = "The online service needs a different version of the game.";
  const ConnectionReport refusal = SummarizeConnection(refused);
  CHECK(!refusal.ready && refusal.verdict == "Not ready: the online service did not accept this game");
  CHECK(Find(refusal, "Online service")->text == refused.lobby_refusal);

  ConnectionFacts identity = Good();
  identity.identity_ok = false;
  const ConnectionReport no_identity = SummarizeConnection(identity);
  CHECK(!no_identity.ready && Find(no_identity, "Online service")->text == "Your online identity could not be created.");
}

void TestDirectConnectionOutcomes() {
  ConnectionFacts off = Good();
  off.stun_off = true;
  off.udp_ok = off.second_answer = off.same_mapping = false;
  const ConnectionReport not_checked = SummarizeConnection(off);
  CHECK(Find(not_checked, "Direct connections")->mark == Mark::kNote && !not_checked.ready);
  CHECK(not_checked.verdict == "Limited: some players may not be able to connect to you");

  ConnectionFacts blocked = Good();
  blocked.udp_ok = blocked.second_answer = blocked.same_mapping = false;
  const ConnectionReport no_udp = SummarizeConnection(blocked);
  CHECK(!no_udp.ready && no_udp.verdict == "Limited: direct connections may be blocked by your network");
  CHECK(Find(no_udp, "Direct connections")->text == "May be blocked by your network or firewall.");

  ConnectionFacts symmetric = Good();
  symmetric.same_mapping = false;
  const ConnectionReport nat = SummarizeConnection(symmetric);
  CHECK(!nat.ready && nat.verdict == "Limited: some players may not be able to connect to you");
  CHECK(Find(nat, "Router configuration")->text == "Automatic setup is not available on your router.");
  CHECK(Detail(nat, "symmetric NAT"));

  symmetric.router = Router::kMapped;
  const ConnectionReport opened = SummarizeConnection(symmetric);
  CHECK(opened.ready && Find(opened, "Direct connections")->text == kAvailableThroughRouter);
  CHECK(!Find(opened, "Router configuration"));

  symmetric.router = Router::kMappedBehindNat;
  const ConnectionReport behind = SummarizeConnection(symmetric);
  CHECK(!behind.ready && Find(behind, "Router configuration")->text.find("Another router is in front of yours") == 0);
}

// The user's network: both STUN servers saw 177.18.65.229:64973 and the router accepted the mapping but reported
// 0.0.0.0. As in Dragon's Crown Pro, the STUN result decides; the double NAT only goes to the log.
void TestDoubleNatWithDirectStunIsReady() {
  ConnectionFacts f = Good();
  f.lobby_latency_ms = 305;
  f.router = ClassifyRouter(net::ParseIp("0.0.0.0"));
  CHECK(f.router == Router::kMappedBehindNat);
  const ConnectionReport r = SummarizeConnection(f);
  CHECK(r.ready && r.verdict == "Ready for online play");
  CHECK(r.lines.size() == 2);
  CHECK(Find(r, "Online service")->mark == Mark::kOk && Find(r, "Online service")->text == "Connected \xC2\xB7 305 ms");
  CHECK(Find(r, "Direct connections")->mark == Mark::kOk && Find(r, "Direct connections")->text == kAvailable);
  CHECK(!Find(r, "Router configuration"));
  CHECK(Detail(r, "double NAT or carrier-grade NAT"));
  for (const ConnectionReport::Line& line : r.lines) {
    for (const char* hidden : {"177.18", "0.0.0.0", "64973", "STUN", "UPnP", "NAT"}) {
      CHECK(line.text.find(hidden) == std::string::npos && line.label.find(hidden) == std::string::npos);
    }
  }
}

// Real test 84b7d2f1: Saulo (UPnP mapped but the router's own address is not public: double NAT/CGNAT) and William
// (no UPnP) both saw "Ready for online play" from one answered STUN query alone, a signal the test now reports as
// untested rather than confirmed. Neither is reported unsupported: direct play (NAT punch) may still work.
void TestSingleStunAnswerIsNotConfirmedReady() {
  ConnectionFacts saulo = Good();
  saulo.second_answer = false;
  saulo.same_mapping = false;
  saulo.router = ClassifyRouter(net::ParseIp("0.0.0.0"));
  const ConnectionReport s = SummarizeConnection(saulo);
  CHECK(!s.ready && s.verdict == "Probably ready: your network's NAT type could not be fully tested");
  CHECK(Find(s, "Direct connections")->mark == Mark::kNote);
  CHECK(!Find(s, "Router configuration"));  // not reported as a router problem: the router isn't what is untested

  ConnectionFacts william = Good();
  william.second_answer = false;
  william.same_mapping = false;
  william.router = Router::kNone;
  const ConnectionReport w = SummarizeConnection(william);
  CHECK(!w.ready && w.verdict == "Probably ready: your network's NAT type could not be fully tested");
  CHECK(Find(w, "Direct connections")->mark == Mark::kNote);
  CHECK(!Find(w, "Router configuration"));
}

void TestRouterClassification() {
  for (const char* router : {"0.0.0.0", "127.0.0.1", "10.0.0.1", "172.16.5.1", "172.31.255.254", "192.168.0.1",
                             "169.254.1.1", "100.64.0.1", "100.127.255.1", "224.0.0.1", "255.255.255.255"}) {
    CHECK(ClassifyRouter(net::ParseIp(router)) == Router::kMappedBehindNat);
    ConnectionFacts f = Good();
    f.router = ClassifyRouter(net::ParseIp(router));
    CHECK(SummarizeConnection(f).ready);
    f.same_mapping = false;
    CHECK(!SummarizeConnection(f).ready);
  }
  CHECK(ClassifyRouter(net::ParseIp("203.0.113.7")) == Router::kMapped);
  CHECK(ClassifyRouter(net::ParseIp("177.18.65.229")) == Router::kMapped);
}

void TestPublicRouterMappingIsReady() {
  ConnectionFacts f = Good();
  f.router = ClassifyRouter(net::ParseIp("177.18.65.229"));
  const ConnectionReport r = SummarizeConnection(f);
  CHECK(r.ready && r.verdict == "Ready for online play");
  CHECK(Find(r, "Direct connections")->text == kAvailable && !Find(r, "Router configuration"));
  CHECK(Detail(r, "router: UPnP mapping made"));

  f.same_mapping = false;
  const ConnectionReport symmetric = SummarizeConnection(f);
  CHECK(symmetric.ready && Find(symmetric, "Direct connections")->text == kAvailableThroughRouter);

  ConnectionFacts without_upnp = Good();
  CHECK(SummarizeConnection(without_upnp).ready);
}

// Dragon's Crown Pro's TestConnectionSummary cases, with the same expected rows, marks and verdicts.
void TestDragonConnectionSummaryCases() {
  const auto labels = [](const ConnectionReport& r) {
    std::vector<std::string> out;
    for (const auto& line : r.lines) out.push_back(line.label);
    return out;
  };
  const auto mark_of = [](const ConnectionReport& r, const std::string& label) {
    const ConnectionReport::Line* line = Find(r, label);
    return line ? line->mark : Mark::kNote;
  };
  ConnectionFacts cone;
  cone.lobby_ok = cone.udp_ok = cone.second_answer = cone.same_mapping = true;
  for (const Router router : {Router::kNone, Router::kMapped, Router::kMappedBehindNat}) {
    ConnectionFacts facts = cone;
    facts.router = router;
    const ConnectionReport ready = SummarizeConnection(facts);
    CHECK(ready.ready && ready.verdict == "Ready for online play");
    CHECK(labels(ready) == (std::vector<std::string>{"Online service", "Direct connections"}));
    CHECK(mark_of(ready, "Online service") == Mark::kOk && mark_of(ready, "Direct connections") == Mark::kOk);
    for (const auto& line : ready.lines) {
      for (const char* hidden : {"stun", "STUN", "UPnP", "NAT", "router", "Router", "ws://", "wss://", "Cloudflare",
                                 "Worker"}) {
        CHECK(line.text.find(hidden) == std::string::npos && line.label.find(hidden) == std::string::npos);
      }
    }
  }
  ConnectionFacts double_nat = cone;
  double_nat.router = Router::kMappedBehindNat;
  CHECK(Detail(SummarizeConnection(double_nat), "double NAT or carrier-grade NAT"));
  CHECK(Detail(SummarizeConnection(cone), "no UPnP mapping"));

  // Diverges from Dragon's Crown Pro here (task 84b7d2f1): one STUN answer alone no longer reads as ready.
  ConnectionFacts one_server = cone;
  one_server.second_answer = one_server.same_mapping = false;
  CHECK(!SummarizeConnection(one_server).ready && Detail(SummarizeConnection(one_server), "only one STUN server"));
  ConnectionFacts public_pc = cone;
  public_pc.same_mapping = false;
  public_pc.public_address = true;
  CHECK(SummarizeConnection(public_pc).ready);

  ConnectionFacts symmetric = cone;
  symmetric.same_mapping = false;
  const ConnectionReport limited = SummarizeConnection(symmetric);
  CHECK(!limited.ready && limited.verdict == "Limited: some players may not be able to connect to you");
  CHECK(mark_of(limited, "Direct connections") == Mark::kProblem &&
        mark_of(limited, "Router configuration") == Mark::kProblem);
  CHECK(Detail(limited, "symmetric NAT"));
  symmetric.router = Router::kMapped;
  const ConnectionReport mapped = SummarizeConnection(symmetric);
  CHECK(mapped.ready && labels(mapped) == (std::vector<std::string>{"Online service", "Direct connections"}));
  symmetric.router = Router::kMappedBehindNat;
  const ConnectionReport behind = SummarizeConnection(symmetric);
  CHECK(!behind.ready);
  CHECK(Find(behind, "Router configuration")->text ==
        "Another router is in front of yours (often your provider's), so it cannot be opened automatically.");

  ConnectionFacts blocked = cone;
  blocked.udp_ok = blocked.second_answer = blocked.same_mapping = false;
  const ConnectionReport no_udp = SummarizeConnection(blocked);
  CHECK(!no_udp.ready && no_udp.verdict == "Limited: direct connections may be blocked by your network");
  CHECK(mark_of(no_udp, "Direct connections") == Mark::kProblem);

  ConnectionFacts offline = cone;
  offline.lobby_ok = false;
  const ConnectionReport unreachable = SummarizeConnection(offline);
  CHECK(!unreachable.ready && unreachable.verdict == "Not ready: cannot reach the online service");
  CHECK(mark_of(unreachable, "Online service") == Mark::kProblem);
  ConnectionFacts no_identity = cone;
  no_identity.identity_ok = false;
  CHECK(!SummarizeConnection(no_identity).ready);

  CHECK(Find(SummarizeConnection(cone), "Online service")->text == "Connected.");
  ConnectionFacts timed = cone;
  timed.lobby_latency_ms = 32;
  const ConnectionReport timed_report = SummarizeConnection(timed);
  CHECK(timed_report.verdict == "Ready for online play");
  CHECK(Find(timed_report, "Online service")->text == "Connected \xC2\xB7 32 ms");
  CHECK(Find(timed_report, "Direct connections")->text == kAvailable);
  for (const auto& line : timed_report.lines) CHECK(line.label.find("Ping") == std::string::npos);
  timed.lobby_ok = false;
  CHECK(Find(SummarizeConnection(timed), "Online service")->text ==
        "Cannot reach the online service. Check your internet connection.");
}

void TestNoFalseReady() {
  for (int bits = 0; bits < 1024; ++bits) {
    ConnectionFacts f;
    f.identity_ok = bits & 1;
    f.lobby_ok = bits & 2;
    f.stun_off = bits & 4;
    f.udp_ok = bits & 8;
    f.public_address = bits & 16;
    f.second_answer = bits & 32;
    f.same_mapping = bits & 64;
    f.router = Router(((bits >> 7) & 3) % 3);
    if (f.stun_off) f.udp_ok = f.public_address = f.second_answer = f.same_mapping = false;
    if (!f.udp_ok) f.public_address = f.second_answer = f.same_mapping = false;
    if (!f.second_answer) f.same_mapping = false;
    const ConnectionReport r = SummarizeConnection(f);
    CHECK(r.ready == (r.verdict == "Ready for online play"));
    if (r.ready) {
      CHECK(f.identity_ok && f.lobby_ok);
      CHECK(f.router == Router::kMapped || (f.udp_ok && (f.public_address || f.same_mapping)));
      CHECK(!Find(r, "Router configuration"));
      // As in Dragon's Crown Pro, a router mapping makes it ready even when STUN got no answer; that row still warns.
      if (f.udp_ok || f.stun_off) {
        for (const ConnectionReport::Line& line : r.lines) CHECK(line.mark != Mark::kProblem);
      }
    }
    if (!f.lobby_ok || !f.identity_ok) CHECK(!r.ready && r.verdict.rfind("Not ready", 0) == 0);
  }
}

// STUN -----------------------------------------------------------------------------------------------------------

bool WaitReadable(net::SocketHandle handle, int ms) {
  fd_set set;
  FD_ZERO(&set);
  FD_SET(handle, &set);
  timeval timeout{ms / 1000, (ms % 1000) * 1000};
  return select(int(handle) + 1, &set, nullptr, nullptr, &timeout) > 0;
}

class FakeStun {
 public:
  explicit FakeStun(int port_shift = 0, bool answer = true) : port_shift_(port_shift), answer_(answer) {
    net::SocketsReady();
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    const sockaddr_in any = net::ToSockaddr({net::ParseIp("127.0.0.1"), 0});
    bind(socket_, reinterpret_cast<const sockaddr*>(&any), sizeof(any));
    sockaddr_in bound{};
#ifdef _WIN32
    int length = sizeof(bound);
#else
    socklen_t length = sizeof(bound);
#endif
    getsockname(socket_, reinterpret_cast<sockaddr*>(&bound), &length);
    port_ = ntohs(bound.sin_port);
    thread_ = std::thread([this] { Serve(); });
  }
  ~FakeStun() {
    stop_ = true;
    thread_.join();
    net::CloseSocket(socket_);
  }
  uint16_t port() const { return port_; }
  std::string address() const { return "127.0.0.1:" + std::to_string(port_); }

 private:
  void Serve() {
    while (!stop_) {
      if (!WaitReadable(socket_, 50)) continue;
      uint8_t request[512];
      sockaddr_in from{};
#ifdef _WIN32
      int length = sizeof(from);
#else
      socklen_t length = sizeof(from);
#endif
      const int n = int(recvfrom(socket_, reinterpret_cast<char*>(request), sizeof(request), 0,
                                 reinterpret_cast<sockaddr*>(&from), &length));
      if (n < 20 || !answer_) continue;
      const net::Endpoint source = net::FromSockaddr(from);
      const uint16_t port = uint16_t(source.port + port_shift_);
      uint8_t reply[32] = {0x01, 0x01, 0x00, 0x0c, 0x21, 0x12, 0xa4, 0x42};
      std::memcpy(reply + 8, request + 8, 12);
      reply[20] = 0x00, reply[21] = 0x20, reply[22] = 0x00, reply[23] = 0x08, reply[24] = 0x00, reply[25] = 0x01;
      reply[26] = uint8_t((port ^ 0x2112) >> 8), reply[27] = uint8_t((port ^ 0x2112) & 0xff);
      const uint32_t ip = source.ip ^ 0x2112a442;
      reply[28] = uint8_t(ip >> 24), reply[29] = uint8_t(ip >> 16), reply[30] = uint8_t(ip >> 8), reply[31] = uint8_t(ip);
      sendto(socket_, reinterpret_cast<const char*>(reply), sizeof(reply), 0, reinterpret_cast<const sockaddr*>(&from), length);
    }
  }

  int port_shift_;
  bool answer_;
  net::SocketHandle socket_ = net::kNoSocket;
  uint16_t port_ = 0;
  std::atomic<bool> stop_{false};
  std::thread thread_;
};

void TestStunMessages() {
  net::stun::TransactionId id{};
  for (size_t i = 0; i < id.size(); ++i) id[i] = uint8_t(i + 1);
  uint8_t request[net::stun::kRequestSize];
  net::stun::MakeRequest(id, request);
  CHECK(request[0] == 0x00 && request[1] == 0x01 && request[2] == 0 && request[3] == 0);
  CHECK(request[4] == 0x21 && request[5] == 0x12 && request[6] == 0xa4 && request[7] == 0x42 && request[8] == 1 && request[19] == 12);

  uint8_t reply[32] = {0x01, 0x01, 0x00, 0x0c, 0x21, 0x12, 0xa4, 0x42};
  std::memcpy(reply + 8, id.data(), 12);
  const uint32_t ip = net::ParseIp("203.0.113.9") ^ 0x2112a442;
  const uint16_t port = uint16_t(40000 ^ 0x2112);
  const uint8_t attribute[12] = {0x00, 0x20, 0x00, 0x08, 0x00, 0x01, uint8_t(port >> 8), uint8_t(port & 0xff),
                                 uint8_t(ip >> 24), uint8_t(ip >> 16), uint8_t(ip >> 8), uint8_t(ip)};
  std::memcpy(reply + 20, attribute, 12);
  net::Endpoint mapped;
  CHECK(net::stun::ParseResponse(reply, sizeof(reply), id, mapped));
  CHECK(net::EndpointText(mapped) == "203.0.113.9:40000");

  net::stun::TransactionId other = id;
  other[0] ^= 0xff;
  net::Endpoint ignored;
  CHECK(!net::stun::ParseResponse(reply, sizeof(reply), other, ignored));
  CHECK(!net::stun::ParseResponse(reply, 24, id, ignored));
  reply[0] = 0x01, reply[1] = 0x11;
  CHECK(!net::stun::ParseResponse(reply, sizeof(reply), id, ignored));
}

void TestStunServerList() {
  CHECK(net::StunServers("off").empty() && net::StunServers("").empty());
  const auto two = net::StunServers("a.example:3478, b.example");
  CHECK(two.size() == 2 && two[0].first == "a.example" && two[0].second == 3478 && two[1].second == 3478);
  CHECK(net::StunServers(net::kDefaultStunServers).size() == 2);
  CHECK(net::StunServers("c.example:19302")[0].second == 19302);
}

void TestStunQueryAgainstALocalServer() {
  FakeStun server;
  net::SocketsReady();
  const net::SocketHandle handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  const sockaddr_in any = net::ToSockaddr({0, 0});
  bind(handle, reinterpret_cast<const sockaddr*>(&any), sizeof(any));
  net::Endpoint mapped;
  CHECK(net::stun::Query(handle, {net::ParseIp("127.0.0.1"), server.port()}, 2000, mapped));
  CHECK(mapped.valid() && mapped.ip == net::ParseIp("127.0.0.1"));
  FakeStun silent(0, false);
  net::Endpoint none;
  CHECK(!net::stun::Query(handle, {net::ParseIp("127.0.0.1"), silent.port()}, 400, none));
  net::CloseSocket(handle);
}

// The whole test ---------------------------------------------------------------------------------------------------

class FakeHttp {
 public:
  explicit FakeHttp(std::string response) : response_(std::move(response)) {
    net::SocketsReady();
    listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    const sockaddr_in any = net::ToSockaddr({net::ParseIp("127.0.0.1"), 0});
    bind(listener_, reinterpret_cast<const sockaddr*>(&any), sizeof(any));
    listen(listener_, 4);
    sockaddr_in bound{};
#ifdef _WIN32
    int length = sizeof(bound);
#else
    socklen_t length = sizeof(bound);
#endif
    getsockname(listener_, reinterpret_cast<sockaddr*>(&bound), &length);
    port_ = ntohs(bound.sin_port);
    thread_ = std::thread([this] { Serve(); });
  }
  ~FakeHttp() {
    stop_ = true;
    thread_.join();
    net::CloseSocket(listener_);
  }
  std::string url() const { return "ws://127.0.0.1:" + std::to_string(port_) + "/"; }

 private:
  void Serve() {
    while (!stop_) {
      if (!WaitReadable(listener_, 50)) continue;
      const net::SocketHandle client = accept(listener_, nullptr, nullptr);
      if (client == net::kNoSocket) continue;
      if (!response_.empty() && WaitReadable(client, 500)) {
        char request[2048];
        recv(client, request, sizeof(request), 0);
        send(client, response_.data(), int(response_.size()), 0);
      } else if (response_.empty()) {
        for (int i = 0; i < 140 && !stop_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
      net::CloseSocket(client);
    }
  }

  std::string response_;
  net::SocketHandle listener_ = net::kNoSocket;
  uint16_t port_ = 0;
  std::atomic<bool> stop_{false};
  std::thread thread_;
};

std::string Http(int status, const std::string& body) {
  return "HTTP/1.1 " + std::to_string(status) + " X\r\nContent-Type: application/json\r\nContent-Length: " +
         std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
}

ConnectionTestRequest RequestFor(const std::string& url) {
  ConnectionTestRequest request;
  request.url = url;
  request.identity.token = std::string(64, 'a');
  request.identity.player_id = "ABCD-2345";
  request.name = "Test";
  request.guest = "base013";
  request.stun = "off";
  request.upnp = false;
  request.game_ports = {};
  return request;
}

void ExpectNotReady(const ConnectionReport& report, const std::string& service_text, const std::string& verdict) {
  CHECK(!report.ready);
  CHECK(report.verdict == verdict);
  const ConnectionReport::Line* line = Find(report, "Online service");
  CHECK(line && line->mark == Mark::kProblem && line->text == service_text);
}

void TestServiceFailures() {
  const std::string unreachable = "Cannot reach the online service. Check your internet connection.";
  const std::string not_ready = "Not ready: cannot reach the online service";

  ExpectNotReady(RunConnectionTest(RequestFor("ws://127.0.0.1:1/")), unreachable, not_ready);

  {
    FakeHttp server(Http(500, "{}"));
    const ConnectionReport report = RunConnectionTest(RequestFor(server.url()));
    ExpectNotReady(report, unreachable, not_ready);
    CHECK(Detail(report, "HTTP 500"));
  }
  {
    FakeHttp server(Http(200, "{\"hello\":1}"));
    ExpectNotReady(RunConnectionTest(RequestFor(server.url())), "The online service answered unexpectedly.",
                   "Not ready: the online service did not accept this game");
  }
  {
    FakeHttp server(Http(200, "{\"service\":\"ssf2t-lobby\",\"protocol\":1,\"status\":\"ok\"}"));
    const ConnectionReport report = RunConnectionTest(RequestFor(server.url()));
    ExpectNotReady(report, "The online service needs a different version of the game.",
                   "Not ready: the online service did not accept this game");
    CHECK(Detail(report, "service protocol 1"));
  }
  {
    FakeHttp server(Http(200, "{\"service\":\"ssf2t-lobby\",\"protocol\":3,\"status\":\"ok\"}"));
    const ConnectionReport report = RunConnectionTest(RequestFor(server.url()));
    ExpectNotReady(report, unreachable, not_ready);
    CHECK(Detail(report, "websocket"));
  }
}

void TestTimeout() {
  FakeHttp silent("");
  const auto started = std::chrono::steady_clock::now();
  const ConnectionReport report = RunConnectionTest(RequestFor(silent.url()));
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count();
  ExpectNotReady(report, "Cannot reach the online service. Check your internet connection.",
                 "Not ready: cannot reach the online service");
  CHECK(seconds < 30);
}

void TestNoIdentityNeverTouchesTheNetwork() {
  ConnectionTestRequest request = RequestFor("ws://127.0.0.1:1/");
  request.identity = {};
  const ConnectionReport report = RunConnectionTest(request);
  ExpectNotReady(report, "Your online identity could not be created.", "Not ready: cannot reach the online service");
  CHECK(Detail(report, "no online identity"));
}

void TestStunResultsReachTheReportEvenWhenTheServiceIsDown() {
  FakeStun first, second, third(7);
  ConnectionTestRequest request = RequestFor("ws://127.0.0.1:1/");
  request.stun = first.address() + "," + second.address();
  ConnectionReport same = RunConnectionTest(request);
  CHECK(Find(same, "Direct connections") && Find(same, "Direct connections")->text == kAvailable);
  CHECK(!same.ready);
  CHECK(Detail(same, "STUN 127.0.0.1: 127.0.0.1:"));

  request.stun = first.address() + "," + third.address();
  const ConnectionReport symmetric = RunConnectionTest(request);
  CHECK(Find(symmetric, "Router configuration") && Detail(symmetric, "symmetric NAT"));
  CHECK(Detail(symmetric, "NAT: keeps the local port"));  // from the first answer, which keeps it
  request.stun = third.address();
  CHECK(Detail(RunConnectionTest(request), "NAT: maps local port"));
  CHECK(Detail(same, "peer: direct UDP is only proven by a match's punch; there is no relay"));

  FakeStun silent(0, false);
  request.stun = silent.address();
  const ConnectionReport blocked = RunConnectionTest(request);
  CHECK(Find(blocked, "Direct connections")->text == "May be blocked by your network or firewall.");
  CHECK(Detail(blocked, "no answer"));

  request.stun = "off";
  CHECK(Find(RunConnectionTest(request), "Direct connections")->mark == Mark::kNote);
  CHECK(Detail(RunConnectionTest(request), "UPnP: turned off"));
}

// Holds a UDP port on every interface, as another program (or the other game instance) would.
class PortHolder {
 public:
  explicit PortHolder(uint16_t port) {
    net::SocketsReady();
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    const sockaddr_in any = net::ToSockaddr({0, port});
    held_ = bind(socket_, reinterpret_cast<const sockaddr*>(&any), sizeof(any)) == 0;
  }
  ~PortHolder() { net::CloseSocket(socket_); }
  bool held() const { return held_; }

 private:
  net::SocketHandle socket_ = net::kNoSocket;
  bool held_ = false;
};

bool PortIsFree(uint16_t port) {
  PortHolder probe(port);
  return probe.held();
}

// The test asks STUN from the port a match would use, so the mapping it reports is that port's, and UPnP maps that
// same port.
void TestStunAndUpnpRunFromTheGamePort() {
  FakeStun first, second;
  ConnectionTestRequest request = RequestFor("ws://127.0.0.1:1/");
  request.stun = first.address() + "," + second.address();
  request.game_ports = {58131, 58132};
  request.upnp = true;
  std::vector<uint16_t> mapped_ports;
  request.probe_upnp = [&mapped_ports](uint16_t port) {
    mapped_ports.push_back(port);
    net::upnp::Status status;
    status.state = net::upnp::State::kFailed;
    status.detail = "no router (test)";
    return status;
  };
  const ConnectionReport report = RunConnectionTest(request);
  CHECK(Detail(report, "UDP socket bound local=0.0.0.0:58131"));
  CHECK(!Detail(report, "was in use"));
  CHECK(Detail(report, "NAT: keeps the local port"));
  CHECK(Detail(report, "STUN 127.0.0.1: 127.0.0.1:58131"));
  CHECK(mapped_ports == std::vector<uint16_t>{58131});
  CHECK(Detail(report, "UPnP port 58131: not available: no router (test)"));
  CHECK(Find(report, "Direct connections")->text ==
        "UDP endpoint available on UDP port 58131. Direct peer connectivity will be verified when joining a match.");
  // Its socket is closed when the report is out: the game can bind the port next.
  CHECK(PortIsFree(58131));
}

// A NAT that gives the port another public one is reported as it is, never as the local port.
void TestMappedPortIsReportedExactly() {
  FakeStun shifted(7);
  ConnectionTestRequest request = RequestFor("ws://127.0.0.1:1/");
  request.stun = shifted.address();
  request.game_ports = {58131, 58132};
  const ConnectionReport report = RunConnectionTest(request);
  CHECK(Detail(report, "STUN 127.0.0.1: 127.0.0.1:58138"));
  CHECK(Detail(report, "NAT: maps local port 58131 to public port 58138"));
}

// Another instance of the game on this PC holds the first port: the test takes the second, as that game would.
void TestSecondInstanceTestsTheSecondPort() {
  PortHolder other_instance(58131);
  CHECK(other_instance.held());
  FakeStun first;
  ConnectionTestRequest request = RequestFor("ws://127.0.0.1:1/");
  request.stun = first.address();
  request.game_ports = {58131, 58132};
  const ConnectionReport report = RunConnectionTest(request);
  CHECK(Detail(report, "UDP socket bound local=0.0.0.0:58132 (port 58131 was in use)"));
  CHECK(Detail(report, "STUN 127.0.0.1: 127.0.0.1:58132"));
  CHECK(Find(report, "Direct connections")->text.find("on UDP port 58132") != std::string::npos);
}

// Neither multiplayer port can be opened: said plainly, never tested from some other port instead.
void TestBindFailureIsReportedNotWorkedAround() {
  PortHolder a(58131), b(58132);
  CHECK(a.held() && b.held());
  FakeStun first;
  ConnectionTestRequest request = RequestFor("ws://127.0.0.1:1/");
  request.stun = first.address();
  request.game_ports = {58131, 58132};
  request.upnp = true;
  bool upnp_probed = false;
  request.probe_upnp = [&upnp_probed](uint16_t) {
    upnp_probed = true;
    return net::upnp::Status{};
  };
  const ConnectionReport report = RunConnectionTest(request);
  const ConnectionReport::Line* direct = Find(report, "Direct connections");
  CHECK(direct && direct->mark == Mark::kProblem &&
        direct->text == "Cannot open UDP port 58131 or 58132. Another program may already be using it, or network "
                        "security may be blocking it.");
  CHECK(!report.ready);
  CHECK(Detail(report, "cannot open UDP port 58131 or 58132 (error "));
  CHECK(!Detail(report, "UDP socket bound") && !Detail(report, "STUN 127.0.0.1:"));
  CHECK(!upnp_probed && Detail(report, "UPnP: not tried, no UDP port is open"));

  ConnectionFacts facts = Good();
  facts.udp_ok = facts.second_answer = facts.same_mapping = false;
  facts.closed_ports = "7001 or 7002";
  const ConnectionReport summary = SummarizeConnection(facts);
  CHECK(!summary.ready && summary.verdict == "Limited: the multiplayer UDP port could not be opened");
  CHECK(Find(summary, "Direct connections")->text.rfind("Cannot open UDP port 7001 or 7002.", 0) == 0);
}

void TestAgainstTheRealService(const std::string& url) {
  FakeStun first, second;
  ConnectionTestRequest request = RequestFor(url);
  request.identity.token = std::string(64, '5');
  request.identity.player_id = DerivePlayerId(request.identity.token);
  request.stun = first.address() + "," + second.address();

  const ConnectionReport ready = RunConnectionTest(request);
  CHECK(ready.ready && ready.verdict == "Ready for online play");
  CHECK(ready.latency_ms >= 0 && Find(ready, "Online service")->text.rfind("Connected \xC2\xB7 ", 0) == 0);
  CHECK(Detail(ready, "health ok; sign-in ok"));

  request.stun = "off";
  const ConnectionReport limited = RunConnectionTest(request);
  CHECK(!limited.ready && limited.verdict == "Limited: some players may not be able to connect to you");
  CHECK(Find(limited, "Online service")->mark == Mark::kOk);

  request.guest = "not-a-game-image";
  const ConnectionReport refused = RunConnectionTest(request);
  CHECK(!refused.ready && refused.verdict == "Not ready: the online service did not accept this game");
  CHECK(Find(refused, "Online service")->text == DescribeServiceError("bad_guest"));
  CHECK(Detail(refused, "sign-in: code bad_guest"));
}

// Direct blocked or uncertain, but the service relays: still ready for online play, and the problem lines become notes.
void TestRelayMakesOnlinePlayReady() {
  ConnectionFacts direct = Good();
  direct.relay_ok = true;
  const ConnectionReport both = SummarizeConnection(direct);
  CHECK(both.ready && both.verdict == "Ready for online play");
  CHECK(Find(both, "Direct connections")->text == kAvailable && Find(both, "Direct connections")->mark == Mark::kOk);
  CHECK(Find(both, "Relay") && Find(both, "Relay")->mark == Mark::kOk);
  CHECK(Detail(both, "relay: available"));

  ConnectionFacts blocked;
  blocked.lobby_ok = true;
  blocked.relay_ok = true;
  const ConnectionReport relayed = SummarizeConnection(blocked);
  CHECK(relayed.ready && relayed.verdict == "Ready for online play");
  CHECK(Find(relayed, "Direct connections")->mark == Mark::kNote);
  CHECK(Find(relayed, "Relay")->text ==
        "Available. A match that cannot connect directly connects through the online service.");
  for (const auto& line : relayed.lines) {
    for (const char* hidden : {"Worker", "Cloudflare", "WebSocket", "ws://", "wss://", "STUN"}) {
      CHECK(line.text.find(hidden) == std::string::npos);
    }
  }

  ConnectionFacts symmetric = Good();
  symmetric.same_mapping = false;
  symmetric.relay_ok = true;
  const ConnectionReport strict = SummarizeConnection(symmetric);
  CHECK(strict.ready && Find(strict, "Router configuration") && Find(strict, "Router configuration")->mark == Mark::kNote);
  CHECK(Detail(strict, "peer: direct UDP is only proven by a match's punch; without it the match is relayed"));

  ConnectionFacts one = Good();
  one.second_answer = one.same_mapping = false;
  one.relay_ok = true;
  CHECK(SummarizeConnection(one).ready && SummarizeConnection(one).verdict == "Ready for online play");

  // A relayed match still plays on the game's UDP socket, and the relay is the service's: neither can be missing.
  ConnectionFacts closed = Good();
  closed.relay_ok = true;
  closed.closed_ports = "7001 or 7002";
  const ConnectionReport no_port = SummarizeConnection(closed);
  CHECK(!no_port.ready && no_port.verdict == "Limited: the multiplayer UDP port could not be opened");
  CHECK(!Find(no_port, "Relay") && Detail(no_port, "relay: needs the UDP port"));
  ConnectionFacts down = blocked;
  down.lobby_ok = false;
  CHECK(!SummarizeConnection(down).ready && !Find(SummarizeConnection(down), "Relay"));

  ConnectionFacts older = blocked;
  older.relay_ok = false;
  CHECK(!SummarizeConnection(older).ready && Detail(SummarizeConnection(older), "relay: not offered by the service"));
}

}  // namespace

int main() {
  std::printf("online connection tests\n");
  TestRelayMakesOnlinePlayReady();
  TestReadyOnlyWhenTheServiceAndDirectConnectionsAreVerified();
  TestServiceProblems();
  TestDirectConnectionOutcomes();
  TestNoFalseReady();
  TestDoubleNatWithDirectStunIsReady();
  TestSingleStunAnswerIsNotConfirmedReady();
  TestRouterClassification();
  TestPublicRouterMappingIsReady();
  TestDragonConnectionSummaryCases();
  TestStunMessages();
  TestStunServerList();
  TestStunQueryAgainstALocalServer();
  TestServiceFailures();
  TestNoIdentityNeverTouchesTheNetwork();
  TestStunResultsReachTheReportEvenWhenTheServiceIsDown();
  TestStunAndUpnpRunFromTheGamePort();
  TestMappedPortIsReportedExactly();
  TestSecondInstanceTestsTheSecondPort();
  TestBindFailureIsReportedNotWorkedAround();
  TestTimeout();
  if (const char* url = std::getenv("SSF2T_LOBBY_TEST_URL"); url && *url) TestAgainstTheRealService(url);
  else std::printf("  real service checks skipped: SSF2T_LOBBY_TEST_URL is not set\n");
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
