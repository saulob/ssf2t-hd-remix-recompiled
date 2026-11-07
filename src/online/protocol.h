#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "online/secret_characters.h"

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

// A player who chose to be listed in the public Lobby. `player` is the friend code Add friend sends to; it is never shown.
struct LobbyPlayer {
  std::string player;
  std::string name;
  FriendState state = FriendState::kOnline;
  std::string platform;
  std::string version;
  std::string guest;
  bool compatible = true;
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
  std::optional<MatchSecrets> secret;  // the room's secret characters; empty from a service that does not say
};

// What the service says it can do, in its hello answer; an older service says nothing, which reads as all false.
struct ServiceCapabilities {
  bool announced = false;
  int protocol_version = 0;
  std::string worker_build;
  bool public_endpoints = false;
  bool relay_supported = false;
  int relay_max_payload = 0;
  bool fight_telemetry = false;  // the service takes passive fight reports (telemetry.fight)
  bool secret_characters = false;  // the service negotiates secret characters (hello secret, secret.update)
  bool public_lobby = false;       // the service has the public Lobby (lobby.publish, lobby.list)
  bool bug_reports = false;        // the service takes problem reports (incident.file, incident.status, incident.read)
};

// What the service says of one report its owner holds a receipt for.
struct IncidentStatus {
  std::string ticket;
  std::string status;
  int64_t updated_at = 0;
  std::string fixed_version;
  std::string player_message;
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
    kMatchSecret,
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
  std::optional<MatchSecrets> secret;               // match.secret, and the answer to secret.update inside a match
  std::optional<std::vector<LobbyPlayer>> lobby_players;  // lobby.list; absent when unchanged
  bool lobby_unchanged = false;                           // lobby.list: the revision the client sent is still current
  int64_t lobby_rev = -1;                                 // lobby.list: -1 when the answer carried none
  std::optional<bool> lobby_visible;                      // lobby.publish and lobby.list: whether this connection is listed
  std::string incident_ticket;                            // incident.file
  std::string incident_receipt;                           // incident.file: proves ownership of the report, never logged
  int64_t incident_submitted_at = 0;                      // incident.file
  std::optional<std::vector<IncidentStatus>> incident_statuses;      // incident.status
  std::optional<std::map<std::string, std::string>> incident_report;  // incident.read: the stored report's text fields
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
  bool telemetry = false;  // this client reports its online fights, anonymously
  SecretCapabilities secret;  // the secret characters this client understands and has turned on
};

std::string EncodeHello(int seq, const HelloFields& hello);
// The same capabilities again after the player changed an option while signed in.
std::string EncodeSecretUpdate(int seq, const SecretCapabilities& secret);
std::string EncodePing(int seq);
std::string EncodeLobbyPublish(int seq, bool visible);
// `held_rev` is the revision of the list the client already shows; the service then answers "unchanged" while it stands.
std::string EncodeLobbyList(int seq, std::optional<int64_t> held_rev = std::nullopt);

// `text` as a JSON string literal.
std::string JsonQuote(std::string_view text);

// A problem report: every field is a string the player could read in the preview.
using IncidentFields = std::vector<std::pair<std::string, std::string>>;
struct IncidentClaim {
  std::string ticket;
  std::string receipt;
};
std::string EncodeIncidentFile(int seq, const IncidentFields& fields);
std::string EncodeIncidentStatus(int seq, const std::vector<IncidentClaim>& claims);
std::string EncodeIncidentRead(int seq, const IncidentClaim& claim);
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

// One passive fight event of the match, from its host: kind "start" or "complete"; `result` ("p1", "p2" or "draw") only
// for a completion. Characters are the game's internal ids.
struct FightEventFields {
  std::string kind;
  uint32_t fight = 0;
  int p1 = 0;
  int p2 = 0;
  std::string build;      // base013 or tu023
  std::string transport;  // direct or relay
  std::string platform;   // Windows or Linux
  std::string result;
};
std::string EncodeTelemetryFight(int seq, const std::string& match, const FightEventFields& event);

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
