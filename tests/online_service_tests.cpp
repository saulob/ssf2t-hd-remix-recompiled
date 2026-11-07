// Online service state machine over a scripted channel: Online off by default, sign-in, friends, Player Match
// advertisements, search and join, the private match and the hand-off to the fight session, reconnects and the
// connection test.
// Headless: no sockets, no threads, no window.
// Run: ssf2t_online_service_tests

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "netplay/relay_frame.h"
#include "online/friend_view.h"
#include "online/online_service.h"
#include "online/secret_view.h"

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

constexpr char kToken[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr char kFriendA[] = "ABCD-2345";
constexpr char kFriendB[] = "EFGH-6789";
constexpr char kSession[] = "00112233445566aa";

bool Contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

struct ChannelState {
  std::string url;
  std::deque<ChannelEvent> in;
  std::vector<std::string> sent;
  std::vector<std::vector<uint8_t>> binary;
  bool closed = false;
};

class FakeChannel final : public LobbyChannel {
 public:
  explicit FakeChannel(std::shared_ptr<ChannelState> state) : state_(std::move(state)) {}
  bool Poll(ChannelEvent& out) override {
    if (state_->in.empty()) return false;
    out = std::move(state_->in.front());
    state_->in.pop_front();
    return true;
  }
  void Send(const std::string& text) override { state_->sent.push_back(text); }
  bool SendBinary(std::vector<uint8_t>&& bytes) override {
    if (state_->closed) return false;
    state_->binary.push_back(std::move(bytes));
    return true;
  }
  void Close() override { state_->closed = true; }

 private:
  std::shared_ptr<ChannelState> state_;
};

int SeqOf(const std::string& request) {
  const size_t at = request.find("\"seq\":");
  return at == std::string::npos ? 0 : std::atoi(request.c_str() + at + 6);
}

std::string FriendJson(const char* player, const char* name, bool online, const char* state = "online",
                       const char* guest = "base013", bool compatible = true) {
  return std::string("{\"player\":\"") + player + "\",\"name\":\"" + name + "\",\"online\":" + (online ? "true" : "false") +
         ",\"state\":\"" + (online ? state : "offline") + "\",\"platform\":\"Windows\",\"version\":\"0.2.2\",\"guest\":\"" +
         guest + "\",\"compatible\":" + (compatible ? "true" : "false") + "}";
}

std::string MatchBody(const char* match, const char* role, const char* guest, const char* peer, const char* name) {
  return std::string("\"match\":\"") + match + "\",\"role\":\"" + role + "\",\"guest\":\"" + guest +
         "\",\"build\":1,\"peer\":{\"player\":\"" + peer + "\",\"name\":\"" + name + "\"}";
}

// The match's gameplay socket on loopback, the STUN server that tells it its mapped endpoint (an Internet-style one,
// with another port than the local one, as a NAT gives), and the peer's own socket.
struct UdpRig {
  ssf2t::netplay::OnlineGameSocket socket;
  ssf2t::netplay::UdpTransport stun, peer;
  ssf2t::netplay::Endpoint mapped{0xB371953F, 54114};  // 179.113.149.63:54114
  ssf2t::netplay::Endpoint stun_saw;                    // where the last STUN request came from
  bool answer = true;
  int lookups = 0;

  UdpRig() {
    socket.Open({58141, 58142}, true);
    stun.Open(58149, true);
    peer.Open(58148, true);
  }

  ssf2t::netplay::Endpoint Game() const { return {0x7F000001, socket.bound_port()}; }

  // Answers whatever STUN request is waiting with `mapped`, as XOR-MAPPED-ADDRESS.
  void Serve() {
    uint8_t request[64];
    ssf2t::netplay::Endpoint from;
    while (const size_t size = stun.Receive(request, sizeof request, from)) {
      if (size != 20) continue;
      stun_saw = from;
      if (!answer) continue;
      uint8_t reply[32] = {0x01, 0x01, 0x00, 0x0c};
      std::memcpy(reply + 4, request + 4, 16);
      const uint16_t port = mapped.port ^ 0x2112;
      const uint32_t ip = mapped.address ^ 0x2112a442u;
      const uint8_t attribute[12] = {0x00, 0x20, 0x00, 0x08, 0x00, 0x01, uint8_t(port >> 8), uint8_t(port),
                                     uint8_t(ip >> 24), uint8_t(ip >> 16), uint8_t(ip >> 8), uint8_t(ip)};
      std::memcpy(reply + 20, attribute, sizeof attribute);
      stun.Send(from, reply, sizeof reply);
    }
  }

  std::vector<PunchPacket> PeerHeard() {
    std::vector<PunchPacket> heard;
    uint8_t data[64];
    ssf2t::netplay::Endpoint from;
    while (const size_t size = peer.Receive(data, sizeof data, from)) {
      if (const auto packet = DecodePunch(data, size); packet && from == Game()) heard.push_back(*packet);
    }
    return heard;
  }

  void PeerSends(PunchKind kind, uint8_t side, uint64_t key) {
    const std::vector<uint8_t> bytes = EncodePunch({kind, side, key});
    peer.Send(Game(), bytes.data(), bytes.size());
  }
};

struct Rig {
  std::vector<std::shared_ptr<ChannelState>> channels;
  std::vector<std::string> logs;
  int test_calls = 0;
  bool test_throws = false;
  ConnectionReport test_result;
  ConnectionTestRequest test_request;
  std::unique_ptr<OnlineService> service;
  std::string me;
  double now = 100;
  SecretCapabilities secret_caps;

  // Without `udp` the service runs signaling only; `socket` stands in for udp's own gameplay socket.
  explicit Rig(const std::string& guest = kGuestBase013, bool ephemeral = false, const std::string& token = kToken,
               UdpRig* udp = nullptr, ssf2t::netplay::OnlineGameSocket* socket = nullptr) {
    ServiceConfig config;
    config.url = "wss://example.test/";
    config.guest = guest;
    config.version = "0.2.2";
    config.platform = "Windows";
    config.synchronous_probes = true;
    config.lan = {"127.0.0.1", "192.168.1.5"};
    config.identity = [ephemeral, token] {
      Identity id;
      id.token = token;
      id.player_id = DerivePlayerId(token);
      id.ephemeral = ephemeral;
      return id;
    };
    config.log = [this](const std::string& line) { logs.push_back(line); };
    config.secret = [this] { return secret_caps; };
    config.open_channel = [this](const std::string& url) {
      auto state = std::make_shared<ChannelState>();
      state->url = url;
      channels.push_back(state);
      return std::unique_ptr<LobbyChannel>(new FakeChannel(state));
    };
    config.test = [this](const ConnectionTestRequest& request) {
      ++test_calls;
      test_request = request;
      if (test_throws) throw std::runtime_error("test broke");
      return test_result;
    };
    if (udp) {
      config.game_socket = socket ? socket : &udp->socket;
      config.resolve_stun = [udp](const std::string&) {
        ++udp->lookups;
        return std::vector<ssf2t::netplay::Endpoint>{{0x7F000001, 58149}};
      };
    }
    service = std::make_unique<OnlineService>(config);
    me = DerivePlayerId(token);
  }

  int CountLogs(const std::string& part) const {
    int n = 0;
    for (const std::string& line : logs) n += Contains(line, part) ? 1 : 0;
    return n;
  }

  ChannelState& Last() { return *channels.back(); }
  void Tick(double seconds = 0.1) {
    now += seconds;
    service->Update(now);
  }
  void Feed(const std::string& text) {
    Last().in.push_back({ChannelEvent::Kind::kText, text});
    Tick(0.01);
  }
  void Event(ChannelEvent::Kind kind, const std::string& why = {}) {
    Last().in.push_back({kind, why});
    Tick(0.01);
  }
  int Count(const std::string& op) {
    int n = 0;
    for (const std::string& s : Last().sent) n += Contains(s, "\"op\":\"" + op + "\"") ? 1 : 0;
    return n;
  }
  std::string LastRequest(const std::string& op) {
    for (auto it = Last().sent.rbegin(); it != Last().sent.rend(); ++it) {
      if (Contains(*it, "\"op\":\"" + op + "\"")) return *it;
    }
    return {};
  }
  void Reply(const std::string& op, const std::string& body = {}, const char* code = "ok") {
    const std::string request = LastRequest(op);
    CHECK(!request.empty());
    Feed("{\"re\":" + std::to_string(SeqOf(request)) + ",\"code\":\"" + code + "\"" + (body.empty() ? "" : "," + body) + "}");
  }

  void Enable(const char* name = "Saulo") { service->Apply({true, name}); }

  void SignIn(const char* name = "Saulo") {
    Enable(name);
    Tick();
    Event(ChannelEvent::Kind::kOpen);
    Reply("hello", "\"player\":\"" + me + "\",\"name\":\"" + name + "\",\"friend_code\":\"" + me + "\"");
  }

  void SignInWithFriends() {
    SignIn();
    Reply("friends.list", "\"friends\":[" + FriendJson(kFriendA, "SauloB", true) + "," + FriendJson(kFriendB, "Zed", false) + "]");
    Reply("friends.requests", "\"incoming\":[],\"outgoing\":[]");
  }

  void StartPlayerMatchAsHost() {
    SignInWithFriends();
    service->HostPlayerMatch({{"rounds", 2}});
    Tick();
    Reply("pm.host", std::string("\"session\":\"") + kSession + "\"");
    Feed(std::string("{\"ev\":\"match.created\",\"session\":\"") + kSession + "\"," +
         MatchBody("m1", "host", "base013", kFriendA, "SauloB") + "}");
  }

  std::string SearchAnswer(const char* guest = "base013") {
    return std::string("\"sessions\":[{\"session\":\"") + kSession + "\",\"player\":\"" + kFriendA +
           "\",\"name\":\"SauloB\",\"guest\":\"" + guest +
           "\",\"build\":1,\"slots\":2,\"rules\":{\"rounds\":2},\"platform\":\"Windows\",\"version\":\"0.2.2\"}]";
  }
};

void TestOnlineOffHasNoNetworkBehaviour() {
  Rig rig;
  for (int i = 0; i < 50; ++i) rig.Tick(1.0);
  CHECK(rig.channels.empty());
  rig.service->AddFriend(kFriendA);
  rig.service->HostPlayerMatch({});
  rig.service->SetPlayerMatchSearch(true);
  CHECK(!rig.service->JoinPlayerMatch(kSession));
  rig.service->TestConnection();
  rig.service->SetNetplayLink(NetplayLink::kNone, std::nullopt);
  rig.Tick();
  CHECK(rig.channels.empty());
  CHECK(rig.test_calls == 0);
  CHECK(!rig.service->Assignment());
  CHECK(rig.service->snapshot().connection == Connection::kOff);

  rig.Enable();
  rig.Tick();
  CHECK(rig.channels.size() == 1);
  rig.service->Apply({false, "Saulo"});
  const size_t opened = rig.channels.size();
  for (int i = 0; i < 20; ++i) rig.Tick(5.0);
  CHECK(rig.channels.size() == opened);
  CHECK(rig.service->snapshot().connection == Connection::kOff);
}

void TestSignInSendsPresenceDetailsAndNeverTheTokenInLogs() {
  Rig rig;
  rig.SignIn();
  const std::string hello = rig.LastRequest("hello");
  CHECK(Contains(hello, "\"presence\":true") && Contains(hello, "\"platform\":\"Windows\"") &&
        Contains(hello, "\"version\":\"0.2.2\"") && Contains(hello, "\"guest\":\"base013\""));
  CHECK(Contains(hello, kToken));
  for (const std::string& line : rig.logs) CHECK(!Contains(line, kToken));
  const Snapshot s = rig.service->snapshot();
  CHECK(s.connection == Connection::kOnline);
  CHECK(s.friend_code == rig.me);
  CHECK(rig.Count("friends.list") == 1 && rig.Count("friends.requests") == 1);
  CHECK(rig.channels[0]->url == "wss://example.test/");
}

void TestFriendsAndPresenceAreShown() {
  Rig rig;
  rig.SignInWithFriends();
  const Snapshot s = rig.service->snapshot();
  CHECK(s.friends.size() == 2 && s.fetched);
  CHECK(FriendStatusLine(s.friends[0]) == "Online \xC2\xB7 Windows \xC2\xB7 v0.2.2");
  CHECK(FriendStatusLine(s.friends[1]) == "Offline");
  CHECK(DescribeFriend(s, s.friends[0]).status == "Online \xC2\xB7 Windows \xC2\xB7 v0.2.2");
  CHECK(!DescribeFriend(s, s.friends[0]).incompatible);

  rig.Feed("{\"ev\":\"friend.status\",\"friend\":" + FriendJson(kFriendB, "Zed", true, "in_game") + "}");
  const Snapshot after = rig.service->snapshot();
  CHECK(after.friends[1].online && FriendStatusLine(after.friends[1]) == "In game \xC2\xB7 Windows \xC2\xB7 v0.2.2");
  rig.Feed("{\"ev\":\"friend.status\",\"friend\":" + FriendJson(kFriendA, "SauloB", true, "player_match") + "}");
  const Snapshot hosting = rig.service->snapshot();
  CHECK(FriendStatusLine(hosting.friends[0]) == "Online Match \xC2\xB7 Windows \xC2\xB7 v0.2.2");
  rig.Feed(std::string("{\"ev\":\"friend.offline\",\"player\":\"") + kFriendB + "\"}");
  CHECK(!rig.service->snapshot().friends[1].online);
}

void TestAddFriendValidatesBeforeSending() {
  Rig rig;
  rig.SignInWithFriends();
  CHECK(rig.service->AddFriend("nonsense") == FriendRequestResult::kInvalid);
  CHECK(Contains(rig.service->snapshot().notice, "not a valid friend code"));
  CHECK(rig.service->AddFriend("ABCD") == FriendRequestResult::kInvalid);
  CHECK(rig.service->AddFriend("ABCD-234") == FriendRequestResult::kInvalid);
  CHECK(rig.service->AddFriend("ABCD-1234") == FriendRequestResult::kInvalid);
  CHECK(rig.service->AddFriend(rig.me) == FriendRequestResult::kOwnCode);
  CHECK(rig.service->snapshot().notice == "That is your own friend code.");
  CHECK(rig.service->AddFriend(kFriendA) == FriendRequestResult::kAlreadyFriends);
  CHECK(rig.service->snapshot().notice == "Already friends.");
  CHECK(rig.Count("friends.add") == 0);
  CHECK(!rig.service->TakeNotices().empty());

  CHECK(rig.service->AddFriend("jklm 3456") == FriendRequestResult::kSent);
  CHECK(rig.Count("friends.add") == 1);
  CHECK(Contains(rig.LastRequest("friends.add"), "\"code\":\"JKLM-3456\""));
  rig.Reply("friends.add", "\"player\":\"JKLM-3456\",\"name\":\"Kim\",\"state\":\"pending\"");
  CHECK(rig.service->snapshot().notice == "Friend request sent.");

  CHECK(rig.service->AddFriend("NPQR-7892") == FriendRequestResult::kSent);
  rig.Reply("friends.add", "", "not_found");
  CHECK(Contains(rig.service->snapshot().notice, "No player has that friend code"));
}

void TestFriendRequestReachesTheServiceAndIsLogged() {
  Rig rig;
  rig.SignInWithFriends();
  CHECK(rig.service->AddFriend(" jklm-3456\r\n") == FriendRequestResult::kSent);
  CHECK(rig.CountLogs("online friend request sent code=JKLM-3456") == 1);
  CHECK(rig.Count("friends.add") == 1);
  rig.Reply("friends.add", "\"player\":\"JKLM-3456\",\"name\":\"Kim\",\"state\":\"pending\"");
  CHECK(rig.CountLogs("friend request accepted by service code=JKLM-3456 state=pending") == 1);

  CHECK(rig.service->AddFriend("NPQR-7892") == FriendRequestResult::kSent);
  rig.Reply("friends.add", "", "not_found");
  CHECK(rig.CountLogs("online friend request failed code=NPQR-7892 reason=not_found") == 1);

  CHECK(rig.service->AddFriend("nonsense") == FriendRequestResult::kInvalid);
  CHECK(rig.CountLogs("online friend request failed code=- reason=invalid") == 1);
  CHECK(rig.service->AddFriend(rig.me) == FriendRequestResult::kOwnCode);
  CHECK(rig.CountLogs("failed code=" + rig.me + " reason=own_code") == 1);
  for (const std::string& line : rig.logs) CHECK(!Contains(line, kToken));
}

void TestPendingAndReverseRequests() {
  Rig rig;
  rig.SignInWithFriends();
  CHECK(rig.service->AddFriend("JKLM-3456") == FriendRequestResult::kSent);
  rig.Reply("friends.add", "\"player\":\"JKLM-3456\",\"name\":\"Kim\",\"state\":\"pending\"");
  rig.Reply("friends.requests", "\"incoming\":[],\"outgoing\":[{\"player\":\"JKLM-3456\",\"name\":\"Kim\"}]");
  CHECK(rig.service->AddFriend("JKLM-3456") == FriendRequestResult::kAlreadyPending);
  CHECK(rig.service->snapshot().notice == "Friend request already pending.");
  CHECK(rig.Count("friends.add") == 1);
  CHECK(rig.CountLogs("failed code=JKLM-3456 reason=already_pending") == 1);

  rig.Feed("{\"ev\":\"friend.request\",\"player\":\"NPQR-7892\",\"name\":\"Zoe\"}");
  rig.Reply("friends.requests", "\"incoming\":[{\"player\":\"NPQR-7892\",\"name\":\"Zoe\"}],\"outgoing\":[]");
  CHECK(rig.service->AddFriend("NPQR-7892") == FriendRequestResult::kSent);
  rig.Reply("friends.add", "\"player\":\"NPQR-7892\",\"name\":\"Zoe\",\"state\":\"friends\"");
  CHECK(rig.service->snapshot().notice == "You are now friends.");
}

void TestFriendRequestNeedsAConnection() {
  Rig rig;
  CHECK(rig.service->AddFriend("JKLM-3456") == FriendRequestResult::kNotConnected);
  CHECK(rig.channels.empty());
  rig.Enable();
  rig.Tick();
  CHECK(rig.service->AddFriend("JKLM-3456") == FriendRequestResult::kNotConnected);
  CHECK(rig.Count("friends.add") == 0);
  CHECK(rig.service->snapshot().notice == "Not connected to the online service.");
}

void TestCopiedFriendCodeCanBePastedByAnotherInstance() {
  Rig a;
  const std::string token_b(64, 'b');
  Rig b(kGuestBase013, false, token_b);
  a.SignIn("TestA");
  b.SignIn("TestB");
  const std::string copied = a.service->snapshot().friend_code;
  CHECK(copied == a.me && copied.size() == 9 && copied[4] == '-');
  CHECK(copied.find_first_of(" \t\r\n") == std::string::npos);
  CHECK(copied != b.service->snapshot().friend_code);
  CHECK(NormalizeFriendCode(copied) == copied);

  CHECK(b.service->AddFriend(copied + "\r\n") == FriendRequestResult::kSent);
  CHECK(Contains(b.LastRequest("friends.add"), "\"code\":\"" + copied + "\""));
  CHECK(a.service->AddFriend(copied) == FriendRequestResult::kOwnCode);
}

void TestFriendRequestsAreAnswered() {
  Rig rig;
  rig.SignInWithFriends();
  rig.Feed(std::string("{\"ev\":\"friend.request\",\"player\":\"") + kFriendB + "\",\"name\":\"Zed\"}");
  CHECK(Contains(rig.service->snapshot().notice, "Zed wants to be your friend"));
  rig.Reply("friends.requests", std::string("\"incoming\":[{\"player\":\"") + kFriendB + "\",\"name\":\"Zed\"}],\"outgoing\":[]");
  CHECK(rig.service->snapshot().incoming.size() == 1);
  rig.service->AcceptFriend(kFriendB);
  CHECK(rig.service->snapshot().incoming.empty());
  CHECK(Contains(rig.LastRequest("friends.accept"), kFriendB));
  rig.service->RemoveFriend(kFriendA);
  CHECK(rig.service->snapshot().friends.size() == 1);
  CHECK(Contains(rig.LastRequest("friends.remove"), kFriendA));
  rig.service->DeclineFriend(kFriendB);
  CHECK(Contains(rig.LastRequest("friends.decline"), kFriendB));
}

void TestHostedPlayerMatchBecomesPlayerOne() {
  Rig rig;
  rig.SignInWithFriends();
  rig.service->HostPlayerMatch({{"rounds", 3}, {"timer", 1}});
  rig.Tick();
  const std::string host = rig.LastRequest("pm.host");
  CHECK(Contains(host, "\"slots\":2") && Contains(host, "\"rules\":{\"rounds\":3,\"timer\":1}"));
  rig.Reply("pm.host", std::string("\"session\":\"") + kSession + "\"");
  CHECK(rig.service->snapshot().player_match.hosting && rig.service->snapshot().player_match.session == kSession);
  CHECK(rig.CountLogs(std::string("player_match create session_id=") + kSession) == 1);
  CHECK(rig.CountLogs("player_match advertise") == 1);
  rig.Tick(30.0);
  CHECK(rig.Count("pm.host") == 1);
  rig.Tick(31.0);
  CHECK(rig.Count("pm.host") == 2);

  rig.Feed(std::string("{\"ev\":\"match.created\",\"session\":\"") + kSession + "\"," +
           MatchBody("m1", "host", "base013", kFriendA, "SauloB") + "}");
  const Snapshot s = rig.service->snapshot();
  CHECK(s.match && s.match->host && !s.player_match.hosting);
  CHECK(Contains(rig.LastRequest("match.endpoint"), "\"port\":7001"));
  CHECK(rig.CountLogs("player_match host ready") == 1);
  CHECK(!rig.service->Assignment());
  rig.Tick(61.0);
  CHECK(rig.Count("pm.host") == 2);

  rig.Feed(std::string("{\"ev\":\"match.peer\",\"match\":\"m1\",\"player\":\"") + kFriendA +
           "\",\"name\":\"SauloB\",\"role\":\"join\",\"ip\":\"203.0.113.9\",\"port\":7002,\"lan\":[\"127.0.0.1\"],"
           "\"same_network\":false}");
  const auto assignment = rig.service->Assignment();
  CHECK(assignment && assignment->host && assignment->peer_port == 7002 && assignment->guest == kGuestBase013);
  CHECK(assignment && assignment->peer_ip == "203.0.113.9");
  const Snapshot connected = rig.service->snapshot();
  CHECK(DescribeFriend(connected, connected.friends[0]).status == "Connected \xC2\xB7 Base013");
}

void TestJoinedPlayerMatchBecomesPlayerTwoOnTheSamePc() {
  Rig rig(kGuestTu023);
  rig.SignInWithFriends();
  rig.service->SetPlayerMatchSearch(true);
  rig.Tick();
  CHECK(rig.Count("pm.search") == 1);
  rig.Reply("pm.search", rig.SearchAnswer("tu023"));
  const std::vector<PlayerMatchAd> results = rig.service->PlayerMatchResults();
  CHECK(results.size() == 1 && results[0].session == kSession && results[0].player == kFriendA);
  CHECK(results.size() == 1 && results[0].rules == (PlayerMatchRules{{"rounds", 2}}));
  CHECK(rig.CountLogs("player_match search results=1") == 1);
  CHECK(rig.CountLogs(std::string("player_match search result friend=") + kFriendA) == 1);
  rig.Tick(1.0);
  CHECK(rig.Count("pm.search") == 1);
  rig.Tick(2.5);
  CHECK(rig.Count("pm.search") == 2);

  CHECK(!rig.service->JoinPlayerMatch("ffffffffffffffff"));
  CHECK(rig.Count("pm.join") == 0);
  CHECK(rig.service->JoinPlayerMatch(kSession));
  CHECK(Contains(rig.LastRequest("pm.join"), std::string("\"session\":\"") + kSession + "\""));
  CHECK(!rig.service->JoinPlayerMatch(kSession));
  rig.Reply("pm.join", std::string("\"session\":\"") + kSession + "\"," +
                           MatchBody("m2", "join", "tu023", kFriendA, "SauloB"));
  CHECK(rig.CountLogs("player_match join service complete") == 1);
  CHECK(rig.service->snapshot().match && !rig.service->snapshot().match->host);
  CHECK(!rig.service->snapshot().player_match.searching);
  CHECK(Contains(rig.LastRequest("match.endpoint"), "\"port\":7002"));
  CHECK(Contains(rig.LastRequest("match.endpoint"), "192.168.1.5"));

  rig.Feed(std::string("{\"ev\":\"match.peer\",\"match\":\"m2\",\"player\":\"") + kFriendA +
           "\",\"name\":\"SauloB\",\"role\":\"host\",\"ip\":\"203.0.113.9\",\"port\":7001,\"lan\":[\"127.0.0.1\"],"
           "\"same_network\":true}");
  const auto assignment = rig.service->Assignment();
  CHECK(assignment && !assignment->host && assignment->peer_ip == "127.0.0.1" && assignment->peer_port == 7001);
  CHECK(assignment && assignment->guest == kGuestTu023);
}

// Real Internet test 84b7d2f1: Saulo's log showed "netplay join failed reason=bad_peer_address" with no logged
// value. These diagnostics exist so the next such failure shows the exact raw address and what became of it.
void TestSignalingPeerAddressIsLoggedForDiagnosis() {
  Rig rig(kGuestTu023);
  rig.SignInWithFriends();
  rig.service->SetPlayerMatchSearch(true);
  rig.Tick();
  rig.Reply("pm.search", rig.SearchAnswer("tu023"));
  CHECK(rig.service->JoinPlayerMatch(kSession));
  rig.Reply("pm.join", std::string("\"session\":\"") + kSession + "\"," +
                           MatchBody("m2", "join", "tu023", kFriendA, "SauloB"));
  rig.Feed(std::string("{\"ev\":\"match.peer\",\"match\":\"m2\",\"player\":\"") + kFriendA +
           "\",\"name\":\"SauloB\",\"role\":\"host\",\"ip\":\"179.113.149.63\",\"port\":54114,\"lan\":[],"
           "\"same_network\":false}");
  CHECK(rig.CountLogs("signaling peer raw_address=179.113.149.63 raw_port=54114") == 1);
  CHECK(rig.CountLogs("signaling peer parsed family=ipv4") == 1);
  CHECK(rig.CountLogs("signaling peer endpoint=179.113.149.63:54114") == 1);
  const auto assignment = rig.service->Assignment();
  CHECK(assignment && assignment->peer_ip == "179.113.149.63");
}

// A WebSocket that reached the signaling service over IPv6 makes CF-Connecting-IP (and so match.peer's ip) an IPv6
// literal: the transport is IPv4 only, so this must fail with a clear, logged reason, not a silent one.
void TestIpv6PeerAddressIsLoggedNotSilentlyAccepted() {
  Rig rig;
  rig.SignInWithFriends();
  rig.service->SetPlayerMatchSearch(true);
  rig.Tick();
  rig.Reply("pm.search", rig.SearchAnswer());
  CHECK(rig.service->JoinPlayerMatch(kSession));
  rig.Reply("pm.join", std::string("\"session\":\"") + kSession + "\"," + MatchBody("m2", "join", "base013", kFriendA, "SauloB"));
  rig.Feed(std::string("{\"ev\":\"match.peer\",\"match\":\"m2\",\"player\":\"") + kFriendA +
           "\",\"name\":\"SauloB\",\"role\":\"host\",\"ip\":\"2804:14d::1\",\"port\":7001,\"lan\":[],\"same_network\":false}");
  CHECK(rig.CountLogs("signaling peer raw_address=2804:14d::1 raw_port=7001") == 1);
  CHECK(rig.CountLogs("signaling peer parsed family=ipv6") == 1);
  CHECK(rig.CountLogs("signaling peer endpoint=none") == 1);
  const auto assignment = rig.service->Assignment();
  CHECK(assignment && assignment->peer_ip == "2804:14d::1");
}

void WaitForLoopback() { std::this_thread::sleep_for(std::chrono::milliseconds(20)); }

// Ticks until STUN has answered and this match's endpoint went out.
void Discover(Rig& rig, UdpRig& udp) {
  const int before = rig.Count("match.endpoint");
  for (int i = 0; i < 20 && rig.Count("match.endpoint") == before; ++i) {
    rig.Tick(0.05);
    WaitForLoopback();
    udp.Serve();
    WaitForLoopback();
  }
}

std::string PeerJson(const char* match, const char* role, const std::string& rest) {
  return std::string("{\"ev\":\"match.peer\",\"match\":\"") + match + "\",\"player\":\"" + kFriendA +
         "\",\"name\":\"SauloB\",\"role\":\"" + role + "\"," + rest + "}";
}

// The endpoint the peer is told is the gameplay socket's own mapping, read from that very socket: public IP and the
// NAT's port for it, which is not the local port (real test 84b7d2f1: 179.113.149.63:54114 for a socket on 7001).
void TestMatchAdvertisesTheGameSocketsMappedEndpoint() {
  UdpRig udp;
  CHECK(udp.socket.is_open() && udp.socket.bound_port() == 58141);
  Rig rig(kGuestBase013, false, kToken, &udp);
  rig.StartPlayerMatchAsHost();
  CHECK(rig.Count("match.endpoint") == 0);  // nothing goes out before the socket's own mapping is known
  CHECK(udp.lookups == 1);
  Discover(rig, udp);
  CHECK(udp.stun_saw == udp.Game());  // the STUN request left from the gameplay socket itself
  const std::string endpoint = rig.LastRequest("match.endpoint");
  CHECK(Contains(endpoint, "\"port\":58141"));
  CHECK(Contains(endpoint, "\"public_ip\":\"179.113.149.63\",\"public_port\":54114"));
  CHECK(rig.CountLogs("stun mapped endpoint=179.113.149.63:54114 local_port=58141") == 1);
  CHECK(rig.CountLogs("match endpoint advertised=179.113.149.63:54114 local=0.0.0.0:58141") == 1);
  CHECK(udp.socket.bound_port() == 58141);
  CHECK(!rig.service->Assignment());
}

void TestPeerIsHandedToTheFightOnlyAfterItsPunchAnswers() {
  UdpRig udp;
  Rig rig(kGuestBase013, false, kToken, &udp);
  rig.StartPlayerMatchAsHost();
  Discover(rig, udp);
  // Its WebSocket came over IPv6: only the STUN endpoint is a UDP candidate.
  rig.Feed(PeerJson("m1", "join", "\"ip\":\"2804:14d::1\",\"port\":7002,\"lan\":[],\"same_network\":false,"
                                  "\"public_ip\":\"127.0.0.1\",\"public_port\":58148"));
  CHECK(rig.CountLogs("peer candidate received type=stun endpoint=127.0.0.1:58148") == 1);
  CHECK(rig.CountLogs("type=observed") == 0);
  for (int i = 0; i < 5; ++i) {
    rig.Tick(0.15);
    WaitForLoopback();
  }
  const std::vector<PunchPacket> heard = udp.PeerHeard();
  CHECK(heard.size() >= 3);
  CHECK(!heard.empty() && heard[0].kind == PunchKind::kPunch && heard[0].side == 1 && heard[0].key == PunchKey("m1"));
  CHECK(rig.CountLogs("udp punch tx endpoint=127.0.0.1:58148") == 1);  // once, not per packet
  CHECK(!rig.service->Assignment());  // punches out are no proof

  // Not this match's, or this side's own: no proof either.
  udp.PeerSends(PunchKind::kAck, 2, PunchKey("m0"));
  udp.PeerSends(PunchKind::kAck, 1, PunchKey("m1"));
  WaitForLoopback();
  rig.Tick(0.05);
  CHECK(!rig.service->Assignment());

  // The peer's own punch is answered, and still selects nothing on this side.
  udp.PeerSends(PunchKind::kPunch, 2, PunchKey("m1"));
  WaitForLoopback();
  rig.Tick(0.05);
  WaitForLoopback();
  bool acked = false;
  for (const PunchPacket& packet : udp.PeerHeard()) acked |= packet.kind == PunchKind::kAck && packet.side == 1;
  CHECK(acked);
  CHECK(rig.CountLogs("udp punch rx endpoint=127.0.0.1:58148") == 1);
  CHECK(!rig.service->Assignment());

  udp.PeerSends(PunchKind::kAck, 2, PunchKey("m1"));
  WaitForLoopback();
  rig.Tick(0.05);
  const auto assignment = rig.service->Assignment();
  CHECK(assignment && assignment->host && assignment->peer_ip == "127.0.0.1" && assignment->peer_port == 58148);
  CHECK(rig.CountLogs("udp candidate selected endpoint=127.0.0.1:58148 type=stun") == 1);
  CHECK(udp.socket.bound_port() == 58141);
}

void TestJoinRolePunchesWithItsOwnSide() {
  UdpRig udp;
  Rig rig(kGuestBase013, false, kToken, &udp);
  rig.SignInWithFriends();
  rig.service->SetPlayerMatchSearch(true);
  rig.Tick();
  rig.Reply("pm.search", rig.SearchAnswer());
  CHECK(rig.service->JoinPlayerMatch(kSession));
  rig.Reply("pm.join", std::string("\"session\":\"") + kSession + "\"," + MatchBody("m2", "join", "base013", kFriendA, "SauloB"));
  Discover(rig, udp);
  CHECK(Contains(rig.LastRequest("match.endpoint"), "\"port\":58141"));  // the bound port, whatever the role
  rig.Feed(PeerJson("m2", "host", "\"ip\":\"203.0.113.9\",\"port\":7001,\"lan\":[],\"same_network\":false,"
                                  "\"public_ip\":\"127.0.0.1\",\"public_port\":58148"));
  rig.Tick(0.15);
  WaitForLoopback();
  const std::vector<PunchPacket> heard = udp.PeerHeard();
  CHECK(!heard.empty() && heard[0].side == 2 && heard[0].key == PunchKey("m2"));
  udp.PeerSends(PunchKind::kAck, 1, PunchKey("m2"));
  WaitForLoopback();
  rig.Tick(0.05);
  const auto assignment = rig.service->Assignment();
  CHECK(assignment && !assignment->host && assignment->peer_port == 58148);
}

// Without a STUN answer the endpoint goes out without a mapping; the peer's own still makes the candidates.
void TestNoStunAnswerAdvertisesNoMapping() {
  UdpRig udp;
  udp.answer = false;
  Rig rig(kGuestBase013, false, kToken, &udp);
  rig.StartPlayerMatchAsHost();
  for (int i = 0; i < 40 && rig.Count("match.endpoint") == 0; ++i) {
    rig.Tick(0.1);
    udp.Serve();
  }
  CHECK(rig.Count("match.endpoint") == 1);
  CHECK(!Contains(rig.LastRequest("match.endpoint"), "public_ip"));
  CHECK(rig.CountLogs("stun mapped endpoint=none reason=no_answer") == 1);
  CHECK(rig.CountLogs("match endpoint advertised=none local=0.0.0.0:58141") == 1);
  rig.Feed(PeerJson("m1", "join", "\"ip\":\"::ffff:127.0.0.1\",\"port\":58148,\"lan\":[],\"same_network\":false,"
                                  "\"public_ip\":\"127.0.0.1\",\"public_port\":58147"));
  CHECK(rig.CountLogs("peer candidate received type=stun endpoint=127.0.0.1:58147") == 1);
  CHECK(rig.CountLogs("peer candidate received type=observed endpoint=127.0.0.1:58148") == 1);
}

// A mapping the service would refuse (not a public address) is not advertised, so the endpoint still goes through.
void TestNonPublicMappingIsNotAdvertised() {
  UdpRig udp;
  udp.mapped = {0x64400102, 40000};  // 100.64.1.2, carrier-grade NAT space
  Rig rig(kGuestBase013, false, kToken, &udp);
  rig.StartPlayerMatchAsHost();
  Discover(rig, udp);
  CHECK(rig.Count("match.endpoint") == 1 && !Contains(rig.LastRequest("match.endpoint"), "public_ip"));
  CHECK(rig.CountLogs("stun mapped endpoint=100.64.1.2:40000") == 1);
  CHECK(rig.CountLogs("match endpoint advertised=none local=0.0.0.0:58141") == 1);
}

const char kNoPublic[] = "\"ip\":\"108.202.118.218\",\"port\":7001,\"lan\":[],\"same_network\":false,\"public_endpoints\":true";
const char kWithPublic[] = "\"ip\":\"108.202.118.218\",\"port\":7001,\"lan\":[],\"same_network\":false,\"public_endpoints\":true,"
                           "\"public_ip\":\"127.0.0.1\",\"public_port\":58148";

void JoinAsPlayerTwo(Rig& rig, const char* match) {
  rig.SignInWithFriends();
  rig.service->SetPlayerMatchSearch(true);
  rig.Tick();
  rig.Reply("pm.search", rig.SearchAnswer());
  CHECK(rig.service->JoinPlayerMatch(kSession));
  rig.Reply("pm.join", std::string("\"session\":\"") + kSession + "\"," + MatchBody(match, "join", "base013", kFriendA, "SauloB"));
}

// Real two-PC test 6d9f1c2a: match.peer came with public=none and the observed guess was punched for 10 s. An Internet
// peer's endpoint is now waited for, and the punch starts only once it is there, for either role.
void TestInternetPeerEndpointIsWaitedFor() {
  for (const bool host : {true, false}) {
    UdpRig udp;
    Rig rig(kGuestBase013, false, kToken, &udp);
    const char* match = host ? "m1" : "m2";
    if (host) rig.StartPlayerMatchAsHost();
    else JoinAsPlayerTwo(rig, match);
    Discover(rig, udp);
    CHECK(rig.CountLogs(std::string("peer endpoint pending match=") + match + " peer=" + kFriendA) == 1);
    const char* peer_role = host ? "join" : "host";
    rig.Feed(PeerJson(match, peer_role, kNoPublic));
    CHECK(rig.CountLogs("peer endpoint update raw=108.202.118.218:7001 public=none "
                        "service_forwards_public=true") == 1);
    for (int i = 0; i < 5; ++i) rig.Tick(1.0);
    CHECK(rig.CountLogs("udp rendezvous begin") == 0 && rig.CountLogs("udp punch tx") == 0);
    CHECK(rig.CountLogs("peer candidate received") == 0);
    CHECK(rig.service->snapshot().match && rig.Count("match.leave") == 0);

    rig.Feed(PeerJson(match, peer_role, kWithPublic));
    CHECK(rig.CountLogs("peer endpoint update raw=108.202.118.218:7001 public=127.0.0.1:58148") == 1);
    CHECK(rig.CountLogs("peer endpoint ready public=127.0.0.1:58148 same_network=false") == 1);
    CHECK(rig.CountLogs("udp rendezvous begin candidates=2") == 1);
    CHECK(rig.CountLogs("peer candidate received type=stun endpoint=127.0.0.1:58148") == 1);
    CHECK(rig.CountLogs("peer candidate received type=observed endpoint=108.202.118.218:7001") == 1);

    // The same update again changes nothing: no second punch, no candidates added twice.
    rig.Feed(PeerJson(match, peer_role, kWithPublic));
    CHECK(rig.CountLogs("udp rendezvous begin") == 1 && rig.CountLogs("type=stun endpoint=127.0.0.1:58148") == 1);

    // The punch's own 10 s run from here, not from the first match.peer.
    for (int i = 0; i < 8; ++i) rig.Tick(1.0);
    CHECK(rig.service->snapshot().match && rig.CountLogs("punch_timeout") == 0);
    udp.PeerSends(PunchKind::kAck, host ? 2 : 1, PunchKey(match));
    WaitForLoopback();
    rig.Tick(0.05);
    const auto assignment = rig.service->Assignment();
    CHECK(assignment && assignment->host == host && assignment->peer_port == 58148 && assignment->generation == 1);
  }
}

// Both endpoints already in when match.peer comes: no wait at all.
void TestReadyPeerEndpointPunchesAtOnce() {
  UdpRig udp;
  Rig rig(kGuestBase013, false, kToken, &udp);
  rig.StartPlayerMatchAsHost();
  Discover(rig, udp);
  rig.Feed(PeerJson("m1", "join", kWithPublic));
  CHECK(rig.CountLogs("peer endpoint ready public=127.0.0.1:58148") == 1);
  CHECK(rig.CountLogs("udp rendezvous begin candidates=2") == 1);
}

// The same network needs no Internet endpoint: its LAN candidates go at once.
void TestSameNetworkPeerDoesNotWait() {
  UdpRig udp;
  Rig rig(kGuestBase013, false, kToken, &udp);
  rig.StartPlayerMatchAsHost();
  Discover(rig, udp);
  rig.Feed(PeerJson("m1", "join", "\"ip\":\"203.0.113.9\",\"port\":58148,\"lan\":[\"127.0.0.1\"],\"same_network\":true"));
  CHECK(rig.CountLogs("peer endpoint ready public=none same_network=true") == 1);
  CHECK(rig.CountLogs("peer candidate received type=lan endpoint=127.0.0.1:58148") == 1);
  CHECK(rig.CountLogs("udp rendezvous begin") == 1);
}

// The peer's endpoint never arrives: said as such (not as a punch failure), the match is left, and Create Match and
// Invite Player work again right away.
void TestMissingPeerEndpointTimesOutAndFreesThePlayerMatch() {
  UdpRig udp;
  Rig rig(kGuestBase013, false, kToken, &udp);
  rig.StartPlayerMatchAsHost();
  Discover(rig, udp);
  rig.Feed(PeerJson("m1", "join", kNoPublic));
  for (int i = 0; i < 9; ++i) rig.Tick(1.0);
  CHECK(rig.service->snapshot().match && rig.Count("match.leave") == 0);
  for (int i = 0; i < 2; ++i) rig.Tick(1.0);
  CHECK(rig.CountLogs("peer endpoint wait timeout") == 1);
  CHECK(rig.CountLogs("udp rendezvous failed reason=peer_endpoint_timeout peer_signaled=true "
                      "service_forwards_public=true") == 1);
  CHECK(rig.CountLogs("udp rendezvous failed reason=no_direct_path") == 1 && rig.CountLogs("punch_timeout") == 0);
  CHECK(rig.CountLogs("udp punch tx") == 0);
  CHECK(rig.Count("match.leave") == 1 && !rig.service->snapshot().match && !rig.service->Assignment());
  CHECK(Contains(rig.service->snapshot().notice, "their connection details never arrived"));

  // A late update for that match changes nothing.
  rig.Feed(PeerJson("m1", "join", kWithPublic));
  CHECK(rig.CountLogs("udp rendezvous begin") == 0);

  rig.service->HostPlayerMatch({{"rounds", 2}});
  rig.Tick();
  rig.Reply("pm.host", std::string("\"session\":\"") + kSession + "\"");
  CHECK(rig.service->snapshot().player_match.hosting);
  CHECK(rig.service->SendInvite(kFriendA));
  CHECK(rig.CountLogs("no_player_match") == 0);
}

// No match.peer at all (the peer never sent its endpoint) ends the same way.
void TestNoPeerSignalingTimesOut() {
  UdpRig udp;
  Rig rig(kGuestBase013, false, kToken, &udp);
  rig.StartPlayerMatchAsHost();
  Discover(rig, udp);
  for (int i = 0; i < 11; ++i) rig.Tick(1.0);
  CHECK(rig.CountLogs("udp rendezvous failed reason=peer_endpoint_timeout peer_signaled=false "
                      "service_forwards_public=false") == 1);
  CHECK(!rig.service->snapshot().match);
}

// Updates for another match, or after leaving, never start a punch.
void TestStaleOrLeftPeerUpdatesAreIgnored() {
  UdpRig udp;
  Rig rig(kGuestBase013, false, kToken, &udp);
  rig.StartPlayerMatchAsHost();
  Discover(rig, udp);
  rig.Feed(PeerJson("m0", "join", kWithPublic));
  CHECK(rig.CountLogs("peer endpoint update") == 0 && rig.CountLogs("udp rendezvous begin") == 0);
  rig.service->LeaveMatch();
  rig.Feed(PeerJson("m1", "join", kWithPublic));
  CHECK(rig.CountLogs("peer endpoint update") == 0 && rig.CountLogs("udp rendezvous begin") == 0);
  for (int i = 0; i < 12; ++i) rig.Tick(1.0);
  CHECK(rig.CountLogs("peer_endpoint_timeout") == 0);  // the wait ended with the match
}

// No relay exists: when no candidate ever answers, the match is left (bounded, logged), not held forever.
void TestUnansweredPunchLeavesTheMatch() {
  UdpRig udp;
  Rig rig(kGuestBase013, false, kToken, &udp);
  rig.StartPlayerMatchAsHost();
  Discover(rig, udp);
  rig.Feed(PeerJson("m1", "join", "\"ip\":\"203.0.113.9\",\"port\":7002,\"lan\":[],\"same_network\":false,"
                                  "\"public_ip\":\"127.0.0.1\",\"public_port\":58147"));
  for (int i = 0; i < 9; ++i) rig.Tick(1.0);
  CHECK(rig.service->snapshot().match && rig.Count("match.leave") == 0);
  for (int i = 0; i < 3; ++i) rig.Tick(1.0);
  CHECK(rig.CountLogs("udp rendezvous failed reason=punch_timeout candidates=2 peer_heard=false") == 1);
  CHECK(rig.CountLogs("udp rendezvous failed reason=no_direct_path") == 1);
  CHECK(rig.Count("match.leave") == 1 && !rig.service->snapshot().match && !rig.service->Assignment());
  CHECK(Contains(rig.service->snapshot().notice, "no direct connection"));
  CHECK(udp.socket.is_open() && udp.socket.bound_port() == 58141);  // kept for the next match
}

// A new match starts over on the same socket: the old match's late answers select nothing.
void TestNextMatchReusesTheSocketAndIgnoresTheOldOne() {
  UdpRig udp;
  Rig rig(kGuestBase013, false, kToken, &udp);
  rig.StartPlayerMatchAsHost();
  Discover(rig, udp);
  rig.Feed(PeerJson("m1", "join", "\"ip\":\"203.0.113.9\",\"port\":7002,\"lan\":[],\"same_network\":false,"
                                  "\"public_ip\":\"127.0.0.1\",\"public_port\":58148"));
  rig.Feed("{\"ev\":\"match.ended\",\"match\":\"m1\",\"reason\":\"left\"}");
  rig.Feed(std::string("{\"ev\":\"match.created\",\"session\":\"") + kSession + "\"," +
           MatchBody("m3", "host", "base013", kFriendA, "SauloB") + "}");
  const int before = rig.Count("match.endpoint");
  for (int i = 0; i < 20 && rig.Count("match.endpoint") == before; ++i) {
    rig.Tick(0.05);
    WaitForLoopback();
    udp.Serve();
    WaitForLoopback();
  }
  CHECK(rig.Count("match.endpoint") == before + 1);
  CHECK(udp.lookups == 1);  // STUN servers looked up once, reused
  CHECK(udp.stun_saw == udp.Game() && udp.socket.bound_port() == 58141);
  rig.Feed(PeerJson("m3", "join", "\"ip\":\"203.0.113.9\",\"port\":7002,\"lan\":[],\"same_network\":false,"
                                  "\"public_ip\":\"127.0.0.1\",\"public_port\":58148"));
  udp.PeerSends(PunchKind::kAck, 2, PunchKey("m1"));
  WaitForLoopback();
  rig.Tick(0.05);
  CHECK(!rig.service->Assignment());
  udp.PeerSends(PunchKind::kAck, 2, PunchKey("m3"));
  WaitForLoopback();
  rig.Tick(0.05);
  CHECK(rig.service->Assignment() && rig.service->Assignment()->generation == 2);
}

// A gameplay socket that could not be bound cannot carry a match: said once, and the match is left.
void TestUnboundSocketLeavesTheMatch() {
  UdpRig udp;
  ssf2t::netplay::OnlineGameSocket unbound;
  Rig rig(kGuestBase013, false, kToken, &udp, &unbound);
  rig.StartPlayerMatchAsHost();
  CHECK(rig.CountLogs("udp rendezvous failed reason=socket_unbound") == 1);
  CHECK(rig.Count("match.leave") == 1 && !rig.service->snapshot().match);
}

void TestCancelSearchStopAndRefusedJoin() {
  Rig rig;
  rig.SignInWithFriends();
  rig.service->HostPlayerMatch({});
  rig.Tick();
  rig.service->CancelPlayerMatch();
  CHECK(rig.Count("pm.cancel") == 1);
  CHECK(!rig.service->snapshot().player_match.hosting);
  rig.Tick(61.0);
  CHECK(rig.Count("pm.host") == 1);

  rig.service->SetPlayerMatchSearch(true);
  rig.Tick();
  rig.Reply("pm.search", rig.SearchAnswer());
  CHECK(rig.service->JoinPlayerMatch(kSession));
  rig.Reply("pm.join", "", "offline");
  CHECK(!rig.service->snapshot().match);
  CHECK(rig.service->snapshot().notice == "Your friend is not online.");
  CHECK(rig.CountLogs("player_match join failed code=offline") == 1);
  rig.service->SetPlayerMatchSearch(false);
  const int searches = rig.Count("pm.search");
  rig.Tick(10.0);
  CHECK(rig.Count("pm.search") == searches);
}

void TestAdvertisementWaitsForTheServiceAndSurvivesAReconnect() {
  Rig rig;
  rig.service->HostPlayerMatch({});
  rig.SignInWithFriends();
  CHECK(rig.Count("pm.host") == 1);
  rig.Reply("pm.host", std::string("\"session\":\"") + kSession + "\"");
  rig.Event(ChannelEvent::Kind::kClosed, "link_lost");
  CHECK(rig.service->snapshot().player_match.hosting && rig.service->snapshot().player_match.session.empty());
  rig.Tick(5.0);
  rig.Event(ChannelEvent::Kind::kOpen);
  rig.Reply("hello", "\"friend_code\":\"" + rig.me + "\"");
  CHECK(rig.Count("pm.host") == 1);
}

void TestNoChallengeOnTheWireOrInLogs() {
  Rig rig;
  rig.StartPlayerMatchAsHost();
  rig.service->LeaveMatch();
  for (const auto& channel : rig.channels) {
    for (const std::string& sent : channel->sent) CHECK(!Contains(sent, "challenge"));
  }
  for (const std::string& line : rig.logs) CHECK(!Contains(line, "challenge") && !Contains(line, kToken));
}

void TestMatchWithAnotherGuestIsRefused() {
  Rig rig(kGuestBase013);
  rig.SignInWithFriends();
  rig.Feed("{\"ev\":\"match.created\"," + MatchBody("mx", "host", "tu023", kFriendA, "SauloB") + "}");
  CHECK(!rig.service->snapshot().match);
  CHECK(!rig.service->Assignment());
  CHECK(Contains(rig.LastRequest("match.leave"), "\"match\":\"mx\""));
  CHECK(rig.service->snapshot().notice == "Game versions do not match.");
}

void TestLinkLossKeepsFriendsAndMatchAndResumes() {
  Rig rig;
  rig.StartPlayerMatchAsHost();
  rig.Feed(std::string("{\"ev\":\"match.peer\",\"match\":\"m1\",\"player\":\"") + kFriendA +
           "\",\"name\":\"SauloB\",\"role\":\"join\",\"ip\":\"203.0.113.9\",\"port\":7002,\"lan\":[],\"same_network\":false}");
  rig.Event(ChannelEvent::Kind::kClosed, "link_lost");
  Snapshot s = rig.service->snapshot();
  CHECK(s.connection == Connection::kChecking && s.connection_text == "Reconnecting");
  CHECK(s.friends.size() == 2 && s.match.has_value());
  CHECK(rig.service->Assignment().has_value());
  CHECK(!s.friends[0].online);

  const size_t before = rig.channels.size();
  rig.Tick(5.0);
  CHECK(rig.channels.size() == before + 1);
  rig.Event(ChannelEvent::Kind::kOpen);
  rig.Reply("hello", "\"friend_code\":\"" + rig.me + "\"");
  CHECK(rig.service->snapshot().connection == Connection::kOnline);
  CHECK(Contains(rig.LastRequest("match.resume"), "\"match\":\"m1\""));
  rig.Reply("match.resume", MatchBody("m1", "host", "base013", kFriendA, "SauloB"));
  CHECK(rig.service->snapshot().match.has_value());
  CHECK(rig.service->Assignment().has_value());
  CHECK(rig.Count("friends.list") == 1);
}

void TestUnreachableServiceRetriesWithoutLosingState() {
  Rig rig;
  rig.Enable();
  rig.Tick();
  rig.Event(ChannelEvent::Kind::kClosed, "unreachable");
  CHECK(rig.service->snapshot().connection == Connection::kOffline);
  CHECK(rig.service->snapshot().connection_text == "Unable to reach online service.");
  size_t opened = rig.channels.size();
  rig.Tick(0.2);
  CHECK(rig.channels.size() == opened);
  for (int i = 0; i < 6; ++i) rig.Tick(10.0);
  CHECK(rig.channels.size() > opened);
}

void TestResumeFailureEndsAMatchThatNeverConnected() {
  Rig rig;
  rig.StartPlayerMatchAsHost();
  rig.Event(ChannelEvent::Kind::kClosed, "link_lost");
  rig.Tick(5.0);
  rig.Event(ChannelEvent::Kind::kOpen);
  rig.Reply("hello", "\"friend_code\":\"" + rig.me + "\"");
  rig.Reply("match.resume", "", "not_found");
  CHECK(!rig.service->snapshot().match);
  CHECK(!rig.service->Assignment());
}

void TestSignalingLossDoesNotKillALiveFight() {
  Rig rig;
  rig.StartPlayerMatchAsHost();
  rig.Feed(std::string("{\"ev\":\"match.peer\",\"match\":\"m1\",\"player\":\"") + kFriendA +
           "\",\"name\":\"SauloB\",\"role\":\"join\",\"ip\":\"203.0.113.9\",\"port\":7002,\"lan\":[],\"same_network\":false}");
  rig.service->SetNetplayLink(NetplayLink::kConnected, std::nullopt);
  CHECK(Contains(rig.LastRequest("match.ready"), "\"ready\":true"));
  rig.service->SetNetplayLink(NetplayLink::kFighting, 32);
  Snapshot s = rig.service->snapshot();
  CHECK(DescribeFriend(s, s.friends[0]).status == "In game \xC2\xB7 Base013 \xC2\xB7 32 ms");

  rig.Feed("{\"ev\":\"match.ended\",\"match\":\"m1\",\"reason\":\"disconnect\"}");
  CHECK(rig.service->Assignment().has_value());
  rig.Event(ChannelEvent::Kind::kClosed, "link_lost");
  CHECK(rig.service->Assignment().has_value());
  rig.service->SetNetplayLink(NetplayLink::kNone, std::nullopt);
  CHECK(!rig.service->Assignment());
  CHECK(!rig.service->snapshot().match);
}

void TestLatencyIsShownOnlyWhenMeasured() {
  Rig rig;
  rig.StartPlayerMatchAsHost();
  rig.Feed(std::string("{\"ev\":\"match.peer\",\"match\":\"m1\",\"player\":\"") + kFriendA +
           "\",\"name\":\"SauloB\",\"role\":\"join\",\"ip\":\"203.0.113.9\",\"port\":7002,\"lan\":[],\"same_network\":false}");
  rig.service->SetNetplayLink(NetplayLink::kConnected, std::nullopt);
  Snapshot s = rig.service->snapshot();
  CHECK(DescribeFriend(s, s.friends[0]).status == "Connected \xC2\xB7 Base013");
  rig.service->SetNetplayLink(NetplayLink::kConnected, 32);
  s = rig.service->snapshot();
  CHECK(DescribeFriend(s, s.friends[0]).status == "Connected \xC2\xB7 Base013 \xC2\xB7 32 ms");
  CHECK(rig.service->netplay_info().second == 32);
}

void TestPeerEndingBeforeConnectionClearsTheMatch() {
  Rig rig;
  rig.StartPlayerMatchAsHost();
  rig.Feed("{\"ev\":\"match.ended\",\"match\":\"m1\",\"reason\":\"left\"}");
  CHECK(!rig.service->snapshot().match);
  CHECK(!rig.service->Assignment());
  CHECK(Contains(rig.service->snapshot().notice, "left the match"));
}

void TestTurningOnlineOffLeavesTheMatchAndStopsEverything() {
  Rig rig;
  rig.StartPlayerMatchAsHost();
  ChannelState& channel = rig.Last();
  rig.service->Apply({false, "Saulo"});
  CHECK(Contains(channel.sent.back(), "match.leave"));
  rig.Tick(5.0);
  const Snapshot s = rig.service->snapshot();
  CHECK(s.connection == Connection::kOff && s.friends.empty() && !s.match && s.friend_code.empty());
  CHECK(!rig.service->Assignment());
  const size_t channels = rig.channels.size();
  for (int i = 0; i < 20; ++i) rig.Tick(5.0);
  CHECK(rig.channels.size() == channels);
}

void TestLeaveMatchAndRename() {
  Rig rig;
  rig.StartPlayerMatchAsHost();
  rig.service->LeaveMatch();
  CHECK(Contains(rig.LastRequest("match.leave"), "\"match\":\"m1\""));
  CHECK(!rig.service->snapshot().match);

  const size_t before = rig.channels.size();
  rig.Enable("Renamed");
  rig.Tick(3.0);
  CHECK(rig.channels.size() == before + 1);
  rig.Event(ChannelEvent::Kind::kOpen);
  CHECK(Contains(rig.LastRequest("hello"), "\"name\":\"Renamed\""));
}

// What the fight runtime does when a connected peer is gone, and what the game's own lobby exits do: the match, its
// hand-off and the peer are cleared, and the player can create or join again.
void TestAfterADisconnectThePlayerCanPlayAgain() {
  Rig rig;
  rig.StartPlayerMatchAsHost();
  rig.Feed(std::string("{\"ev\":\"match.peer\",\"match\":\"m1\",\"player\":\"") + kFriendA +
           "\",\"name\":\"SauloB\",\"role\":\"join\",\"ip\":\"203.0.113.9\",\"port\":7002,\"lan\":[],"
           "\"same_network\":false}");
  CHECK(rig.service->Assignment().has_value());
  const uint32_t first = rig.service->Assignment() ? rig.service->Assignment()->generation : 0;
  CHECK(first != 0);
  rig.service->SetNetplayLink(NetplayLink::kFighting, 40);
  rig.service->SetNetplayLink(NetplayLink::kNone, std::nullopt);
  rig.service->LeaveMatch();
  CHECK(Contains(rig.LastRequest("match.leave"), "\"match\":\"m1\""));
  CHECK(!rig.service->Assignment() && !rig.service->Peer() && !rig.service->snapshot().match);
  rig.service->LeaveMatch();
  CHECK(rig.Count("match.leave") == 1);

  const int hosts = rig.Count("pm.host");
  rig.service->HostPlayerMatch({{"rounds", 2}});
  CHECK(rig.Count("pm.host") == hosts + 1);
  rig.service->CancelPlayerMatch();

  rig.service->SetPlayerMatchSearch(true);
  rig.Tick(3.0);
  rig.Reply("pm.search", rig.SearchAnswer());
  CHECK(rig.service->JoinPlayerMatch(kSession));
  CHECK(Contains(rig.LastRequest("pm.join"), std::string("\"session\":\"") + kSession + "\""));

  // The next match is a new generation, so no fight session of the first one carries over.
  rig.Feed(std::string("{\"ev\":\"match.created\",\"session\":\"") + kSession + "\"," +
           MatchBody("m2", "join", "base013", kFriendA, "SauloB") + "}");
  rig.Feed(std::string("{\"ev\":\"match.peer\",\"match\":\"m2\",\"player\":\"") + kFriendA +
           "\",\"name\":\"SauloB\",\"role\":\"host\",\"ip\":\"203.0.113.9\",\"port\":7001,\"lan\":[],"
           "\"same_network\":false}");
  const auto second = rig.service->Assignment();
  CHECK(second && !second->host && second->generation == first + 1);
}

void TestFatalSignInErrorsDoNotRetryAtOnce() {
  Rig rig;
  rig.Enable();
  rig.Tick();
  rig.Event(ChannelEvent::Kind::kOpen);
  rig.Reply("hello", "", "proto");
  CHECK(rig.service->snapshot().connection == Connection::kError);
  CHECK(Contains(rig.service->snapshot().connection_text, "different version"));
  const size_t opened = rig.channels.size();
  rig.Tick(10.0);
  CHECK(rig.channels.size() == opened);
}

void TestMalformedServerMessagesAreIgnored() {
  Rig rig;
  rig.SignInWithFriends();
  for (const char* text : {"", "not json", "[]", "{}", "{\"ev\":\"match.peer\"}", "{\"ev\":\"nope\"}",
                           "{\"re\":99,\"code\":\"ok\"}", "{\"ev\":\"match.created\",\"match\":\"x\"}",
                           "{\"ev\":\"friend.status\",\"friend\":{}}"}) {
    rig.Feed(text);
  }
  const Snapshot s = rig.service->snapshot();
  CHECK(s.connection == Connection::kOnline && s.friends.size() == 2 && !s.match);
}

// PLAY waits for a finished Connection test, whatever it said; one that stopped without a report has not finished.
void TestEveryConnectionReportCountsAsFinished() {
  Rig rig;
  rig.Enable();
  CHECK(rig.service->snapshot().tests_finished == 0);
  for (const char* verdict : {"Ready for online play", "Limited: the multiplayer UDP port could not be opened",
                              "Not ready: cannot reach the online service"}) {
    rig.test_result.verdict = verdict;
    rig.service->TestConnection();
    CHECK(rig.service->snapshot().testing || rig.service->snapshot().tests_finished > 0);
    rig.Tick();
    CHECK(!rig.service->snapshot().testing && rig.service->snapshot().test->verdict == verdict);
  }
  CHECK(rig.service->snapshot().tests_finished == 3);

  rig.test_throws = true;
  rig.service->TestConnection();
  rig.Tick();
  const Snapshot stopped = rig.service->snapshot();
  CHECK(!stopped.testing && stopped.tests_finished == 3);
  CHECK(rig.CountLogs("online connection test stopped without a report: test broke") == 1);

  // Online play off drops a test that is still running.
  Rig off;
  off.Enable();
  off.service->Apply({false, "Saulo"});
  off.service->TestConnection();
  off.Tick();
  CHECK(off.test_calls == 0 && off.service->snapshot().tests_finished == 0);
}

void TestConnectionTestRunsOnlyWhenEnabled() {
  Rig rig;
  rig.service->TestConnection();
  CHECK(rig.test_calls == 0);
  rig.Enable();
  rig.test_result.verdict = "Ready for online play";
  rig.test_result.ready = true;
  rig.test_result.latency_ms = 42;
  rig.test_result.details = {"service: ok", "STUN a: 1.2.3.4:5"};
  rig.service->TestConnection();
  rig.Tick();
  CHECK(rig.test_calls == 1);
  const Snapshot s = rig.service->snapshot();
  CHECK(s.test && s.test->latency_ms == 42 && s.test->verdict == "Ready for online play" && !s.testing);
  CHECK(rig.test_request.url == "wss://example.test/" && rig.test_request.name == "Saulo" &&
        rig.test_request.guest == "base013" && rig.test_request.upnp &&
        rig.test_request.game_ports == (std::vector<uint16_t>{7001, 7002}));
  CHECK(rig.test_request.identity.player_id == rig.me && rig.test_request.stun == net::kDefaultStunServers);
  CHECK(rig.CountLogs("online connection test service: ok") == 1);
  CHECK(rig.CountLogs("online connection test Ready for online play") == 1);
  for (const std::string& line : rig.logs) CHECK(!Contains(line, kToken));
}


void TestTemporaryIdentityGetsASuffix() {
  Rig rig(kGuestBase013, true);
  rig.Enable("Saulo");
  rig.Tick();
  rig.Event(ChannelEvent::Kind::kOpen);
  CHECK(Contains(rig.LastRequest("hello"), "\"name\":\"Saulo-2\""));
}

void TestFriendRowTexts() {
  CHECK(GuestLabel(kGuestBase013) == "Base013");
  CHECK(GuestLabel(kGuestTu023) == "TU023");
  Friend f;
  f.online = true;
  f.state = FriendState::kPlayerMatch;
  f.platform = "Linux";
  CHECK(FriendStatusLine(f) == "Online Match \xC2\xB7 Linux");
  f.state = FriendState::kConnecting;
  f.version = "0.3.0";
  CHECK(FriendStatusLine(f) == "Connecting \xC2\xB7 Linux \xC2\xB7 v0.3.0");
}

}  // namespace


