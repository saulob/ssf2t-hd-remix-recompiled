#include "online/net_addr.h"

#include <chrono>
#include <cstdio>
#include <mutex>

#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#endif

namespace ssf2t::online::net {

AddressKind Classify(uint32_t ip) {
  const uint32_t a = ip >> 24, b = (ip >> 16) & 0xff;
  if (ip == 0) return AddressKind::kInvalid;
  if (a == 127) return AddressKind::kLoopback;
  if (a == 10 || (a == 172 && b >= 16 && b < 32) || (a == 192 && b == 168)) return AddressKind::kPrivate;
  if (a == 169 && b == 254) return AddressKind::kLinkLocal;
  if (a == 100 && b >= 64 && b < 128) return AddressKind::kCarrierNat;
  if (a == 0 || a >= 224) return AddressKind::kReserved;
  return AddressKind::kPublic;
}

bool IsLan(uint32_t ip) {
  const AddressKind kind = Classify(ip);
  return kind == AddressKind::kLoopback || kind == AddressKind::kPrivate || kind == AddressKind::kLinkLocal;
}

std::string IpText(uint32_t ip) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff);
  return buffer;
}

std::string EndpointText(const Endpoint& endpoint) { return IpText(endpoint.ip) + ":" + std::to_string(endpoint.port); }

uint32_t ParseIp(const std::string& text) {
  unsigned a, b, c, d;
  char tail;
  if (std::sscanf(text.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4 || a > 255 || b > 255 || c > 255 || d > 255) {
    return 0;
  }
  return a << 24 | b << 16 | c << 8 | d;
}

bool SocketsReady() {
#ifdef _WIN32
  static std::once_flag once;
  static bool ready = false;
  std::call_once(once, [] {
    WSADATA data;
    ready = WSAStartup(MAKEWORD(2, 2), &data) == 0;
  });
  return ready;
#else
  return true;
#endif
}

void CloseSocket(SocketHandle socket) {
  if (socket == kNoSocket) return;
#ifdef _WIN32
  closesocket(socket);
#else
  close(socket);
#endif
}

bool SetNonBlocking(SocketHandle socket) {
#ifdef _WIN32
  u_long on = 1;
  return ioctlsocket(socket, FIONBIO, &on) == 0;
#else
  const int flags = fcntl(socket, F_GETFL, 0);
  return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

int LastSocketError() {
#ifdef _WIN32
  return WSAGetLastError();
#else
  return errno;
#endif
}

bool WouldBlock(int error) {
#ifdef _WIN32
  return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
#else
  return error == EWOULDBLOCK || error == EAGAIN || error == EINPROGRESS;
#endif
}

sockaddr_in ToSockaddr(const Endpoint& endpoint) {
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(endpoint.port);
  address.sin_addr.s_addr = htonl(endpoint.ip);
  return address;
}

Endpoint FromSockaddr(const sockaddr_in& address) { return {ntohl(address.sin_addr.s_addr), ntohs(address.sin_port)}; }

uint32_t ResolveIpv4(const std::string& host) {
  if (const uint32_t ip = ParseIp(host)) return ip;
  if (!SocketsReady() || host.empty()) return 0;
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo* found = nullptr;
  if (getaddrinfo(host.c_str(), nullptr, &hints, &found) != 0 || !found) return 0;
  uint32_t ip = 0;
  for (const addrinfo* p = found; p && !ip; p = p->ai_next) {
    if (p->ai_family == AF_INET) ip = ntohl(reinterpret_cast<const sockaddr_in*>(p->ai_addr)->sin_addr.s_addr);
  }
  freeaddrinfo(found);
  return ip;
}

uint32_t DefaultRouteIp() {
  if (!SocketsReady()) return 0;
  const SocketHandle socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (socket_handle == kNoSocket) return 0;
  // Connecting a UDP socket only picks the route; nothing is sent.
  const sockaddr_in target = ToSockaddr({ParseIp("192.0.2.1"), 9});
  uint32_t ip = 0;
  if (connect(socket_handle, reinterpret_cast<const sockaddr*>(&target), sizeof(target)) == 0) {
    sockaddr_in local{};
#ifdef _WIN32
    int length = sizeof(local);
#else
    socklen_t length = sizeof(local);
#endif
    if (getsockname(socket_handle, reinterpret_cast<sockaddr*>(&local), &length) == 0) ip = ntohl(local.sin_addr.s_addr);
  }
  CloseSocket(socket_handle);
  return ip;
}

uint64_t NowMs() {
  return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                      .count());
}

}  // namespace ssf2t::online::net
