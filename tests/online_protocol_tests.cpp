// Online protocol encoding and decoding: requests, answers, events, malformed input, friend codes, version
// compatibility and the peer address choice. Headless.
// Run: ssf2t_online_protocol_tests

#include <cstdio>
#include <string>

#include "online/protocol.h"

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

bool Contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

void TestRequestsAreWellFormed() {
  const std::string hello = EncodeHello(3, {"tu023", 1, "ABCD-2345", std::string(64, 'a'), "Sa\"ulo", "0.2.2", "Windows", true});
  CHECK(hello.rfind("{\"op\":\"hello\",\"seq\":3,", 0) == 0);
  CHECK(Contains(hello, "\"proto\":3") && Contains(hello, "\"game\":\"ssf2t-hd-remix-pc\"") &&
        Contains(hello, "\"guest\":\"tu023\"") && Contains(hello, "\"build\":1") && Contains(hello, "\"name\":\"Sa\\\"ulo\"") &&
        Contains(hello, "\"presence\":true") && Contains(hello, "\"version\":\"0.2.2\"") &&
        Contains(hello, "\"platform\":\"Windows\""));
  CHECK(Contains(EncodeHello(1, {"base013", 1, "p", "t", "n", "", "", false}), "\"presence\":false"));
  CHECK(EncodeFriendsAdd(4, "ABCD-2345") == "{\"op\":\"friends.add\",\"seq\":4,\"code\":\"ABCD-2345\"}");
  CHECK(EncodePlayerMatchHost(5, {{"rounds", 2}, {"timer", 1}}) ==
        "{\"op\":\"pm.host\",\"seq\":5,\"slots\":2,\"rules\":{\"rounds\":2,\"timer\":1}}");
  CHECK(EncodePlayerMatchHost(6, {}) == "{\"op\":\"pm.host\",\"seq\":6,\"slots\":2,\"rules\":{}}");
  CHECK(EncodePlayerMatchJoin(7, "00112233445566aa") == "{\"op\":\"pm.join\",\"seq\":7,\"session\":\"00112233445566aa\"}");
  CHECK(EncodePlayerMatchSearch(1) == "{\"op\":\"pm.search\",\"seq\":1}");
  CHECK(EncodePlayerMatchCancel(1) == "{\"op\":\"pm.cancel\",\"seq\":1}");
  CHECK(Contains(EncodeFriendsAccept(1, "x"), "friends.accept") && Contains(EncodeFriendsDecline(1, "x"), "friends.decline") &&
        Contains(EncodeFriendsRemove(1, "x"), "friends.remove") && Contains(EncodeFriendsList(1), "friends.list") &&
        Contains(EncodeFriendsRequests(1), "friends.requests") && Contains(EncodePing(1), "\"op\":\"ping\""));
  const std::string endpoint = EncodeMatchEndpoint(6, "m1", 7001, {"127.0.0.1", "192.168.1.5"});
  CHECK(Contains(endpoint, "\"op\":\"match.endpoint\"") && Contains(endpoint, "\"match\":\"m1\"") &&
        Contains(endpoint, "\"port\":7001") && Contains(endpoint, "\"lan\":[\"127.0.0.1\",\"192.168.1.5\"]") &&
        !Contains(endpoint, "public_ip") && !Contains(endpoint, "public_port"));
  // The STUN-mapped endpoint goes whole: its own port, never the local one.
  const std::string mapped = EncodeMatchEndpoint(6, "m1", 7001, {}, "179.113.149.63", 54114);
  CHECK(Contains(mapped, "\"port\":7001") && Contains(mapped, "\"public_ip\":\"179.113.149.63\"") &&
        Contains(mapped, "\"public_port\":54114"));
  CHECK(!Contains(EncodeMatchEndpoint(6, "m1", 7001, {}, "179.113.149.63", 0), "public_ip"));
  CHECK(!Contains(EncodeMatchEndpoint(6, "m1", 7001, {}, "", 54114), "public_port"));
  CHECK(Contains(EncodeMatchReady(1, "m1", false), "\"ready\":false") && Contains(EncodeMatchLeave(1, "m1"), "match.leave") &&
        Contains(EncodeMatchResume(1, "m1"), "match.resume"));
}

