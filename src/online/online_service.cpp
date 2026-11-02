#include "online/online_service.h"

#include <algorithm>
#include <exception>
#include <thread>

#include "localization/ui_text.h"
#include "netplay/udp_transport.h"
#include "online/net_addr.h"
#include "online/stun.h"

namespace ssf2t::online {
namespace {

constexpr double kStepTimeoutSeconds = 15.0;
constexpr double kMaxRetrySeconds = 20.0;
constexpr double kFatalRetrySeconds = 60.0;
constexpr double kRefreshSeconds = 30.0;
constexpr double kNameSettleSeconds = 1.5;
constexpr double kLeaveFlushSeconds = 2.0;
constexpr double kNoticeSeconds = 12.0;
constexpr double kHostRefreshSeconds = 60.0;
constexpr double kSearchSeconds = 3.0;
constexpr double kSearchFreshSeconds = 1.0;
constexpr double kSearchTimeoutSeconds = 5.0;
// From this side's own advertisement: the peer's STUN (up to 2.5 s) and its endpoint's way through the service.
constexpr double kPeerEndpointWaitSeconds = 10.0;
constexpr double kRelayStatsSeconds = 10.0;
constexpr double kFailedPeerSeconds = 30.0;

bool Fatal(const std::string& code) {
  return code == "proto" || code == "bad_game" || code == "bad_guest" || code == "unauthorized";
}

const char* RoleName(bool host) { return host ? "host" : "join"; }

std::vector<netplay::Endpoint> ResolveStunServers(const std::string& stun_config) {
  std::vector<netplay::Endpoint> servers;
  if (!net::SocketsReady()) return servers;
  for (const auto& [host, port] : net::StunServers(stun_config)) {
    if (const uint32_t ip = net::ResolveIpv4(host)) servers.push_back({ip, port});
  }
  return servers;
}

uint64_t Milliseconds(double seconds) { return uint64_t(seconds * 1000.0); }

const char* Flag(bool value) { return value ? "true" : "false"; }

}  // namespace

const char* MatchTransportName(MatchTransport transport) {
  switch (transport) {
    case MatchTransport::kDirectProbing: return "direct_probing";
    case MatchTransport::kDirect: return "direct";
    case MatchTransport::kRelay: return "relay";
    case MatchTransport::kFailed: return "failed";
    default: return "pending";
  }
}

struct OnlineService::TestState {
  std::mutex mutex;
  bool done = false;
  std::optional<ConnectionReport> report;  // none when the test stopped without one
  std::string stopped;
};

struct OnlineService::StunLookup {
  std::mutex mutex;
  bool done = false;
  std::vector<netplay::Endpoint> servers;
};

OnlineService::OnlineService(ServiceConfig config)
    : config_(std::move(config)), rendezvous_([this](const std::string& line) { Log(line); }) {
  if (config_.game_socket) config_.game_socket->SetTap(&rendezvous_);
}

OnlineService::~OnlineService() {
  if (config_.game_socket) config_.game_socket->SetTap(nullptr);
}

void OnlineService::SetGuest(const std::string& guest, int build) {
  std::lock_guard<std::mutex> lock(mutex_);
  config_.guest = guest;
  config_.build = build;
}

void OnlineService::Apply(const UserSettings& settings) {
  std::lock_guard<std::mutex> lock(mutex_);
  const bool was_enabled = settings_.enabled;
  const std::string before = settings_.name;
  settings_ = settings;
  settings_.name = CleanOnlineName(settings.name);
  if (settings_.name != before) name_since_ = now_;
  if (was_enabled && !settings.enabled) Disable();
}

void OnlineService::Disable() {
  if (Connected() && channel_) {
    if (match_) SendLocked(EncodeMatchLeave(next_seq_++, match_->match));
    leaving_ = std::move(channel_);
    leaving_deadline_ = now_ + kLeaveFlushSeconds;
  }
  if (channel_) channel_->Close();
  channel_.reset();
  step_ = Step::kIdle;
  pending_.clear();
  ClearMatch();
  friends_.clear();
  incoming_.clear();
  outgoing_.clear();
  hosting_.reset();
  hosting_session_.clear();
  StopPlayerMatchSearch();
  results_.clear();
  invites_.clear();
  accepted_invite_.reset();
  joining_ = false;
  friend_code_.clear();
  fetched_ = false;
  connection_ = Connection::kOff;
  connection_text_.clear();
  notice_.clear();
  notices_.clear();
  test_.reset();
  testing_ = false;
  test_probe_.reset();
  retry_at_ = 0;
  retry_delay_ = 1.0;
  link_ = NetplayLink::kNone;
  rtt_ms_.reset();
}

void OnlineService::Update(double now) {
  std::lock_guard<std::mutex> lock(mutex_);
  now_ = now;
  if (leaving_) {
    ChannelEvent event;
    bool done = now >= leaving_deadline_;
    while (!done && leaving_->Poll(event)) done = event.kind != ChannelEvent::Kind::kOpen;
    if (done) {
      leaving_->Close();
      leaving_.reset();
    }
  }
  if (!settings_.enabled) return;
  CollectTest();
  PumpUdpSetup(now);
  if (step_ == Step::kIdle && !channel_ && now >= retry_at_) OpenChannel(now);
  PumpChannel(now);
  PumpRelay(now);
  FlushFightReports();
  const bool opening = step_ == Step::kOpening || step_ == Step::kGreeting;
  if (opening && now > step_deadline_) OnLinkLost("timeout", now);
  if (step_ != Step::kOnline) return;
  if (now >= next_refresh_) Refresh();
  PumpPlayerMatch();
  if (SessionName() != greeted_name_ && !match_ && now - name_since_ >= kNameSettleSeconds) {
    Log("online name changed, signing in again");
    if (channel_) channel_->Close();
    channel_.reset();
    step_ = Step::kIdle;
    pending_.clear();
    OpenChannel(now);
  }
}

void OnlineService::OpenChannel(double now) {
  if (!EnsureIdentity()) {
    connection_ = Connection::kError;
    connection_text_ = Tr("Online identity is unavailable.");
    retry_at_ = now + kFatalRetrySeconds;
    return;
  }
  channel_ = config_.open_channel(config_.url);
  step_ = Step::kOpening;
  step_deadline_ = now + kStepTimeoutSeconds;
  if (connection_ != Connection::kChecking) {
    connection_ = Connection::kChecking;
    connection_text_ = Tr("Connecting");
  }
}

void OnlineService::PumpChannel(double now) {
  ChannelEvent event;
  while (channel_ && channel_->Poll(event)) OnEvent(event, now);
}

void OnlineService::OnEvent(const ChannelEvent& event, double now) {
  switch (event.kind) {
    case ChannelEvent::Kind::kOpen:
      Log("online worker connected");
      step_ = Step::kGreeting;
      step_deadline_ = now + kStepTimeoutSeconds;
      Send(Purpose::kHello, [this](int seq) {
        return EncodeHello(seq, {config_.guest, config_.build, identity_.player_id, identity_.token, SessionName(),
                                 config_.version, config_.platform, true, config_.relay && config_.game_socket, config_.fight_telemetry});
      });
      break;
    case ChannelEvent::Kind::kText:
      OnText(event.text, now);
      break;
    case ChannelEvent::Kind::kBinary:
      if (config_.game_socket) {
        config_.game_socket->DeliverRelay(reinterpret_cast<const uint8_t*>(event.text.data()), event.text.size());
      }
      break;
    case ChannelEvent::Kind::kClosed:
      OnLinkLost(event.text.empty() ? "closed" : event.text, now);
      break;
  }
}

void OnlineService::OnText(const std::string& text, double now) {
  const std::optional<ServerMessage> message = DecodeServerMessage(text);
  if (!message) {
    Log("online error malformed server message");
    return;
  }
  if (message->kind == ServerMessage::Kind::kResponse) OnResponse(*message, now);
  else OnServerEvent(*message, now);
}

void OnlineService::OnResponse(const ServerMessage& message, double now) {
  const auto found = pending_.find(message.seq);
  if (found == pending_.end()) return;
  const Pending pending = found->second;
  pending_.erase(found);
  const bool ok = message.code == "ok";

  switch (pending.purpose) {
    case Purpose::kHello:
      if (!ok) {
        Log("online sign-in refused code=" + message.code);
        connection_ = Fatal(message.code) ? Connection::kError : Connection::kOffline;
        connection_text_ = DescribeServiceError(message.code);
        if (channel_) channel_->Close();
        channel_.reset();
        step_ = Step::kIdle;
        retry_at_ = now + (Fatal(message.code) ? kFatalRetrySeconds : retry_delay_);
        return;
      }
      OnGreeted(message, now);
      break;
    case Purpose::kFriends:
      if (ok && message.friends) {
        friends_ = *message.friends;
        fetched_ = true;
        Log("online friends count=" + std::to_string(friends_.size()));
      }
      break;
    case Purpose::kRequests:
      if (ok) {
        if (message.incoming) incoming_ = *message.incoming;
        if (message.outgoing) outgoing_ = *message.outgoing;
      }
      break;
    case Purpose::kAdd:
      if (!ok) {
        Notice(DescribeServiceError(message.code));
        Log("online friend request failed code=" + pending.player + " reason=" + message.code);
      } else {
        Notice(message.state == "friends" ? Tr("You are now friends.") : Tr("Friend request sent."));
        Log("online friend request accepted by service code=" + pending.player + " state=" + message.state);
        Refresh();
      }
      break;
    case Purpose::kAccept:
    case Purpose::kDecline:
    case Purpose::kRemove:
      if (!ok) Notice(DescribeServiceError(message.code));
      Refresh();
      break;
    case Purpose::kPlayerMatchHost:
      if (!hosting_) break;
      if (!ok) {
        Log("player_match advertise failed code=" + message.code);
        break;
      }
      if (message.session != hosting_session_) {
        hosting_session_ = message.session;
        Log("player_match create session_id=" + hosting_session_);
        Log("player_match advertise");
      }
      break;
    case Purpose::kPlayerMatchSearch:
      if (!searching_ || pending.search_epoch != search_epoch_) {
        Log("player_match search ignored stale");
        break;
      }
      if (!ok || !message.sessions) {
        Log("player_match search failed code=" + message.code);
        break;
      }
      if (message.sessions->size() != results_.size() || search_generation_ == 0) {
        Log("player_match search results=" + std::to_string(message.sessions->size()));
        for (const PlayerMatchAd& ad : *message.sessions) {
          Log("player_match search result friend=" + ad.player + " session=" + ad.session);
        }
      }
      results_ = *message.sessions;
      ++search_generation_;
      search_answered_at_ = now_;
      break;
    case Purpose::kPlayerMatchJoin:
      joining_ = false;
      if (!ok || !message.match) {
        Notice(DescribeServiceError(ok ? "bad_request" : message.code));
        Log("player_match join failed code=" + message.code);
        if (accepted_invite_ && accepted_invite_->session == pending.player) {
          DropAcceptedInvite("join_rejected_" + message.code);
        }
      } else {
        Log("player_match join service complete session=" + message.match->session);
        OnMatchStarted(*message.match);
      }
      break;
    case Purpose::kInviteSend:
      if (!ok) {
        Notice(DescribeServiceError(message.code));
        Log("player_match invite failed friend=" + pending.player + " code=" + message.code);
      } else {
        Notice(Tr("Invite sent to ") + NameOf(pending.player) + ".");
        Log("player_match invite sent session=" + message.session + " friend=" + pending.player);
      }
      break;
    case Purpose::kTransportRelay:
      if (!match_ || pending.player != match_->match || !relay_requested_ || udp_setup_ == UdpSetup::kSelected) break;
      if (!ok) {
        Log("relay fallback refused code=" + message.code);
        FailUdpSetup("relay_unavailable");
        break;
      }
      SelectRelay();
      break;
    case Purpose::kTransportDirect:
      if (!ok) Log("transport choice failed code=" + message.code);
      break;
    case Purpose::kFightReport:
      if (!ok && ++fight_reports_failed_ <= 3) Log("fight telemetry refused code=" + message.code);
      break;
    case Purpose::kInviteDecline:
    case Purpose::kPlayerMatchCancel:
    case Purpose::kEndpoint:
    case Purpose::kReady:
    case Purpose::kLeave:
      if (!ok) Log("online request failed code=" + message.code);
      break;
    case Purpose::kResume:
      if (!ok) {
        Log("online match resume failed code=" + message.code);
        if (link_ == NetplayLink::kNone || link_ == NetplayLink::kConnecting) {
          MarkMatchFailed("resume_failed");
          ClearMatch();
          Notice(Tr("The match was closed."));
        }
      } else {
        peer_lost_ = message.match ? false : peer_lost_;
        Log("online match resumed");
        if (match_ && link_ >= NetplayLink::kConnected) {
          Send(Purpose::kReady, [this](int seq) { return EncodeMatchReady(seq, match_->match, true); });
        }
        if (match_ && (!config_.game_socket || udp_setup_ >= UdpSetup::kWaitingPeer)) SendEndpoint();
      }
      break;
  }
}

void OnlineService::OnGreeted(const ServerMessage& message, double now) {
  const bool was_reconnecting = connection_ == Connection::kChecking && !friend_code_.empty();
  step_ = Step::kOnline;
  connection_ = Connection::kOnline;
  connection_text_ = Tr("Online");
  retry_delay_ = 1.0;
  friend_code_ = message.friend_code.empty() ? identity_.player_id : message.friend_code;
  greeted_name_ = SessionName();
  Log(std::string("online signed in") + (was_reconnecting ? " (reconnected)" : "") + " name=" + greeted_name_ +
      " code=" + friend_code_);
  capabilities_ = message.capabilities.value_or(ServiceCapabilities{});
  if (capabilities_.announced) {
    Log("online service capabilities protocol=" + std::to_string(capabilities_.protocol_version) +
        " worker_build=" + (capabilities_.worker_build.empty() ? std::string("-") : capabilities_.worker_build) +
        " public_endpoints=" + Flag(capabilities_.public_endpoints) +
        " relay_supported=" + Flag(capabilities_.relay_supported));
  } else {
    Log("online service capabilities protocol=legacy worker_build=legacy public_endpoints=unknown "
        "relay_supported=false");
  }
  Log(std::string("relay capability available=") + Flag(capabilities_.relay_supported));
  Refresh();
  if (match_) {
    const std::string id = match_->match;
    Send(Purpose::kResume, [id](int seq) { return EncodeMatchResume(seq, id); });
  } else if (hosting_) {
    SendPlayerMatchHost();
  }
  (void)now;
}

void OnlineService::OnServerEvent(const ServerMessage& message, double now) {
  using Kind = ServerMessage::Kind;
  (void)now;
  switch (message.kind) {
    case Kind::kFriendRequest:
      Notice(message.request.name + Tr(" wants to be your friend."));
      Log("online friend request received from=" + message.request.player);
      Refresh();
      break;
    case Kind::kFriendAdded:
      Notice(message.friend_view.name + Tr(" is now your friend."));
      UpdateFriend(message.friend_view);
      Refresh();
      break;
    case Kind::kFriendRemoved:
      friends_.erase(std::remove_if(friends_.begin(), friends_.end(),
                                    [&](const Friend& f) { return f.player == message.player; }),
                     friends_.end());
      Refresh();
      break;
    case Kind::kFriendStatus:
      UpdateFriend(message.friend_view);
      break;
    case Kind::kFriendOffline:
      for (Friend& f : friends_) {
        if (f.player == message.player) {
          f.online = false;
          f.state = FriendState::kOffline;
        }
      }
      break;
    case Kind::kMatchCreated:
      if (message.match) OnMatchStarted(*message.match);
      break;
    case Kind::kMatchPeer:
      OnPeer(message.peer);
      break;
    case Kind::kMatchEnded:
      if (match_ && match_->match == message.match_id) OnMatchEnded(message.reason);
      break;
    case Kind::kMatchPeerLost:
      if (match_ && match_->match == message.match_id) peer_lost_ = true;
      break;
    case Kind::kMatchPeerBack:
      if (match_ && match_->match == message.match_id) peer_lost_ = false;
      break;
    case Kind::kMatchReady:
      Log(std::string("online peer ready=") + (message.ready ? "true" : "false"));
      break;
    case Kind::kMatchTransport:
      OnMatchTransport(message);
      break;
    case Kind::kRelayClosed:
      OnRelayClosed(message);
      break;
    case Kind::kInvite: {
      const Invite& invite = *message.invite;
      invites_.erase(std::remove_if(invites_.begin(), invites_.end(),
                                    [&](const Invite& i) { return i.ad.session == invite.ad.session; }),
                     invites_.end());
      invites_.push_back(invite);
      Notice(invite.ad.name + Tr(" invited you to an Online Match."));
      Log("player_match invite received session=" + invite.ad.session + " from=" + invite.ad.player);
      break;
    }
    case Kind::kInviteEnded:
      invites_.erase(std::remove_if(invites_.begin(), invites_.end(),
                                    [&](const Invite& i) { return i.id == message.invite_id; }),
                     invites_.end());
      if (accepted_invite_ && accepted_invite_->session == message.session && message.reason != "started") {
        DropAcceptedInvite("invite_ended_" + message.reason);
      }
      Log("player_match invite ended session=" + message.session + " reason=" + message.reason);
      break;
    case Kind::kResponse:
      break;
  }
}

void OnlineService::PumpPlayerMatch() {
  if (match_) return;
  if (hosting_ && now_ >= next_host_refresh_) SendPlayerMatchHost();
  if (searching_ && now_ >= next_search_) {
    SendPlayerMatchSearch();
  }
}

void OnlineService::SendPlayerMatchSearch() {
  next_search_ = now_ + kSearchSeconds;
  Send(Purpose::kPlayerMatchSearch, [](int seq) { return EncodePlayerMatchSearch(seq); });
}

void OnlineService::SendPlayerMatchHost() {
  next_host_refresh_ = now_ + kHostRefreshSeconds;
  const PlayerMatchRules rules = *hosting_;
  const std::string qos = hosting_qos_;
  Send(Purpose::kPlayerMatchHost, [&rules, &qos](int seq) { return EncodePlayerMatchHost(seq, rules, qos); });
}

void OnlineService::HostPlayerMatch(const PlayerMatchRules& rules) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (match_) return;
  const bool fresh = !hosting_;
  hosting_ = rules;
  if (fresh) Log("player_match create begin");
  if (Connected()) SendPlayerMatchHost();
}

