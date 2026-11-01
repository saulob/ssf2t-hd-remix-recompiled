#include "online/protocol.h"

#include <algorithm>
#include <cctype>

#include "localization/ui_text.h"
#include "updater/json.h"

namespace ssf2t::online {
namespace json = trg::update::json;
namespace {

constexpr char kLoopback[] = "127.0.0.1";

std::string Quote(std::string_view text) {
  std::string out = "\"";
  for (const char c : text) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          static constexpr char kHex[] = "0123456789abcdef";
          out += "\\u00";
          out += kHex[(c >> 4) & 0x0F];
          out += kHex[c & 0x0F];
        } else {
          out += c;
        }
    }
  }
  return out + "\"";
}

std::string Envelope(const char* op, int seq, const std::string& fields = {}) {
  return std::string("{\"op\":") + Quote(op) + ",\"seq\":" + std::to_string(seq) + fields + "}";
}

std::string WithPlayer(const char* op, int seq, const std::string& player) {
  return Envelope(op, seq, ",\"player\":" + Quote(player));
}

std::string WithMatch(const char* op, int seq, const std::string& match, const std::string& more = {}) {
  return Envelope(op, seq, ",\"match\":" + Quote(match) + more);
}

std::string StringOf(const json::Value* v) { return v && v->IsString() ? v->AsString() : std::string(); }
bool BoolOf(const json::Value* v, bool fallback) { return v && v->IsBool() ? v->AsBool() : fallback; }
int IntOf(const json::Value* v) {
  const auto n = v && v->IsNumber() ? v->AsInt64() : std::nullopt;
  return n ? int(*n) : 0;
}

FriendState StateOf(const std::string& text) {
  if (text == "online") return FriendState::kOnline;
  if (text == "player_match") return FriendState::kPlayerMatch;
  if (text == "connecting") return FriendState::kConnecting;
  if (text == "in_game") return FriendState::kInGame;
  return FriendState::kOffline;
}

std::optional<Friend> ReadFriend(const json::Value* v) {
  if (!v || !v->IsObject()) return std::nullopt;
  Friend f;
  f.player = StringOf(v->Find("player"));
  f.name = StringOf(v->Find("name"));
  if (f.player.empty()) return std::nullopt;
  f.online = BoolOf(v->Find("online"), false);
  f.state = f.online ? StateOf(StringOf(v->Find("state"))) : FriendState::kOffline;
  f.platform = StringOf(v->Find("platform"));
  f.version = StringOf(v->Find("version"));
  f.guest = StringOf(v->Find("guest"));
  f.compatible = BoolOf(v->Find("compatible"), true);
  return f;
}

std::optional<std::vector<Friend>> ReadFriends(const json::Value* v) {
  if (!v || !v->IsArray()) return std::nullopt;
  std::vector<Friend> out;
  for (const json::Value& row : v->AsArray()) {
    const std::optional<Friend> f = ReadFriend(&row);
    if (!f) return std::nullopt;
    out.push_back(*f);
  }
  return out;
}

std::optional<std::vector<Request>> ReadRequests(const json::Value* v) {
  if (!v || !v->IsArray()) return std::nullopt;
  std::vector<Request> out;
  for (const json::Value& row : v->AsArray()) {
    if (!row.IsObject()) return std::nullopt;
    Request r{StringOf(row.Find("player")), StringOf(row.Find("name"))};
    if (r.player.empty()) return std::nullopt;
    out.push_back(std::move(r));
  }
  return out;
}

std::optional<PlayerMatchAd> ReadPlayerMatch(const json::Value& row) {
  if (!row.IsObject()) return std::nullopt;
  PlayerMatchAd ad;
  ad.session = StringOf(row.Find("session"));
  ad.player = StringOf(row.Find("player"));
  ad.name = StringOf(row.Find("name"));
  ad.guest = StringOf(row.Find("guest"));
  ad.build = IntOf(row.Find("build"));
  ad.slots = IntOf(row.Find("slots"));
  ad.platform = StringOf(row.Find("platform"));
  ad.qos = StringOf(row.Find("qos"));
  ad.version = StringOf(row.Find("version"));
  if (const json::Value* rules = row.Find("rules"); rules && rules->IsObject()) {
    for (const auto& [key, value] : rules->AsObject()) {
      if (value.IsNumber()) ad.rules.emplace_back(key, IntOf(&value));
    }
  }
  if (ad.session.empty() || ad.player.empty() || ad.guest.empty()) return std::nullopt;
  return ad;
}