void TestAnswersDecode() {
  auto ok = DecodeServerMessage("{\"re\":7,\"code\":\"ok\",\"friend_code\":\"ABCD-2345\"}");
  CHECK(ok && ok->kind == ServerMessage::Kind::kResponse && ok->seq == 7 && ok->code == "ok" && ok->friend_code == "ABCD-2345");
  auto list = DecodeServerMessage(
      "{\"re\":8,\"code\":\"ok\",\"friends\":[{\"player\":\"ABCD-2345\",\"name\":\"A\",\"online\":true,\"state\":\"player_match\","
      "\"platform\":\"Linux\",\"version\":\"0.3.0\",\"guest\":\"tu023\",\"compatible\":false},"
      "{\"player\":\"EFGH-6789\",\"name\":\"B\",\"online\":false,\"state\":\"offline\"}]}");
  CHECK(list && list->friends && list->friends->size() == 2);
  CHECK(list && (*list->friends)[0].state == FriendState::kPlayerMatch && !(*list->friends)[0].compatible &&
        (*list->friends)[0].platform == "Linux" && (*list->friends)[0].guest == "tu023");
  CHECK(list && !(*list->friends)[1].online && (*list->friends)[1].state == FriendState::kOffline);
  auto accept = DecodeServerMessage(
      "{\"re\":9,\"code\":\"ok\",\"match\":\"m1\",\"role\":\"join\",\"guest\":\"base013\",\"build\":1,"
      "\"peer\":{\"player\":\"ABCD-2345\",\"name\":\"A\"},\"peer_lost\":false}");
  CHECK(accept && accept->match && accept->match->role == "join" && accept->match->peer_name == "A" && accept->match->build == 1);
  auto hosted = DecodeServerMessage("{\"re\":3,\"code\":\"ok\",\"session\":\"00112233445566aa\"}");
  CHECK(hosted && hosted->session == "00112233445566aa" && !hosted->match);
  auto search = DecodeServerMessage(
      "{\"re\":4,\"code\":\"ok\",\"sessions\":[{\"session\":\"00112233445566aa\",\"player\":\"ABCD-2345\",\"name\":\"A\","
      "\"guest\":\"tu023\",\"build\":1,\"slots\":2,\"rules\":{\"rounds\":2,\"timer\":1},\"platform\":\"Windows\","
      "\"version\":\"0.2.2\"}]}");
  CHECK(search && search->sessions && search->sessions->size() == 1);
  CHECK(search && (*search->sessions)[0].guest == "tu023" && (*search->sessions)[0].slots == 2 &&
        (*search->sessions)[0].rules == (PlayerMatchRules{{"rounds", 2}, {"timer", 1}}));
  CHECK(!DecodeServerMessage("{\"re\":4,\"code\":\"ok\",\"sessions\":[{\"player\":\"A\"}]}")->sessions);
  auto joined = DecodeServerMessage(
      "{\"re\":9,\"code\":\"ok\",\"session\":\"00112233445566aa\",\"match\":\"m1\",\"role\":\"join\","
      "\"guest\":\"base013\",\"build\":1,\"peer\":{\"player\":\"ABCD-2345\",\"name\":\"A\"}}");
  CHECK(joined && joined->match && joined->match->session == "00112233445566aa" && joined->match->role == "join");
  auto error = DecodeServerMessage("{\"re\":2,\"code\":\"version_mismatch\"}");
  CHECK(error && error->code == "version_mismatch" && !error->match && !error->friends);
}