void OnlineService::CancelPlayerMatch() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!hosting_) return;
  hosting_.reset();
  hosting_session_.clear();
  hosting_qos_.clear();
  Log("player_match advertisement removed");
  if (Connected()) Send(Purpose::kPlayerMatchCancel, [](int seq) { return EncodePlayerMatchCancel(seq); });
}

void OnlineService::SetPlayerMatchQos(const std::vector<uint8_t>& data) {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string hex = HexOf(data);
  if (hex == hosting_qos_) return;
  hosting_qos_ = hex;
  Log("player_match qos listen bytes=" + std::to_string(data.size()));
  if (hosting_ && Connected()) SendPlayerMatchHost();
}

void OnlineService::SetPlayerMatchSearch(bool on) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (on) BeginPlayerMatchSearch();
  else StopPlayerMatchSearch();
}

void OnlineService::BeginPlayerMatchSearch() {
  if (searching_) return;
  searching_ = true;
  ++search_epoch_;
  next_search_ = 0;
  results_.clear();
  search_generation_ = 0;
  search_answered_at_ = -1e9;
  Log("player_match search begin");
}

void OnlineService::StopPlayerMatchSearch() {
  if (!searching_) return;
  searching_ = false;
  ++search_epoch_;
  next_search_ = 0;
}