std::string InviteEvent(const char* id, const char* session = kSession) {
  return std::string("{\"ev\":\"invite\",\"invite\":{\"id\":\"") + id + "\",\"session\":\"" + session + "\",\"player\":\"" +
         kFriendA + "\",\"name\":\"SauloB\",\"guest\":\"base013\",\"build\":1,\"slots\":2,\"rules\":{\"rounds\":2},"
         "\"qos\":\"abcd\",\"platform\":\"Windows\",\"version\":\"0.2.2\",\"expires\":1}}";
}

// Relay fallback: direct UDP first, the service's relay when no direct path appears within the probe window. The host
// chooses and the joiner follows, so both ends always agree; the fight session only ever sees one transport.
constexpr uint32_t kRelayToken = 0xA1B2C3D4;
const char kRelayCaps[] = "\"capabilities\":{\"protocol_version\":3,\"worker_build\":\"test-build\",\"public_endpoints\":true,"
                          "\"relay_supported\":true,\"relay_max_payload\":1472}";
const char kRelayNoPublicCaps[] = "\"capabilities\":{\"protocol_version\":3,\"worker_build\":\"test-build\","
                                  "\"public_endpoints\":false,\"relay_supported\":true,\"relay_max_payload\":1472}";
const char kOldServicePeer[] = "\"ip\":\"108.202.118.218\",\"port\":7001,\"lan\":[],\"same_network\":false";

