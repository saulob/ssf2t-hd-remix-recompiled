#include "online/connection_test.h"

#include <algorithm>
#include <chrono>
#include <thread>

#include "localization/ui_text.h"
#include "online/lobby_link.h"
#include "online/net_addr.h"
#include "online/protocol.h"
#include "online/upnp.h"
#include "online/websocket.h"
#include "updater/json.h"

namespace ssf2t::online {
namespace json = trg::update::json;
namespace {

constexpr int kStepTimeoutMs = 6000;
constexpr int kStunTimeoutMs = 2500;
constexpr char kServiceName[] = "ssf2t-lobby";
constexpr char kNotReachable[] = "Cannot reach the online service. Check your internet connection.";
constexpr char kVerifiedInMatch[] = " Direct peer connectivity will be verified when joining a match.";

struct ServiceProbe {
  bool ok = false;
  bool relay = false;
  std::string refusal;
  int latency_ms = -1;
  std::string detail;
};

ServiceProbe Refused(const std::string& text, const std::string& detail) {
  ServiceProbe probe;
  probe.refusal = text;
  probe.detail = detail;
  return probe;
}

ServiceProbe Unreachable(const std::string& detail) {
  ServiceProbe probe;
  probe.detail = detail;
  return probe;
}

int ElapsedMs(std::chrono::steady_clock::time_point since) {
  return int(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - since).count());
}

ServiceProbe CheckHealth(const std::string& url) {
  HttpResult http;
  std::string error;
  if (!http_get(url, "/health", kStepTimeoutMs, http, &error)) return Unreachable("health: " + error);
  if (http.status != 200) return Unreachable("health: HTTP " + std::to_string(http.status));
  const std::optional<json::Value> body = json::Parse(http.body);
  const json::Value* service = body && body->IsObject() ? body->Find("service") : nullptr;
  const json::Value* protocol = body && body->IsObject() ? body->Find("protocol") : nullptr;
  if (!service || !service->IsString() || service->AsString() != kServiceName || !protocol || !protocol->IsNumber()) {
    return Refused(Tr("The online service answered unexpectedly."), "health: body not recognised");
  }
  const int version = int(protocol->AsInt64().value_or(0));
  if (version != kLobbyProtocol) {
    return Refused(Tr("The online service needs a different version of the game."),
                   "health: service protocol " + std::to_string(version) + ", game " + std::to_string(kLobbyProtocol));
  }
  ServiceProbe probe;
  probe.ok = true;
  probe.detail = "health ok";
  return probe;
}

ServiceProbe SignIn(const ConnectionTestRequest& request) {
  const auto started = std::chrono::steady_clock::now();
  std::unique_ptr<LobbyChannel> channel = OpenLobbyLink(request.url);
  const auto deadline = started + std::chrono::milliseconds(kStepTimeoutMs * 2);
  bool sent = false;
  ChannelEvent event;
  while (std::chrono::steady_clock::now() < deadline) {
    if (!channel->Poll(event)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      continue;
    }
    if (event.kind == ChannelEvent::Kind::kClosed) return Unreachable("websocket: " + event.text);
    if (event.kind == ChannelEvent::Kind::kOpen && !sent) {
      sent = true;
      channel->Send(EncodeHello(
          1, {request.guest, request.build, request.identity.player_id, request.identity.token, request.name, "", "", false}));
      continue;
    }
    if (event.kind == ChannelEvent::Kind::kText) {
      const std::optional<ServerMessage> reply = DecodeServerMessage(event.text);
      const int latency = ElapsedMs(started);
      channel->Close();
      if (!reply || reply->kind != ServerMessage::Kind::kResponse) {
        return Refused(Tr("The online service answered unexpectedly."), "sign-in: reply not recognised");
      }
      if (reply->code != "ok") return Refused(DescribeServiceError(reply->code), "sign-in: code " + reply->code);
      ServiceProbe probe;
      probe.ok = true;
      probe.relay = reply->capabilities && reply->capabilities->relay_supported;
      probe.latency_ms = latency;
      probe.detail = "sign-in ok in " + std::to_string(latency) + " ms; relay " +
                     (probe.relay ? "supported" : "not offered");
      return probe;
    }
  }
  channel->Close();
  return Unreachable("websocket: timed out");
}

ServiceProbe ProbeService(const ConnectionTestRequest& request) {
  ServiceProbe health = CheckHealth(request.url);
  if (!health.ok) return health;
  ServiceProbe signed_in = SignIn(request);
  signed_in.detail = health.detail + "; " + signed_in.detail;
  return signed_in;
}

std::string PortList(const std::vector<uint16_t>& ports) {
  std::string text;
  for (size_t i = 0; i < ports.size(); ++i) {
    text += (i == 0 ? "" : i + 1 == ports.size() ? " or " : ", ") + std::to_string(ports[i]);
  }
  return text;
}

// The joiner of PortList is English text that is also logged, so it is translated where the report shows it.
std::string LocalizedPortList(std::string ports) {
  const std::string joiner = " or ";
  const std::string translated = Tr(joiner);
  if (translated == joiner) return ports;
  for (size_t at = ports.find(joiner); at != std::string::npos; at = ports.find(joiner, at + translated.size())) {
    ports.replace(at, joiner.size(), translated);
  }
  return ports;
}

// On 0.0.0.0, the first free port of the game's, as the game's own socket binds; `local_port` is the one it got.
net::SocketHandle OpenTestSocket(const ConnectionTestRequest& request, ConnectionFacts& facts,
                                 std::vector<std::string>& details, uint16_t& local_port) {
  local_port = 0;
  if (!net::SocketsReady()) {
    details.push_back("sockets are unavailable");
    if (!request.game_ports.empty()) facts.closed_ports = PortList(request.game_ports);
    return net::kNoSocket;
  }
  const std::vector<uint16_t> ports = request.game_ports.empty() ? std::vector<uint16_t>{0} : request.game_ports;
  int error = 0;
  for (const uint16_t port : ports) {
    const net::SocketHandle handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (handle == net::kNoSocket) {
      error = net::LastSocketError();
      break;
    }
    const sockaddr_in any = net::ToSockaddr({0, port});
    sockaddr_in bound{};
#ifdef _WIN32
    int length = sizeof(bound);
#else
    socklen_t length = sizeof(bound);
#endif
    if (bind(handle, reinterpret_cast<const sockaddr*>(&any), sizeof(any)) == 0 &&
        getsockname(handle, reinterpret_cast<sockaddr*>(&bound), &length) == 0) {
      local_port = net::FromSockaddr(bound).port;
      if (port) facts.udp_port = port;
      details.push_back("UDP socket bound local=0.0.0.0:" + std::to_string(local_port) +
                        (port && port != ports.front() ? " (port " + std::to_string(ports.front()) + " was in use)"
                                                       : ""));
      return handle;
    }
    error = net::LastSocketError();
    net::CloseSocket(handle);
  }
  if (!request.game_ports.empty()) facts.closed_ports = PortList(request.game_ports);
  details.push_back("cannot open UDP port " + (request.game_ports.empty() ? std::string("(any)") : facts.closed_ports) +
                    " (error " + std::to_string(error) + ")");
  return net::kNoSocket;
}

uint32_t ProbeStun(const std::vector<std::pair<std::string, uint16_t>>& servers, net::SocketHandle handle,
                   uint16_t local_port, ConnectionFacts& facts, std::vector<std::string>& details) {
  if (servers.empty() || handle == net::kNoSocket) return 0;
  net::Endpoint first, second;
  for (const auto& [host, port] : servers) {
    const uint32_t ip = net::ResolveIpv4(host);
    net::Endpoint mapped;
    if (!ip || !net::stun::Query(handle, {ip, port}, kStunTimeoutMs, mapped)) {
      details.push_back("STUN " + host + ": no answer");
      continue;
    }
    (first.valid() ? second : first) = mapped;
    details.push_back("STUN " + host + ": " + net::EndpointText(mapped));
    if (second.valid()) break;
  }
  facts.udp_ok = first.valid();
  facts.public_address = facts.udp_ok && first.ip == net::DefaultRouteIp();
  facts.second_answer = second.valid();
  facts.same_mapping = second.valid() && second == first;
  if (first.valid()) {
    details.push_back(first.port == local_port ? "NAT: keeps the local port"
                                               : "NAT: maps local port " + std::to_string(local_port) +
                                                     " to public port " + std::to_string(first.port));
  }
  return first.ip;
}

void ProbeRouter(const ConnectionTestRequest& request, uint32_t stun_public_ip, uint16_t local_port,
                 ConnectionFacts& facts, std::vector<std::string>& details) {
  if (!request.upnp) {
    details.push_back("UPnP: turned off");
    return;
  }
  if (!facts.closed_ports.empty() || !local_port || !request.probe_upnp) {
    details.push_back("UPnP: not tried, no UDP port is open");
    return;
  }
  // Probe maps the port and removes the mapping again before it returns.
  const net::upnp::Status status = request.probe_upnp(local_port);
  const std::string upnp = "UPnP port " + std::to_string(local_port) + ": ";
  if (status.state != net::upnp::State::kMapped) {
    details.push_back(upnp + "not available: " + status.detail);
    return;
  }
  facts.router = ClassifyRouter(status.external.ip);
  details.push_back(upnp + status.detail + "; router address " + net::IpText(status.external.ip));
  if (stun_public_ip && status.external.ip && status.external.ip != stun_public_ip) {
    details.push_back("the router's address is not the public address STUN sees (" + net::IpText(stun_public_ip) + ")");
  }
}

}  // namespace