bool OnlineService::PlayerMatchSearchInFlight(double* sent_at) const {
  bool found = false;
  for (const auto& [seq, request] : pending_) {
    if (request.purpose != Purpose::kPlayerMatchSearch || request.search_epoch != search_epoch_) continue;
    if (!found || request.sent_at < *sent_at) *sent_at = request.sent_at;
    found = true;
  }
  return found;
}

OnlineService::PlayerMatchSearchWait OnlineService::AskPlayerMatchSearch() {
  std::lock_guard<std::mutex> lock(mutex_);
  BeginPlayerMatchSearch();
  PlayerMatchSearchWait wait;
  wait.epoch = search_epoch_;
  if (!Connected()) return wait;
  double sent_at = 0;
  const bool fresh = now_ - search_answered_at_ < kSearchFreshSeconds;
  if (!fresh && !PlayerMatchSearchInFlight(&sent_at)) {
    SendPlayerMatchSearch();
    Log("player_match search requested for guest");
    sent_at = now_;
  }
  const bool nothing_to_show = results_.empty() && !accepted_invite_;
  wait.pending = nothing_to_show && !fresh && now_ - sent_at < kSearchTimeoutSeconds;
  if (wait.pending) Log("player_match search pending");
  return wait;
}

bool OnlineService::PlayerMatchSearchPending(uint32_t epoch) const {
  std::lock_guard<std::mutex> lock(mutex_);
  double sent_at = 0;
  if (!searching_ || epoch != search_epoch_ || !PlayerMatchSearchInFlight(&sent_at)) return false;
  return now_ - sent_at < kSearchTimeoutSeconds;
}

std::vector<PlayerMatchAd> OnlineService::PlayerMatchResults(uint32_t epoch) const {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!searching_ || epoch != search_epoch_) return {};
  }
  return PlayerMatchResults();
}

std::vector<PlayerMatchAd> OnlineService::PlayerMatchResults() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<PlayerMatchAd> results = results_;
  const auto same = [this](const PlayerMatchAd& ad) { return ad.session == accepted_invite_->session; };
  if (accepted_invite_ && std::none_of(results.begin(), results.end(), same)) results.push_back(*accepted_invite_);
  return results;
}

bool OnlineService::JoinPlayerMatch(const std::string& session) {
  std::lock_guard<std::mutex> lock(mutex_);
  const bool known = std::any_of(results_.begin(), results_.end(),
                                 [&](const PlayerMatchAd& ad) { return ad.session == session; }) ||
                     (accepted_invite_ && accepted_invite_->session == session);
  if (!Connected() || !known || match_ || joining_) {
    Log("player_match join refused session=" + session);
    return false;
  }
  joining_ = true;
  StopPlayerMatchSearch();
  Log("player_match join begin session=" + session);
  Send(Purpose::kPlayerMatchJoin, [&session](int seq) { return EncodePlayerMatchJoin(seq, session); }, session);
  return true;
}

bool OnlineService::SendInvite(const std::string& player) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!Connected() || !hosting_ || hosting_session_.empty() || match_) {
    Log("player_match invite refused friend=" + player + " reason=no_player_match");
    Notice(Tr("Create an Online Match first."));
    return false;
  }
  Log("player_match invite friend=" + player + " session=" + hosting_session_);
  Send(Purpose::kInviteSend, [&player](int seq) { return EncodeInviteSend(seq, player); }, player);
  return true;
}

std::optional<PlayerMatchAd> OnlineService::AcceptInvite(const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = std::find_if(invites_.begin(), invites_.end(), [&](const Invite& i) { return i.id == id; });
  if (found == invites_.end() || !Connected() || match_) return std::nullopt;
  DropAcceptedInvite("replaced");
  accepted_invite_ = found->ad;
  ++accepted_generation_;
  invites_.erase(found);
  Log("player_match invite accepted session=" + accepted_invite_->session);
  return accepted_invite_;
}

uint32_t OnlineService::AcceptedInviteGeneration() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return accepted_generation_;
}

void OnlineService::DropAcceptedInvite(const std::string& reason) {
  if (!accepted_invite_) return;
  Log("invite transition failed generation=" + std::to_string(accepted_generation_) + " reason=" + reason);
  accepted_invite_.reset();
}

std::vector<Invite> OnlineService::Invites() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return invites_;
}

void OnlineService::DeclineInvite(const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = std::find_if(invites_.begin(), invites_.end(), [&](const Invite& i) { return i.id == id; });
  if (found == invites_.end()) return;
  Log("player_match invite declined session=" + found->ad.session);
  invites_.erase(found);
  if (Connected()) Send(Purpose::kInviteDecline, [&id](int seq) { return EncodeInviteDecline(seq, id); });
}

bool OnlineService::SignedIn() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return settings_.enabled && Connected();
}

std::optional<ActivePeer> OnlineService::Peer() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!match_) return std::nullopt;
  return ActivePeer{match_->match, match_->peer_player, match_->role == "host"};
}

std::optional<ActivePeer> OnlineService::FailedPeer() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (match_ || !failed_peer_ || now_ - failed_at_ > kFailedPeerSeconds) return std::nullopt;
  return failed_peer_;
}