void SignInRelay(Rig& rig, const std::string& caps = kRelayCaps) {
  rig.Enable();
  rig.Tick();
  rig.Event(ChannelEvent::Kind::kOpen);
  rig.Reply("hello", "\"player\":\"" + rig.me + "\",\"name\":\"Saulo\",\"friend_code\":\"" + rig.me + "\"" +
                         (caps.empty() ? std::string() : "," + caps));
  rig.Reply("friends.list", "\"friends\":[" + FriendJson(kFriendA, "SauloB", true) + "]");
  rig.Reply("friends.requests", "\"incoming\":[],\"outgoing\":[]");
}

std::string RelayField(uint32_t token) { return token ? ",\"relay_token\":" + std::to_string(token) : std::string(); }

void CreateRelayMatch(Rig& rig, const char* match, uint32_t token) {
  rig.service->HostPlayerMatch({{"rounds", 2}});
  rig.Tick();
  rig.Reply("pm.host", std::string("\"session\":\"") + kSession + "\"");
  rig.Feed(std::string("{\"ev\":\"match.created\",\"session\":\"") + kSession + "\"," +
           MatchBody(match, "host", "base013", kFriendA, "SauloB") + RelayField(token) + "}");
}

void HostRelayMatch(Rig& rig, const char* match = "m1", uint32_t token = kRelayToken,
                    const std::string& caps = kRelayCaps) {
  SignInRelay(rig, caps);
  CreateRelayMatch(rig, match, token);
}