ConnectionFacts::Router ClassifyRouter(uint32_t router_ip) {
  return net::Classify(router_ip) == net::AddressKind::kPublic ? ConnectionFacts::Router::kMapped
                                                               : ConnectionFacts::Router::kMappedBehindNat;
}

ConnectionReport SummarizeConnection(const ConnectionFacts& f) {
  using Mark = ConnectionReport::Mark;
  using Router = ConnectionFacts::Router;
  ConnectionReport r;
  auto line = [&r](Mark mark, std::string label, std::string text) {
    r.lines.push_back({mark, std::move(label), std::move(text)});
  };

  if (!f.identity_ok) {
    line(Mark::kProblem, "Online service", Tr("Your online identity could not be created."));
  } else if (f.lobby_ok) {
    r.latency_ms = int(f.lobby_latency_ms);
    line(Mark::kOk, "Online service",
         f.lobby_latency_ms >= 0 ? Tr("Connected \xC2\xB7 ") + std::to_string(f.lobby_latency_ms) + Tr(" ms")
                                 : std::string(Tr("Connected.")));
  } else {
    line(Mark::kProblem, "Online service", f.lobby_refusal.empty() ? std::string(Tr(kNotReachable)) : f.lobby_refusal);
  }

  // A second STUN server agreeing (same_mapping) or seeing this PC's own address (public_address) actually tests
  // the NAT; one answer alone does not. Neither proves a given peer can reach this PC: only a match's punch does.
  const bool direct_confident = f.udp_ok && (f.public_address || f.same_mapping);
  const bool direct_untested = f.udp_ok && !f.second_answer && !direct_confident;
  const bool router_opened = f.router == Router::kMapped;
  const bool port_open = f.closed_ports.empty();
  const std::string on_port = f.udp_port ? Tr(" on UDP port ") + std::to_string(f.udp_port) : std::string();
  if (!port_open) {
    line(Mark::kProblem, "Direct connections",
         Tr("Cannot open UDP port ") + LocalizedPortList(f.closed_ports) +
             Tr(". Another program may already be using it, or network security may be blocking it."));
  } else if (f.stun_off) {
    line(Mark::kNote, "Direct connections", Tr("Not checked."));
  } else if (!f.udp_ok) {
    line(Mark::kProblem, "Direct connections", Tr("May be blocked by your network or firewall") + on_port + ".");
  } else if (direct_confident) {
    line(Mark::kOk, "Direct connections", Tr("UDP endpoint available") + on_port + "." + Tr(kVerifiedInMatch));
  } else if (direct_untested) {
    line(Mark::kNote, "Direct connections",
         Tr("UDP endpoint available") + on_port +
             Tr(", but your network's NAT type could not be confirmed (only one STUN server answered).") +
             Tr(kVerifiedInMatch));
  } else if (router_opened) {
    line(Mark::kOk, "Direct connections",
         Tr("UDP endpoint available through your router") + on_port + "." + Tr(kVerifiedInMatch));
  } else {
    line(Mark::kProblem, "Direct connections", Tr("Your router may stop some players from connecting to you."));
    line(Mark::kProblem, "Router configuration",
         f.router == Router::kMappedBehindNat
             ? Tr("Another router is in front of yours (often your provider's), so it cannot be opened automatically.")
             : Tr("Automatic setup is not available on your router."));
  }

  // A relayed match plays on the same UDP socket, so the relay only stands in for the direct path when that port opened.
  const bool relay = f.identity_ok && f.lobby_ok && f.relay_ok && port_open;
  if (relay) {
    for (ConnectionReport::Line& entry : r.lines) {
      if (entry.mark == Mark::kProblem && (entry.label == "Direct connections" || entry.label == "Router configuration")) {
        entry.mark = Mark::kNote;
      }
    }
    line(Mark::kOk, "Relay",
         Tr("Available. A match that cannot connect directly connects through the online service."));
  }
  if (f.udp_ok && f.second_answer && !f.same_mapping && !f.public_address) {
    r.details.push_back("NAT: another public port per destination (symmetric NAT)");
  } else if (f.udp_ok && !f.second_answer) {
    r.details.push_back("NAT: type unknown, only one STUN server answered");
  }
  if (f.udp_ok) {
    r.details.push_back(relay ? "peer: direct UDP is only proven by a match's punch; without it the match is relayed"
                              : "peer: direct UDP is only proven by a match's punch; there is no relay");
  }
  r.details.push_back(relay ? "relay: available" : f.relay_ok ? "relay: needs the UDP port" : "relay: not offered by the service");
  r.details.push_back(f.router == Router::kMapped ? "router: UPnP mapping made"
                      : f.router == Router::kMappedBehindNat
                          ? "router: UPnP mapping made, but the router's own address is not public (double NAT or "
                            "carrier-grade NAT)"
                          : "router: no UPnP mapping");

  const bool service_ok = f.identity_ok && f.lobby_ok;
  r.ready = service_ok && port_open && (direct_confident || router_opened || relay);
  if (!service_ok) {
    r.verdict = !f.lobby_refusal.empty() && f.identity_ok ? "Not ready: the online service did not accept this game"
                                                          : "Not ready: cannot reach the online service";
  } else if (r.ready) {
    r.verdict = "Ready for online play";
  } else if (!port_open) {
    r.verdict = "Limited: the multiplayer UDP port could not be opened";
  } else if (direct_untested) {
    r.verdict = "Probably ready: your network's NAT type could not be fully tested";
  } else if (!f.udp_ok && !f.stun_off) {
    r.verdict = "Limited: direct connections may be blocked by your network";
  } else {
    r.verdict = "Limited: some players may not be able to connect to you";
  }
  return r;
}