ServiceCapabilities OnlineService::Capabilities() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return capabilities_;
}

OnlineProfile OnlineService::Profile() const {
  std::lock_guard<std::mutex> lock(mutex_);
  OnlineProfile profile;
  profile.signed_in = settings_.enabled && Connected();
  profile.friend_code = friend_code_;
  profile.name = greeted_name_;
  return profile;
}

void OnlineService::OnMatchStarted(const MatchInfo& info) {
  if (!GuestsCompatible(info.guest, info.build, config_.guest, config_.build)) {
    Log("online match refused: game versions differ");
    DropAcceptedInvite("game_versions_differ");
    Send(Purpose::kLeave, [&info](int seq) { return EncodeMatchLeave(seq, info.match); });
    Notice(Tr("Game versions do not match."));
    return;
  }
  match_ = info;
  ++match_generation_;
  peer_.reset();
  assignment_.reset();
  peer_lost_ = false;
  failed_peer_.reset();
  relay_available_ = info.relay_token && config_.game_socket;
  if (config_.game_socket) {
    config_.game_socket->CloseRelay();
    config_.game_socket->ResetTraffic();
  }
  if (relay_available_) {
    const std::weak_ptr<LobbyChannel> link = channel_;
    config_.game_socket->OpenRelay(info.relay_token, [link](std::vector<uint8_t>&& frame) {
      const std::shared_ptr<LobbyChannel> channel = link.lock();
      return channel && channel->SendBinary(std::move(frame));
    });
    Log("relay available match=" + info.match + " role=" + RoleName(info.role == "host") +
        " relay_available=true");
  } else if (capabilities_.relay_supported && config_.game_socket) {
    Log("relay available=false match=" + info.match + " reason=peer_without_relay");
  }
  hosting_.reset();
  hosting_session_.clear();
  StopPlayerMatchSearch();
  joining_ = false;
  if (accepted_invite_ && accepted_invite_->session == info.session) {
    Log("invite transition consumed generation=" + std::to_string(accepted_generation_));
    accepted_invite_.reset();
  } else {
    DropAcceptedInvite("superseded");
  }
  signaling_ended_ = false;
  guest_wait_logged_ = false;
  if (config_.game_socket) {
    establishment_.Begin(now_);
    Log("network establishment begin match=" + info.match + " role=" + RoleName(info.role == "host") +
        " deadline_ms=" + std::to_string(int(kEstablishmentBudgetSeconds * 1000)));
  } else {
    establishment_.Reset();
  }
  Log(std::string("online match created role=") + RoleName(info.role == "host") + " guest=" + info.guest);
  Log(std::string("player_match ") + (info.role == "host" ? "host" : "join") + " ready peer=" +
      info.peer_player);
  Notice(Tr("Connecting to ") + info.peer_name + "...");
  BeginUdpSetup();
}

void OnlineService::OnPeer(const PeerInfo& peer) {
  if (!match_ || peer.match != match_->match || peer.role == match_->role) return;
  if (config_.game_socket) {
    const bool unchanged = peer_ && peer_->public_ip == peer.public_ip && peer_->public_port == peer.public_port &&
                           peer_->ip == peer.ip && peer_->port == peer.port && peer_->lan == peer.lan;
    peer_ = peer;
    if (unchanged) return;
    Log(std::string("online signaling complete role=") + RoleName(match_->role == "host") +
        " same_network=" + (peer.same_network ? "true" : "false") + " peer_port=" + std::to_string(peer.port));
    Log("signaling peer raw_address=" + peer.ip + " raw_port=" + std::to_string(peer.port) +
        " public=" + (peer.public_port ? peer.public_ip + ":" + std::to_string(peer.public_port) : "none"));
    Log("peer endpoint update raw=" + peer.ip + ":" + std::to_string(peer.port) + " public=" +
        (peer.public_port ? peer.public_ip + ":" + std::to_string(peer.public_port) : "none") +
        " service_forwards_public=" + (peer.service_forwards_public ? "true" : "false"));
    if (FailIfServiceCannotForwardPublic()) return;
    if (udp_setup_ == UdpSetup::kWaitingPeer || udp_setup_ == UdpSetup::kPunching) ConsiderPeer();
    return;
  }
  NetplayAssignment assignment;
  assignment.host = match_->role == "host";
  assignment.peer_ip = ChoosePeerAddress(config_.lan, peer);
  assignment.peer_port = peer.port;
  assignment.guest = config_.guest;
  assignment.generation = match_generation_;
  const bool changed = !assignment_ || assignment_->peer_ip != assignment.peer_ip ||
                       assignment_->peer_port != assignment.peer_port;
  peer_ = peer;
  assignment_ = assignment;
  if (!changed) return;
  Log(std::string("online signaling complete role=") + RoleName(assignment.host) +
      " same_network=" + (peer.same_network ? "true" : "false") + " peer_port=" + std::to_string(peer.port));
  Log("signaling peer raw_address=" + peer.ip + " raw_port=" + std::to_string(peer.port));
  uint32_t parsed_address = 0;
  const netplay::PeerAddressKind kind = netplay::ParsePeerAddress(assignment.peer_ip, parsed_address);
  Log(std::string("signaling peer parsed family=") + netplay::PeerAddressKindName(kind));
  Log("signaling peer endpoint=" +
      (netplay::UsableAddress(kind) ? net::IpText(parsed_address) + ":" + std::to_string(assignment.peer_port) : "none"));
  Notice(Tr("Connected with ") + peer.name + ".");
}

void OnlineService::OnMatchEnded(const std::string& reason) {
  Log("online match ended reason=" + reason);
  if (link_ == NetplayLink::kConnected || link_ == NetplayLink::kFighting) {
    signaling_ended_ = true;
    return;
  }
  const std::string name = match_ ? match_->peer_name : std::string();
  MarkMatchFailed("match_ended_" + reason);
  ClearMatch();
  Notice(reason == "disconnect" ? name + Tr(" disconnected.") : name + Tr(" left the match."));
}

void OnlineService::ClearMatch() {
  if (match_) {
    FlushFightReports();
    LogTransportSummary();
  }
  match_.reset();
  peer_.reset();
  assignment_.reset();
  peer_lost_ = false;
  signaling_ended_ = false;
  if (config_.game_socket && udp_setup_ != UdpSetup::kNone) {
    config_.game_socket->Use([this](netplay::Transport&) { rendezvous_.Stop(); });
  }
  if (config_.game_socket) config_.game_socket->CloseRelay();
  udp_setup_ = UdpSetup::kNone;
  stun_started_ = false;
  mapped_ = {};
  transport_ = MatchTransport::kPending;
  probe_started_ = -1;
  direct_elapsed_ms_ = -1;
  relay_available_ = false;
  relay_requested_ = false;
  relay_used_ = false;
  relay_logged_tx_ = 0;
  relay_logged_rx_ = 0;
  establishment_.Reset();
}

void OnlineService::UpdateFriend(const Friend& entry) {
  for (Friend& f : friends_) {
    if (f.player == entry.player) {
      f = entry;
      return;
    }
  }
  friends_.push_back(entry);
}

void OnlineService::Refresh() {
  if (!Connected()) return;
  next_refresh_ = now_ + kRefreshSeconds;
  Send(Purpose::kFriends, [](int seq) { return EncodeFriendsList(seq); });
  Send(Purpose::kRequests, [](int seq) { return EncodeFriendsRequests(seq); });
}

void OnlineService::OnLinkLost(const std::string& why, double now) {
  Log("online worker disconnected reason=" + why);
  if (channel_) channel_->Close();
  channel_.reset();
  pending_.clear();
  // The relay lives on this connection: a match carried by it, or still choosing with it as the fallback, cannot go on.
  if (match_ && (transport_ == MatchTransport::kRelay || (Coordinated() && transport_ == MatchTransport::kDirectProbing))) {
    Log("relay closed reason=service_disconnected match=" + match_->match);
    MarkMatchFailed("service_disconnected");
    const std::string name = match_->peer_name;
    ClearMatch();
    Notice(Tr("The connection to ") + name + Tr(" was lost."));
  }
  const bool was_online = step_ == Step::kOnline;
  step_ = Step::kIdle;
  hosting_session_.clear();
  joining_ = false;
  invites_.clear();
  for (Friend& f : friends_) {
    f.online = false;
    f.state = FriendState::kOffline;
  }
  if (was_online) Notice(Tr("Connection lost. Reconnecting..."));
  const bool known = !friend_code_.empty();
  connection_ = known ? Connection::kChecking : Connection::kOffline;
  connection_text_ = known ? Tr("Reconnecting") : DescribeServiceError("unreachable");
  retry_at_ = now + retry_delay_;
  retry_delay_ = std::min(retry_delay_ * 2, kMaxRetrySeconds);
}

