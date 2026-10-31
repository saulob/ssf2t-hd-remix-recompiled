#pragma once

#include <cstdint>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace ssf2t::online::net {

#ifdef _WIN32
using SocketHandle = SOCKET;
inline constexpr SocketHandle kNoSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
inline constexpr SocketHandle kNoSocket = -1;
#endif

// Address and port in host order.
struct Endpoint {
  uint32_t ip = 0;
  uint16_t port = 0;

  bool valid() const { return ip != 0 && port != 0; }
  bool operator==(const Endpoint&) const = default;
};

enum class AddressKind { kInvalid, kLoopback, kPrivate, kLinkLocal, kCarrierNat, kReserved, kPublic };

AddressKind Classify(uint32_t ip);
bool IsLan(uint32_t ip);  // loopback, RFC 1918 or link-local
std::string IpText(uint32_t ip);
std::string EndpointText(const Endpoint& endpoint);
// 0 when it is not a dotted IPv4 address.
uint32_t ParseIp(const std::string& text);

// Starts Winsock once per process (no-op elsewhere); false when it is unavailable.
bool SocketsReady();
void CloseSocket(SocketHandle socket);
bool SetNonBlocking(SocketHandle socket);
int LastSocketError();
bool WouldBlock(int error);

sockaddr_in ToSockaddr(const Endpoint& endpoint);
Endpoint FromSockaddr(const sockaddr_in& address);

// IPv4 of a host name or dotted address; 0 when it does not resolve. Blocks.
uint32_t ResolveIpv4(const std::string& host);

// The local address the system would send Internet traffic from (no packet is sent); 0 when there is no route.
uint32_t DefaultRouteIp();

// Milliseconds of a monotonic clock.
uint64_t NowMs();

}  // namespace ssf2t::online::net