std::optional<std::vector<PlayerMatchAd>> ReadPlayerMatches(const json::Value* v) {
  if (!v || !v->IsArray()) return std::nullopt;
  std::vector<PlayerMatchAd> out;
  for (const json::Value& row : v->AsArray()) {
    std::optional<PlayerMatchAd> ad = ReadPlayerMatch(row);
    if (!ad) return std::nullopt;
    out.push_back(std::move(*ad));
  }
  return out;
}

uint32_t RelayTokenOf(const json::Value* v) {
  const auto n = v && v->IsNumber() ? v->AsInt64() : std::nullopt;
  return n && *n > 0 && *n <= 0xFFFFFFFFll ? uint32_t(*n) : 0;
}

ServiceCapabilities ReadCapabilities(const json::Value& v) {
  ServiceCapabilities caps;
  caps.announced = true;
  caps.protocol_version = IntOf(v.Find("protocol_version"));
  caps.worker_build = StringOf(v.Find("worker_build"));
  caps.public_endpoints = BoolOf(v.Find("public_endpoints"), false);
  caps.relay_supported = BoolOf(v.Find("relay_supported"), false);
  caps.relay_max_payload = IntOf(v.Find("relay_max_payload"));
  return caps;
}

bool ReadMatch(const json::Value& v, MatchInfo& out) {
  out.match = StringOf(v.Find("match"));
  out.relay_token = RelayTokenOf(v.Find("relay_token"));
  out.session = StringOf(v.Find("session"));
  out.role = StringOf(v.Find("role"));
  out.guest = StringOf(v.Find("guest"));
  out.build = IntOf(v.Find("build"));
  if (const json::Value* peer = v.Find("peer"); peer && peer->IsObject()) {
    out.peer_player = StringOf(peer->Find("player"));
    out.peer_name = StringOf(peer->Find("name"));
  }
  return !out.match.empty() && (out.role == "host" || out.role == "join") && !out.guest.empty();
}

bool IsLoopback(const std::string& ip) { return ip.rfind("127.", 0) == 0; }

}  // namespace

