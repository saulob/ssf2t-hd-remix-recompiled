#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "online/identity.h"
#include "online/stun.h"
#include "online/upnp.h"

namespace ssf2t::online {

enum class Connection { kOff, kChecking, kOnline, kOffline, kError };

struct ConnectionFacts {
  enum class Router { kNone, kMapped, kMappedBehindNat };
  bool identity_ok = true;
  bool lobby_ok = false;
  std::string lobby_refusal;     // the service answered but refused this game; shown instead of "cannot reach"
  int64_t lobby_latency_ms = -1;  // the sign-in's round trip to the online service, not a player's Ping
  bool stun_off = false;
  bool udp_ok = false;          // a STUN server answered
  bool public_address = false;  // the STUN address is this PC's own
  bool second_answer = false;   // a second STUN server answered
  bool same_mapping = false;    // and saw the same public port
  Router router = Router::kNone;
  uint16_t udp_port = 0;     // the multiplayer port the test opened; 0: none, or any free one when none was asked
  std::string closed_ports;  // the multiplayer ports it could not open ("7001 or 7002"); empty when it opened one
  bool relay_ok = false;     // the service relays a match that cannot connect directly
};

struct ConnectionReport {
  enum class Mark { kOk, kNote, kProblem };
  struct Line {
    Mark mark = Mark::kProblem;
    std::string label;
    std::string text;
  };
  std::vector<Line> lines;  // plain language, no addresses; when ready only what makes it ready
  bool ready = false;
  std::string verdict;
  int latency_ms = -1;
  std::vector<std::string> details;  // addresses, servers and errors, for the log
};

ConnectionReport SummarizeConnection(const ConnectionFacts& facts);

// A router that accepted the UPnP mapping and reports a private, carrier-grade or empty (0.0.0.0) address has another NAT
// in front of it.
ConnectionFacts::Router ClassifyRouter(uint32_t router_ip);

struct ConnectionTestRequest {
  std::string url;
  Identity identity;
  std::string name;
  std::string guest;
  int build = 1;
  std::string stun = net::kDefaultStunServers;
  bool upnp = true;
  // STUN and UPnP use one socket bound to the first of these that is free, as the game's socket is, and never
  // another port. Empty: any free port.
  std::vector<uint16_t> game_ports = {7001, 7002};
  std::function<net::upnp::Status(uint16_t port)> probe_upnp = net::upnp::Probe;
};

// Blocks for a few seconds (service, two STUN servers, the router); run it off the UI thread. Its socket is closed
// when it returns.
ConnectionReport RunConnectionTest(const ConnectionTestRequest& request);

}  // namespace ssf2t::online