void JoinRelayMatch(Rig& rig, const char* match = "m2", uint32_t token = kRelayToken) {
  SignInRelay(rig);
  rig.service->SetPlayerMatchSearch(true);
  rig.Tick();
  rig.Reply("pm.search", rig.SearchAnswer());
  CHECK(rig.service->JoinPlayerMatch(kSession));
  rig.Reply("pm.join", std::string("\"session\":\"") + kSession + "\"," +
                           MatchBody(match, "join", "base013", kFriendA, "SauloB") + RelayField(token));
}

std::vector<uint8_t> RelayFrameOf(uint32_t token, const std::vector<uint8_t>& payload, uint32_t seq = 1) {
  return ssf2t::netplay::EncodeRelayFrame(token, seq, payload.data(), payload.size());
}

void FeedBinary(Rig& rig, const std::vector<uint8_t>& frame) {
  rig.Last().in.push_back({ChannelEvent::Kind::kBinary, std::string(frame.begin(), frame.end())});
  rig.Tick(0.01);
}

std::vector<uint8_t> GameReceive(ssf2t::netplay::OnlineGameSocket& socket, ssf2t::netplay::Endpoint& from) {
  uint8_t buffer[2048];
  const size_t size = socket.Receive(buffer, sizeof buffer, from);
  return std::vector<uint8_t>(buffer, buffer + size);
}

const std::vector<uint8_t> kLobbyNine = {7, 1, 2, 3, 4, 5, 6, 7, 8};

// Ticks for `seconds` in small steps, as frames would.
void Run(Rig& rig, double seconds) {
  for (double t = 0; t < seconds; t += 0.1) {
    rig.Tick(0.1);
    WaitForLoopback();
  }
}

void FallBackToRelay(Rig& rig) {
  Run(rig, kDirectProbeWindowSeconds + 0.2);
  CHECK(Contains(rig.LastRequest("match.transport"), "\"transport\":\"relay\""));
  rig.Reply("match.transport");
}

void TestServiceCapabilitiesAreLoggedAndRelayIsOffered() {
  UdpRig udp;
  {
    Rig rig(kGuestBase013, false, kToken, &udp);
    SignInRelay(rig);
    CHECK(Contains(rig.LastRequest("hello"), "\"relay\":true"));
    CHECK(rig.CountLogs("online service capabilities protocol=3 worker_build=test-build "
                        "public_endpoints=true relay_supported=true") == 1);
    CHECK(rig.CountLogs("relay capability available=true") == 1);
    CHECK(rig.service->Capabilities().relay_supported);
  }

  Rig legacy(kGuestBase013, false, kToken, &udp);
  SignInRelay(legacy, "");
  CHECK(legacy.CountLogs("online service capabilities protocol=legacy worker_build=legacy "
                         "public_endpoints=unknown relay_supported=false") == 1);
  CHECK(legacy.CountLogs("relay capability available=false") == 1);
  CHECK(!legacy.service->Capabilities().relay_supported);

  Rig signaling_only;  // no gameplay socket: nothing to relay with
  signaling_only.SignIn();
  CHECK(!Contains(signaling_only.LastRequest("hello"), "relay"));
}

// 1 and 7: the punch is answered inside the window, so the match is direct and its datagrams stay on UDP.
void TestDirectWinsWhenThePunchAnswersInTime() {
  UdpRig udp;
  Rig rig(kGuestBase013, false, kToken, &udp);
  HostRelayMatch(rig);
  CHECK(rig.CountLogs("relay available match=m1 role=host relay_available=true") == 1);
  Discover(rig, udp);
  CHECK(rig.CountLogs("direct probe begin match=m1 budget_ms=2500 relay_available=true") == 1);
  CHECK(rig.service->snapshot().match->transport == MatchTransport::kDirectProbing);
  rig.Feed(PeerJson("m1", "join", kWithPublic));
  rig.Tick(0.15);
  WaitForLoopback();
  udp.PeerSends(PunchKind::kAck, 2, PunchKey("m1"));
  WaitForLoopback();
  rig.Tick(0.05);
  const auto assignment = rig.service->Assignment();
  CHECK(assignment && assignment->host && !assignment->relay && assignment->peer_port == 58148);
  CHECK(rig.CountLogs("transport selected=direct match=m1 endpoint=127.0.0.1:58148 type=stun") == 1);
  CHECK(rig.Count("match.transport") == 1 && Contains(rig.LastRequest("match.transport"), "\"transport\":\"direct\""));
  CHECK(rig.service->snapshot().match->transport == MatchTransport::kDirect);

  Run(rig, kDirectProbeWindowSeconds + 1.0);  // the window passing later changes nothing
  CHECK(rig.CountLogs("relay fallback begin") == 0 && rig.Count("match.transport") == 1);
  CHECK(!udp.socket.relay_selected());
  udp.PeerHeard();
  CHECK(udp.socket.Send({0x7F000001, 58148}, kLobbyNine.data(), kLobbyNine.size()));
  CHECK(udp.socket.Send(ssf2t::netplay::kRelayPeer, kLobbyNine.data(), kLobbyNine.size()));
  WaitForLoopback();
  uint8_t data[64];
  ssf2t::netplay::Endpoint from;
  CHECK(udp.peer.Receive(data, sizeof data, from) == 9 && from == udp.Game());
  CHECK(rig.Last().binary.empty());
  rig.service->LeaveMatch();
  CHECK(rig.CountLogs("match transport summary match=m1 selected=direct direct_attempted=true") == 1);
  CHECK(rig.CountLogs("relay_available=true relay_used=false") == 1);
}

// 2, 5, 6, 42: nothing answers the punch, the window passes, the host chooses the relay, and gameplay datagrams travel
// as relay frames both ways.
void TestNoDirectPathFallsBackToTheRelay() {
  UdpRig udp;
  Rig rig(kGuestBase013, false, kToken, &udp);
  HostRelayMatch(rig);
  Discover(rig, udp);
  rig.Feed(PeerJson("m1", "join", kWithPublic));
  Run(rig, kDirectProbeWindowSeconds - 0.6);
  CHECK(rig.CountLogs("udp punch tx endpoint=127.0.0.1:58148") == 1);
  CHECK(rig.Count("match.transport") == 0 && !rig.service->Assignment());
  Run(rig, 0.8);
  CHECK(rig.CountLogs("direct path not established elapsed_ms=") == 1);
  CHECK(rig.CountLogs("reason=no_punch_answer") == 1);
  CHECK(rig.CountLogs("relay fallback begin match=m1 direct_elapsed_ms=") == 1);
  CHECK(Contains(rig.LastRequest("match.transport"), "\"match\":\"m1\",\"transport\":\"relay\""));
  CHECK(!rig.service->Assignment());  // the service has not confirmed it yet
  rig.Reply("match.transport");
  CHECK(rig.CountLogs("relay ready match=m1 role=host") == 1);
  CHECK(rig.CountLogs("transport selected=relay match=m1") == 1);
  const auto assignment = rig.service->Assignment();
  CHECK(assignment && assignment->host && assignment->relay && assignment->peer_ip == "127.0.0.2" &&
        assignment->peer_port == ssf2t::netplay::kRelayPeer.port);
  CHECK(rig.service->snapshot().match->transport == MatchTransport::kRelay);
  CHECK(udp.socket.relay_selected());

  // The lobby packet of the real logs (9 bytes) leaves as one relay frame of this match.
  CHECK(udp.socket.Send(ssf2t::netplay::kRelayPeer, kLobbyNine.data(), kLobbyNine.size()));
  CHECK(rig.Last().binary.size() == 1);
  const auto out = ssf2t::netplay::DecodeRelayFrame(rig.Last().binary[0].data(), rig.Last().binary[0].size());
  CHECK(out && out->token == kRelayToken && out->payload == kLobbyNine);

  const std::vector<uint8_t> rollback(40, 0x5A);
  FeedBinary(rig, RelayFrameOf(kRelayToken, rollback));
  ssf2t::netplay::Endpoint from;
  CHECK(GameReceive(udp.socket, from) == rollback && from == ssf2t::netplay::kRelayPeer);
  CHECK(GameReceive(udp.socket, from).empty());
  rig.Tick();
  CHECK(rig.CountLogs("relay tx n=1 bytes=9") == 1 && rig.CountLogs("relay rx n=1 bytes=40") == 1);
  Run(rig, 10.5);
  CHECK(rig.CountLogs("relay stats tx_packets=1 tx_bytes=9 rx_packets=1 rx_bytes=40") >= 1);
  CHECK(rig.service->snapshot().match);  // a relayed match is not timed out by the direct path's own limits
  rig.service->LeaveMatch();
  CHECK(rig.CountLogs("match transport summary match=m1 selected=relay direct_attempted=true") == 1);
  CHECK(rig.CountLogs("relay_available=true relay_used=true tx_packets=1 rx_packets=1") == 1);
  CHECK(!udp.socket.relay_selected());
}

// The real Saulo/Munari failure: the peer's public endpoint never arrives. With a relay that is no longer fatal.
void TestMissingPeerEndpointFallsBackToTheRelay() {
  UdpRig udp;
  Rig rig(kGuestBase013, false, kToken, &udp);
  HostRelayMatch(rig);
  Discover(rig, udp);
  rig.Feed(PeerJson("m1", "join", kNoPublic));
  FallBackToRelay(rig);
  CHECK(rig.CountLogs("reason=peer_endpoint_missing") == 1);
  CHECK(rig.CountLogs("udp punch tx") == 0 && rig.CountLogs("peer_endpoint_timeout") == 0);
  CHECK(rig.service->Assignment() && rig.service->Assignment()->relay && rig.Count("match.leave") == 0);
}

// 3: a service that cannot forward public endpoints but relays goes straight to the relay instead of failing.
void TestNoPublicEndpointsButRelayGoesStraightToTheRelay() {
  {
    UdpRig udp;
    Rig rig(kGuestBase013, false, kToken, &udp);
    HostRelayMatch(rig, "m1", kRelayToken, kRelayNoPublicCaps);
    Discover(rig, udp);
    CHECK(rig.CountLogs("direct unavailable reason=service_public_endpoints_unsupported") == 1);
    CHECK(rig.CountLogs("relay fallback begin match=m1 direct_elapsed_ms=0") == 1);
    CHECK(rig.CountLogs("direct path not established") == 0);
    rig.Feed(PeerJson("m1", "join", kOldServicePeer));
    CHECK(rig.CountLogs("service_capability_mismatch") == 0 && rig.Count("match.leave") == 0);
    rig.Reply("match.transport");
    CHECK(rig.service->Assignment() && rig.service->Assignment()->relay);
  }

  // A peer update without public endpoints from a relayed match's service means the same: no failure.
  UdpRig udp2;
  Rig late(kGuestBase013, false, kToken, &udp2);
  HostRelayMatch(late);
  Discover(late, udp2);
  late.Feed(PeerJson("m1", "join", kOldServicePeer));
  CHECK(late.CountLogs("direct unavailable reason=service_public_endpoints_unsupported") == 1);
  CHECK(late.CountLogs("relay fallback begin match=m1") == 1 && late.Count("match.leave") == 0);
}

// 4: no direct path and no relay: a precise, immediate failure, and the game hears the match is lost.
void TestNoDirectPathAndNoRelayFails() {
  {
    UdpRig udp;
    Rig rig(kGuestBase013, false, kToken, &udp);
    HostRelayMatch(rig, "m1", 0, "");
    CHECK(rig.CountLogs("relay available") == 0);
    Discover(rig, udp);
    CHECK(rig.CountLogs("budget_ms=2500 relay_available=false") == 1);
    rig.Feed(PeerJson("m1", "join", kOldServicePeer));
    CHECK(rig.CountLogs("reason=service_capability_mismatch") == 1);
    CHECK(rig.CountLogs("transport selected=failed match=m1 cause=no_direct_path relay_available=false") ==
          1);
    CHECK(rig.CountLogs("match transport summary match=m1 selected=failed") == 1);
    CHECK(rig.Count("match.leave") == 1 && !rig.service->snapshot().match);
    const auto failed = rig.service->FailedPeer();
    CHECK(failed && failed->match == "m1" && failed->peer_player == kFriendA);
    rig.Tick(31.0);
    CHECK(!rig.service->FailedPeer());
  }

  // A relaying service, but the friend's game is older: the match has no relay and keeps the direct-only rules.
  UdpRig udp2;
  Rig older(kGuestBase013, false, kToken, &udp2);
  HostRelayMatch(older, "m1", 0);
  CHECK(older.CountLogs("relay available=false match=m1 reason=peer_without_relay") == 1);
  Discover(older, udp2);
  older.Feed(PeerJson("m1", "join", kNoPublic));
  Run(older, kDirectProbeWindowSeconds + 1.0);
  CHECK(older.Count("match.transport") == 0 && older.service->snapshot().match);
  Run(older, 8.0);
  CHECK(older.CountLogs("reason=peer_endpoint_timeout") == 1 && !older.service->snapshot().match);
}

// 8, 9: a frame of another match, or of an earlier one, never reaches the game.
void TestRelayFramesOfOtherMatchesAreIgnored() {
  UdpRig udp;
  Rig rig(kGuestBase013, false, kToken, &udp);
  HostRelayMatch(rig);
  Discover(rig, udp);
  FallBackToRelay(rig);
  ssf2t::netplay::Endpoint from;
  FeedBinary(rig, RelayFrameOf(kRelayToken ^ 0x10, kLobbyNine));
  CHECK(GameReceive(udp.socket, from).empty());
  CHECK(udp.socket.traffic().relay_rejected == 1);

  rig.service->LeaveMatch();
  CreateRelayMatch(rig, "m3", kRelayToken + 1);
  Discover(rig, udp);
  FallBackToRelay(rig);
  CHECK(rig.service->Assignment() && rig.service->Assignment()->generation == 2);
  FeedBinary(rig, RelayFrameOf(kRelayToken, kLobbyNine));
  CHECK(GameReceive(udp.socket, from).empty());
  FeedBinary(rig, RelayFrameOf(kRelayToken + 1, kLobbyNine));
  CHECK(GameReceive(udp.socket, from) == kLobbyNine);
}

// 10: losing the relay is a clean failure: the match is left, nothing pretends the peer is still there.
void TestRelayLossEndsTheMatchCleanly() {
  for (const char* how : {"relay.closed", "socket", "overflow"}) {
    UdpRig udp;
    Rig rig(kGuestBase013, false, kToken, &udp);
    HostRelayMatch(rig);
    Discover(rig, udp);
    FallBackToRelay(rig);
    CHECK(rig.service->Assignment());
    const std::string which = how;
    if (which == "relay.closed") {
      rig.Feed("{\"ev\":\"relay.closed\",\"match\":\"m1\",\"reason\":\"peer_disconnected\"}");
      CHECK(rig.CountLogs("relay closed reason=peer_disconnected match=m1") == 1);
      CHECK(rig.CountLogs("transport lost reason=relay_closed") == 1);
      CHECK(rig.Count("match.leave") == 1);
    } else if (which == "socket") {
      rig.Event(ChannelEvent::Kind::kClosed, "timeout");
      CHECK(rig.CountLogs("relay closed reason=service_disconnected match=m1") == 1);
    } else {
      for (uint32_t i = 0; i < 300; ++i) {
        rig.Last().in.push_back({ChannelEvent::Kind::kBinary, [&] {
          const std::vector<uint8_t> f = RelayFrameOf(kRelayToken, kLobbyNine, i);
          return std::string(f.begin(), f.end());
        }()});
      }
      rig.Tick(0.01);
      CHECK(rig.CountLogs("relay closed reason=receive_overflow match=m1") == 1);
      CHECK(rig.Count("match.leave") == 1);
    }
    CHECK(!rig.service->snapshot().match && !rig.service->Assignment());
    CHECK(!udp.socket.relay_selected());
    CHECK(rig.service->FailedPeer());
    CHECK(rig.CountLogs("match transport summary match=m1 selected=relay") == 1);
    CHECK(Contains(rig.service->snapshot().notice, which == "socket" ? "Reconnecting" : "was lost"));
  }
}

// 11: Find Match: the joiner never chooses, it follows the host's relay choice.
void TestFindMatchJoinerFollowsTheRelayChoice() {
  UdpRig udp;
  Rig rig(kGuestBase013, false, kToken, &udp);
  JoinRelayMatch(rig);
  CHECK(rig.CountLogs("relay available match=m2 role=join relay_available=true") == 1);
  Discover(rig, udp);
  rig.Feed(PeerJson("m2", "host", kNoPublic));
  Run(rig, 8.0);
  CHECK(rig.Count("match.transport") == 0 && !rig.service->Assignment() && rig.Count("match.leave") == 0);
  CHECK(rig.CountLogs("peer_endpoint_timeout") == 0);
  rig.Feed("{\"ev\":\"match.transport\",\"match\":\"m0\",\"transport\":\"relay\"}");
  CHECK(!rig.service->Assignment());
  rig.Feed("{\"ev\":\"match.transport\",\"match\":\"m2\",\"transport\":\"relay\"}");
  CHECK(rig.CountLogs("transport choice from host transport=relay") == 1);
  CHECK(rig.CountLogs("reason=host_chose_relay") == 1 && rig.CountLogs("relay fallback begin match=m2") == 1);
  CHECK(rig.CountLogs("relay ready match=m2 role=join") == 1);
  const auto assignment = rig.service->Assignment();
  CHECK(assignment && !assignment->host && assignment->relay);
  FeedBinary(rig, RelayFrameOf(kRelayToken, kLobbyNine));
  ssf2t::netplay::Endpoint from;
  CHECK(GameReceive(udp.socket, from) == kLobbyNine && from == ssf2t::netplay::kRelayPeer);
  CHECK(rig.Count("match.transport") == 0);
}

// The host heard this joiner's answer and chose direct: the joiner uses the endpoint the host's punches came from.
void TestJoinerFollowsTheDirectChoice() {
  UdpRig udp;
  Rig rig(kGuestBase013, false, kToken, &udp);
  JoinRelayMatch(rig);
  Discover(rig, udp);
  rig.Feed(PeerJson("m2", "host", kWithPublic));
  udp.PeerSends(PunchKind::kPunch, 1, PunchKey("m2"));
  WaitForLoopback();
  rig.Tick(0.05);
  CHECK(!rig.service->Assignment());
  udp.PeerSends(PunchKind::kAck, 1, PunchKey("m2"));  // its own proof alone does not decide either
  WaitForLoopback();
  rig.Tick(0.05);
  CHECK(!rig.service->Assignment());
  rig.Feed("{\"ev\":\"match.transport\",\"match\":\"m2\",\"transport\":\"direct\"}");
  const auto assignment = rig.service->Assignment();
  CHECK(assignment && !assignment->relay && assignment->peer_ip == "127.0.0.1" && assignment->peer_port == 58148);
  CHECK(rig.CountLogs("transport selected=direct match=m2") == 1);
  CHECK(!udp.socket.relay_selected());
}

// 12: Invite takes the same path after its join.
void TestInvitedJoinerFollowsTheRelayChoice() {
  UdpRig udp;
  Rig rig(kGuestBase013, false, kToken, &udp);
  SignInRelay(rig);
  rig.Feed(InviteEvent("i1"));
  CHECK(rig.service->AcceptInvite("i1"));
  CHECK(rig.service->JoinPlayerMatch(kSession));
  CHECK(rig.Count("pm.join") == 1);
  rig.Reply("pm.join", std::string("\"session\":\"") + kSession + "\"," +
                           MatchBody("m4", "join", "base013", kFriendA, "SauloB") + RelayField(kRelayToken));
  CHECK(rig.CountLogs("invite transition consumed generation=1") == 1);
  Discover(rig, udp);
  rig.Feed("{\"ev\":\"match.transport\",\"match\":\"m4\",\"transport\":\"relay\"}");
  CHECK(rig.service->Assignment() && rig.service->Assignment()->relay);
  CHECK(rig.Count("pm.join") == 1);
}

// 13, 14, 15: after a relay match, whatever came before is gone: a retry relays again, and a direct match never
// touches the relay.
void TestEveryMatchAfterARelayMatchStartsClean() {
  UdpRig udp;
  Rig rig(kGuestBase013, false, kToken, &udp);
  HostRelayMatch(rig);
  Discover(rig, udp);
  FallBackToRelay(rig);
  CHECK(udp.socket.Send(ssf2t::netplay::kRelayPeer, kLobbyNine.data(), kLobbyNine.size()));
  rig.Feed("{\"ev\":\"relay.closed\",\"match\":\"m1\",\"reason\":\"rate_limit\"}");
  CHECK(!rig.service->snapshot().match);

  CreateRelayMatch(rig, "m5", kRelayToken + 5);
  CHECK(!rig.service->Assignment() && !udp.socket.relay_selected());
  CHECK(rig.service->snapshot().match->transport == MatchTransport::kDirectProbing);
  CHECK(udp.socket.traffic().relay_tx_packets == 0);
  Discover(rig, udp);
  FallBackToRelay(rig);
  CHECK(rig.service->Assignment() && rig.service->Assignment()->relay && rig.service->Assignment()->generation == 2);
  rig.service->LeaveMatch();

  const size_t frames = rig.Last().binary.size();
  CreateRelayMatch(rig, "m6", kRelayToken + 6);
  Discover(rig, udp);
  rig.Feed(PeerJson("m6", "join", kWithPublic));
  rig.Tick(0.15);
  WaitForLoopback();
  udp.PeerSends(PunchKind::kAck, 2, PunchKey("m6"));
  WaitForLoopback();
  rig.Tick(0.05);
  const auto direct = rig.service->Assignment();
  CHECK(direct && !direct->relay && direct->generation == 3);
  CHECK(!udp.socket.relay_selected());
  CHECK(udp.socket.Send({0x7F000001, 58148}, kLobbyNine.data(), kLobbyNine.size()));
  CHECK(udp.socket.Send(ssf2t::netplay::kRelayPeer, kLobbyNine.data(), kLobbyNine.size()));
  CHECK(rig.Last().binary.size() == frames);
  FeedBinary(rig, RelayFrameOf(kRelayToken + 6, kLobbyNine));
  ssf2t::netplay::Endpoint from;
  for (std::vector<uint8_t> got = GameReceive(udp.socket, from); !got.empty(); got = GameReceive(udp.socket, from)) {
    CHECK(!(from == ssf2t::netplay::kRelayPeer));
  }
}

// A refused relay choice (the relay went away in between) fails the match instead of leaving it waiting.
void TestRefusedRelayChoiceFailsTheMatch() {
  UdpRig udp;
  Rig rig(kGuestBase013, false, kToken, &udp);
  HostRelayMatch(rig);
  Discover(rig, udp);
  Run(rig, kDirectProbeWindowSeconds + 0.2);
  rig.Reply("match.transport", {}, "relay_unavailable");
  CHECK(rig.CountLogs("relay fallback refused code=relay_unavailable") == 1);
  CHECK(rig.CountLogs("transport selected=failed match=m1 cause=relay_unavailable") == 1);
  CHECK(!rig.service->snapshot().match && !rig.service->Assignment() && rig.Count("match.leave") == 1);
}