FriendRequestResult OnlineService::AddFriend(const std::string& text) {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string code = NormalizeFriendCode(text);
  const auto refuse = [&](FriendRequestResult result, const char* reason, const std::string& notice) {
    Log("online friend request failed code=" + (code.empty() ? std::string("-") : code) + " reason=" + reason);
    Notice(notice);
    return result;
  };
  if (!Connected()) return refuse(FriendRequestResult::kNotConnected, "not_connected", Tr("Not connected to the online service."));
  if (code.empty()) return refuse(FriendRequestResult::kInvalid, "invalid", DescribeServiceError("bad_request"));
  if (code == friend_code_) return refuse(FriendRequestResult::kOwnCode, "own_code", Tr("That is your own friend code."));
  const auto is_code = [&code](const auto& entry) { return entry.player == code; };
  if (std::any_of(friends_.begin(), friends_.end(), is_code)) {
    return refuse(FriendRequestResult::kAlreadyFriends, "already_friends", Tr("Already friends."));
  }
  if (std::any_of(outgoing_.begin(), outgoing_.end(), is_code)) {
    return refuse(FriendRequestResult::kAlreadyPending, "already_pending", Tr("Friend request already pending."));
  }
  Log("online friend request sent code=" + code);
  Send(Purpose::kAdd, [&code](int seq) { return EncodeFriendsAdd(seq, code); }, code);
  return FriendRequestResult::kSent;
}

void OnlineService::AcceptFriend(const std::string& player) {
  std::lock_guard<std::mutex> lock(mutex_);
  incoming_.erase(std::remove_if(incoming_.begin(), incoming_.end(), [&](const Request& r) { return r.player == player; }),
                  incoming_.end());
  Send(Purpose::kAccept, [&player](int seq) { return EncodeFriendsAccept(seq, player); });
}

void OnlineService::DeclineFriend(const std::string& player) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto drop = [&](std::vector<Request>& list) {
    list.erase(std::remove_if(list.begin(), list.end(), [&](const Request& r) { return r.player == player; }), list.end());
  };
  drop(incoming_);
  drop(outgoing_);
  Send(Purpose::kDecline, [&player](int seq) { return EncodeFriendsDecline(seq, player); });
}

void OnlineService::RemoveFriend(const std::string& player) {
  std::lock_guard<std::mutex> lock(mutex_);
  friends_.erase(std::remove_if(friends_.begin(), friends_.end(), [&](const Friend& f) { return f.player == player; }),
                 friends_.end());
  Send(Purpose::kRemove, [&player](int seq) { return EncodeFriendsRemove(seq, player); });
}

void OnlineService::LeaveMatch() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!match_) return;
  FlushFightReports();
  const std::string id = match_->match;
  if (Connected()) Send(Purpose::kLeave, [&id](int seq) { return EncodeMatchLeave(seq, id); });
  ClearMatch();
}

void OnlineService::SetNetplayLink(NetplayLink link, std::optional<int> rtt_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  const NetplayLink before = link_;
  link_ = link;
  rtt_ms_ = rtt_ms;
  if (before == link) return;
  ReportNetplayEdge(before, link);
}

void OnlineService::ReportNetplayEdge(NetplayLink before, NetplayLink after) {
  const bool was_up = before >= NetplayLink::kConnected;
  const bool is_up = after >= NetplayLink::kConnected;
  if (match_ && Connected() && was_up != is_up) {
    const std::string id = match_->match;
    Send(Purpose::kReady, [&id, is_up](int seq) { return EncodeMatchReady(seq, id, is_up); });
  }
  if (after == NetplayLink::kNone && signaling_ended_) ClearMatch();
}

void OnlineService::Send(Purpose purpose, const std::function<std::string(int)>& encode, const std::string& player) {
  if (!channel_ || step_ == Step::kIdle || step_ == Step::kOpening) return;
  const int seq = next_seq_++;
  pending_[seq] = {purpose, player, search_epoch_, now_};
  channel_->Send(encode(seq));
}

void OnlineService::SendLocked(const std::string& text) {
  if (channel_) channel_->Send(text);
}

bool OnlineService::ReportFight(const FightReport& report) {
  if (!config_.fight_telemetry) return false;
  std::lock_guard<std::mutex> lock(fight_mutex_);
  return fight_queue_.Push(report);
}

void OnlineService::FlushFightReports() {
  std::vector<FightReport> batch;
  {
    std::lock_guard<std::mutex> lock(fight_mutex_);
    while (const std::optional<FightReport> report = fight_queue_.Pop()) batch.push_back(*report);
  }
  for (const FightReport& report : batch) SendFightReport(report);
}

// Only the match's host reports, on the transport the fight session was handed; anything else is dropped.
void OnlineService::SendFightReport(const FightReport& report) {
  const bool kind_start = report.kind == FightEventKind::kStart;
  if (!Connected() || !capabilities_.fight_telemetry || !match_ || match_->role != "host" || !assignment_ ||
      assignment_->generation != report.generation) {
    Log(std::string("fight telemetry dropped kind=") + (kind_start ? "start" : "complete") +
        " fight=" + std::to_string(report.fight));
    return;
  }
  FightEventFields event;
  event.kind = kind_start ? "start" : "complete";
  event.fight = report.fight;
  event.p1 = report.p1;
  event.p2 = report.p2;
  event.build = config_.guest;
  event.transport = FightTransportOf(*assignment_);
  event.platform = config_.platform;
  event.result = FightResultName(report.result);
  const std::string id = match_->match;
  Send(Purpose::kFightReport, [&](int seq) { return EncodeTelemetryFight(seq, id, event); });
  ++fight_reports_sent_;
  Log("fight telemetry sent kind=" + event.kind + " fight=" + std::to_string(report.fight) + " transport=" + event.transport);
}

void OnlineService::TestConnection() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!settings_.enabled || testing_) return;
  test_.reset();
  EnsureIdentity();
  testing_ = true;
  auto state = std::make_shared<TestState>();
  test_probe_ = state;
  ConnectionTestRequest request;
  request.url = config_.url;
  request.identity = identity_;
  request.name = SessionName();
  request.guest = config_.guest;
  request.build = config_.build;
  request.stun = config_.stun;
  request.upnp = config_.upnp;
  request.game_ports = {config_.host_port, config_.join_port};
  auto work = [state, run = config_.test, request = std::move(request)] {
    std::optional<ConnectionReport> report;
    std::string stopped;
    try {
      report = run(request);
    } catch (const std::exception& e) {
      stopped = e.what();
    } catch (...) {
      stopped = "unknown error";
    }
    std::lock_guard<std::mutex> lock(state->mutex);
    state->report = std::move(report);
    state->stopped = std::move(stopped);
    state->done = true;
  };
  if (config_.synchronous_probes) work();
  else std::thread(work).detach();
}

void OnlineService::CollectTest() {
  if (!test_probe_) return;
  std::optional<ConnectionReport> finished;
  std::string stopped;
  {
    std::lock_guard<std::mutex> lock(test_probe_->mutex);
    if (!test_probe_->done) return;
    finished = test_probe_->report;
    stopped = test_probe_->stopped;
  }
  test_probe_.reset();
  testing_ = false;
  if (!finished) {
    Log("online connection test stopped without a report: " + stopped);
    return;
  }
  test_ = finished;
  ++tests_finished_;
  for (const std::string& detail : finished->details) Log("online connection test " + detail);
  Log("online connection test " + finished->verdict);
}

