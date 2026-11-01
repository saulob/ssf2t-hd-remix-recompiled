#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "online/connection_test.h"
#include "online/establishment.h"
#include "online/identity.h"
#include "online/lobby_link.h"
#include "online/protocol.h"
#include "online/rendezvous.h"

namespace ssf2t::online {

inline constexpr char kDefaultServiceUrl[] = "wss://ssf2t-lobby.saulobenigno.workers.dev/";

// From this side's endpoint advertisement (and again from its first punch): how long a direct path may take before a
// match that has the service's relay falls back to it.
inline constexpr double kDirectProbeWindowSeconds = 2.5;
// A direct path that starts punching late still gets this long before the host falls back to the relay.
inline constexpr double kMinPunchSeconds = 1.5;

struct ServiceConfig {
  std::string url = kDefaultServiceUrl;
  std::string guest = kGuestBase013;
  int build = 1;
  std::string version;   // the port version friends see
  std::string platform;  // Windows or Linux
  uint16_t host_port = 7001;
  uint16_t join_port = 7002;
  std::vector<std::string> lan;  // this PC's own addresses, offered to a peer on the same network
  std::function<Identity()> identity;
  std::function<void(const std::string&)> log;
  ChannelFactory open_channel = OpenLobbyLink;
  std::string stun = net::kDefaultStunServers;
  bool upnp = true;
  std::function<ConnectionReport(const ConnectionTestRequest&)> test = RunConnectionTest;
  // The early-bound gameplay socket. Every match learns its public endpoint from it and proves the peer's endpoint
  // over it. Null means signaling only: the configured ports are advertised and the peer is used as signaled.
  netplay::OnlineGameSocket* game_socket = nullptr;
  // The STUN servers of `stun` as addresses; the default resolves them through DNS.
  std::function<std::vector<netplay::Endpoint>(const std::string& stun)> resolve_stun;
  // Tests run the connection test and the STUN lookup inline instead of on a thread.
  bool synchronous_probes = false;
  // Tells the service this client can play through its relay (with a gameplay socket only).
  bool relay = true;
};

struct UserSettings {
  bool enabled = false;
  std::string name = kDefaultOnlineName;
};

// How the existing fight session should connect, once the peer's endpoint is known (with a gameplay socket: proven).
struct NetplayAssignment {
  bool host = false;
  std::string peer_ip;
  uint16_t peer_port = 0;
  std::string guest;
  uint32_t generation = 0;  // this process's count of matches; a session never outlives its match
  bool relay = false;       // the peer is netplay::kRelayPeer on the gameplay socket's relay
};

// What the game's WAIT_FOR_PLAYER_LIST timer should do while the match is being established.
enum class GuestWait { kUnmanaged, kProgressing, kFailed };

inline constexpr uint32_t kWaitForPlayerListOp = 6;

// The game's op timer check (its answer is `game_expired`) as it applies to the lobby's wait for the host's player list.
inline bool WaitTimerExpired(uint32_t current_op, GuestWait wait, bool game_expired) {
  if (current_op != kWaitForPlayerListOp || wait == GuestWait::kUnmanaged) return game_expired;
  return wait == GuestWait::kFailed;
}

// What the fight runtime reports about its UDP session.
enum class NetplayLink { kNone, kConnecting, kConnected, kFighting };

// How the current match's datagrams travel. Chosen once, before the fight session is told the peer, and kept.
enum class MatchTransport { kPending, kDirectProbing, kDirect, kRelay, kFailed };

const char* MatchTransportName(MatchTransport transport);

struct MatchSnapshot {
  std::string peer_player;
  std::string peer_name;
  std::string guest;
  bool host = false;
  bool peer_lost = false;
  bool endpoints_exchanged = false;
  NetplayLink link = NetplayLink::kNone;
  std::optional<int> rtt_ms;
  MatchTransport transport = MatchTransport::kPending;
};

// The Player Match this player is advertising or looking for, driven by the game's own Player Match menus.
struct PlayerMatchSnapshot {
  bool hosting = false;
  std::string session;  // the advertisement, once the service has answered
  bool searching = false;
  std::vector<PlayerMatchAd> results;
  int search_generation = 0;  // grows with every answered search
};

struct Snapshot {
  Connection connection = Connection::kOff;
  std::string connection_text;
  std::string friend_code;
  bool fetched = false;
  std::vector<Friend> friends;
  std::vector<Request> incoming;  // friend requests to answer
  std::vector<Request> outgoing;  // friend requests still waiting
  std::vector<Invite> invites;    // friends' invitations to their Player Match, oldest first
  PlayerMatchSnapshot player_match;
  std::optional<MatchSnapshot> match;
  std::string notice;  // the last thing worth telling the player; stays until replaced
  bool testing = false;
  std::optional<ConnectionReport> test;
  int tests_finished = 0;  // Connection tests that produced a report; one that stopped without one is not counted
};

// Who this player is on the service right now, for the game's title-local LIVE profile. Never the token.
struct OnlineProfile {
  bool signed_in = false;
  std::string friend_code;
  std::string name;
};

// The other player of the current Player Match, once the service has paired the two.
struct ActivePeer {
  std::string match;
  std::string peer_player;
  bool host = false;
};

// What AddFriend did with the text it was given; only kSent reaches the service.
enum class FriendRequestResult { kSent, kNotConnected, kInvalid, kOwnCode, kAlreadyFriends, kAlreadyPending };

// The player's connection to the online service: identity, friends, presence, Player Match advertisements and the
// private match two friends are paired into. It signals, and proves each match's UDP path on the gameplay socket;
// fights never pass through it. Thread-safe.
class OnlineService {
 public:
  explicit OnlineService(ServiceConfig config);
  ~OnlineService();
  OnlineService(const OnlineService&) = delete;
  OnlineService& operator=(const OnlineService&) = delete;