// ---- Network establishment budget: one absolute 15 s clock per match, the game's lobby wait follows it ----
void AdvanceTo(Rig& rig, double at) {
  while (rig.now < at - 1e-9) rig.Tick(std::min(0.25, at - rig.now));
}

// The match's clock starts when the service creates it; the tests measure from there.
struct Joiner {
  UdpRig udp;
  Rig rig;
  double t0 = 0;
  explicit Joiner(uint32_t token = kRelayToken, bool invite = false) : rig(kGuestBase013, false, kToken, &udp) {
    if (invite) {
      SignInRelay(rig);
      rig.Feed(InviteEvent("i1"));
      CHECK(rig.service->AcceptInvite("i1"));
      CHECK(rig.service->JoinPlayerMatch(kSession));
      t0 = rig.now;
      rig.Reply("pm.join", std::string("\"session\":\"") + kSession + "\"," +
                               MatchBody("m4", "join", "base013", kFriendA, "SauloB") + RelayField(token));
    } else {
      SignInRelay(rig);
      rig.service->SetPlayerMatchSearch(true);
      rig.Tick();
      rig.Reply("pm.search", rig.SearchAnswer());
      CHECK(rig.service->JoinPlayerMatch(kSession));
      t0 = rig.now;
      rig.Reply("pm.join", std::string("\"session\":\"") + kSession + "\"," +
                               MatchBody("m2", "join", "base013", kFriendA, "SauloB") + RelayField(token));
    }
    Discover(rig, udp);
  }
  const char* Match() const { return rig.CountLogs("match=m4") ? "m4" : "m2"; }
  void HostChooses(const char* transport) {
    rig.Feed(std::string("{\"ev\":\"match.transport\",\"match\":\"") + Match() + "\",\"transport\":\"" + transport + "\"}");
  }
  GuestWait Wait() { return rig.service->GuestWaitState(); }
  double Elapsed() const { return rig.now - t0; }
};

// One program hosts a match, leaves it and joins the next as Player 2, on either game image: nothing of the host
// role, its relay route or its generation carries over.
void TestHostThenJoinWithoutRestartReversesRoles() {
  for (const char* guest : {kGuestBase013, kGuestTu023}) {
    UdpRig udp;
    Rig rig(guest, false, kToken, &udp);
    SignInRelay(rig);
    rig.service->HostPlayerMatch({{"rounds", 2}});
    rig.Tick();
    rig.Reply("pm.host", std::string("\"session\":\"") + kSession + "\"");
    rig.Feed(std::string("{\"ev\":\"match.created\",\"session\":\"") + kSession + "\"," +
             MatchBody("m1", "host", guest, kFriendA, "SauloB") + RelayField(kRelayToken) + "}");
    Discover(rig, udp);
    FallBackToRelay(rig);
    const auto hosted = rig.service->Assignment();
    CHECK(hosted && hosted->host && hosted->relay && hosted->guest == guest);
    rig.service->LeaveMatch();
    CHECK(!rig.service->Assignment() && !rig.service->snapshot().match);

    rig.service->SetPlayerMatchSearch(true);
    rig.Tick();
    rig.Reply("pm.search", rig.SearchAnswer());
    CHECK(rig.service->JoinPlayerMatch(kSession));
    rig.Reply("pm.join", std::string("\"session\":\"") + kSession + "\"," +
                             MatchBody("m2", "join", guest, kFriendA, "SauloB") + RelayField(kRelayToken + 1));
    Discover(rig, udp);
    rig.Feed(PeerJson("m2", "host", kNoPublic));
    rig.Feed("{\"ev\":\"match.transport\",\"match\":\"m2\",\"transport\":\"relay\"}");
    const auto joined = rig.service->Assignment();
    CHECK(joined && !joined->host && joined->relay && joined->guest == guest);
    CHECK(hosted && joined && joined->generation == hosted->generation + 1);

    ssf2t::netplay::Endpoint from;
    FeedBinary(rig, RelayFrameOf(kRelayToken, kLobbyNine));
    CHECK(GameReceive(udp.socket, from).empty());
    FeedBinary(rig, RelayFrameOf(kRelayToken + 1, kLobbyNine));
    CHECK(GameReceive(udp.socket, from) == kLobbyNine && from == ssf2t::netplay::kRelayPeer);
  }
}

void TestEstablishmentBudgetModel() {
  Establishment e;
  CHECK(e.phase() == Establishment::Phase::kIdle && !e.GuestWaitExpired(100) && !e.ConnectingExpired(100));
  e.Begin(100);
  CHECK(e.phase() == Establishment::Phase::kConnecting && e.AbsoluteDeadline() == 115);
  CHECK(!e.ConnectingExpired(114.9) && e.ConnectingExpired(115.0));
  CHECK(e.Select(103) && !e.Select(110) && e.phase() == Establishment::Phase::kSelected);
  CHECK(e.HandshakeDeadline() == 113 && !e.GuestWaitExpired(112.9) && e.GuestWaitExpired(113.0));
  CHECK(!e.ConnectingExpired(200));  // selected: the lobby's own wait is bounded, not the connecting phase
  CHECK(e.Fail(104, "relay_closed") && !e.Fail(105, "other") && e.reason() == "relay_closed");
  CHECK(e.GuestWaitExpired(104) && !e.Select(104));

  // The relay budget is 10 s after selection, never past the absolute 15 s.
  const std::vector<std::pair<double, double>> cases = {{3.0, 13.0}, {5.0, 15.0}, {7.5, 15.0}, {14.0, 15.0}};
  for (const auto& [selected, deadline] : cases) {
    Establishment late;
    late.Begin(0);
    late.Select(selected);
    CHECK(late.HandshakeDeadline() == deadline);
  }
  Establishment fresh;
  fresh.Begin(50);
  CHECK(fresh.AbsoluteDeadline() == 65 && fresh.reason().empty() && fresh.Elapsed(52) == 2);
  fresh.Reset();
  CHECK(fresh.phase() == Establishment::Phase::kIdle);
}

void TestGameWaitTimerOnlyChangesTheLobbyWait() {
  CHECK(WaitTimerExpired(5, GuestWait::kFailed, false) == false);       // another op: the game's answer
  CHECK(WaitTimerExpired(5, GuestWait::kProgressing, true) == true);
  CHECK(WaitTimerExpired(kWaitForPlayerListOp, GuestWait::kUnmanaged, true) == true);   // not an online match
  CHECK(WaitTimerExpired(kWaitForPlayerListOp, GuestWait::kUnmanaged, false) == false);
  CHECK(WaitTimerExpired(kWaitForPlayerListOp, GuestWait::kProgressing, true) == false);  // its 10 s is not the limit
  CHECK(WaitTimerExpired(kWaitForPlayerListOp, GuestWait::kProgressing, false) == false);
  CHECK(WaitTimerExpired(kWaitForPlayerListOp, GuestWait::kFailed, false) == true);       // known failure: now
}

// 1: Direct wins at once; nothing waits for a deadline and no relay is involved.
void TestDirectAt500msIsReadyAtOnce() {
  Joiner j;
  CHECK(j.Wait() == GuestWait::kProgressing);
  j.rig.Feed(PeerJson("m2", "host", kWithPublic));
  AdvanceTo(j.rig, j.t0 + 0.5);
  j.udp.PeerSends(PunchKind::kPunch, 1, PunchKey("m2"));
  WaitForLoopback();
  j.rig.Tick(0.05);
  j.HostChooses("direct");
  const auto assignment = j.rig.service->Assignment();
  CHECK(assignment && !assignment->relay);
  CHECK(j.rig.CountLogs("network establishment ready match=m2 transport=direct elapsed_ms=") == 1);
  CHECK(j.rig.CountLogs("relay fallback begin") == 0 && j.rig.CountLogs("relay establishment begin") == 0);
  CHECK(j.Elapsed() < 2.0);
  j.rig.service->NoteGuestWaitComplete(true);
  CHECK(j.rig.CountLogs("guest wait complete result=connected elapsed_ms=") == 1);
  j.rig.service->NoteGuestWaitComplete(true);
  CHECK(j.rig.CountLogs("guest wait complete") == 1);  // once per match
}

// 3, 4, 5: a relay ready at 3 s, 7.5 s and 14 s still lets the lobby wait finish; none fails early.
void TestRelayReadyAtAnyTimeInsideTheBudgetSucceeds() {
  for (const double ready : {3.0, 7.5, 14.0}) {
    Joiner j;
    AdvanceTo(j.rig, j.t0 + ready - 0.3);
    CHECK(j.Wait() == GuestWait::kProgressing && j.rig.service->snapshot().match);
    AdvanceTo(j.rig, j.t0 + ready);
    j.HostChooses("relay");
    const auto assignment = j.rig.service->Assignment();
    CHECK(assignment && assignment->relay);
    CHECK(j.rig.CountLogs("relay establishment begin match=m2 elapsed_ms=") == 1);
    CHECK(j.rig.CountLogs("network establishment ready match=m2 transport=relay elapsed_ms=") == 1);
    AdvanceTo(j.rig, j.t0 + std::min(ready + 0.5, 14.9));
    CHECK(j.Wait() == GuestWait::kProgressing);
    j.rig.service->NoteGuestWaitComplete(true);
    CHECK(j.rig.CountLogs("guest wait complete result=connected") == 1);
    CHECK(j.rig.CountLogs("network establishment failed") == 0 && j.rig.CountLogs("establishment timeout") == 0);
  }
}

// The relay's remaining budget is logged exactly: 10 s after the selection, but never past the absolute 15 s.
void TestRelayBudgetIsTenSecondsCappedByTheAbsoluteDeadline() {
  {
    Joiner early;
    AdvanceTo(early.rig, early.t0 + 3.0);
    early.HostChooses("relay");
    CHECK(early.rig.CountLogs("relay_budget_remaining_ms=10000 absolute_remaining_ms=1") == 1);
    AdvanceTo(early.rig, early.t0 + 12.9);
    CHECK(early.Wait() == GuestWait::kProgressing);
    AdvanceTo(early.rig, early.t0 + 13.1);
    CHECK(early.Wait() == GuestWait::kFailed);
  }
  Joiner late;
  AdvanceTo(late.rig, late.t0 + 11.0);
  late.HostChooses("relay");
  CHECK(late.rig.CountLogs("relay_budget_remaining_ms=39") + late.rig.CountLogs("relay_budget_remaining_ms=40") == 1);
  AdvanceTo(late.rig, late.t0 + 14.8);
  CHECK(late.Wait() == GuestWait::kProgressing);
  AdvanceTo(late.rig, late.t0 + 15.1);
  CHECK(late.Wait() == GuestWait::kFailed);
  late.rig.service->NoteGuestWaitComplete(false);
  CHECK(late.rig.CountLogs("guest wait complete result=failed reason=handshake_timeout elapsed_ms=15") == 1);
}

// 6: nothing usable by 15 s is a clean timeout at once, reported and handed to the guest.
void TestNothingReadyAtFifteenSecondsIsATerminalTimeout() {
  Joiner j;
  AdvanceTo(j.rig, j.t0 + 14.5);
  CHECK(j.Wait() == GuestWait::kProgressing && j.rig.service->snapshot().match);
  AdvanceTo(j.rig, j.t0 + 15.3);
  CHECK(j.rig.CountLogs("network establishment timeout match=m2 elapsed_ms=15") == 1);
  CHECK(j.rig.CountLogs("transport_state=direct_probing direct_state=waiting_peer relay_state=available") == 1);
  CHECK(j.rig.CountLogs("network establishment failed match=m2 reason=establishment_timeout elapsed_ms=15") == 1);
  CHECK(!j.rig.service->snapshot().match && j.rig.Count("match.leave") == 1);
  CHECK(j.Wait() == GuestWait::kFailed);  // the very next check of the game's wait ends it
  j.rig.service->NoteGuestWaitComplete(false);
  CHECK(j.rig.CountLogs("guest wait complete result=failed reason=establishment_timeout elapsed_ms=15") == 1);
}

// 7, 8: terminal failures reach the guest at once, long before 15 s.
void TestTerminalFailuresDoNotWaitForTheDeadline() {
  {
    Joiner j;
    AdvanceTo(j.rig, j.t0 + 3.0);
    j.HostChooses("relay");
    AdvanceTo(j.rig, j.t0 + 4.0);
    CHECK(j.Wait() == GuestWait::kProgressing);
    j.rig.Event(ChannelEvent::Kind::kClosed, "timeout");
    CHECK(j.Wait() == GuestWait::kFailed);
    CHECK(j.rig.CountLogs("network establishment failed match=m2 reason=service_disconnected elapsed_ms=") == 1);
    CHECK(j.rig.CountLogs("terminal=true") == 1);
    j.rig.service->NoteGuestWaitComplete(false);
    CHECK(j.rig.CountLogs("guest wait complete result=failed reason=service_disconnected elapsed_ms=") == 1);
  }
  {
    Joiner j;
    AdvanceTo(j.rig, j.t0 + 3.0);
    j.HostChooses("relay");
    AdvanceTo(j.rig, j.t0 + 6.0);
    CHECK(j.Wait() == GuestWait::kProgressing);
    j.rig.Feed("{\"ev\":\"relay.closed\",\"match\":\"m2\",\"reason\":\"peer_disconnected\"}");
    CHECK(j.Wait() == GuestWait::kFailed && !j.rig.service->snapshot().match);
    CHECK(j.rig.CountLogs("network establishment failed match=m2 reason=relay_closed elapsed_ms=") == 1);
  }
  {
    // The service connection lost while the transport is still being chosen, with the relay as the only fallback.
    Joiner j;
    AdvanceTo(j.rig, j.t0 + 1.0);
    j.rig.Event(ChannelEvent::Kind::kClosed, "timeout");
    CHECK(j.Wait() == GuestWait::kFailed);
  }
  {
    // The host left, the match ended, the peer's game vanished.
    Joiner j;
    AdvanceTo(j.rig, j.t0 + 2.0);
    j.rig.Feed("{\"ev\":\"match.ended\",\"match\":\"m2\",\"reason\":\"left\"}");
    CHECK(j.Wait() == GuestWait::kFailed);
    CHECK(j.rig.CountLogs("network establishment failed match=m2 reason=match_ended_left") == 1);
  }
  {
    // A relay queue that overflowed.
    Joiner j;
    AdvanceTo(j.rig, j.t0 + 3.0);
    j.HostChooses("relay");
    for (uint32_t i = 0; i < 300; ++i) {
      const std::vector<uint8_t> f = RelayFrameOf(kRelayToken, kLobbyNine, i);
      j.rig.Last().in.push_back({ChannelEvent::Kind::kBinary, std::string(f.begin(), f.end())});
    }
    j.rig.Tick(0.01);
    CHECK(j.Wait() == GuestWait::kFailed);
    CHECK(j.rig.CountLogs("network establishment failed match=m2 reason=relay_receive_overflow") == 1);
  }
}

// 9: no direct path and no relay is known at once, with the game's lobby wait ended at the same moment.
void TestUnreachableWithoutRelayFailsTheGuestImmediately() {
  Joiner j(0);
  CHECK(j.rig.CountLogs("network establishment begin match=m2 role=join deadline_ms=15000") == 1);
  CHECK(j.Wait() == GuestWait::kProgressing);
  AdvanceTo(j.rig, j.t0 + 0.8);
  j.rig.Feed(PeerJson("m2", "host", kOldServicePeer));
  CHECK(j.Wait() == GuestWait::kFailed);
  CHECK(j.rig.CountLogs("network establishment failed match=m2 reason=no_direct_path elapsed_ms=") == 1);
  CHECK(j.Elapsed() < 2.0);
}

// 11, 12, 13, 14: the joiner only follows the host; stale or repeated choices never move a deadline.
void TestJoinerNeverDecidesAndRepeatsChangeNothing() {
  {
    Joiner j;
    AdvanceTo(j.rig, j.t0 + 14.0);
    CHECK(j.rig.Count("match.transport") == 0 && !j.rig.service->Assignment() && !j.udp.socket.relay_selected());
    CHECK(j.rig.CountLogs("relay fallback begin") == 0);
  }
  {
    Joiner stale;
    AdvanceTo(stale.rig, stale.t0 + 9.0);
    stale.rig.Feed("{\"ev\":\"match.transport\",\"match\":\"m0\",\"transport\":\"relay\"}");
    CHECK(!stale.rig.service->Assignment());
    AdvanceTo(stale.rig, stale.t0 + 15.3);
    CHECK(stale.rig.CountLogs("network establishment timeout match=m2") == 1);  // not 9 s + anything
  }

  Joiner twice;
  AdvanceTo(twice.rig, twice.t0 + 3.0);
  twice.HostChooses("relay");
  AdvanceTo(twice.rig, twice.t0 + 9.0);
  twice.HostChooses("relay");
  CHECK(twice.rig.CountLogs("relay ready match=m2") == 1);
  CHECK(twice.rig.CountLogs("network establishment ready") == 1 && twice.rig.CountLogs("transport choice ignored") == 1);
  AdvanceTo(twice.rig, twice.t0 + 13.1);
  CHECK(twice.Wait() == GuestWait::kFailed);  // still 3 s + 10 s, not 9 s + 10 s
}

// 15: peer endpoint updates keep arriving and change nothing about the absolute deadline.
void TestRepeatedPeerEndpointUpdatesDoNotExtendTheDeadline() {
  Joiner j;
  for (int second = 1; second <= 14; ++second) {
    AdvanceTo(j.rig, j.t0 + second);
    const std::string port = std::to_string(58100 + second);
    j.rig.Feed(PeerJson("m2", "host", std::string("\"ip\":\"108.202.118.218\",\"port\":7001,\"lan\":[],\"same_network\":false,"
                                                  "\"public_endpoints\":true,\"public_ip\":\"127.0.0.1\",\"public_port\":") +
                                          port));
  }
  CHECK(j.rig.service->snapshot().match);
  AdvanceTo(j.rig, j.t0 + 15.3);
  CHECK(!j.rig.service->snapshot().match);
  CHECK(j.rig.CountLogs("network establishment timeout match=m2") == 1);
}

// 16, 18: a new match, after a timeout or after leaving, has its own fresh 15 s.
void TestEveryMatchGetsAFreshBudget() {
  Joiner j;
  AdvanceTo(j.rig, j.t0 + 15.3);
  CHECK(!j.rig.service->snapshot().match && j.Wait() == GuestWait::kFailed);

  j.rig.service->SetPlayerMatchSearch(true);
  j.rig.Tick();
  j.rig.Reply("pm.search", j.rig.SearchAnswer());
  CHECK(j.rig.service->JoinPlayerMatch(kSession));
  const double second_start = j.rig.now;
  j.rig.Reply("pm.join", std::string("\"session\":\"") + kSession + "\"," +
                             MatchBody("m7", "join", "base013", kFriendA, "SauloB") + RelayField(kRelayToken + 7));
  CHECK(j.Wait() == GuestWait::kProgressing);  // the earlier failure is not this match
  Discover(j.rig, j.udp);
  AdvanceTo(j.rig, second_start + 14.0);
  CHECK(j.rig.service->snapshot().match && j.Wait() == GuestWait::kProgressing);
  j.rig.service->LeaveMatch();
  CHECK(j.rig.CountLogs("network establishment begin match=m7") == 1);

  j.rig.service->SetPlayerMatchSearch(true);
  j.rig.Tick();
  j.rig.Reply("pm.search", j.rig.SearchAnswer());
  CHECK(j.rig.service->JoinPlayerMatch(kSession));
  const double third_start = j.rig.now;
  j.rig.Reply("pm.join", std::string("\"session\":\"") + kSession + "\"," +
                             MatchBody("m8", "join", "base013", kFriendA, "SauloB") + RelayField(kRelayToken + 8));
  Discover(j.rig, j.udp);
  AdvanceTo(j.rig, third_start + 14.0);
  CHECK(j.rig.service->snapshot().match && j.Wait() == GuestWait::kProgressing);
  AdvanceTo(j.rig, third_start + 15.3);
  CHECK(j.rig.CountLogs("network establishment timeout match=m8") == 1);
}

// 17: fights come and go on a ready match; none of it touches the establishment clock.
void TestFightsDoNotReuseTheEstablishmentTimer() {
  Joiner j;
  AdvanceTo(j.rig, j.t0 + 3.0);
  j.HostChooses("relay");
  for (const NetplayLink link : {NetplayLink::kConnecting, NetplayLink::kConnected, NetplayLink::kFighting,
                                 NetplayLink::kConnected, NetplayLink::kFighting, NetplayLink::kConnected}) {
    j.rig.service->SetNetplayLink(link, 40);
    AdvanceTo(j.rig, j.rig.now + 2.0);
  }
  CHECK(j.rig.CountLogs("network establishment begin") == 1 && j.rig.CountLogs("network establishment ready") == 1);
  CHECK(j.rig.CountLogs("establishment timeout") == 0 && j.rig.service->snapshot().match);
}

// 19, 20: Find Match and Invite both start the budget when their pm.join completes, not before.
void TestFindAndInviteUseTheSameBudgetAfterTheJoin() {
  for (const bool invite : {false, true}) {
    Joiner j(kRelayToken, invite);
    CHECK(j.rig.CountLogs("network establishment begin match=") == 1);
    AdvanceTo(j.rig, j.t0 + 14.0);
    CHECK(j.Wait() == GuestWait::kProgressing);
    AdvanceTo(j.rig, j.t0 + 15.3);
    CHECK(j.Wait() == GuestWait::kFailed && j.rig.CountLogs("network establishment timeout") == 1);
  }
  // The search itself stays at 5 s.
  Rig rig;
  rig.SignInWithFriends();
  const auto wait = rig.service->AskPlayerMatchSearch();
  CHECK(wait.pending);
  rig.Tick(4.9);
  CHECK(rig.service->PlayerMatchSearchPending(wait.epoch));
  rig.Tick(0.2);
  CHECK(!rig.service->PlayerMatchSearchPending(wait.epoch));
}

// A punch that starts late still gets its minimum window, and the relay follows right after; never the old 2.5 + 2.5.
void TestLatePunchGetsOnlyAMinimumWindow() {
  UdpRig udp;
  Rig rig(kGuestBase013, false, kToken, &udp);
  HostRelayMatch(rig);
  Discover(rig, udp);
  const double probe = rig.now;
  AdvanceTo(rig, probe + 2.4);
  rig.Feed(PeerJson("m1", "join", kWithPublic));
  AdvanceTo(rig, probe + 2.4 + kMinPunchSeconds - 0.3);
  CHECK(rig.Count("match.transport") == 0);
  AdvanceTo(rig, probe + 2.4 + kMinPunchSeconds + 0.3);
  CHECK(rig.Count("match.transport") == 1);
  CHECK(rig.CountLogs("relay fallback begin match=m1 direct_elapsed_ms=") == 1);
}