std::optional<ServerMessage> DecodeServerMessage(std::string_view text) {
  const std::optional<json::Value> root = json::Parse(text);
  if (!root || !root->IsObject()) return std::nullopt;
  ServerMessage message;
  if (root->Find("re")) {
    message.kind = ServerMessage::Kind::kResponse;
    message.seq = IntOf(root->Find("re"));
    message.code = StringOf(root->Find("code"));
    if (message.code.empty()) return std::nullopt;
    message.friend_code = StringOf(root->Find("friend_code"));
    message.invite_id = StringOf(root->Find("invite"));
    message.state = StringOf(root->Find("state"));
    message.sessions = ReadPlayerMatches(root->Find("sessions"));
    if (!root->Find("role")) message.session = StringOf(root->Find("session"));
    message.friends = ReadFriends(root->Find("friends"));
    message.incoming = ReadRequests(root->Find("incoming"));
    message.outgoing = ReadRequests(root->Find("outgoing"));
    if (const json::Value* caps = root->Find("capabilities"); caps && caps->IsObject()) {
      message.capabilities = ReadCapabilities(*caps);
    }
    if (root->Find("match") && root->Find("role")) {
      MatchInfo info;
      if (!ReadMatch(*root, info)) return std::nullopt;
      message.match = info;
    }
    return message;
  }
  const std::string event = StringOf(root->Find("ev"));
  using Kind = ServerMessage::Kind;
  if (event == "friend.status" || event == "friend.added") {
    const std::optional<Friend> f = ReadFriend(root->Find("friend"));
    if (!f) return std::nullopt;
    message.kind = event == "friend.status" ? Kind::kFriendStatus : Kind::kFriendAdded;
    message.friend_view = *f;
    return message;
  }
  if (event == "friend.request") {
    message.kind = Kind::kFriendRequest;
    message.request = {StringOf(root->Find("player")), StringOf(root->Find("name"))};
    return message.request.player.empty() ? std::nullopt : std::optional<ServerMessage>(message);
  }
  if (event == "friend.removed" || event == "friend.offline") {
    message.kind = event == "friend.removed" ? Kind::kFriendRemoved : Kind::kFriendOffline;
    message.player = StringOf(root->Find("player"));
    return message.player.empty() ? std::nullopt : std::optional<ServerMessage>(message);
  }
  if (event == "match.created") {
    MatchInfo info;
    if (!ReadMatch(*root, info)) return std::nullopt;
    message.kind = Kind::kMatchCreated;
    message.match = info;
    return message;
  }
  if (event == "match.ended" || event == "match.ready" || event == "match.peer_lost" || event == "match.peer_back") {
    message.match_id = StringOf(root->Find("match"));
    if (message.match_id.empty()) return std::nullopt;
    message.kind = event == "match.ended" ? Kind::kMatchEnded
                   : event == "match.ready" ? Kind::kMatchReady
                   : event == "match.peer_lost" ? Kind::kMatchPeerLost : Kind::kMatchPeerBack;
    message.reason = StringOf(root->Find("reason"));
    message.ready = BoolOf(root->Find("ready"), false);
    return message;
  }
  if (event == "invite") {
    const json::Value* invite = root->Find("invite");
    const std::optional<PlayerMatchAd> ad = invite ? ReadPlayerMatch(*invite) : std::nullopt;
    const std::string id = invite ? StringOf(invite->Find("id")) : std::string();
    if (!ad || id.empty()) return std::nullopt;
    message.kind = Kind::kInvite;
    message.invite = Invite{id, *ad};
    return message;
  }
  if (event == "invite.ended") {
    message.kind = Kind::kInviteEnded;
    message.invite_id = StringOf(root->Find("invite"));
    message.session = StringOf(root->Find("session"));
    message.reason = StringOf(root->Find("reason"));
    return message.invite_id.empty() ? std::nullopt : std::optional<ServerMessage>(message);
  }
  if (event == "match.transport" || event == "relay.closed") {
    message.kind = event == "match.transport" ? Kind::kMatchTransport : Kind::kRelayClosed;
    message.match_id = StringOf(root->Find("match"));
    message.transport = StringOf(root->Find("transport"));
    message.reason = StringOf(root->Find("reason"));
    const bool known = message.kind == Kind::kRelayClosed || message.transport == "direct" || message.transport == "relay";
    return message.match_id.empty() || !known ? std::nullopt : std::optional<ServerMessage>(message);
  }
  if (event == "match.peer") {
    message.kind = Kind::kMatchPeer;
    PeerInfo& peer = message.peer;
    peer.match = StringOf(root->Find("match"));
    peer.player = StringOf(root->Find("player"));
    peer.name = StringOf(root->Find("name"));
    peer.role = StringOf(root->Find("role"));
    peer.ip = StringOf(root->Find("ip"));
    const int port = IntOf(root->Find("port"));
    if (peer.match.empty() || port < 1 || port > 65535 || (peer.role != "host" && peer.role != "join") ||
        peer.ip.empty()) {
      return std::nullopt;
    }
    peer.port = uint16_t(port);
    peer.same_network = BoolOf(root->Find("same_network"), false);
    if (const json::Value* lan = root->Find("lan"); lan && lan->IsArray()) {
      for (const json::Value& ip : lan->AsArray()) {
        if (ip.IsString()) peer.lan.push_back(ip.AsString());
      }
    }
    peer.service_forwards_public = BoolOf(root->Find("public_endpoints"), false);
    const std::string public_ip = StringOf(root->Find("public_ip"));
    const int public_port = IntOf(root->Find("public_port"));
    if (!public_ip.empty() && public_port >= 1 && public_port <= 65535) {
      peer.public_ip = public_ip;
      peer.public_port = uint16_t(public_port);
    }
    return message;
  }
  return std::nullopt;
}