void OnlineService::SendEndpoint() {
  if (!match_) return;
  if (!config_.game_socket) {
    const uint16_t port = match_->role == "host" ? config_.host_port : config_.join_port;
    Send(Purpose::kEndpoint, [this, port](int seq) { return EncodeMatchEndpoint(seq, match_->match, port, config_.lan); });
    return;
  }
  const uint16_t port = config_.game_socket->bound_port();
  // The service refuses a mapping that is not a public address, which would leave the match without an endpoint.
  const bool mapped = mapped_.port && net::Classify(mapped_.address) == net::AddressKind::kPublic;
  const std::string public_ip = mapped ? net::IpText(mapped_.address) : std::string();
  const uint16_t public_port = mapped ? mapped_.port : 0;
  Log("match endpoint advertised=" + (mapped ? EndpointText(mapped_) : std::string("none")) +
      " local=0.0.0.0:" + std::to_string(port));
  Send(Purpose::kEndpoint, [this, port, public_ip, public_port](int seq) {
    return EncodeMatchEndpoint(seq, match_->match, port, config_.lan, public_ip, public_port);
  });
}

void OnlineService::BeginUdpSetup() {
  if (!config_.game_socket) {
    SendEndpoint();
    return;
  }
  if (!config_.game_socket->is_open()) {
    FailUdpSetup("socket_unbound");
    return;
  }
  udp_setup_ = UdpSetup::kDiscovering;
  transport_ = MatchTransport::kDirectProbing;
  stun_started_ = false;
  mapped_ = {};
  if (stun_servers_ || stun_lookup_) return;
  auto lookup = std::make_shared<StunLookup>();
  stun_lookup_ = lookup;
  const auto resolve = config_.resolve_stun ? config_.resolve_stun : &ResolveStunServers;
  auto work = [lookup, resolve, stun = config_.stun] {
    std::vector<netplay::Endpoint> servers = resolve(stun);
    std::lock_guard<std::mutex> lock(lookup->mutex);
    lookup->servers = std::move(servers);
    lookup->done = true;
  };
  if (config_.synchronous_probes) work();
  else std::thread(work).detach();
}

std::optional<std::vector<netplay::Endpoint>> OnlineService::StunServers() {
  if (stun_servers_) return stun_servers_;
  if (!stun_lookup_) return std::vector<netplay::Endpoint>();
  std::vector<netplay::Endpoint> servers;
  {
    std::lock_guard<std::mutex> lock(stun_lookup_->mutex);
    if (!stun_lookup_->done) return std::nullopt;
    servers = stun_lookup_->servers;
  }
  stun_lookup_.reset();
  // Kept for later matches only once found: a lookup that found nothing (DNS not ready yet) is tried again.
  if (!servers.empty()) stun_servers_ = servers;
  return servers;
}

void OnlineService::PumpUdpSetup(double now) {
  if (!config_.game_socket || !match_ || udp_setup_ == UdpSetup::kNone || udp_setup_ == UdpSetup::kSelected) return;
  if (establishment_.ConnectingExpired(now)) {
    LogEstablishmentTimeout();
    FailUdpSetup("establishment_timeout", Tr("Could not connect to ") + match_->peer_name + ".");
    return;
  }
  std::optional<std::vector<netplay::Endpoint>> servers;
  if (udp_setup_ == UdpSetup::kDiscovering && !stun_started_) {
    servers = StunServers();
    if (!servers) return;
  }
  const uint64_t now_ms = Milliseconds(now);
  const uint16_t local_port = config_.game_socket->bound_port();
  Rendezvous::Stage stun = Rendezvous::Stage::kIdle, punch = Rendezvous::Stage::kIdle;
  netplay::Endpoint mapped;
  Candidate selected;
  config_.game_socket->Pump();
  config_.game_socket->Use([&](netplay::Transport& out) {
    if (servers) rendezvous_.StartStun(*servers, local_port, now_ms, out);
    rendezvous_.Tick(now_ms, out);
    stun = rendezvous_.stun_stage();
    punch = rendezvous_.punch_stage();
    mapped = rendezvous_.mapped();
    selected = rendezvous_.selected();
  });
  if (servers) stun_started_ = true;
  if (udp_setup_ == UdpSetup::kDiscovering) {
    if (stun != Rendezvous::Stage::kDone && stun != Rendezvous::Stage::kFailed) return;
    mapped_ = stun == Rendezvous::Stage::kDone ? mapped : netplay::Endpoint{};
    udp_setup_ = UdpSetup::kWaitingPeer;
    peer_deadline_ = now + kPeerEndpointWaitSeconds;
    BeginDirectProbe(now);
    SendEndpoint();
    Log("peer endpoint pending match=" + match_->match + " peer=" + match_->peer_player);
    ConsiderPeer();
    return;
  }
  if (Coordinated()) {
    PumpCoordinated(now, punch, selected);
    return;
  }
  if (udp_setup_ == UdpSetup::kWaitingPeer) {
    if (now < peer_deadline_) return;
    Log("peer endpoint wait timeout");
    Log(std::string("udp rendezvous failed reason=peer_endpoint_timeout peer_signaled=") +
        (peer_ ? "true" : "false") +
        " service_forwards_public=" + (peer_ && peer_->service_forwards_public ? "true" : "false"));
    FailUdpSetup("no_direct_path",
                 Tr("Could not connect to ") + match_->peer_name + Tr(": their connection details never arrived."));
    return;
  }
  if (udp_setup_ != UdpSetup::kPunching) return;
  if (punch == Rendezvous::Stage::kFailed) {
    FailUdpSetup("no_direct_path");
    return;
  }
  if (punch != Rendezvous::Stage::kDone) return;
  SelectDirect(selected);
}

bool OnlineService::Coordinated() const { return match_ && match_->relay_token && relay_available_; }

void OnlineService::BeginDirectProbe(double now) {
  probe_started_ = now;
  direct_deadline_ = now + kDirectProbeWindowSeconds;
  Log("direct probe begin match=" + match_->match + " budget_ms=" +
      std::to_string(int(kDirectProbeWindowSeconds * 1000)) + " relay_available=" + Flag(Coordinated()));
  if (Coordinated() && match_->role == "host" && capabilities_.announced && !capabilities_.public_endpoints) {
    Log("direct unavailable reason=service_public_endpoints_unsupported");
    RequestRelay("service_public_endpoints_unsupported");
  }
}

// The host chooses: the direct path once its punch is answered, the relay once the window has passed without one.
// The joiner only follows that choice, so both ends always use the same transport.
void OnlineService::PumpCoordinated(double now, Rendezvous::Stage punch, const Candidate& selected) {
  if (match_->role != "host" || relay_requested_) return;
  if (udp_setup_ == UdpSetup::kPunching && punch == Rendezvous::Stage::kDone) {
    SelectDirect(selected);
    return;
  }
  const bool punch_failed = udp_setup_ == UdpSetup::kPunching && punch == Rendezvous::Stage::kFailed;
  if (now >= direct_deadline_ || punch_failed) {
    RequestRelay(udp_setup_ == UdpSetup::kWaitingPeer ? "peer_endpoint_missing" : "no_punch_answer");
  }
}

int OnlineService::DirectElapsedMs() const { return probe_started_ < 0 ? 0 : int((now_ - probe_started_) * 1000.0); }

void OnlineService::RequestRelay(const std::string& why) {
  if (!match_ || relay_requested_ || udp_setup_ == UdpSetup::kSelected) return;
  relay_requested_ = true;
  direct_elapsed_ms_ = DirectElapsedMs();
  const std::string elapsed = std::to_string(direct_elapsed_ms_);
  const std::string id = match_->match;
  if (why != "service_public_endpoints_unsupported") {
    Log("direct path not established elapsed_ms=" + elapsed + " reason=" + why);
  }
  Log("relay fallback begin match=" + id + " direct_elapsed_ms=" + elapsed);
  Send(Purpose::kTransportRelay, [&id](int seq) { return EncodeMatchTransport(seq, id, "relay"); }, id);
}

void OnlineService::SelectDirect(const Candidate& selected) {
  const bool tell_joiner = Coordinated() && match_->role == "host";
  udp_setup_ = UdpSetup::kSelected;
  transport_ = MatchTransport::kDirect;
  direct_elapsed_ms_ = DirectElapsedMs();
  config_.game_socket->CloseRelay();
  const std::string id = match_->match;
  Log("transport selected=direct match=" + id + " endpoint=" + EndpointText(selected.endpoint) +
      " type=" + CandidateTypeName(selected.type) + " elapsed_ms=" + std::to_string(direct_elapsed_ms_));
  if (tell_joiner) Send(Purpose::kTransportDirect, [&id](int seq) { return EncodeMatchTransport(seq, id, "direct"); }, id);
  Assign(selected.endpoint, false);
}