  void SetGuest(const std::string& guest, int build);
  void Apply(const UserSettings& settings);
  // Every frame, from whichever thread is presenting.
  void Update(double now);

  FriendRequestResult AddFriend(const std::string& code);
  void AcceptFriend(const std::string& player);
  void DeclineFriend(const std::string& player);  // also withdraws an outgoing request
  void RemoveFriend(const std::string& player);
  // Advertises this player's Player Match to compatible online friends until cancelled or joined.
  void HostPlayerMatch(const PlayerMatchRules& rules);
  void CancelPlayerMatch();
  // The game's QoS listen data for its hosted Player Match; friends' QoS probes return it.
  void SetPlayerMatchQos(const std::vector<uint8_t>& data);
  // While on, friends' Player Matches are fetched again every few seconds; PlayerMatchResults() holds the last answer.
  void SetPlayerMatchSearch(bool on);
  std::vector<PlayerMatchAd> PlayerMatchResults() const;
  // One guest search call. The service is asked again when its last answer is no longer fresh and no search is under
  // way; while `pending`, the answer for `epoch` is on its way and the guest must not be told "no match" yet.
  struct PlayerMatchSearchWait {
    uint32_t epoch = 0;
    bool pending = false;
  };
  PlayerMatchSearchWait AskPlayerMatchSearch();
  bool PlayerMatchSearchPending(uint32_t epoch) const;
  // Empty once `epoch` was left, joined or replaced by a newer search.
  std::vector<PlayerMatchAd> PlayerMatchResults(uint32_t epoch) const;
  // False when the session is not among the last results or this player is not connected.
  bool JoinPlayerMatch(const std::string& session);
  // Invites a friend to the Player Match this player is advertising; false when there is none yet.
  bool SendInvite(const std::string& player);
  // The invite's Player Match becomes joinable as if a search had listed it; nothing when the invite is gone.
  std::optional<PlayerMatchAd> AcceptInvite(const std::string& invite);
  // Grows with every accepted invite; 0 before the first.
  uint32_t AcceptedInviteGeneration() const;
  void DeclineInvite(const std::string& invite);
  std::vector<Invite> Invites() const;
  bool SignedIn() const;
  OnlineProfile Profile() const;
  std::optional<ActivePeer> Peer() const;
  // The peer of the last match that ended because no connection could be made, for a while after it ended.
  std::optional<ActivePeer> FailedPeer() const;
  ServiceCapabilities Capabilities() const;
  // The game's lobby wait for the host's player list: not over while the match is still being established, over at
  // once when it failed or its budget ran out. Anything else is left to the game.
  GuestWait GuestWaitState();
  // The game left the wait; logged once per match.
  void NoteGuestWaitComplete(bool connected);
  void LeaveMatch();
  void TestConnection();
  // A line from the launcher page, in the same log.
  void LogLine(const std::string& line) { Log(line); }
  // From the fight runtime, every frame while it owns a session.
  void SetNetplayLink(NetplayLink link, std::optional<int> rtt_ms);

