#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "online/net_addr.h"

namespace ssf2t::online::net {

inline constexpr char kDefaultStunServers[] = "stun.cloudflare.com:3478,stun.l.google.com:19302";

// "host:port" entries of a comma list; the port defaults to 3478. "off" or an empty list means no STUN.
std::vector<std::pair<std::string, uint16_t>> StunServers(const std::string& list);

}  // namespace ssf2t::online::net

namespace ssf2t::online::net::stun {

using TransactionId = std::array<uint8_t, 12>;

inline constexpr size_t kRequestSize = 20;

void MakeRequest(const TransactionId& id, uint8_t out[kRequestSize]);
// The mapped address of a Binding success response to `id` (XOR-MAPPED-ADDRESS preferred over MAPPED-ADDRESS).
bool ParseResponse(const uint8_t* data, size_t size, const TransactionId& id, Endpoint& mapped);

// Asks one server from `socket` (blocking, up to `timeout_ms`).
bool Query(SocketHandle socket, const Endpoint& server, int timeout_ms, Endpoint& mapped);

}  // namespace ssf2t::online::net::stun