void OnlineService::SelectRelay() {
  const double absolute = establishment_.AbsoluteRemaining(now_);
  Log("relay establishment begin match=" + match_->match +
      " elapsed_ms=" + std::to_string(int(establishment_.Elapsed(now_) * 1000)) +
      " relay_budget_remaining_ms=" + std::to_string(int(std::min(kHandshakeBudgetSeconds, absolute) * 1000)) +
      " absolute_remaining_ms=" + std::to_string(int(absolute * 1000)));
  udp_setup_ = UdpSetup::kSelected;
  transport_ = MatchTransport::kRelay;
  relay_used_ = true;
  if (direct_elapsed_ms_ < 0) direct_elapsed_ms_ = DirectElapsedMs();
  config_.game_socket->Use([this](netplay::Transport&) { rendezvous_.Stop(); });
  config_.game_socket->SelectRelay();
  next_relay_stats_ = now_ + kRelayStatsSeconds;
  Log("relay ready match=" + match_->match + " role=" + RoleName(match_->role == "host"));
  Log("transport selected=relay match=" + match_->match);
  Assign(netplay::kRelayPeer, true);
}

void OnlineService::Assign(const netplay::Endpoint& peer, bool relay) {
  NetplayAssignment assignment;
  assignment.host = match_->role == "host";
  assignment.peer_ip = net::IpText(peer.address);
  assignment.peer_port = peer.port;
  assignment.guest = config_.guest;
  assignment.generation = match_generation_;
  assignment.relay = relay;
  assignment_ = assignment;
  if (establishment_.Select(now_)) {
    Log("network establishment ready match=" + match_->match + " transport=" + (relay ? "relay" : "direct") +
        " elapsed_ms=" + std::to_string(int(establishment_.Elapsed(now_) * 1000)));
  }
  Notice(Tr("Connected with ") + (peer_ ? peer_->name : match_->peer_name) + ".");
}

void OnlineService::OnMatchTransport(const ServerMessage& message) {
  if (!match_ || message.match_id != match_->match || match_->role == "host") return;
  if (!Coordinated() || udp_setup_ == UdpSetup::kNone || udp_setup_ == UdpSetup::kSelected) {
    Log("transport choice ignored transport=" + message.transport);
    return;
  }
  Log("transport choice from host transport=" + message.transport);
  if (message.transport == "relay") {
    const std::string elapsed = std::to_string(DirectElapsedMs());
    Log("direct path not established elapsed_ms=" + elapsed + " reason=host_chose_relay");
    Log("relay fallback begin match=" + match_->match + " direct_elapsed_ms=" + elapsed);
    SelectRelay();
    return;
  }
  // The host heard this side's answer, so this side heard the host: its own proven candidate, else where the host's
  // punches came from.
  Candidate chosen;
  bool known = false;
  config_.game_socket->Use([&](netplay::Transport&) {
    if (rendezvous_.punch_stage() == Rendezvous::Stage::kDone) {
      chosen = rendezvous_.selected();
      known = true;
    } else if (!rendezvous_.heard().empty()) {
      chosen = {CandidateType::kPeer, rendezvous_.heard().front()};
      known = true;
    }
  });
  if (!known) {
    FailUdpSetup("direct_unconfirmed");
    return;
  }
  SelectDirect(chosen);
}

void OnlineService::OnRelayClosed(const ServerMessage& message) {
  if (!match_ || message.match_id != match_->match) return;
  Log("relay closed reason=" + message.reason + " match=" + match_->match);
  if (transport_ == MatchTransport::kRelay) {
    FailUdpSetup("relay_closed", Tr("The connection to ") + match_->peer_name + Tr(" was lost."));
    return;
  }
  if (transport_ != MatchTransport::kDirectProbing) return;
  if (message.reason == "peer_disconnected" || relay_requested_) {
    FailUdpSetup(message.reason == "peer_disconnected" ? "peer_disconnected" : "relay_unavailable");
    return;
  }
  // Without its relay the match can still connect directly, by the rules of a match that never had one.
  config_.game_socket->CloseRelay();
  relay_available_ = false;
}

void OnlineService::PumpRelay(double now) {
  if (!match_ || transport_ != MatchTransport::kRelay || !config_.game_socket) return;
  const std::string failure = config_.game_socket->relay_failure();
  if (!failure.empty()) {
    Log("relay closed reason=" + failure + " match=" + match_->match);
    FailUdpSetup("relay_" + failure, Tr("The connection to ") + match_->peer_name + Tr(" was lost."));
    return;
  }
  const netplay::MatchTraffic traffic = config_.game_socket->traffic();
  for (; relay_logged_tx_ < traffic.relay_tx_sizes.size(); ++relay_logged_tx_) {
    Log("relay tx n=" + std::to_string(relay_logged_tx_ + 1) +
        " bytes=" + std::to_string(traffic.relay_tx_sizes[relay_logged_tx_]));
  }
  for (; relay_logged_rx_ < traffic.relay_rx_sizes.size(); ++relay_logged_rx_) {
    Log("relay rx n=" + std::to_string(relay_logged_rx_ + 1) +
        " bytes=" + std::to_string(traffic.relay_rx_sizes[relay_logged_rx_]));
  }
  if (now < next_relay_stats_) return;
  next_relay_stats_ = now + kRelayStatsSeconds;
  Log("relay stats tx_packets=" + std::to_string(traffic.relay_tx_packets) +
      " tx_bytes=" + std::to_string(traffic.relay_tx_bytes) + " rx_packets=" + std::to_string(traffic.relay_rx_packets) +
      " rx_bytes=" + std::to_string(traffic.relay_rx_bytes) + " rejected=" + std::to_string(traffic.relay_rejected));
}

void OnlineService::LogTransportSummary() {
  const netplay::MatchTraffic traffic = config_.game_socket ? config_.game_socket->traffic() : netplay::MatchTraffic{};
  const bool relayed = transport_ == MatchTransport::kRelay;
  if (relay_used_) {
    Log("relay stats tx_packets=" + std::to_string(traffic.relay_tx_packets) +
        " tx_bytes=" + std::to_string(traffic.relay_tx_bytes) +
        " rx_packets=" + std::to_string(traffic.relay_rx_packets) +
        " rx_bytes=" + std::to_string(traffic.relay_rx_bytes) + " rejected=" + std::to_string(traffic.relay_rejected));
  }
  const char* selected = transport_ == MatchTransport::kDirect ? "direct"
                         : relayed                             ? "relay"
                         : transport_ == MatchTransport::kFailed ? "failed"
                                                                 : "none";
  Log("match transport summary match=" + match_->match + " selected=" + selected +
      " direct_attempted=" + Flag(probe_started_ >= 0) +
      " direct_elapsed_ms=" + std::to_string(direct_elapsed_ms_ >= 0 ? direct_elapsed_ms_ : DirectElapsedMs()) +
      " relay_available=" + Flag(relay_available_ || relay_used_) + " relay_used=" + Flag(relay_used_) +
      " tx_packets=" + std::to_string(relayed ? traffic.relay_tx_packets : traffic.udp_tx_packets) +
      " rx_packets=" + std::to_string(relayed ? traffic.relay_rx_packets : traffic.udp_rx_packets));
}

// Over the Internet only the peer's own STUN endpoint is worth a punch: its observed address with its local port is a
// guess that the peer's endpoint, still on its way, would only replace.
bool OnlineService::PeerEndpointReady(const PeerInfo& peer) const {
  uint32_t address = 0;
  const bool has_public =
      peer.public_port && netplay::UsableAddress(netplay::ParsePeerAddress(peer.public_ip, address));
  return has_public || peer.same_network;
}

// A service that does not forward public endpoints can never deliver the one this match is waiting for: with a relay
// the host goes straight to it, without one the match is left.
bool OnlineService::FailIfServiceCannotForwardPublic() {
  const bool setting_up = udp_setup_ == UdpSetup::kDiscovering || udp_setup_ == UdpSetup::kWaitingPeer;
  if (!match_ || !peer_ || !setting_up || peer_->service_forwards_public || peer_->same_network ||
      PeerEndpointReady(*peer_)) {
    return false;
  }
  if (Coordinated()) {
    if (relay_requested_) return true;
    Log("direct unavailable reason=service_public_endpoints_unsupported");
    if (match_->role == "host") RequestRelay("service_public_endpoints_unsupported");
    return true;
  }
  Log("join network capability service_forwards_public=false action=fail_fast");
  Log("udp rendezvous failed reason=service_capability_mismatch worker_public_endpoints_unsupported=true");
  FailUdpSetup("no_direct_path",
               Tr("Could not connect to ") + match_->peer_name + Tr(": the online service is out of date."));
  return true;
}