void TestEventsDecode() {
  using Kind = ServerMessage::Kind;
  auto peer = DecodeServerMessage(
      "{\"ev\":\"match.peer\",\"match\":\"m1\",\"player\":\"ABCD-2345\",\"name\":\"A\",\"role\":\"host\",\"ip\":\"203.0.113.9\","
      "\"port\":7001,\"lan\":[\"127.0.0.1\",\"192.168.1.9\"],\"same_network\":true}");
  CHECK(peer && peer->kind == Kind::kMatchPeer && peer->peer.port == 7001 && peer->peer.lan.size() == 2 && peer->peer.same_network);
  CHECK(peer->peer.public_ip.empty() && peer->peer.public_port == 0);
  const auto mapped_peer = DecodeServerMessage(
      "{\"ev\":\"match.peer\",\"match\":\"m1\",\"role\":\"host\",\"ip\":\"2804:14d::1\",\"port\":7001,"
      "\"public_ip\":\"177.18.65.229\",\"public_port\":57219}");
  CHECK(mapped_peer && mapped_peer->peer.ip == "2804:14d::1" && mapped_peer->peer.public_ip == "177.18.65.229" &&
        mapped_peer->peer.public_port == 57219);
  for (const char* bad : {"\"public_ip\":\"177.18.65.229\",\"public_port\":0",
                          "\"public_ip\":\"177.18.65.229\",\"public_port\":70000",
                          "\"public_ip\":\"177.18.65.229\"", "\"public_port\":57219",
                          "\"public_ip\":\"\",\"public_port\":57219"}) {
    const auto partial = DecodeServerMessage(
        std::string("{\"ev\":\"match.peer\",\"match\":\"m1\",\"role\":\"host\",\"ip\":\"1.2.3.4\",\"port\":7001,") + bad + "}");
    CHECK(partial && partial->peer.public_ip.empty() && partial->peer.public_port == 0);
  }
  auto created = DecodeServerMessage(
      "{\"ev\":\"match.created\",\"match\":\"m1\",\"role\":\"host\",\"guest\":\"tu023\",\"build\":1,\"peer\":{\"player\":\"p\",\"name\":\"n\"}}");
  CHECK(created && created->kind == Kind::kMatchCreated && created->match && created->match->guest == "tu023");
  CHECK(!DecodeServerMessage("{\"ev\":\"challenge\",\"player\":\"ABCD-2345\",\"name\":\"A\"}"));
  CHECK(!DecodeServerMessage("{\"ev\":\"challenge.ended\",\"player\":\"ABCD-2345\",\"reason\":\"declined\"}"));
  auto status = DecodeServerMessage("{\"ev\":\"friend.status\",\"friend\":{\"player\":\"ABCD-2345\",\"name\":\"A\",\"online\":true,\"state\":\"in_game\"}}");
  CHECK(status && status->kind == Kind::kFriendStatus && status->friend_view.state == FriendState::kInGame);
  CHECK(DecodeServerMessage("{\"ev\":\"match.ended\",\"match\":\"m1\",\"reason\":\"left\"}")->kind == Kind::kMatchEnded);
  CHECK(DecodeServerMessage("{\"ev\":\"match.peer_lost\",\"match\":\"m1\"}")->kind == Kind::kMatchPeerLost);
  CHECK(DecodeServerMessage("{\"ev\":\"match.peer_back\",\"match\":\"m1\"}")->kind == Kind::kMatchPeerBack);
  CHECK(DecodeServerMessage("{\"ev\":\"match.ready\",\"match\":\"m1\",\"ready\":true}")->ready);
  CHECK(DecodeServerMessage("{\"ev\":\"friend.request\",\"player\":\"ABCD-2345\",\"name\":\"A\"}")->kind == Kind::kFriendRequest);
  CHECK(DecodeServerMessage("{\"ev\":\"friend.offline\",\"player\":\"ABCD-2345\"}")->kind == Kind::kFriendOffline);
  CHECK(DecodeServerMessage("{\"ev\":\"friend.removed\",\"player\":\"ABCD-2345\"}")->kind == Kind::kFriendRemoved);
}

void TestMalformedMessagesAreRejected() {
  for (const char* text : {"", "x", "[]", "{}", "{\"re\":1}", "{\"ev\":5}", "{\"ev\":\"unknown\"}", "{\"ev\":\"match.peer\"}",
                           "{\"ev\":\"match.peer\",\"match\":\"m\",\"role\":\"join\",\"ip\":\"1.2.3.4\",\"port\":70000}",
                           "{\"ev\":\"match.peer\",\"match\":\"m\",\"role\":\"boss\",\"ip\":\"1.2.3.4\",\"port\":7001}",
                           "{\"ev\":\"match.created\",\"match\":\"m\",\"role\":\"join\"}",
                           "{\"ev\":\"challenge\"}", "{\"ev\":\"friend.status\",\"friend\":5}", "{\"ev\":\"match.ended\"}"}) {
    CHECK(!DecodeServerMessage(text));
  }
}

void TestFriendCodes() {
  CHECK(NormalizeFriendCode("abcd 2345") == "ABCD-2345");
  CHECK(NormalizeFriendCode("ABCD-2345") == "ABCD-2345");
  CHECK(NormalizeFriendCode("abcd2345") == "ABCD-2345");
  for (const char* bad : {"", "ABCD", "ABCD-23456", "ABCI-2345", "ABCO-2345", "ABC0-2345", "ABC1-2345", "ABCD-234!"}) {
    CHECK(NormalizeFriendCode(bad).empty());
  }
}

void TestPastedFriendCodesAreNormalized() {
  for (const char* pasted : {"ABCD-2345", "abcd-2345", "ABCD2345", "abcd2345", "ABCD 2345", " ABCD-2345\r\n", "\tabcd 2345 \n",
                             "  a b c d - 2 3 4 5  ", "CEQN-Z7SE\r\n"}) {
    const std::string code = NormalizeFriendCode(pasted);
    CHECK(code.size() == 9 && code[4] == '-' && NormalizeFriendCode(code) == code);
  }
  CHECK(NormalizeFriendCode("ceqn z7se\r\n") == "CEQN-Z7SE");
  CHECK(NormalizeFriendCode("SXFF8P9T") == "SXFF-8P9T");
}