std::string EncodeHello(int seq, const HelloFields& hello) {
  return Envelope("hello", seq,
                 ",\"proto\":" + std::to_string(kLobbyProtocol) + ",\"game\":" + Quote(kGameId) +
                     ",\"guest\":" + Quote(hello.guest) + ",\"build\":" + std::to_string(hello.build) +
                     ",\"player\":" + Quote(hello.player) + ",\"token\":" + Quote(hello.token) +
                     ",\"name\":" + Quote(hello.name) + ",\"version\":" + Quote(hello.version) +
                     ",\"platform\":" + Quote(hello.platform) + ",\"presence\":" + (hello.presence ? "true" : "false") +
                     (hello.relay ? ",\"relay\":true" : ""));
}

std::string EncodePing(int seq) { return Envelope("ping", seq); }
std::string EncodeFriendsList(int seq) { return Envelope("friends.list", seq); }
std::string EncodeFriendsRequests(int seq) { return Envelope("friends.requests", seq); }
std::string EncodeFriendsAdd(int seq, const std::string& code) { return Envelope("friends.add", seq, ",\"code\":" + Quote(code)); }
std::string EncodeFriendsAccept(int seq, const std::string& player) { return WithPlayer("friends.accept", seq, player); }
std::string EncodeFriendsDecline(int seq, const std::string& player) { return WithPlayer("friends.decline", seq, player); }
std::string EncodeFriendsRemove(int seq, const std::string& player) { return WithPlayer("friends.remove", seq, player); }
std::string EncodePlayerMatchHost(int seq, const PlayerMatchRules& rules, const std::string& qos_hex) {
  std::string object = "{";
  for (size_t i = 0; i < rules.size(); ++i) {
    object += (i ? "," : "") + Quote(rules[i].first) + ":" + std::to_string(rules[i].second);
  }
  const std::string qos = qos_hex.empty() ? std::string() : ",\"qos\":" + Quote(qos_hex);
  return Envelope("pm.host", seq, ",\"slots\":2,\"rules\":" + object + "}" + qos);
}

std::string EncodePlayerMatchCancel(int seq) { return Envelope("pm.cancel", seq); }
std::string EncodePlayerMatchSearch(int seq) { return Envelope("pm.search", seq); }
std::string EncodePlayerMatchJoin(int seq, const std::string& session) {
  return Envelope("pm.join", seq, ",\"session\":" + Quote(session));
}

std::string EncodeInviteSend(int seq, const std::string& player) { return WithPlayer("invite.send", seq, player); }
std::string EncodeInviteDecline(int seq, const std::string& invite) {
  return Envelope("invite.decline", seq, ",\"invite\":" + Quote(invite));
}

std::string EncodeMatchEndpoint(int seq, const std::string& match, uint16_t port, const std::vector<std::string>& lan,
                                const std::string& public_ip, uint16_t public_port) {
  std::string list = "[";
  for (size_t i = 0; i < lan.size(); ++i) list += (i ? "," : "") + Quote(lan[i]);
  std::string mapped;
  if (public_port && !public_ip.empty()) {
    mapped = ",\"public_ip\":" + Quote(public_ip) + ",\"public_port\":" + std::to_string(public_port);
  }
  return WithMatch("match.endpoint", seq, match,
                   ",\"port\":" + std::to_string(port) + ",\"lan\":" + list + "]" + mapped);
}

std::string EncodeMatchReady(int seq, const std::string& match, bool ready) {
  return WithMatch("match.ready", seq, match, std::string(",\"ready\":") + (ready ? "true" : "false"));
}