// The host's own lobby wait is never managed, and Online off or no match leaves the game alone.
void TestGuestWaitIsUnmanagedOutsideAJoinedMatch() {
  UdpRig udp;
  Rig rig(kGuestBase013, false, kToken, &udp);
  CHECK(rig.service->GuestWaitState() == GuestWait::kUnmanaged);
  HostRelayMatch(rig);
  CHECK(rig.service->GuestWaitState() == GuestWait::kUnmanaged);
  rig.service->LeaveMatch();
  CHECK(rig.service->GuestWaitState() == GuestWait::kUnmanaged);
  Rig signaling_only;
  signaling_only.SignIn();
  CHECK(signaling_only.service->GuestWaitState() == GuestWait::kUnmanaged);
}

void TestInviteNeedsAnAdvertisedPlayerMatch() {
  Rig rig;
  rig.SignInWithFriends();
  CHECK(!rig.service->SendInvite(kFriendA));
  CHECK(rig.Count("invite.send") == 0);
  rig.service->HostPlayerMatch({{"rounds", 2}});
  rig.Tick();
  CHECK(!rig.service->SendInvite(kFriendA));
  rig.Reply("pm.host", std::string("\"session\":\"") + kSession + "\"");
  CHECK(rig.service->SendInvite(kFriendA));
  CHECK(Contains(rig.LastRequest("invite.send"), std::string("\"player\":\"") + kFriendA + "\""));
  rig.Reply("invite.send", std::string("\"invite\":\"i1\",\"session\":\"") + kSession + "\"");
  CHECK(rig.CountLogs(std::string("player_match invite friend=") + kFriendA) == 1);
  CHECK(rig.CountLogs(std::string("player_match invite sent session=") + kSession) == 1);
}

void TestAcceptedInviteJoinsThatSameSession() {
  Rig rig;
  rig.SignInWithFriends();
  rig.Feed(InviteEvent("i1"));
  const std::vector<Invite> invites = rig.service->Invites();
  CHECK(invites.size() == 1 && invites[0].id == "i1" && invites[0].ad.session == kSession && invites[0].ad.qos == "abcd");
  CHECK(rig.CountLogs(std::string("player_match invite received session=") + kSession) == 1);
  CHECK(rig.service->PlayerMatchResults().empty());
  CHECK(!rig.service->AcceptInvite("nope"));
  const auto accepted = rig.service->AcceptInvite("i1");
  CHECK(accepted && accepted->session == kSession && accepted->player == kFriendA);
  CHECK(rig.service->Invites().empty());
  const std::vector<PlayerMatchAd> joinable = rig.service->PlayerMatchResults();
  CHECK(joinable.size() == 1 && joinable[0].session == kSession && joinable[0].qos == "abcd");
  CHECK(rig.service->JoinPlayerMatch(kSession));
  CHECK(Contains(rig.LastRequest("pm.join"), std::string("\"session\":\"") + kSession + "\""));
  rig.Reply("pm.join", std::string("\"session\":\"") + kSession + "\"," + MatchBody("m3", "join", "base013", kFriendA, "SauloB"));
  CHECK(rig.service->snapshot().match && !rig.service->snapshot().match->host);
  CHECK(rig.service->PlayerMatchResults().empty());
}

void TestDeclinedOrEndedInvitesDisappear() {
  Rig rig;
  rig.SignInWithFriends();
  rig.Feed(InviteEvent("i1"));
  rig.service->DeclineInvite("i1");
  CHECK(Contains(rig.LastRequest("invite.decline"), "\"invite\":\"i1\""));
  CHECK(rig.service->Invites().empty());
  CHECK(rig.CountLogs("player_match invite declined session=") == 1);

  rig.Feed(InviteEvent("i2"));
  rig.Feed(InviteEvent("i3"));
  CHECK(rig.service->Invites().size() == 1 && rig.service->Invites()[0].id == "i3");
  rig.Feed(std::string("{\"ev\":\"invite.ended\",\"invite\":\"i3\",\"session\":\"") + kSession + "\",\"reason\":\"host_left\"}");
  CHECK(rig.service->Invites().empty());

  rig.Feed(InviteEvent("i4"));
  CHECK(rig.service->AcceptInvite("i4"));
  rig.Feed(std::string("{\"ev\":\"invite.ended\",\"invite\":\"i4\",\"session\":\"") + kSession + "\",\"reason\":\"cancelled\"}");
  CHECK(rig.service->PlayerMatchResults().empty());
  CHECK(!rig.service->JoinPlayerMatch(kSession));
}

void TestInviteListOffersOnlyReachableFriends() {
  const auto make = [](const char* player, const char* name, bool online, FriendState state) {
    Friend f;
    f.player = player;
    f.name = name;
    f.online = online;
    f.state = state;
    return f;
  };
  std::vector<Friend> friends = {make("AAAA-2222", "Off", false, FriendState::kOffline),
                                 make("BBBB-3333", "On", true, FriendState::kOnline),
                                 make("CCCC-4444", "Hosting", true, FriendState::kPlayerMatch),
                                 make("DDDD-5555", "Fighting", true, FriendState::kInGame),
                                 make("EEEE-6666", "Other", true, FriendState::kOnline)};
  friends[4].compatible = false;
  const std::vector<InviteChoice> choices = InviteChoices(friends);
  CHECK(choices.size() == 5);
  CHECK(choices[0].name == "On" && choices[0].status == "Online" && choices[0].selectable);
  CHECK(choices[1].name == "Hosting" && choices[1].status == "Online Match" && choices[1].selectable);
  int selectable = 0;
  for (const InviteChoice& c : choices) {
    selectable += c.selectable ? 1 : 0;
    if (c.name == "Off") CHECK(c.status == "Offline" && !c.selectable);
    if (c.name == "Fighting") CHECK(c.status == "In game" && !c.selectable);
    if (c.name == "Other") CHECK(c.status == "Game versions do not match" && !c.selectable);
  }
  CHECK(selectable == 2);
}

std::string SearchReply(int seq, const std::string& sessions) {
  return "{\"re\":" + std::to_string(seq) + ",\"code\":\"ok\"," + sessions + "}";
}

const char kNoSessions[] = "\"sessions\":[]";

// Real test c7a41e95: the first guest search saw an empty cache and completed with zero 150 ms before the service's
// answer, which listed the room. The guest now waits for the answer to its own search.
void TestFindMatchWaitsForTheServiceInsteadOfAnEmptyCache() {
  Rig rig;
  rig.SignInWithFriends();
  const auto ask = rig.service->AskPlayerMatchSearch();
  CHECK(ask.pending && rig.Count("pm.search") == 1);
  CHECK(rig.service->PlayerMatchResults().empty());
  CHECK(rig.service->PlayerMatchSearchPending(ask.epoch));
  rig.Tick(0.15);
  CHECK(rig.service->PlayerMatchSearchPending(ask.epoch) && rig.service->PlayerMatchResults(ask.epoch).empty());

  rig.Reply("pm.search", rig.SearchAnswer());
  CHECK(!rig.service->PlayerMatchSearchPending(ask.epoch));
  const std::vector<PlayerMatchAd> found = rig.service->PlayerMatchResults(ask.epoch);
  CHECK(found.size() == 1 && found[0].session == kSession && found[0].player == kFriendA);
  CHECK(rig.CountLogs("player_match search begin") == 1);
  CHECK(rig.CountLogs("player_match search results=1") == 1);
}

void TestRepeatedGuestCallsShareOneSearch() {
  Rig rig;
  rig.SignInWithFriends();
  const auto first = rig.service->AskPlayerMatchSearch();
  rig.Tick(0.01);
  const auto second = rig.service->AskPlayerMatchSearch();
  rig.Tick(0.01);
  const auto third = rig.service->AskPlayerMatchSearch();
  CHECK(first.pending && second.pending && third.pending);
  CHECK(first.epoch == second.epoch && second.epoch == third.epoch);
  CHECK(rig.Count("pm.search") == 1 && rig.CountLogs("player_match search begin") == 1);
  rig.Reply("pm.search", rig.SearchAnswer());
  for (const auto& call : {first, second, third}) {
    CHECK(!rig.service->PlayerMatchSearchPending(call.epoch) && rig.service->PlayerMatchResults(call.epoch).size() == 1);
  }
  const auto after = rig.service->AskPlayerMatchSearch();
  CHECK(!after.pending && rig.Count("pm.search") == 1);
}

void TestAnAuthoritativeEmptyAnswerEndsTheSearchWithZero() {
  Rig rig;
  rig.SignInWithFriends();
  const auto ask = rig.service->AskPlayerMatchSearch();
  rig.Reply("pm.search", kNoSessions);
  CHECK(!rig.service->PlayerMatchSearchPending(ask.epoch) && rig.service->PlayerMatchResults(ask.epoch).empty());
  rig.Tick(0.2);
  const auto again = rig.service->AskPlayerMatchSearch();
  CHECK(!again.pending && again.epoch == ask.epoch && rig.Count("pm.search") == 1);
}

void TestUnansweredSearchTimesOutAndIsNotPendingForever() {
  Rig rig;
  rig.SignInWithFriends();
  const auto ask = rig.service->AskPlayerMatchSearch();
  rig.Tick(4.0);
  CHECK(rig.service->PlayerMatchSearchPending(ask.epoch));
  rig.Tick(1.2);
  CHECK(!rig.service->PlayerMatchSearchPending(ask.epoch) && rig.service->PlayerMatchResults(ask.epoch).empty());
  CHECK(!rig.service->AskPlayerMatchSearch().pending);
  for (int i = 0; i < 20; ++i) rig.Tick(1.0);
  CHECK(!rig.service->AskPlayerMatchSearch().pending);

  rig.Reply("pm.search", rig.SearchAnswer());  // a late answer is still the current search's
  CHECK(rig.service->PlayerMatchResults(ask.epoch).size() == 1);
}

void TestFailedSearchAnswerEndsTheWait() {
  Rig rig;
  rig.SignInWithFriends();
  const auto ask = rig.service->AskPlayerMatchSearch();
  rig.Reply("pm.search", "", "server_error");
  CHECK(!rig.service->PlayerMatchSearchPending(ask.epoch) && rig.service->PlayerMatchResults(ask.epoch).empty());
  CHECK(rig.CountLogs("player_match search failed code=server_error") == 1);
}

void TestNoConnectionNeverHoldsTheGuest() {
  Rig rig;
  const auto offline = rig.service->AskPlayerMatchSearch();
  CHECK(!offline.pending && rig.channels.empty());
  Rig connecting;
  connecting.Enable();
  connecting.Tick();
  CHECK(!connecting.service->AskPlayerMatchSearch().pending);
}

void TestStaleSearchAnswerNeverFillsALaterSearch() {
  Rig rig;
  rig.SignInWithFriends();
  const auto a = rig.service->AskPlayerMatchSearch();
  const int stale_seq = SeqOf(rig.LastRequest("pm.search"));
  rig.service->SetPlayerMatchSearch(false);
  CHECK(!rig.service->PlayerMatchSearchPending(a.epoch));
  rig.Tick(0.2);
  const auto b = rig.service->AskPlayerMatchSearch();
  CHECK(b.epoch != a.epoch && b.pending && rig.Count("pm.search") == 2);

  rig.Feed(SearchReply(stale_seq, rig.SearchAnswer()));
  CHECK(rig.CountLogs("player_match search ignored stale") == 1);
  CHECK(rig.service->PlayerMatchResults().empty());
  CHECK(rig.service->PlayerMatchSearchPending(b.epoch));
  CHECK(rig.service->PlayerMatchResults(a.epoch).empty());

  rig.Reply("pm.search", kNoSessions);
  CHECK(!rig.service->PlayerMatchSearchPending(b.epoch) && rig.service->PlayerMatchResults(b.epoch).empty());
}

void TestNewFindMatchAfterAnEmptySearchAsksAgain() {
  Rig rig;
  rig.SignInWithFriends();
  const auto first = rig.service->AskPlayerMatchSearch();
  rig.Reply("pm.search", kNoSessions);
  CHECK(!rig.service->PlayerMatchSearchPending(first.epoch));

  rig.Tick(1.5);
  const auto again = rig.service->AskPlayerMatchSearch();
  CHECK(again.pending && rig.Count("pm.search") == 2);
  CHECK(again.epoch == first.epoch && rig.CountLogs("player_match search begin") == 1);
  rig.Reply("pm.search", rig.SearchAnswer());
  CHECK(!rig.service->PlayerMatchSearchPending(again.epoch) && rig.service->PlayerMatchResults(again.epoch).size() == 1);

  rig.service->SetPlayerMatchSearch(false);
  const auto restarted = rig.service->AskPlayerMatchSearch();
  CHECK(restarted.pending && restarted.epoch != first.epoch && rig.service->PlayerMatchResults(restarted.epoch).empty());
}

void TestFoundMatchJoinsExactlyOnce() {
  Rig rig;
  rig.SignInWithFriends();
  const auto ask = rig.service->AskPlayerMatchSearch();
  rig.Reply("pm.search", rig.SearchAnswer());
  const int answered = SeqOf(rig.LastRequest("pm.search"));
  rig.Feed(SearchReply(answered, rig.SearchAnswer()));  // the same answer again
  CHECK(rig.service->PlayerMatchResults(ask.epoch).size() == 1);

  CHECK(rig.service->JoinPlayerMatch(kSession));
  CHECK(!rig.service->JoinPlayerMatch(kSession));
  CHECK(rig.Count("pm.join") == 1 && rig.CountLogs("player_match join begin") == 1);
  rig.Feed(SearchReply(SeqOf(rig.LastRequest("pm.search")), rig.SearchAnswer()));
  CHECK(rig.Count("pm.join") == 1);
  CHECK(!rig.service->PlayerMatchSearchPending(ask.epoch) && rig.service->PlayerMatchResults(ask.epoch).empty());

  rig.Reply("pm.join", std::string("\"session\":\"") + kSession + "\"," + MatchBody("m2", "join", "base013", kFriendA, "SauloB"));
  CHECK(rig.service->snapshot().match && rig.CountLogs("online match created role=join") == 1);
  CHECK(rig.service->PlayerMatchResults(ask.epoch).empty() && !rig.service->snapshot().player_match.searching);
}

void TestLeavingWhileASearchIsPendingCancelsIt() {
  Rig rig;
  rig.SignInWithFriends();
  const auto ask = rig.service->AskPlayerMatchSearch();
  CHECK(ask.pending);
  const int seq = SeqOf(rig.LastRequest("pm.search"));
  rig.service->SetPlayerMatchSearch(false);
  CHECK(!rig.service->PlayerMatchSearchPending(ask.epoch) && !rig.service->snapshot().player_match.searching);
  rig.Feed(SearchReply(seq, rig.SearchAnswer()));
  CHECK(rig.service->PlayerMatchResults().empty() && rig.service->PlayerMatchResults(ask.epoch).empty());
  rig.Tick(10.0);
  CHECK(rig.Count("pm.search") == 1);
}

void TestInviteAndCreateMatchDoNotWaitOnTheSearch() {
  Rig invited;
  invited.SignInWithFriends();
  invited.Feed(InviteEvent("i1"));
  CHECK(invited.service->AcceptInvite("i1"));
  const auto ask = invited.service->AskPlayerMatchSearch();
  CHECK(!ask.pending);
  const std::vector<PlayerMatchAd> listed = invited.service->PlayerMatchResults(ask.epoch);
  CHECK(listed.size() == 1 && listed[0].session == kSession);
  CHECK(invited.service->JoinPlayerMatch(kSession) && invited.Count("pm.join") == 1);

  Rig host;
  host.SignInWithFriends();
  host.service->HostPlayerMatch({{"rounds", 2}});
  host.Tick();
  host.Reply("pm.host", std::string("\"session\":\"") + kSession + "\"");
  CHECK(host.Count("pm.search") == 0 && host.Count("pm.host") == 1);
  CHECK(host.service->snapshot().player_match.hosting && !host.service->snapshot().player_match.searching);
}

// Real test c7a41e95: a Worker that predates public endpoints answered both players with public_endpoints absent, and
// each waited the full 10 s. Over the Internet that can never be satisfied, so it is said at once.
void TestOutdatedServiceFailsFastWithItsOwnReason() {
  for (const bool host : {true, false}) {
    UdpRig udp;
    Rig rig(kGuestBase013, false, kToken, &udp);
    const char* match = host ? "m1" : "m2";
    if (host) rig.StartPlayerMatchAsHost();
    else JoinAsPlayerTwo(rig, match);
    Discover(rig, udp);
    rig.Feed(PeerJson(match, host ? "join" : "host",
                      "\"ip\":\"108.202.118.218\",\"port\":7001,\"lan\":[],\"same_network\":false"));
    CHECK(rig.CountLogs("peer endpoint update raw=108.202.118.218:7001 public=none service_forwards_public=false") == 1);
    CHECK(rig.CountLogs("udp rendezvous failed reason=service_capability_mismatch "
                        "worker_public_endpoints_unsupported=true") == 1);
    CHECK(rig.CountLogs("udp rendezvous begin") == 0 && rig.CountLogs("udp punch tx") == 0);
    CHECK(!rig.service->snapshot().match && rig.Count("match.leave") == 1);
    CHECK(Contains(rig.service->snapshot().notice, "online service is out of date"));
    for (int i = 0; i < 12; ++i) rig.Tick(1.0);
    CHECK(rig.CountLogs("peer_endpoint_timeout") == 0);
  }
}

// The same missing endpoint from a service that does forward them keeps waiting, and the same network never needs one.
void TestCurrentServiceWithoutAnEndpointYetKeepsWaiting() {
  UdpRig udp;
  Rig rig(kGuestBase013, false, kToken, &udp);
  rig.StartPlayerMatchAsHost();
  Discover(rig, udp);
  rig.Feed(PeerJson("m1", "join", kNoPublic));
  CHECK(rig.CountLogs("service_capability_mismatch") == 0 && rig.service->snapshot().match);
  rig.Feed(PeerJson("m1", "join", kWithPublic));
  CHECK(rig.CountLogs("udp rendezvous begin candidates=2") == 1);
}

std::string InviteJoinBody(const char* match) {
  return std::string("\"session\":\"") + kSession + "\"," + MatchBody(match, "join", "base013", kFriendA, "SauloB");
}

// Real test 31b7d4e8: accepting an invite makes the game leave its lobby and re-enter the Online Match zone before it
// joins. The accepted invite lives through all of that and is spent by the one join.
void TestAcceptedInviteSurvivesTheGuestTeardownAndJoinsOnce() {
  Rig rig;
  rig.SignInWithFriends();
  rig.Feed(InviteEvent("i1"));
  CHECK(rig.service->Invites().size() == 1 && rig.service->AcceptedInviteGeneration() == 0);
  CHECK(rig.service->AcceptInvite("i1") && rig.service->AcceptedInviteGeneration() == 1);
  CHECK(!rig.service->AcceptInvite("i1") && rig.service->AcceptedInviteGeneration() == 1);
  CHECK(rig.CountLogs("player_match invite accepted session=") == 1);

  rig.service->CancelPlayerMatch();
  rig.service->SetPlayerMatchSearch(false);
  rig.Tick(0.5);
  CHECK(rig.service->PlayerMatchResults().size() == 1 && rig.CountLogs("invite transition failed") == 0);
  const auto ask = rig.service->AskPlayerMatchSearch();
  CHECK(!ask.pending && rig.service->PlayerMatchResults(ask.epoch).size() == 1);

  CHECK(rig.service->JoinPlayerMatch(kSession));
  for (int i = 0; i < 10; ++i) rig.Tick(0.2);
  CHECK(!rig.service->JoinPlayerMatch(kSession));
  CHECK(rig.Count("pm.join") == 1 && rig.CountLogs("player_match join begin") == 1);
  CHECK(rig.CountLogs("invite transition consumed") == 0);

  rig.Reply("pm.join", InviteJoinBody("m3"));
  CHECK(rig.CountLogs("player_match join service complete") == 1);
  CHECK(rig.CountLogs("invite transition consumed generation=1") == 1);
  CHECK(rig.CountLogs("online match created role=join") == 1);
  CHECK(rig.service->PlayerMatchResults().empty() && rig.service->snapshot().match);
}

void TestRejectedInviteJoinEndsOnlyThatInvite() {
  Rig rig;
  rig.SignInWithFriends();
  rig.Feed(InviteEvent("i1"));
  CHECK(rig.service->AcceptInvite("i1") && rig.service->JoinPlayerMatch(kSession));
  rig.Reply("pm.join", "", "not_found");
  CHECK(rig.CountLogs("invite transition failed generation=1 reason=join_rejected_not_found") == 1);
  CHECK(rig.service->PlayerMatchResults().empty() && !rig.service->snapshot().match);

  rig.Feed(InviteEvent("i2"));
  CHECK(rig.service->AcceptInvite("i2") && rig.service->AcceptedInviteGeneration() == 2);
  CHECK(rig.service->JoinPlayerMatch(kSession) && rig.Count("pm.join") == 2);
  rig.Reply("pm.join", InviteJoinBody("m3"));
  CHECK(rig.CountLogs("invite transition consumed generation=2") == 1);
}

void TestEndedOrReplacedInviteFailsItsGeneration() {
  Rig rig;
  rig.SignInWithFriends();
  rig.Feed(InviteEvent("i1"));
  CHECK(rig.service->AcceptInvite("i1"));
  rig.Feed(std::string("{\"ev\":\"invite.ended\",\"invite\":\"i1\",\"session\":\"") + kSession + "\",\"reason\":\"closed\"}");
  CHECK(rig.CountLogs("invite transition failed generation=1 reason=invite_ended_closed") == 1);
  CHECK(rig.service->PlayerMatchResults().empty());

  rig.Feed(InviteEvent("i2"));
  rig.Feed(InviteEvent("i3", "aabbccddeeff0011"));
  CHECK(rig.service->AcceptInvite("i2") && rig.service->AcceptInvite("i3"));
  CHECK(rig.CountLogs("invite transition failed generation=2 reason=replaced") == 1);
  const std::vector<PlayerMatchAd> ads = rig.service->PlayerMatchResults();
  CHECK(ads.size() == 1 && ads[0].session == "aabbccddeeff0011");
}