  Snapshot snapshot() const;
  // The fight session's link and measured round trip, without the cost of a full snapshot.
  std::pair<NetplayLink, std::optional<int>> netplay_info() const;
  std::vector<std::string> TakeNotices();
  std::optional<NetplayAssignment> Assignment() const;
  const std::string& url() const { return config_.url; }

 private:
  enum class Step { kIdle, kOpening, kGreeting, kOnline };
  enum class Purpose {
    kHello, kFriends, kRequests, kAdd, kAccept, kDecline, kRemove, kPlayerMatchHost, kPlayerMatchCancel,
    kPlayerMatchSearch, kPlayerMatchJoin, kEndpoint, kReady, kLeave, kResume, kInviteSend, kInviteDecline,
    kTransportDirect, kTransportRelay
  };
  struct Pending {
    Purpose purpose = Purpose::kHello;
    std::string player;
    uint32_t search_epoch = 0;
    double sent_at = 0;
  };

  void OpenChannel(double now);
  void PumpChannel(double now);
  void OnEvent(const ChannelEvent& event, double now);
  void OnText(const std::string& text, double now);
  void OnResponse(const ServerMessage& message, double now);
  void OnServerEvent(const ServerMessage& message, double now);
  void OnGreeted(const ServerMessage& message, double now);
  void OnLinkLost(const std::string& why, double now);
  void OnMatchStarted(const MatchInfo& info);
  void OnPeer(const PeerInfo& peer);
  void OnMatchEnded(const std::string& reason);
  void PumpPlayerMatch();
  void SendPlayerMatchHost();
  void DropAcceptedInvite(const std::string& reason);
  bool FailIfServiceCannotForwardPublic();
  void SendPlayerMatchSearch();
  void BeginPlayerMatchSearch();
  void StopPlayerMatchSearch();
  bool PlayerMatchSearchInFlight(double* sent_at) const;
  void ClearMatch();
  void UpdateFriend(const Friend& entry);
  void Refresh();
  void Send(Purpose purpose, const std::function<std::string(int)>& encode, const std::string& player = {});
  void SendLocked(const std::string& text);
  bool Connected() const { return step_ == Step::kOnline; }
  void Notice(const std::string& text);
  void Log(const std::string& line);
  void Disable();
  void CollectTest();
  void SendEndpoint();
  void BeginUdpSetup();
  // Nothing while the lookup is still running; empty when no STUN server could be found.
  std::optional<std::vector<netplay::Endpoint>> StunServers();
  void PumpUdpSetup(double now);
  bool PeerEndpointReady(const PeerInfo& peer) const;
  void ConsiderPeer();
  void StartPunch();
  void FailUdpSetup(const std::string& reason, const std::string& notice = {});
  void MarkMatchFailed(const std::string& reason);
  void LogEstablishmentTimeout();
  // Relay fallback: only in a match the service relays, where the host chooses and the joiner follows.
  bool Coordinated() const;
  void BeginDirectProbe(double now);
  void PumpCoordinated(double now, Rendezvous::Stage punch, const Candidate& selected);
  void RequestRelay(const std::string& why);
  void SelectDirect(const Candidate& selected);
  void SelectRelay();
  void Assign(const netplay::Endpoint& peer, bool relay);
  void OnMatchTransport(const ServerMessage& message);
  void OnRelayClosed(const ServerMessage& message);
  void PumpRelay(double now);
  int DirectElapsedMs() const;
  void LogTransportSummary();
  void ReportNetplayEdge(NetplayLink before, NetplayLink after);
  bool EnsureIdentity();
  std::string SessionName() const;
  std::string NameOf(const std::string& player) const;