void OnlineService::ConsiderPeer() {
  if (!match_ || !peer_ || !PeerEndpointReady(*peer_) || relay_requested_ || udp_setup_ == UdpSetup::kSelected) return;
  if (udp_setup_ == UdpSetup::kWaitingPeer) {
    Log("peer endpoint ready public=" +
        (peer_->public_port ? peer_->public_ip + ":" + std::to_string(peer_->public_port) : std::string("none")) +
        " same_network=" + (peer_->same_network ? "true" : "false"));
  }
  StartPunch();
}

void OnlineService::StartPunch() {
  if (!match_ || !peer_) return;
  const std::vector<Candidate> candidates = PeerCandidates(*peer_, config_.lan, mapped_);
  for (const Candidate& candidate : candidates) {
    Log(std::string("peer candidate received type=") + CandidateTypeName(candidate.type) +
        " endpoint=" + EndpointText(candidate.endpoint));
  }
  if (candidates.empty()) {
    FailUdpSetup("no_candidates");
    return;
  }
  if (Coordinated() && udp_setup_ != UdpSetup::kPunching) {
    direct_deadline_ = std::max(direct_deadline_, now_ + kMinPunchSeconds);
  }
  udp_setup_ = UdpSetup::kPunching;
  Log("udp rendezvous begin candidates=" + std::to_string(candidates.size()));
  const uint64_t key = PunchKey(match_->match);
  const uint8_t side = match_->role == "host" ? 1 : 2;
  const uint64_t now_ms = Milliseconds(now_);
  config_.game_socket->Use(
      [&](netplay::Transport& out) { rendezvous_.StartPunch(key, side, candidates, now_ms, out); });
}

// A match that cannot connect, or lost the relay it played on, is left at once rather than holding both players (and
// Invite Player) in it.
void OnlineService::FailUdpSetup(const std::string& reason, const std::string& notice) {
  const bool established = transport_ == MatchTransport::kDirect || transport_ == MatchTransport::kRelay;
  Log(std::string(established ? "transport lost reason=" : "udp rendezvous failed reason=") +
      reason);
  if (!match_) return;
  if (!established) {
    transport_ = MatchTransport::kFailed;
    Log("transport selected=failed match=" + match_->match + " cause=" + reason +
        " relay_available=" + Flag(Coordinated()));
  }
  MarkMatchFailed(reason);
  const std::string id = match_->match;
  const std::string name = match_->peer_name;
  if (Connected()) Send(Purpose::kLeave, [&id](int seq) { return EncodeMatchLeave(seq, id); });
  ClearMatch();
  Notice(notice.empty() ? Tr("Could not connect to ") + name + Tr(": no direct connection between your networks.")
                         : notice);
}

void OnlineService::MarkMatchFailed(const std::string& reason) {
  if (!match_) return;
  failed_peer_ = ActivePeer{match_->match, match_->peer_player, match_->role == "host"};
  failed_at_ = now_;
  if (establishment_.Fail(now_, reason)) {
    failed_reason_ = reason;
    failed_elapsed_ms_ = int(establishment_.Elapsed(now_) * 1000);
    Log("network establishment failed match=" + match_->match + " reason=" + reason +
        " elapsed_ms=" + std::to_string(failed_elapsed_ms_) + " terminal=true");
  }
}

void OnlineService::LogEstablishmentTimeout() {
  const char* direct = udp_setup_ == UdpSetup::kDiscovering   ? "discovering"
                       : udp_setup_ == UdpSetup::kWaitingPeer ? "waiting_peer"
                       : udp_setup_ == UdpSetup::kPunching    ? "punching"
                                                              : "idle";
  const char* relay = !relay_available_ ? "unavailable" : relay_requested_ ? "requested" : "available";
  Log("network establishment timeout match=" + match_->match +
      " elapsed_ms=" + std::to_string(int(establishment_.Elapsed(now_) * 1000)) +
      " transport_state=" + MatchTransportName(transport_) + " direct_state=" + direct + " relay_state=" + relay);
}

GuestWait OnlineService::GuestWaitState() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!settings_.enabled || !config_.game_socket) return GuestWait::kUnmanaged;
  if (!match_) {
    const bool failed = failed_peer_ && now_ - failed_at_ <= kFailedPeerSeconds;
    return failed ? GuestWait::kFailed : GuestWait::kUnmanaged;
  }
  if (match_->role == "host") return GuestWait::kUnmanaged;
  return establishment_.GuestWaitExpired(now_) ? GuestWait::kFailed : GuestWait::kProgressing;
}

void OnlineService::NoteGuestWaitComplete(bool connected) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!config_.game_socket || guest_wait_logged_ || (!match_ && !failed_peer_)) return;
  guest_wait_logged_ = true;
  if (connected) {
    Log("guest wait complete result=connected elapsed_ms=" +
        std::to_string(int(establishment_.Elapsed(now_) * 1000)));
    return;
  }
  std::string reason = failed_reason_;
  int elapsed_ms = failed_elapsed_ms_;
  if (match_) {
    elapsed_ms = int(establishment_.Elapsed(now_) * 1000);
    reason = !establishment_.reason().empty() ? establishment_.reason()
             : establishment_.phase() == Establishment::Phase::kSelected ? "handshake_timeout"
                                                                         : "establishment_timeout";
  }
  Log("guest wait complete result=failed reason=" + (reason.empty() ? std::string("unknown") : reason) +
      " elapsed_ms=" + std::to_string(elapsed_ms));
}

bool OnlineService::EnsureIdentity() {
  if (!identity_loaded_) {
    identity_loaded_ = true;
    identity_ = config_.identity ? config_.identity() : Identity{};
    if (!identity_.ok()) Log("online identity unavailable " + identity_.error);
  }
  return identity_.ok();
}

std::string OnlineService::SessionName() const {
  std::string name = CleanOnlineName(settings_.name);
  if (!identity_.ephemeral) return name;
  return name.substr(0, kMaxOnlineNameChars - 2) + "-2";
}

std::string OnlineService::NameOf(const std::string& player) const {
  for (const Friend& f : friends_) {
    if (f.player == player) return f.name;
  }
  return Tr("Your friend");
}

void OnlineService::Notice(const std::string& text) {
  notices_.push_back(text);
  notice_ = text;
  notice_at_ = now_;
}

void OnlineService::Log(const std::string& line) {
  if (config_.log) config_.log(line);
}

Snapshot OnlineService::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  Snapshot s;
  s.connection = connection_;
  s.connection_text = connection_text_;
  s.friend_code = friend_code_;
  s.fetched = fetched_;
  s.friends = friends_;
  s.incoming = incoming_;
  s.outgoing = outgoing_;
  s.invites = invites_;
  s.player_match.hosting = hosting_.has_value();
  s.player_match.session = hosting_session_;
  s.player_match.searching = searching_;
  s.player_match.results = results_;
  s.player_match.search_generation = search_generation_;
  if (now_ - notice_at_ <= kNoticeSeconds) s.notice = notice_;
  s.testing = testing_;
  s.test = test_;
  s.tests_finished = tests_finished_;
  if (match_) {
    MatchSnapshot m;
    m.peer_player = match_->peer_player;
    m.peer_name = match_->peer_name;
    m.guest = match_->guest;
    m.host = match_->role == "host";
    m.peer_lost = peer_lost_;
    m.endpoints_exchanged = assignment_.has_value();
    m.link = link_;
    m.rtt_ms = rtt_ms_;
    m.transport = transport_;
    s.match = m;
  }
  return s;
}

std::pair<NetplayLink, std::optional<int>> OnlineService::netplay_info() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return {link_, rtt_ms_};
}

std::vector<std::string> OnlineService::TakeNotices() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> out;
  out.swap(notices_);
  return out;
}

std::optional<NetplayAssignment> OnlineService::Assignment() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return assignment_;
}

}  // namespace ssf2t::online