// The stale Worker of real test 31b7d4e8: the invite joined, then match.peer came without public endpoints. It is
// told at once, not after the STUN lookup or the 10 s wait, and the player can invite, host or search again.
void TestOutdatedServiceAfterAnInviteJoinFailsAtOnceAndLeavesAFreshStart() {
  UdpRig udp;
  Rig rig(kGuestBase013, false, kToken, &udp);
  rig.SignInWithFriends();
  rig.Feed(InviteEvent("i1"));
  CHECK(rig.service->AcceptInvite("i1") && rig.service->JoinPlayerMatch(kSession));
  rig.Reply("pm.join", InviteJoinBody("m3"));
  CHECK(rig.CountLogs("invite transition consumed generation=1") == 1);

  rig.Feed(PeerJson("m3", "host", "\"ip\":\"177.18.65.229\",\"port\":7001,\"lan\":[],\"same_network\":false"));
  CHECK(rig.CountLogs("join network capability service_forwards_public=false action=fail_fast") == 1);
  CHECK(rig.CountLogs("udp rendezvous failed reason=service_capability_mismatch "
                      "worker_public_endpoints_unsupported=true") == 1);
  CHECK(rig.CountLogs("udp rendezvous failed reason=no_direct_path") == 1 && rig.Count("match.leave") == 1);
  CHECK(!rig.service->snapshot().match && !rig.service->Assignment());
  for (int i = 0; i < 12; ++i) {
    rig.Tick(1.0);
    WaitForLoopback();
    udp.Serve();
  }
  CHECK(rig.CountLogs("peer_endpoint_timeout") == 0 && rig.CountLogs("udp rendezvous begin") == 0);
  CHECK(Contains(rig.service->snapshot().notice, "online service is out of date"));

  rig.Feed(InviteEvent("i2"));
  CHECK(rig.service->AcceptInvite("i2") && rig.service->AcceptedInviteGeneration() == 2);
  CHECK(rig.service->JoinPlayerMatch(kSession) && rig.Count("pm.join") == 2);
  rig.Reply("pm.join", InviteJoinBody("m4"));
  CHECK(rig.service->snapshot().match && rig.service->snapshot().match->peer_player == kFriendA);
  rig.service->LeaveMatch();
  rig.Tick();

  rig.service->HostPlayerMatch({{"rounds", 2}});
  rig.Tick();
  CHECK(rig.Count("pm.host") >= 1);
  const auto ask = rig.service->AskPlayerMatchSearch();
  CHECK(ask.pending && rig.Count("pm.search") >= 1);
}

// A peer on the same network has no use for a public endpoint, and a service that forwards them keeps the wait.
void TestCapabilityCheckSparesSameNetworkAndCurrentServices() {
  UdpRig udp;
  Rig rig(kGuestBase013, false, kToken, &udp);
  rig.StartPlayerMatchAsHost();
  rig.Feed(PeerJson("m1", "join", "\"ip\":\"203.0.113.9\",\"port\":58148,\"lan\":[\"127.0.0.1\"],\"same_network\":true"));
  CHECK(rig.CountLogs("service_capability_mismatch") == 0 && rig.service->snapshot().match);
  Discover(rig, udp);
  CHECK(rig.CountLogs("udp rendezvous begin") == 1);

  UdpRig udp2;
  Rig current(kGuestBase013, false, kToken, &udp2);
  current.StartPlayerMatchAsHost();
  current.Feed(PeerJson("m1", "join", kNoPublic));
  CHECK(current.CountLogs("service_capability_mismatch") == 0 && current.service->snapshot().match);
  Discover(current, udp2);
  for (int i = 0; i < 9; ++i) current.Tick(1.0);
  CHECK(current.service->snapshot().match && current.CountLogs("peer_endpoint_timeout") == 0);
  for (int i = 0; i < 3; ++i) current.Tick(1.0);
  CHECK(current.CountLogs("reason=peer_endpoint_timeout peer_signaled=true service_forwards_public=true") == 1);
}

// ---- secret characters ----

std::string FlagsJson(const SecretSet& set) {
  return std::string("{\"akuma\":") + (set.akuma ? "true" : "false") + ",\"evil_ryu\":" + (set.evil_ryu ? "true" : "false") + "}";
}

std::string SecretJson(const SecretCapabilities& peer, const SecretSet& effective) {
  return "\"secret\":{\"peer\":{\"supports\":" + FlagsJson(peer.supports) + ",\"enabled\":" + FlagsJson(peer.enabled) +
         "},\"effective\":" + FlagsJson(effective) + "}";
}

SecretCapabilities Caps(bool akuma, bool evil, bool supports = true) {
  SecretCapabilities caps;
  caps.supports = {supports, supports};
  caps.enabled = {akuma, evil};
  return caps;
}

void SignInToSecretService(Rig& rig, bool secret_service = true) {
  rig.Enable();
  rig.Tick();
  rig.Event(ChannelEvent::Kind::kOpen);
  rig.Reply("hello", "\"player\":\"" + rig.me + "\",\"name\":\"Saulo\",\"friend_code\":\"" + rig.me +
                         "\",\"capabilities\":{\"protocol_version\":3,\"worker_build\":\"test\",\"secret_characters\":" +
                         (secret_service ? "true" : "false") + "}");
}

void StartSecretMatch(Rig& rig, const char* id, const char* role, const std::string& secret) {
  rig.Feed(std::string("{\"ev\":\"match.created\",\"session\":\"") + kSession + "\"," +
           MatchBody(id, role, "base013", kFriendA, "SauloB") + (secret.empty() ? "" : "," + secret) + "}");
}

// What the Worker answers: both clients' AND, which the client then checks against its own.
SecretSet WorkerAnd(const SecretCapabilities& a, const SecretCapabilities& b) { return EffectiveSecrets(a, b); }

void TestHelloAdvertisesWhatThisBuildSupportsAndWhatIsOn() {
  Rig rig;
  rig.secret_caps = Caps(true, false);
  SignInToSecretService(rig);
  const std::string hello = rig.LastRequest("hello");
  CHECK(Contains(hello, "\"secret\":{\"supports\":{\"akuma\":true,\"evil_ryu\":true,\"violent_ken\":false},\"enabled\":{\"akuma\":true,"
                        "\"evil_ryu\":false,\"violent_ken\":false}}"));
  CHECK(rig.CountLogs("online secret-cap local akuma=1 evil=0") == 1);
  CHECK(rig.CountLogs("online secret-cap service supported=true") == 1);

  Rig bare;
  SignInToSecretService(bare);
  CHECK(Contains(bare.LastRequest("hello"),
                 "\"secret\":{\"supports\":{\"akuma\":false,\"evil_ryu\":false,\"violent_ken\":false},\"enabled\":{\"akuma\":false,"
                 "\"evil_ryu\":false,\"violent_ken\":false}}"));
}

void TestEveryOnOffCombinationIsDeterministic() {
  for (int mine = 0; mine < 4; ++mine) {
    for (int theirs = 0; theirs < 4; ++theirs) {
      const SecretCapabilities local = Caps((mine & 1) != 0, (mine & 2) != 0);
      const SecretCapabilities peer = Caps((theirs & 1) != 0, (theirs & 2) != 0);
      Rig rig;
      rig.secret_caps = local;
      SignInToSecretService(rig);
      StartSecretMatch(rig, "m1", "host", SecretJson(peer, WorkerAnd(local, peer)));
      const SecretSet effective = rig.service->SecretEffective();
      CHECK(effective.akuma == (((mine & 1) != 0) && ((theirs & 1) != 0)));
      CHECK(effective.evil_ryu == (((mine & 2) != 0) && ((theirs & 2) != 0)));
      CHECK(rig.service->snapshot().secret.effective == effective);
    }
  }
}

void TestSecretStatusTextsExplainWhyACharacterIsOrIsNotAvailable() {
  const auto row = [](const Snapshot& state, const SecretCapabilities& edited, SecretCharacter which) {
    for (const SecretRowView& view : DescribeSecretCharacters(state, edited)) {
      if (view.character == which) return view;
    }
    return SecretRowView{};
  };
  Rig rig;
  rig.secret_caps = Caps(true, false);
  SignInToSecretService(rig);
  const Snapshot alone = rig.service->snapshot();
  CHECK(row(alone, Caps(true, false), SecretCharacter::kAkuma).status == "Enabled locally");
  CHECK(row(alone, Caps(true, false), SecretCharacter::kEvilRyu).status == "Off");

  StartSecretMatch(rig, "m1", "host", SecretJson(Caps(true, true), WorkerAnd(Caps(true, false), Caps(true, true))));
  const Snapshot together = rig.service->snapshot();
  CHECK(row(together, Caps(true, false), SecretCharacter::kAkuma).status == "Available");
  CHECK(row(together, Caps(true, false), SecretCharacter::kAkuma).available);
  CHECK(row(together, Caps(true, false), SecretCharacter::kEvilRyu).status == "Disabled locally");
  CHECK(!row(together, Caps(true, false), SecretCharacter::kEvilRyu).available);

  rig.Feed(std::string("{\"ev\":\"match.secret\",\"match\":\"m1\",") + SecretJson(Caps(false, true), {}) + "}");
  CHECK(row(rig.service->snapshot(), Caps(true, false), SecretCharacter::kAkuma).status == "Disabled by opponent");
  rig.Feed(std::string("{\"ev\":\"match.secret\",\"match\":\"m1\",") + SecretJson(Caps(true, true, false), {}) + "}");
  CHECK(row(rig.service->snapshot(), Caps(true, false), SecretCharacter::kAkuma).status == "Unsupported by opponent");
  rig.Feed(std::string("{\"ev\":\"match.peer_lost\",\"match\":\"m1\"}"));
  CHECK(row(rig.service->snapshot(), Caps(true, false), SecretCharacter::kAkuma).status == "Waiting for opponent");

  Rig old;
  old.secret_caps = Caps(true, true);
  SignInToSecretService(old, false);
  StartSecretMatch(old, "m1", "host", "");
  CHECK(row(old.service->snapshot(), Caps(true, true), SecretCharacter::kAkuma).status == "Unsupported by the service");
  CHECK(row(old.service->snapshot(), Caps(true, true), SecretCharacter::kEvilRyu).status == "Unsupported by the service");
}

void TestAnOldPeerOrAnOldServiceNeverOffersASecretCharacter() {
  Rig old_peer;
  old_peer.secret_caps = Caps(true, true);
  SignInToSecretService(old_peer);
  StartSecretMatch(old_peer, "m1", "host", SecretJson(Caps(false, false, false), {}));
  CHECK(!old_peer.service->SecretEffective().Any());

  Rig old_service;
  old_service.secret_caps = Caps(true, true);
  SignInToSecretService(old_service, false);
  StartSecretMatch(old_service, "m1", "host", "");
  CHECK(!old_service.service->SecretEffective().Any());
  CHECK(!old_service.service->snapshot().secret.service_supported);
  CHECK(old_service.Count("secret.update") == 0);

  Rig missing_fields;
  missing_fields.secret_caps = Caps(true, true);
  SignInToSecretService(missing_fields);
  StartSecretMatch(missing_fields, "m1", "host", "\"secret\":{\"effective\":{\"akuma\":true,\"evil_ryu\":true}}");
  CHECK(!missing_fields.service->SecretEffective().Any());
  CHECK(missing_fields.CountLogs("service answer overridden") == 1);
}

void TestTheClientChecksTheServicesAnswerAgainstItsOwnOptions() {
  Rig rig;
  rig.secret_caps = Caps(false, true);
  SignInToSecretService(rig);
  StartSecretMatch(rig, "m1", "join", SecretJson(Caps(true, true), {true, true}));
  const SecretSet effective = rig.service->SecretEffective();
  CHECK(!effective.akuma && effective.evil_ryu);
  CHECK(rig.CountLogs("online secret-cap service answer overridden service=3 verified=2") == 1);
  CHECK(rig.CountLogs("online secret-cap effective akuma=0 evil=1") == 1);
}

void TestAnOptionChangeIsSentOnceAndRecomputesTheRoom() {
  Rig rig;
  rig.secret_caps = Caps(false, false);
  SignInToSecretService(rig);
  for (int i = 0; i < 5; ++i) rig.Tick(1.0);
  CHECK(rig.Count("secret.update") == 0);

  StartSecretMatch(rig, "m1", "host", SecretJson(Caps(true, true), {}));
  rig.secret_caps = Caps(true, true);
  rig.Tick();
  CHECK(rig.Count("secret.update") == 1);
  CHECK(Contains(rig.LastRequest("secret.update"), "\"enabled\":{\"akuma\":true,\"evil_ryu\":true,\"violent_ken\":false}"));
  CHECK(!rig.service->SecretEffective().Any());
  for (int i = 0; i < 5; ++i) rig.Tick(1.0);
  CHECK(rig.Count("secret.update") == 1);
  rig.Reply("secret.update", SecretJson(Caps(true, true), {true, true}));
  CHECK(rig.service->SecretEffective().akuma && rig.service->SecretEffective().evil_ryu);
  for (int i = 0; i < 5; ++i) rig.Tick(1.0);
  CHECK(rig.Count("secret.update") == 1);

  rig.secret_caps = Caps(true, false);
  rig.Tick();
  CHECK(rig.Count("secret.update") == 2);
  CHECK(rig.service->SecretEffective().akuma && !rig.service->SecretEffective().evil_ryu);
}

void TestARefusedOptionChangeIsRetriedLater() {
  Rig rig;
  rig.secret_caps = Caps(false, false);
  SignInToSecretService(rig);
  rig.secret_caps = Caps(true, false);
  rig.Tick();
  CHECK(rig.Count("secret.update") == 1);
  rig.Reply("secret.update", "", "limit");
  for (int i = 0; i < 3; ++i) rig.Tick(1.0);
  CHECK(rig.Count("secret.update") == 1);
  for (int i = 0; i < 4; ++i) rig.Tick(1.0);
  CHECK(rig.Count("secret.update") == 2);
}

void TestARoomEventForAnotherMatchIsIgnored() {
  Rig rig;
  rig.secret_caps = Caps(true, true);
  SignInToSecretService(rig);
  StartSecretMatch(rig, "m1", "host", SecretJson(Caps(true, true), {true, true}));
  rig.Feed(std::string("{\"ev\":\"match.secret\",\"match\":\"other\",") + SecretJson(Caps(false, false), {}) + "}");
  CHECK(rig.service->SecretEffective().akuma && rig.service->SecretEffective().evil_ryu);
}

void TestDisconnectReconnectAndLeaveClearTheNegotiatedState() {
  Rig rig;
  rig.secret_caps = Caps(true, true);
  SignInToSecretService(rig);
  StartSecretMatch(rig, "m1", "host", SecretJson(Caps(true, true), {true, true}));
  CHECK(rig.service->SecretEffective().Any());

  rig.Feed("{\"ev\":\"match.peer_lost\",\"match\":\"m1\"}");
  CHECK(!rig.service->SecretEffective().Any());
  rig.Feed("{\"ev\":\"match.peer_back\",\"match\":\"m1\"}");
  CHECK(rig.service->SecretEffective().akuma && rig.service->SecretEffective().evil_ryu);

  rig.Feed("{\"ev\":\"match.ended\",\"match\":\"m1\",\"reason\":\"left\"}");
  CHECK(!rig.service->SecretEffective().Any());

  StartSecretMatch(rig, "m2", "join", SecretJson(Caps(true, false), {true, false}));
  CHECK(rig.service->SecretEffective().akuma && !rig.service->SecretEffective().evil_ryu);
  rig.service->LeaveMatch();
  CHECK(!rig.service->SecretEffective().Any());
  CHECK(!rig.service->snapshot().secret.peer);

  StartSecretMatch(rig, "m3", "host", SecretJson(Caps(false, true), {false, true}));
  CHECK(!rig.service->SecretEffective().akuma && rig.service->SecretEffective().evil_ryu);

  rig.service->Apply({false, "Saulo"});
  CHECK(!rig.service->SecretEffective().Any());
}

void TestAMalformedRoomAnswerOffersNothing() {
  Rig rig;
  rig.secret_caps = Caps(true, true);
  SignInToSecretService(rig);
  StartSecretMatch(rig, "m1", "host", "\"secret\":{\"peer\":5,\"effective\":\"yes\"}");
  CHECK(!rig.service->SecretEffective().Any());
  StartSecretMatch(rig, "m2", "host", "\"secret\":{\"peer\":{\"supports\":{\"akuma\":\"true\"},\"enabled\":{\"akuma\":1}},"
                                      "\"effective\":{\"akuma\":\"true\",\"evil_ryu\":1}}");
  CHECK(!rig.service->SecretEffective().Any());
}

// ---- public Lobby ----
const char kLobbyCaps[] = "\"capabilities\":{\"protocol_version\":3,\"worker_build\":\"test\",\"public_lobby\":true}";

std::string LobbyRow(const char* player, const char* name, const char* guest = "base013", bool compatible = true,
                     const char* state = "online") {
  return std::string("{\"player\":\"") + player + "\",\"name\":\"" + name + "\",\"state\":\"" + state +
         "\",\"platform\":\"Windows\",\"version\":\"0.4.0\",\"guest\":\"" + guest + "\",\"compatible\":" +
         (compatible ? "true" : "false") + "}";
}

std::string LobbyAnswer(long long rev, const std::string& rows) {
  return "\"players\":[" + rows + "],\"rev\":" + std::to_string(rev) + ",\"visible\":false";
}

void SignInToLobbyService(Rig& rig, bool visible = false, bool lobby = true) {
  rig.service->Apply({true, "Saulo", visible});
  rig.Tick();
  rig.Event(ChannelEvent::Kind::kOpen);
  rig.Reply("hello", "\"player\":\"" + rig.me + "\",\"name\":\"Saulo\",\"friend_code\":\"" + rig.me + "\"" +
                         (lobby ? std::string(",") + kLobbyCaps : std::string()));
}

// Keeps the Lobby page "open" for `seconds`, as the launcher does by drawing it every frame.
void ViewLobby(Rig& rig, double seconds) {
  for (double spent = 0; spent < seconds; spent += 1.0) {
    rig.service->NoteLobbyViewed();
    rig.Tick(1.0);
  }
}

void TestLobbyVisibilityDefaultsToOffAndSendsNothing() {
  CHECK(!UserSettings{}.lobby_visible);
  Rig rig;
  SignInToLobbyService(rig);
  for (int i = 0; i < 30; ++i) rig.Tick(5.0);
  CHECK(rig.Count("lobby.publish") == 0 && rig.Count("lobby.list") == 0);
  CHECK(rig.service->snapshot().lobby.supported && !rig.service->snapshot().lobby.visible);
  CHECK(rig.CountLogs("online lobby service supported=true") == 1);
}

void TestLobbyVisibilityIsPublishedOnceAndOnChange() {
  Rig rig;
  SignInToLobbyService(rig, true);
  CHECK(rig.Count("lobby.publish") == 1 && Contains(rig.LastRequest("lobby.publish"), "\"visible\":true"));
  for (int i = 0; i < 10; ++i) rig.Tick(1.0);
  CHECK(rig.Count("lobby.publish") == 1);
  CHECK(!rig.service->snapshot().lobby.visible);
  rig.Reply("lobby.publish", "\"visible\":true");
  CHECK(rig.service->snapshot().lobby.visible && rig.CountLogs("online lobby visible=true") == 1);
  for (int i = 0; i < 10; ++i) rig.Tick(5.0);
  CHECK(rig.Count("lobby.publish") == 1);

  rig.service->Apply({true, "Saulo", false});
  rig.Tick();
  CHECK(rig.Count("lobby.publish") == 2 && Contains(rig.LastRequest("lobby.publish"), "\"visible\":false"));
  rig.Reply("lobby.publish", "\"visible\":false");
  CHECK(!rig.service->snapshot().lobby.visible);
  for (int i = 0; i < 10; ++i) rig.Tick(5.0);
  CHECK(rig.Count("lobby.publish") == 2);
}

void TestLobbyVisibilityChangedWhileSendingFollowsTheLastChoice() {
  Rig rig;
  SignInToLobbyService(rig, true);
  rig.service->Apply({true, "Saulo", false});
  for (int i = 0; i < 5; ++i) rig.Tick(1.0);
  CHECK(rig.Count("lobby.publish") == 1);
  rig.Reply("lobby.publish", "\"visible\":true");
  rig.Tick();
  CHECK(rig.Count("lobby.publish") == 2 && Contains(rig.LastRequest("lobby.publish"), "\"visible\":false"));
  rig.Reply("lobby.publish", "\"visible\":false");
  CHECK(!rig.service->snapshot().lobby.visible);
}

void TestRefusedLobbyPublishIsRetriedLaterNotInALoop() {
  Rig rig;
  SignInToLobbyService(rig, true);
  rig.Reply("lobby.publish", "", "limit");
  CHECK(!rig.service->snapshot().lobby.visible && rig.CountLogs("online lobby publish refused code=limit") == 1);
  for (int i = 0; i < 12; ++i) rig.Tick(1.0);
  CHECK(rig.Count("lobby.publish") == 1);
  for (int i = 0; i < 6; ++i) rig.Tick(1.0);
  CHECK(rig.Count("lobby.publish") == 2);
  CHECK(rig.service->snapshot().connection == Connection::kOnline);
}

void TestLobbyIsReadOnlyWhileItsPageIsShownAndRefreshesBySelf() {
  Rig rig;
  SignInToLobbyService(rig);
  for (int i = 0; i < 20; ++i) rig.Tick(5.0);
  CHECK(rig.Count("lobby.list") == 0);

  rig.service->NoteLobbyViewed();
  rig.Tick();
  CHECK(rig.Count("lobby.list") == 1 && !Contains(rig.LastRequest("lobby.list"), "\"rev\""));
  CHECK(!rig.service->snapshot().lobby.fetched);
  rig.Reply("lobby.list", LobbyAnswer(1700000000123, LobbyRow("JKLM-3456", "Kim") + "," + LobbyRow("NPQR-7892", "Zoe", "tu023", false)));
  const Snapshot first = rig.service->snapshot();
  CHECK(first.lobby.fetched && !first.lobby.failed && first.lobby.players.size() == 2);
  CHECK(first.lobby.players[0].name == "Kim" && first.lobby.players[1].guest == "tu023" && !first.lobby.players[1].compatible);
  CHECK(rig.CountLogs("online lobby players=2") == 1);

  int ticks = 0;
  while (rig.Count("lobby.list") == 1 && ticks < 60) {
    rig.service->NoteLobbyViewed();
    rig.Tick(1.0);
    ++ticks;
  }
  CHECK(ticks >= 28 && ticks <= 32);
  CHECK(rig.Count("lobby.list") == 2 && Contains(rig.LastRequest("lobby.list"), "\"rev\":1700000000123"));
  rig.Reply("lobby.list", "\"unchanged\":true,\"rev\":1700000000123,\"visible\":false");
  CHECK(rig.service->snapshot().lobby.players.size() == 2 && !rig.service->snapshot().lobby.failed);
  CHECK(rig.CountLogs("online lobby players=") == 1);

  for (int i = 0; i < 60; ++i) rig.Tick(5.0);
  CHECK(rig.Count("lobby.list") == 2);
}