void TestPartialAndMalformedTextIsNotACode() {
  for (const char* typed : {"A", "ABCD", "ABCD-", "ABCD-234", "ABC!-2345", "ABCD-23456", "ABCD\xC2\xA0" "2345", "abcd-1234",
                            "ABCI-OOOO", "\r\n"}) {
    CHECK(NormalizeFriendCode(typed).empty());
  }
}

void TestVersionCompatibility() {
  CHECK(GuestsCompatible(kGuestBase013, 1, kGuestBase013, 1));
  CHECK(GuestsCompatible(kGuestTu023, 1, kGuestTu023, 1));
  CHECK(!GuestsCompatible(kGuestBase013, 1, kGuestTu023, 1));
  CHECK(!GuestsCompatible(kGuestTu023, 1, kGuestBase013, 1));
  CHECK(!GuestsCompatible(kGuestBase013, 1, kGuestBase013, 2));
  CHECK(DescribeServiceError("version_mismatch") == "Game versions do not match.");
}

void TestPeerAddressChoice() {
  const std::vector<std::string> mine = {"127.0.0.1", "192.168.1.5"};
  PeerInfo remote;
  remote.ip = "203.0.113.9";
  remote.lan = {"127.0.0.1", "10.0.0.4"};
  CHECK(ChoosePeerAddress(mine, remote) == "203.0.113.9");
  remote.same_network = true;
  CHECK(ChoosePeerAddress(mine, remote) == "10.0.0.4");
  remote.lan = {"127.0.0.1", "192.168.1.5"};
  CHECK(ChoosePeerAddress(mine, remote) == "127.0.0.1");
  remote.lan = {"127.0.0.1"};
  CHECK(ChoosePeerAddress(mine, remote) == "127.0.0.1");
}

}  // namespace


void TestInviteMessages() {
  using Kind = ServerMessage::Kind;
  CHECK(EncodeInviteSend(3, "ABCD-2345") == "{\"op\":\"invite.send\",\"seq\":3,\"player\":\"ABCD-2345\"}");
  CHECK(EncodeInviteDecline(4, "i1") == "{\"op\":\"invite.decline\",\"seq\":4,\"invite\":\"i1\"}");
  auto sent = DecodeServerMessage("{\"re\":3,\"code\":\"ok\",\"invite\":\"i1\",\"session\":\"00112233445566aa\"}");
  CHECK(sent && sent->invite_id == "i1" && sent->session == "00112233445566aa");
  auto invite = DecodeServerMessage(
      "{\"ev\":\"invite\",\"invite\":{\"id\":\"i1\",\"session\":\"00112233445566aa\",\"player\":\"ABCD-2345\","
      "\"name\":\"A\",\"guest\":\"tu023\",\"build\":1,\"slots\":2,\"rules\":{\"rounds\":2},\"qos\":\"ab\",\"expires\":5}}");
  CHECK(invite && invite->kind == Kind::kInvite && invite->invite && invite->invite->id == "i1");
  CHECK(invite && invite->invite && invite->invite->ad.session == "00112233445566aa" && invite->invite->ad.qos == "ab");
  CHECK(!DecodeServerMessage("{\"ev\":\"invite\",\"invite\":{\"session\":\"00112233445566aa\",\"player\":\"x\",\"guest\":\"tu023\"}}"));
  CHECK(!DecodeServerMessage("{\"ev\":\"invite\",\"invite\":{\"id\":\"i1\"}}"));
  auto ended = DecodeServerMessage("{\"ev\":\"invite.ended\",\"invite\":\"i1\",\"session\":\"s\",\"reason\":\"declined\"}");
  CHECK(ended && ended->kind == Kind::kInviteEnded && ended->invite_id == "i1" && ended->reason == "declined");
  CHECK(!DecodeServerMessage("{\"ev\":\"invite.ended\",\"session\":\"s\"}"));
}