std::string EncodeMatchLeave(int seq, const std::string& match) { return WithMatch("match.leave", seq, match); }
std::string EncodeMatchTransport(int seq, const std::string& match, const std::string& transport) {
  return WithMatch("match.transport", seq, match, ",\"transport\":" + Quote(transport));
}
std::string EncodeMatchResume(int seq, const std::string& match) { return WithMatch("match.resume", seq, match); }

std::string HexOf(const std::vector<uint8_t>& bytes) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string text;
  for (const uint8_t b : bytes) {
    text += kHex[b >> 4];
    text += kHex[b & 0xF];
  }
  return text;
}

std::vector<uint8_t> BytesOfHex(std::string_view hex) {
  const auto digit = [](char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
  };
  std::vector<uint8_t> bytes;
  if (hex.size() % 2) return bytes;
  for (size_t i = 0; i < hex.size(); i += 2) {
    const int hi = digit(hex[i]), lo = digit(hex[i + 1]);
    if (hi < 0 || lo < 0) return {};
    bytes.push_back(uint8_t(hi << 4 | lo));
  }
  return bytes;
}

std::string NormalizeFriendCode(std::string_view text) {
  std::string compact;
  for (const char c : text) {
    if (c == '-' || std::isspace(static_cast<unsigned char>(c))) continue;
    compact += char(std::toupper(static_cast<unsigned char>(c)));
  }
  if (compact.size() != 8) return {};
  for (const char c : compact) {
    const bool letter = c >= 'A' && c <= 'Z' && c != 'I' && c != 'O';
    const bool digit = c >= '2' && c <= '9';
    if (!letter && !digit) return {};
  }
  return compact.substr(0, 4) + "-" + compact.substr(4);
}

std::string DescribeServiceError(const std::string& code) {
  if (code == "version_mismatch") return Tr("Game versions do not match.");
  if (code == "not_found") return Tr("No player has that friend code.");
  if (code == "not_friends") return Tr("That player is not your friend.");
  if (code == "offline") return Tr("Your friend is not online.");
  if (code == "busy") return Tr("One of you is already in a match.");
  if (code == "bad_request") return Tr("That is not a valid friend code.");
  if (code == "rate_limited" || code == "limit") return Tr("Too many requests. Try again in a moment.");
  if (code == "proto") return Tr("The online service needs a different version of the game.");
  if (code == "unauthorized" || code == "bad_game" || code == "bad_guest") return Tr("The online service did not accept this game.");
  if (code == "unreachable" || code == "link_lost" || code == "timeout") return Tr("Unable to reach online service.");
  if (code == "relay_unavailable") return Tr("The connection through the online service is not available.");
  return std::string(Tr("The online service refused the request (")) + code + ").";
}

bool GuestsCompatible(const std::string& guest_a, int build_a, const std::string& guest_b, int build_b) {
  return guest_a == guest_b && build_a == build_b;
}

std::string ChoosePeerAddress(const std::vector<std::string>& mine, const PeerInfo& peer) {
  if (!peer.same_network) return peer.ip;
  std::vector<std::string> remote_private;
  for (const std::string& ip : peer.lan) {
    if (!IsLoopback(ip)) remote_private.push_back(ip);
  }
  for (const std::string& ip : remote_private) {
    if (std::find(mine.begin(), mine.end(), ip) != mine.end()) return kLoopback;
  }
  return remote_private.empty() ? kLoopback : remote_private.front();
}

std::string FriendStatusLine(const Friend& entry) {
  if (!entry.online) return Tr("Offline");
  std::string line;
  switch (entry.state) {
    case FriendState::kPlayerMatch: line = Tr("Online Match"); break;
    case FriendState::kConnecting: line = Tr("Connecting"); break;
    case FriendState::kInGame: line = Tr("In game"); break;
    default: line = Tr("Online"); break;
  }
  if (!entry.platform.empty()) line += Tr(" \xC2\xB7 ") + entry.platform;
  if (!entry.version.empty()) line += Tr(" \xC2\xB7 v") + entry.version;
  return line;
}

}  // namespace ssf2t::online
