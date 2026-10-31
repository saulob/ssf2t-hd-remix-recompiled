#pragma once

#include <cstdint>
#include <string>

#include "online/net_addr.h"

namespace ssf2t::online::net::upnp {

enum class State { kOff, kMapped, kFailed };

struct Status {
  State state = State::kOff;
  Endpoint external;   // the router's WAN address and the mapped port
  std::string detail;  // what happened, for the log and the connection test
};

// The connection test: maps UDP `port` on the home router (Internet Gateway Device) for a minute and removes it again.
Status Probe(uint16_t port);

}  // namespace ssf2t::online::net::upnp