// The relay's signalling: the client says it can relay, the service says what it can do, a relayed match carries its
// token, the host's transport choice reaches the joiner, and a relay that ends says why.
void TestRelaySignalling() {
  HelloFields hello{"base013", 1, "ABCD-2345", "tok", "Saulo", "0.2.2", "Windows", true, true};
  CHECK(Contains(EncodeHello(1, hello), ",\"relay\":true"));
  hello.relay = false;
  CHECK(!Contains(EncodeHello(1, hello), "relay"));

  const auto greeted = DecodeServerMessage(
      "{\"re\":1,\"code\":\"ok\",\"friend_code\":\"ABCD-2345\",\"capabilities\":{\"protocol_version\":3,"
      "\"worker_build\":\"2026-10-31-relay1\",\"public_endpoints\":true,\"relay_supported\":true,"
      "\"relay_max_payload\":1472}}");
  CHECK(greeted && greeted->capabilities && greeted->capabilities->announced);
  CHECK(greeted->capabilities->protocol_version == 3 && greeted->capabilities->worker_build == "2026-10-31-relay1");
  CHECK(greeted->capabilities->public_endpoints && greeted->capabilities->relay_supported);
  CHECK(greeted->capabilities->relay_max_payload == 1472);
  const auto legacy = DecodeServerMessage("{\"re\":1,\"code\":\"ok\",\"friend_code\":\"ABCD-2345\"}");
  CHECK(legacy && !legacy->capabilities);
  CHECK(!ServiceCapabilities{}.relay_supported && !ServiceCapabilities{}.public_endpoints);

  const auto created = DecodeServerMessage(
      "{\"ev\":\"match.created\",\"session\":\"s1\",\"match\":\"m1\",\"role\":\"host\",\"guest\":\"base013\","
      "\"build\":1,\"peer\":{\"player\":\"EFGH-6789\",\"name\":\"Munari\"},\"relay_token\":4000000000}");
  CHECK(created && created->match && created->match->relay_token == 4000000000u);
  const auto joined = DecodeServerMessage(
      "{\"re\":5,\"code\":\"ok\",\"session\":\"s1\",\"match\":\"m1\",\"role\":\"join\",\"guest\":\"base013\","
      "\"build\":1,\"peer\":{\"player\":\"ABCD-2345\",\"name\":\"Saulo\"},\"relay_token\":77}");
  CHECK(joined && joined->match && joined->match->relay_token == 77);
  for (const char* token : {"-1", "4294967296", "\"77\"", "0"}) {
    const auto odd = DecodeServerMessage(std::string("{\"ev\":\"match.created\",\"match\":\"m1\",\"role\":\"host\","
                                                     "\"guest\":\"base013\",\"relay_token\":") + token + "}");
    CHECK(odd && odd->match && odd->match->relay_token == 0);
  }
  const auto older = DecodeServerMessage(
      "{\"ev\":\"match.created\",\"match\":\"m1\",\"role\":\"host\",\"guest\":\"base013\"}");
  CHECK(older && older->match->relay_token == 0);

  const auto relay = DecodeServerMessage("{\"ev\":\"match.transport\",\"match\":\"m1\",\"transport\":\"relay\"}");
  CHECK(relay && relay->kind == ServerMessage::Kind::kMatchTransport && relay->match_id == "m1" &&
        relay->transport == "relay");
  const auto direct = DecodeServerMessage("{\"ev\":\"match.transport\",\"match\":\"m1\",\"transport\":\"direct\"}");
  CHECK(direct && direct->transport == "direct");
  CHECK(!DecodeServerMessage("{\"ev\":\"match.transport\",\"match\":\"m1\",\"transport\":\"tcp\"}"));
  CHECK(!DecodeServerMessage("{\"ev\":\"match.transport\",\"transport\":\"relay\"}"));
  const auto closed = DecodeServerMessage("{\"ev\":\"relay.closed\",\"match\":\"m1\",\"reason\":\"peer_disconnected\"}");
  CHECK(closed && closed->kind == ServerMessage::Kind::kRelayClosed && closed->reason == "peer_disconnected");
  CHECK(!DecodeServerMessage("{\"ev\":\"relay.closed\",\"reason\":\"x\"}"));

  CHECK(EncodeMatchTransport(9, "m1", "relay") == "{\"op\":\"match.transport\",\"seq\":9,\"match\":\"m1\",\"transport\":\"relay\"}");
  CHECK(DescribeServiceError("relay_unavailable") == "The connection through the online service is not available.");
}

int main() {
  std::printf("online protocol tests\n");
  TestRelaySignalling();
  TestRequestsAreWellFormed();
  TestAnswersDecode();
  TestInviteMessages();
  TestEventsDecode();
  TestMalformedMessagesAreRejected();
  TestFriendCodes();
  TestPastedFriendCodesAreNormalized();
  TestPartialAndMalformedTextIsNotACode();
  TestVersionCompatibility();
  TestPeerAddressChoice();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
