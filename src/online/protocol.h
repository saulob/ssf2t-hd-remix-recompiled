#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ssf2t::online {

inline constexpr int kLobbyProtocol = 3;
inline constexpr char kGameId[] = "ssf2t-hd-remix-pc";
inline constexpr char kGuestBase013[] = "base013";
inline constexpr char kGuestTu023[] = "tu023";

enum class FriendState { kOffline, kOnline, kPlayerMatch, kConnecting, kInGame };

struct Friend {
  std::string player;  // the friend code, XXXX-XXXX
  std::string name;
  bool online = false;
  FriendState state = FriendState::kOffline;
  std::string platform;
  std::string version;
  std::string guest;
  bool compatible = true;
};

struct Request {
  std::string player;
  std::string name;
};

// The game's Player Match settings that both sides must agree on, as small named integers.
using PlayerMatchRules = std::vector<std::pair<std::string, int>>;

// A friend's advertised Player Match, as a search returns it.
struct PlayerMatchAd {
  std::string session;
  std::string player;
  std::string name;
  std::string guest;
  int build = 0;
  int slots = 2;
  PlayerMatchRules rules;
  std::string qos;  // the host's QoS listen data, lower-case hex
  std::string platform;
  std::string version;
};

// A friend's invitation to the Player Match they host: the same advertisement a search returns.
struct Invite {
  std::string id;
  PlayerMatchAd ad;
};

struct MatchInfo {
  std::string match;
  std::string session;  // the Player Match it came from
  std::string role;     // "host" (Player 1, the Player Match owner) or "join" (Player 2)
  std::string guest;
  int build = 0;
  std::string peer_player;
  std::string peer_name;
  uint32_t relay_token = 0;  // nonzero: the service relays this match's frames, if it ever has to
};

// What the service says it can do, in its hello answer; an older service says nothing, which reads as all false.
struct ServiceCapabilities {
  bool announced = false;
  int protocol_version = 0;
  std::string worker_build;
  bool public_endpoints = false;
  bool relay_supported = false;
  int relay_max_payload = 0;
};

struct PeerInfo {
  std::string match;
  std::string player;
  std::string name;
  std::string role;
  std::string ip;
  uint16_t port = 0;
  std::vector<std::string> lan;
  bool same_network = false;
  std::string public_ip;  // its gameplay socket's STUN-mapped endpoint; empty when it sent none
  uint16_t public_port = 0;
  bool service_forwards_public = false;  // the service passes public endpoints on at all (an older one does not)
};

struct ServerMessage {
  enum class Kind {
    kResponse,
    kFriendRequest,
    kFriendAdded,
    kFriendRemoved,
    kFriendStatus,
    kFriendOffline,
    kMatchCreated,
    kMatchPeer,
    kMatchEnded,
    kMatchReady,
    kMatchPeerLost,
    kMatchPeerBack,
    kInvite,
    kInviteEnded,
    kMatchTransport,
    kRelayClosed,
  };
  Kind kind = Kind::kResponse;

  int seq = 0;
  std::string code;  // responses: "ok" or an error code
  std::string friend_code;
  std::optional<std::vector<Friend>> friends;
  std::optional<std::vector<Request>> incoming;
  std::optional<std::vector<Request>> outgoing;
  std::string state;  // friends.add: "pending" or "friends"
  std::string session;  // pm.host
  std::optional<std::vector<PlayerMatchAd>> sessions;  // pm.search
  std::optional<MatchInfo> match;

  Friend friend_view;  // friend.* events
  Request request;     // friend.request
  std::string player;  // friend.removed, friend.offline
  std::string reason;  // match.ended, invite.ended
  std::optional<Invite> invite;  // invite
  std::string invite_id;         // invite.send, invite.ended
  std::string match_id;
  bool ready = false;
  PeerInfo peer;
  std::string transport;  // match.transport: "direct" or "relay"
  std::optional<ServiceCapabilities> capabilities;  // hello
};

// Nothing for text that is not a known, well-formed server message.
std::optional<ServerMessage> DecodeServerMessage(std::string_view text);

struct HelloFields {
  std::string guest;
  int build = 0;
  std::string player;
  std::string token;
  std::string name;
  std::string version;
  std::string platform;
  bool presence = true;  // false: a short session that is not shown to friends (the connection test)
  bool relay = false;    // this client can play a match through the service's relay
};

std::string EncodeHello(int seq, const HelloFields& hello);
std::string EncodePing(int seq);
std::string EncodeFriendsList(int seq);
std::string EncodeFriendsRequests(int seq);
std::string EncodeFriendsAdd(int seq, const std::string& code);
std::string EncodeFriendsAccept(int seq, const std::string& player);
std::string EncodeFriendsDecline(int seq, const std::string& player);
std::string EncodeFriendsRemove(int seq, const std::string& player);
std::string EncodePlayerMatchHost(int seq, const PlayerMatchRules& rules, const std::string& qos_hex = {});
std::string EncodePlayerMatchCancel(int seq);
std::string EncodePlayerMatchSearch(int seq);
std::string EncodePlayerMatchJoin(int seq, const std::string& session);
std::string EncodeInviteSend(int seq, const std::string& player);
std::string EncodeInviteDecline(int seq, const std::string& invite);
// `port` is the gameplay socket's local port; `public_ip`:`public_port` is that socket's STUN-mapped endpoint, left out
// when `public_port` is 0.
std::string EncodeMatchEndpoint(int seq, const std::string& match, uint16_t port, const std::vector<std::string>& lan,
                                const std::string& public_ip = {}, uint16_t public_port = 0);
std::string EncodeMatchReady(int seq, const std::string& match, bool ready);
std::string EncodeMatchLeave(int seq, const std::string& match);
// The host's choice for the match, "direct" or "relay"; the service tells the joiner.
std::string EncodeMatchTransport(int seq, const std::string& match, const std::string& transport);
std::string EncodeMatchResume(int seq, const std::string& match);

std::string HexOf(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> BytesOfHex(std::string_view hex);

// "abcd 2345" and "ABCD-2345" both give "ABCD-2345"; empty when the text is not a friend code.
std::string NormalizeFriendCode(std::string_view text);

// User-facing sentence for a service error code.
std::string DescribeServiceError(const std::string& code);

// The same rule the fight handshake enforces: same recompiled image and same netplay build.
bool GuestsCompatible(const std::string& guest_a, int build_a, const std::string& guest_b, int build_b);

// The address the joiner uses for the host's gameplay port. `mine` are this PC's own LAN candidates.
std::string ChoosePeerAddress(const std::vector<std::string>& mine, const PeerInfo& peer);

// "Online · Windows · v0.2.2" (or "Online Match" / "In game" first) for a friend that is online, "Offline" otherwise.
std::string FriendStatusLine(const Friend& entry);

}  // namespace ssf2t::online