void TestReopeningTheLobbyPageReadsAtOnce() {
  Rig rig;
  SignInToLobbyService(rig);
  ViewLobby(rig, 3);
  CHECK(rig.Count("lobby.list") == 1);
  rig.Reply("lobby.list", LobbyAnswer(10, LobbyRow("JKLM-3456", "Kim")));
  for (int i = 0; i < 4; ++i) rig.Tick(1.0);
  rig.service->NoteLobbyViewed();
  rig.Tick();
  CHECK(rig.Count("lobby.list") == 2 && Contains(rig.LastRequest("lobby.list"), "\"rev\":10"));
}

void TestLobbyAnswerReplacesThePlayers() {
  Rig rig;
  SignInToLobbyService(rig);
  ViewLobby(rig, 1);
  rig.Reply("lobby.list", LobbyAnswer(10, LobbyRow("JKLM-3456", "Kim")));
  CHECK(rig.service->snapshot().lobby.players.size() == 1);
  ViewLobby(rig, 31);
  rig.Reply("lobby.list", LobbyAnswer(11, LobbyRow("NPQR-7892", "Zoe") + "," + LobbyRow("STUV-2345", "Ann")));
  const Snapshot s = rig.service->snapshot();
  CHECK(s.lobby.players.size() == 2 && s.lobby.players[0].name == "Zoe");
  ViewLobby(rig, 31);
  CHECK(Contains(rig.LastRequest("lobby.list"), "\"rev\":11"));
  rig.Reply("lobby.list", LobbyAnswer(12, ""));
  CHECK(rig.service->snapshot().lobby.fetched && rig.service->snapshot().lobby.players.empty());
}

void TestLobbyFailureNeverTakesTheServiceOffline() {
  Rig rig;
  SignInToLobbyService(rig);
  rig.Reply("friends.list", "\"friends\":[" + FriendJson(kFriendA, "SauloB", true) + "]");
  rig.Reply("friends.requests", "\"incoming\":[],\"outgoing\":[]");
  ViewLobby(rig, 1);
  rig.Reply("lobby.list", "", "internal");
  Snapshot s = rig.service->snapshot();
  CHECK(s.connection == Connection::kOnline && s.lobby.failed && !s.lobby.fetched && s.friends.size() == 1);
  CHECK(LobbyMessage(s.lobby) == "Cannot read the Lobby right now.");
  CHECK(rig.CountLogs("online lobby read failed code=internal") == 1);
  CHECK(rig.service->AddFriend("JKLM-3456") == FriendRequestResult::kSent);
  rig.Reply("friends.add", "\"player\":\"JKLM-3456\",\"name\":\"Kim\",\"state\":\"pending\"");

  ViewLobby(rig, 20);
  CHECK(rig.Count("lobby.list") == 1);
  ViewLobby(rig, 12);
  CHECK(rig.Count("lobby.list") == 2);
  rig.Reply("lobby.list", LobbyAnswer(3, LobbyRow("JKLM-3456", "Kim")));
  s = rig.service->snapshot();
  CHECK(!s.lobby.failed && s.lobby.fetched && s.connection == Connection::kOnline);

  ViewLobby(rig, 31);
  rig.Reply("lobby.list", "", "limit");
  s = rig.service->snapshot();
  CHECK(s.lobby.failed && s.lobby.fetched && s.lobby.players.size() == 1 && LobbyMessage(s.lobby).empty());
  CHECK(s.connection == Connection::kOnline);
}

void TestMalformedLobbyAnswerIsAFailedRead() {
  Rig rig;
  SignInToLobbyService(rig);
  ViewLobby(rig, 1);
  rig.Reply("lobby.list", "\"players\":[{\"name\":\"NoPlayer\"}],\"rev\":4");
  const Snapshot s = rig.service->snapshot();
  CHECK(s.lobby.failed && !s.lobby.fetched && s.connection == Connection::kOnline);
  CHECK(rig.CountLogs("online lobby read failed code=malformed") == 1);
}

void TestAnOlderServiceGetsNoLobbyRequests() {
  for (const bool announces_nothing : {true, false}) {
    Rig rig;
    if (announces_nothing) {
      SignInToLobbyService(rig, true, false);
    } else {
      rig.service->Apply({true, "Saulo", true});
      rig.Tick();
      rig.Event(ChannelEvent::Kind::kOpen);
      rig.Reply("hello", "\"player\":\"" + rig.me + "\",\"name\":\"Saulo\",\"friend_code\":\"" + rig.me +
                             "\",\"capabilities\":{\"protocol_version\":3,\"public_lobby\":false}");
    }
    ViewLobby(rig, 120);
    CHECK(rig.Count("lobby.publish") == 0 && rig.Count("lobby.list") == 0);
    const Snapshot s = rig.service->snapshot();
    CHECK(!s.lobby.supported && !s.lobby.visible && !s.lobby.fetched);
    CHECK(Contains(LobbyMessage(s.lobby), "does not have the public Lobby"));
    CHECK(s.connection == Connection::kOnline && rig.CountLogs("online lobby service supported=false") == 1);
    rig.Reply("friends.list", "\"friends\":[]");
    CHECK(rig.service->snapshot().fetched);
  }
}

void TestAServiceThatDoesNotKnowTheOperationIsLeftAlone() {
  Rig rig;
  SignInToLobbyService(rig, true);
  rig.Reply("lobby.publish", "", "bad_request");
  ViewLobby(rig, 120);
  CHECK(rig.Count("lobby.publish") == 1 && rig.Count("lobby.list") == 0);
  CHECK(!rig.service->snapshot().lobby.supported && rig.service->snapshot().connection == Connection::kOnline);

  Rig reads;
  SignInToLobbyService(reads);
  ViewLobby(reads, 1);
  reads.Reply("lobby.list", "", "bad_request");
  ViewLobby(reads, 120);
  CHECK(reads.Count("lobby.list") == 1 && !reads.service->snapshot().lobby.supported);
}

void TestLobbyStartsOverOnEveryNewConnection() {
  Rig rig;
  SignInToLobbyService(rig, true);
  rig.Reply("lobby.publish", "\"visible\":true");
  ViewLobby(rig, 1);
  rig.Reply("lobby.list", LobbyAnswer(7, LobbyRow("JKLM-3456", "Kim")));
  CHECK(rig.service->snapshot().lobby.visible && rig.service->snapshot().lobby.players.size() == 1);

  rig.Event(ChannelEvent::Kind::kClosed, "link_lost");
  Snapshot s = rig.service->snapshot();
  CHECK(!s.lobby.visible && !s.lobby.fetched && s.lobby.players.empty());
  rig.Tick(2.0);
  CHECK(rig.channels.size() == 2);
  rig.Event(ChannelEvent::Kind::kOpen);
  rig.Reply("hello", "\"player\":\"" + rig.me + "\",\"name\":\"Saulo\",\"friend_code\":\"" + rig.me + "\"," + kLobbyCaps);
  CHECK(rig.Count("lobby.publish") == 1 && Contains(rig.LastRequest("lobby.publish"), "\"visible\":true"));
  rig.service->NoteLobbyViewed();
  rig.Tick();
  CHECK(rig.Count("lobby.list") == 1 && !Contains(rig.LastRequest("lobby.list"), "\"rev\""));
}

void TestRenamingSignsInAgainAndListsAgain() {
  Rig rig;
  SignInToLobbyService(rig, true);
  rig.Reply("lobby.publish", "\"visible\":true");
  rig.service->Apply({true, "Renamed", true});
  for (int i = 0; i < 4; ++i) rig.Tick(1.0);
  CHECK(rig.channels.size() == 2);
  CHECK(!rig.service->snapshot().lobby.visible);
  rig.Event(ChannelEvent::Kind::kOpen);
  rig.Reply("hello", "\"player\":\"" + rig.me + "\",\"name\":\"Renamed\",\"friend_code\":\"" + rig.me + "\"," + kLobbyCaps);
  CHECK(rig.Count("lobby.publish") == 1);
}

void TestOnlinePlayOffLeavesTheLobbyAndTurningItOnPublishesAgain() {
  Rig rig;
  SignInToLobbyService(rig, true);
  rig.Reply("lobby.publish", "\"visible\":true");
  rig.service->Apply({false, "Saulo", true});
  const Snapshot off = rig.service->snapshot();
  CHECK(!off.lobby.visible && !off.lobby.fetched && off.lobby.players.empty());
  rig.Tick(3.0);
  CHECK(rig.Last().closed);
  const size_t opened = rig.channels.size();
  for (int i = 0; i < 10; ++i) rig.Tick(5.0);
  CHECK(rig.channels.size() == opened);

  rig.service->Apply({true, "Saulo", true});
  rig.Tick();
  rig.Event(ChannelEvent::Kind::kOpen);
  rig.Reply("hello", "\"player\":\"" + rig.me + "\",\"name\":\"Saulo\",\"friend_code\":\"" + rig.me + "\"," + kLobbyCaps);
  CHECK(rig.channels.size() == opened + 1 && rig.Count("lobby.publish") == 1);
}

void TestLobbyRowsFollowTheFriendsTab() {
  Rig rig;
  rig.SignIn();
  rig.Reply("friends.list", "\"friends\":[" + FriendJson(kFriendA, "SauloB", true) + "]");
  rig.Reply("friends.requests", "\"incoming\":[{\"player\":\"NPQR-7892\",\"name\":\"Zoe\"}],"
                                "\"outgoing\":[{\"player\":\"JKLM-3456\",\"name\":\"Kim\"}]");
  const Snapshot s = rig.service->snapshot();
  const auto row = [](const char* player, const char* guest = "base013", bool compatible = true) {
    LobbyPlayer p;
    p.player = player;
    p.name = "Someone";
    p.platform = "Windows";
    p.version = "0.4.0";
    p.guest = guest;
    p.compatible = compatible;
    return p;
  };

  const LobbyRowView friend_row = DescribeLobbyPlayer(s, row(kFriendA));
  CHECK(friend_row.relation == LobbyRelation::kFriend && !friend_row.can_add);
  const LobbyRowView sent = DescribeLobbyPlayer(s, row("JKLM-3456"));
  CHECK(sent.relation == LobbyRelation::kOutgoing && !sent.can_add);
  const LobbyRowView waiting = DescribeLobbyPlayer(s, row("NPQR-7892"));
  CHECK(waiting.relation == LobbyRelation::kIncoming && !waiting.can_add);
  const LobbyRowView own = DescribeLobbyPlayer(s, row(rig.me.c_str()));
  CHECK(own.relation == LobbyRelation::kNone && !own.can_add);
  const LobbyRowView stranger = DescribeLobbyPlayer(s, row("STUV-2345"));
  CHECK(stranger.relation == LobbyRelation::kNone && stranger.can_add && !stranger.incompatible);
  CHECK(stranger.status == "Online \xC2\xB7 Windows \xC2\xB7 v0.4.0 \xC2\xB7 Base013");

  const LobbyRowView other = DescribeLobbyPlayer(s, row("STUV-2345", "tu023", false));
  CHECK(other.incompatible && other.can_add && Contains(other.status, "TU023"));

  Snapshot offline = s;
  offline.connection = Connection::kOffline;
  CHECK(!DescribeLobbyPlayer(offline, row("STUV-2345")).can_add);
  for (const Connection state : {Connection::kChecking, Connection::kError, Connection::kOff}) {
    offline.connection = state;
    CHECK(!DescribeLobbyPlayer(offline, row("STUV-2345")).can_add);
  }
}

void TestLobbyMessagesSayWhyThereIsNoList() {
  LobbySnapshot lobby;
  CHECK(Contains(LobbyMessage(lobby), "does not have the public Lobby"));
  lobby.supported = true;
  CHECK(LobbyMessage(lobby) == "Looking for players...");
  lobby.failed = true;
  CHECK(LobbyMessage(lobby) == "Cannot read the Lobby right now.");
  lobby.failed = false;
  lobby.fetched = true;
  CHECK(LobbyMessage(lobby) == "No players are in the Lobby right now.");
  lobby.players.push_back(LobbyPlayer{});
  CHECK(LobbyMessage(lobby).empty());
  lobby.failed = true;
  CHECK(LobbyMessage(lobby).empty());
}

void TestAddFriendFromTheLobbyIsTheFriendRequest() {
  Rig rig;
  SignInToLobbyService(rig);
  rig.Reply("friends.list", "\"friends\":[" + FriendJson(kFriendA, "SauloB", true) + "]");
  rig.Reply("friends.requests", "\"incoming\":[],\"outgoing\":[]");
  CHECK(rig.service->AddFriend("JKLM-3456") == FriendRequestResult::kSent);
  CHECK(Contains(rig.LastRequest("friends.add"), "\"code\":\"JKLM-3456\""));
  rig.Reply("friends.add", "\"player\":\"JKLM-3456\",\"name\":\"Kim\",\"state\":\"pending\"");
  rig.Reply("friends.requests", "\"incoming\":[],\"outgoing\":[{\"player\":\"JKLM-3456\",\"name\":\"Kim\"}]");
  CHECK(rig.service->AddFriend("JKLM-3456") == FriendRequestResult::kAlreadyPending);
  CHECK(rig.service->AddFriend(kFriendA) == FriendRequestResult::kAlreadyFriends);
  CHECK(rig.service->AddFriend(rig.me) == FriendRequestResult::kOwnCode);
  CHECK(rig.Count("friends.add") == 1);
  const Snapshot s = rig.service->snapshot();
  LobbyPlayer p;
  p.player = "JKLM-3456";
  p.name = "Kim";
  CHECK(DescribeLobbyPlayer(s, p).relation == LobbyRelation::kOutgoing);
}

void TestLobbyRequestsNeverCarryPrivateData() {
  Rig rig;
  SignInToLobbyService(rig, true);
  ViewLobby(rig, 1);
  for (const std::string& request : rig.Last().sent) {
    if (!Contains(request, "\"op\":\"lobby.")) continue;
    CHECK(!Contains(request, kToken) && !Contains(request, "token") && !Contains(request, "lan") && !Contains(request, "ip"));
    CHECK(!Contains(request, rig.me));
  }
  for (const std::string& line : rig.logs) CHECK(!Contains(line, kToken));
}

int main() {
  std::printf("online service tests\n");
  TestHelloAdvertisesWhatThisBuildSupportsAndWhatIsOn();
  TestEveryOnOffCombinationIsDeterministic();
  TestSecretStatusTextsExplainWhyACharacterIsOrIsNotAvailable();
  TestAnOldPeerOrAnOldServiceNeverOffersASecretCharacter();
  TestTheClientChecksTheServicesAnswerAgainstItsOwnOptions();
  TestAnOptionChangeIsSentOnceAndRecomputesTheRoom();
  TestARefusedOptionChangeIsRetriedLater();
  TestARoomEventForAnotherMatchIsIgnored();
  TestDisconnectReconnectAndLeaveClearTheNegotiatedState();
  TestAMalformedRoomAnswerOffersNothing();
  TestOnlineOffHasNoNetworkBehaviour();
  TestSignInSendsPresenceDetailsAndNeverTheTokenInLogs();
  TestFriendsAndPresenceAreShown();
  TestAddFriendValidatesBeforeSending();
  TestFriendRequestReachesTheServiceAndIsLogged();
  TestPendingAndReverseRequests();
  TestFriendRequestNeedsAConnection();
  TestCopiedFriendCodeCanBePastedByAnotherInstance();
  TestFriendRequestsAreAnswered();
  TestHostedPlayerMatchBecomesPlayerOne();
  TestJoinedPlayerMatchBecomesPlayerTwoOnTheSamePc();
  TestSignalingPeerAddressIsLoggedForDiagnosis();
  TestIpv6PeerAddressIsLoggedNotSilentlyAccepted();
  TestMatchAdvertisesTheGameSocketsMappedEndpoint();
  TestPeerIsHandedToTheFightOnlyAfterItsPunchAnswers();
  TestJoinRolePunchesWithItsOwnSide();
  TestNoStunAnswerAdvertisesNoMapping();
  TestNonPublicMappingIsNotAdvertised();
  TestUnansweredPunchLeavesTheMatch();
  TestNextMatchReusesTheSocketAndIgnoresTheOldOne();
  TestUnboundSocketLeavesTheMatch();
  TestInternetPeerEndpointIsWaitedFor();
  TestReadyPeerEndpointPunchesAtOnce();
  TestSameNetworkPeerDoesNotWait();
  TestMissingPeerEndpointTimesOutAndFreesThePlayerMatch();
  TestNoPeerSignalingTimesOut();
  TestStaleOrLeftPeerUpdatesAreIgnored();
  TestOutdatedServiceFailsFastWithItsOwnReason();
  TestCurrentServiceWithoutAnEndpointYetKeepsWaiting();
  TestFindMatchWaitsForTheServiceInsteadOfAnEmptyCache();
  TestRepeatedGuestCallsShareOneSearch();
  TestAnAuthoritativeEmptyAnswerEndsTheSearchWithZero();
  TestUnansweredSearchTimesOutAndIsNotPendingForever();
  TestFailedSearchAnswerEndsTheWait();
  TestNoConnectionNeverHoldsTheGuest();
  TestStaleSearchAnswerNeverFillsALaterSearch();
  TestNewFindMatchAfterAnEmptySearchAsksAgain();
  TestFoundMatchJoinsExactlyOnce();
  TestAcceptedInviteSurvivesTheGuestTeardownAndJoinsOnce();
  TestRejectedInviteJoinEndsOnlyThatInvite();
  TestEndedOrReplacedInviteFailsItsGeneration();
  TestOutdatedServiceAfterAnInviteJoinFailsAtOnceAndLeavesAFreshStart();
  TestCapabilityCheckSparesSameNetworkAndCurrentServices();
  TestLeavingWhileASearchIsPendingCancelsIt();
  TestInviteAndCreateMatchDoNotWaitOnTheSearch();
  TestCancelSearchStopAndRefusedJoin();
  TestAdvertisementWaitsForTheServiceAndSurvivesAReconnect();
  TestNoChallengeOnTheWireOrInLogs();
  TestMatchWithAnotherGuestIsRefused();
  TestLinkLossKeepsFriendsAndMatchAndResumes();
  TestUnreachableServiceRetriesWithoutLosingState();
  TestResumeFailureEndsAMatchThatNeverConnected();
  TestSignalingLossDoesNotKillALiveFight();
  TestLatencyIsShownOnlyWhenMeasured();
  TestPeerEndingBeforeConnectionClearsTheMatch();
  TestTurningOnlineOffLeavesTheMatchAndStopsEverything();
  TestLeaveMatchAndRename();
  TestAfterADisconnectThePlayerCanPlayAgain();
  TestFatalSignInErrorsDoNotRetryAtOnce();
  TestMalformedServerMessagesAreIgnored();
  TestConnectionTestRunsOnlyWhenEnabled();
  TestEveryConnectionReportCountsAsFinished();
  TestTemporaryIdentityGetsASuffix();
  TestFriendRowTexts();
  TestInviteNeedsAnAdvertisedPlayerMatch();
  TestAcceptedInviteJoinsThatSameSession();
  TestDeclinedOrEndedInvitesDisappear();
  TestInviteListOffersOnlyReachableFriends();
  TestServiceCapabilitiesAreLoggedAndRelayIsOffered();
  TestDirectWinsWhenThePunchAnswersInTime();
  TestNoDirectPathFallsBackToTheRelay();
  TestMissingPeerEndpointFallsBackToTheRelay();
  TestNoPublicEndpointsButRelayGoesStraightToTheRelay();
  TestNoDirectPathAndNoRelayFails();
  TestRelayFramesOfOtherMatchesAreIgnored();
  TestRelayLossEndsTheMatchCleanly();
  TestFindMatchJoinerFollowsTheRelayChoice();
  TestJoinerFollowsTheDirectChoice();
  TestEstablishmentBudgetModel();
  TestGameWaitTimerOnlyChangesTheLobbyWait();
  TestDirectAt500msIsReadyAtOnce();
  TestRelayReadyAtAnyTimeInsideTheBudgetSucceeds();
  TestRelayBudgetIsTenSecondsCappedByTheAbsoluteDeadline();
  TestNothingReadyAtFifteenSecondsIsATerminalTimeout();
  TestTerminalFailuresDoNotWaitForTheDeadline();
  TestUnreachableWithoutRelayFailsTheGuestImmediately();
  TestJoinerNeverDecidesAndRepeatsChangeNothing();
  TestRepeatedPeerEndpointUpdatesDoNotExtendTheDeadline();
  TestEveryMatchGetsAFreshBudget();
  TestFightsDoNotReuseTheEstablishmentTimer();
  TestFindAndInviteUseTheSameBudgetAfterTheJoin();
  TestLatePunchGetsOnlyAMinimumWindow();
  TestGuestWaitIsUnmanagedOutsideAJoinedMatch();
  TestInvitedJoinerFollowsTheRelayChoice();
  TestEveryMatchAfterARelayMatchStartsClean();
  TestRefusedRelayChoiceFailsTheMatch();
  TestHostThenJoinWithoutRestartReversesRoles();
  TestLobbyVisibilityDefaultsToOffAndSendsNothing();
  TestLobbyVisibilityIsPublishedOnceAndOnChange();
  TestLobbyVisibilityChangedWhileSendingFollowsTheLastChoice();
  TestRefusedLobbyPublishIsRetriedLaterNotInALoop();
  TestLobbyIsReadOnlyWhileItsPageIsShownAndRefreshesBySelf();
  TestReopeningTheLobbyPageReadsAtOnce();
  TestLobbyAnswerReplacesThePlayers();
  TestLobbyFailureNeverTakesTheServiceOffline();
  TestMalformedLobbyAnswerIsAFailedRead();
  TestAnOlderServiceGetsNoLobbyRequests();
  TestAServiceThatDoesNotKnowTheOperationIsLeftAlone();
  TestLobbyStartsOverOnEveryNewConnection();
  TestRenamingSignsInAgainAndListsAgain();
  TestOnlinePlayOffLeavesTheLobbyAndTurningItOnPublishesAgain();
  TestLobbyRowsFollowTheFriendsTab();
  TestLobbyMessagesSayWhyThereIsNoList();
  TestAddFriendFromTheLobbyIsTheFriendRequest();
  TestLobbyRequestsNeverCarryPrivateData();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