  ServiceConfig config_;
  mutable std::mutex mutex_;
  UserSettings settings_;
  Identity identity_;
  bool identity_loaded_ = false;

  Connection connection_ = Connection::kOff;
  std::string connection_text_;
  // Shared with the gameplay socket's relay, which sends on it from the fight's threads.
  std::shared_ptr<LobbyChannel> channel_;
  std::shared_ptr<LobbyChannel> leaving_;
  ServiceCapabilities capabilities_;
  double leaving_deadline_ = 0;
  double now_ = 0;
  double step_deadline_ = 0;
  Step step_ = Step::kIdle;
  double retry_at_ = 0;
  double retry_delay_ = 1.0;
  double next_refresh_ = 0;
  double name_since_ = 0;
  std::string greeted_name_;
  int next_seq_ = 1;
  std::map<int, Pending> pending_;

  std::string friend_code_;
  bool fetched_ = false;
  std::vector<Friend> friends_;
  std::vector<Request> incoming_, outgoing_;
  std::optional<PlayerMatchRules> hosting_;
  std::string hosting_session_;
  std::string hosting_qos_;
  double next_host_refresh_ = 0;
  bool searching_ = false;
  double next_search_ = 0;
  std::vector<PlayerMatchAd> results_;
  int search_generation_ = 0;
  uint32_t search_epoch_ = 0;
  double search_answered_at_ = -1e9;
  std::vector<Invite> invites_;
  std::optional<PlayerMatchAd> accepted_invite_;
  uint32_t accepted_generation_ = 0;
  bool joining_ = false;
  std::string notice_;
  double notice_at_ = 0;
  std::vector<std::string> notices_;

  std::optional<MatchInfo> match_;
  std::optional<PeerInfo> peer_;
  std::optional<NetplayAssignment> assignment_;
  uint32_t match_generation_ = 0;
  bool peer_lost_ = false;
  bool signaling_ended_ = false;
  NetplayLink link_ = NetplayLink::kNone;
  std::optional<int> rtt_ms_;

  struct TestState;
  std::shared_ptr<TestState> test_probe_;
  std::optional<ConnectionReport> test_;
  bool testing_ = false;
  int tests_finished_ = 0;

  // Match setup on the gameplay socket, in order; only kSelected hands the peer to the fight session.
  enum class UdpSetup { kNone, kDiscovering, kWaitingPeer, kPunching, kSelected };
  struct StunLookup;
  std::shared_ptr<StunLookup> stun_lookup_;
  std::optional<std::vector<netplay::Endpoint>> stun_servers_;
  Rendezvous rendezvous_;
  UdpSetup udp_setup_ = UdpSetup::kNone;
  bool stun_started_ = false;
  double peer_deadline_ = 0;  // the peer's usable endpoint must have arrived by then
  netplay::Endpoint mapped_;  // this match's STUN endpoint of the gameplay socket; none when STUN gave none

  MatchTransport transport_ = MatchTransport::kPending;
  double probe_started_ = -1;  // -1: no direct attempt yet
  double direct_deadline_ = 0;
  Establishment establishment_;
  std::string failed_reason_;
  int failed_elapsed_ms_ = 0;
  bool guest_wait_logged_ = false;
  int direct_elapsed_ms_ = -1;  // from probing to the choice
  bool relay_available_ = false;
  bool relay_requested_ = false;
  bool relay_used_ = false;
  size_t relay_logged_tx_ = 0;
  size_t relay_logged_rx_ = 0;
  double next_relay_stats_ = 0;
  std::optional<ActivePeer> failed_peer_;
  double failed_at_ = 0;
};

}  // namespace ssf2t::online