ConnectionReport RunConnectionTest(const ConnectionTestRequest& request) {
  ConnectionFacts facts;
  std::vector<std::string> details;

  facts.identity_ok = request.identity.ok();
  if (!facts.identity_ok) {
    details.push_back("no online identity: " + request.identity.error);
  } else {
    const ServiceProbe service = ProbeService(request);
    facts.lobby_ok = service.ok;
    facts.relay_ok = service.ok && service.relay;
    facts.lobby_refusal = service.refusal;
    facts.lobby_latency_ms = service.latency_ms;
    details.push_back("service " + request.url + ": " + service.detail);
  }

  const auto servers = net::StunServers(request.stun);
  facts.stun_off = servers.empty();
  uint16_t local_port = 0;
  const net::SocketHandle handle = !request.game_ports.empty() || !servers.empty()
                                       ? OpenTestSocket(request, facts, details, local_port)
                                       : net::kNoSocket;
  const uint32_t stun_public_ip = ProbeStun(servers, handle, local_port, facts, details);
  ProbeRouter(request, stun_public_ip, local_port, facts, details);
  if (handle != net::kNoSocket) net::CloseSocket(handle);

  ConnectionReport report = SummarizeConnection(facts);
  details.insert(details.end(), report.details.begin(), report.details.end());
  report.details = std::move(details);
  return report;
}

}  // namespace ssf2t::online
