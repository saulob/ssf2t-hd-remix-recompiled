// Services against a real online service: node server/ssf2t-lobby/local/server.js, then
//   SSF2T_LOBBY_TEST_URL=ws://127.0.0.1:8791/ ssf2t_online_integration_tests
// Exits 77 (not run) without the variable. Only the online protocol runs; no game, no window.

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <thread>

#include "online/online_service.h"

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

double Now() {
  static const auto start = std::chrono::steady_clock::now();
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

struct Client {
  OnlineService service;
  std::string player;
  Client(const std::string& url, const std::string& guest, char fill, const char* name)
      : service(Config(url, guest, fill)), player(DerivePlayerId(std::string(64, fill))) {
    service.Apply({true, name});
  }

  static ServiceConfig Config(const std::string& url, const std::string& guest, char fill) {
    ServiceConfig config;
    config.url = url;
    config.guest = guest;
    config.version = "0.2.2";
    config.platform = "Windows";
    config.lan = {"127.0.0.1"};
    config.identity = [fill] {
      Identity id;
      id.token = std::string(64, fill);
      id.player_id = DerivePlayerId(id.token);
      return id;
    };
    return config;
  }
};

bool WaitFor(const std::function<bool()>& done, std::initializer_list<Client*> clients, double seconds = 8.0) {
  const double deadline = Now() + seconds;
  while (Now() < deadline) {
    for (Client* client : clients) client->service.Update(Now());
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

bool SignedIn(Client& c) { return c.service.snapshot().connection == Connection::kOnline && !c.service.snapshot().friend_code.empty(); }

std::string AsPasted(std::string code) {
  for (char& c : code) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return "  " + code + "\r\n";
}

bool BecomeFriends(Client& a, Client& b) {
  if (!WaitFor([&] { return SignedIn(a) && SignedIn(b); }, {&a, &b})) return false;
  if (a.service.AddFriend(AsPasted(b.service.snapshot().friend_code)) != FriendRequestResult::kSent) return false;
  if (!WaitFor([&] { return !b.service.snapshot().incoming.empty(); }, {&a, &b})) return false;
  b.service.AcceptFriend(a.player);
  return WaitFor([&] { return a.service.snapshot().friends.size() == 1 && b.service.snapshot().friends.size() == 1 &&
                              a.service.snapshot().friends[0].online; },
                 {&a, &b});
}

// The owner of a Player Match hosts; a friend's search finds it and joins.
bool PlayerMatch(Client& host, Client& joiner, std::initializer_list<Client*> all) {
  host.service.HostPlayerMatch({{"rounds", 2}});
  if (!WaitFor([&] { return !host.service.snapshot().player_match.session.empty(); }, all)) return false;
  joiner.service.SetPlayerMatchSearch(true);
  const std::string session = host.service.snapshot().player_match.session;
  if (!WaitFor([&] {
        for (const PlayerMatchAd& ad : joiner.service.PlayerMatchResults()) {
          if (ad.session == session) return true;
        }
        return false;
      }, all)) {
    return false;
  }
  return joiner.service.JoinPlayerMatch(session);
}

void TestFriendsPlayerMatchAndHandoff(const std::string& url) {
  Client a(url, kGuestBase013, 'a', "SauloA");
  Client b(url, kGuestBase013, 'b', "SauloB");
  CHECK(BecomeFriends(a, b));
  CHECK(!a.service.snapshot().friend_code.empty() && a.service.snapshot().friend_code != b.service.snapshot().friend_code);
  const Friend seen = a.service.snapshot().friends[0];
  CHECK(seen.name == "SauloB" && seen.platform == "Windows" && seen.version == "0.2.2" && seen.compatible);

  CHECK(PlayerMatch(a, b, {&a, &b}));
  CHECK(WaitFor([&] { return a.service.Assignment() && b.service.Assignment(); }, {&a, &b}));
  const auto host_plan = a.service.Assignment();
  const auto join_plan = b.service.Assignment();
  CHECK(host_plan && host_plan->host);
  CHECK(join_plan && !join_plan->host && join_plan->peer_ip == "127.0.0.1" && join_plan->peer_port == 7001);
  CHECK(a.service.snapshot().match && a.service.snapshot().match->host);
  CHECK(b.service.snapshot().match && !b.service.snapshot().match->host);

  b.service.LeaveMatch();
  CHECK(WaitFor([&] { return !a.service.snapshot().match.has_value() && !a.service.Assignment(); }, {&a, &b}));
}

void TestStrangersAndSimultaneousMatches(const std::string& url) {
  Client a(url, kGuestTu023, 'c', "A");
  Client b(url, kGuestTu023, 'd', "B");
  Client c(url, kGuestTu023, 'e', "C");
  Client d(url, kGuestTu023, 'f', "D");
  CHECK(BecomeFriends(a, b));
  CHECK(BecomeFriends(c, d));
  a.service.HostPlayerMatch({});
  c.service.SetPlayerMatchSearch(true);
  CHECK(WaitFor([&] { return !a.service.snapshot().player_match.session.empty() &&
                             c.service.snapshot().player_match.search_generation > 1; }, {&a, &b, &c, &d}));
  CHECK(c.service.PlayerMatchResults().empty());
  c.service.SetPlayerMatchSearch(false);

  CHECK(PlayerMatch(a, b, {&a, &b, &c, &d}));
  CHECK(PlayerMatch(c, d, {&a, &b, &c, &d}));
  CHECK(WaitFor([&] { return a.service.Assignment() && b.service.Assignment() && c.service.Assignment() && d.service.Assignment(); },
                {&a, &b, &c, &d}));
  CHECK(a.service.snapshot().match->peer_player == b.player);
  CHECK(c.service.snapshot().match->peer_player == d.player);
  CHECK(a.service.Assignment()->host && c.service.Assignment()->host);
  CHECK(!b.service.Assignment()->host && !d.service.Assignment()->host);
}

void TestVersionsMustMatch(const std::string& url) {
  Client a(url, kGuestBase013, '1', "A");
  Client b(url, kGuestTu023, '2', "B");
  CHECK(BecomeFriends(a, b));
  CHECK(!a.service.snapshot().friends[0].compatible);
  a.service.HostPlayerMatch({});
  b.service.SetPlayerMatchSearch(true);
  CHECK(WaitFor([&] { return !a.service.snapshot().player_match.session.empty() &&
                             b.service.snapshot().player_match.search_generation > 1; }, {&a, &b}));
  CHECK(b.service.PlayerMatchResults().empty());
}

void TestFriendGoesOfflineAndReconnectKeepsFriends(const std::string& url) {
  Client a(url, kGuestBase013, '3', "A");
  Client b(url, kGuestBase013, '4', "B");
  CHECK(BecomeFriends(a, b));
  b.service.Apply({false, "B"});
  CHECK(WaitFor([&] { return !a.service.snapshot().friends[0].online; }, {&a, &b}));
  b.service.Apply({true, "B"});
  CHECK(WaitFor([&] { return a.service.snapshot().friends[0].online && b.service.snapshot().friends.size() == 1; }, {&a, &b}));
}

}  // namespace

int main() {
  const char* url = std::getenv("SSF2T_LOBBY_TEST_URL");
  if (!url || !*url) {
    std::printf("skipped: SSF2T_LOBBY_TEST_URL is not set\n");
    return 77;
  }
  TestFriendsPlayerMatchAndHandoff(url);
  TestStrangersAndSimultaneousMatches(url);
  TestVersionsMustMatch(url);
  TestFriendGoesOfflineAndReconnectKeepsFriends(url);
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
