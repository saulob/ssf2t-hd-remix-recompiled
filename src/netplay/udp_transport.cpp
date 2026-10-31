#include "netplay/udp_transport.h"

#include <algorithm>
#include <atomic>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace ssf2t::netplay {
namespace {

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalid = INVALID_SOCKET;

std::atomic<int> g_wsa_users{0};

bool AcquireSockets() {
  if (g_wsa_users.fetch_add(1) == 0) {
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
      g_wsa_users.fetch_sub(1);
      return false;
    }
  }
  return true;
}

void ReleaseSockets() {
  if (g_wsa_users.fetch_sub(1) == 1) WSACleanup();
}

void CloseNative(NativeSocket s) { closesocket(s); }

bool SetNonBlocking(NativeSocket s) {
  u_long on = 1;
  return ioctlsocket(s, FIONBIO, &on) == 0;
}

void IgnoreConnReset(NativeSocket s) {
  BOOL off = FALSE;
  DWORD returned = 0;
  WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof off, nullptr, 0, &returned, nullptr, nullptr);
}
#else
using NativeSocket = int;
constexpr NativeSocket kInvalid = -1;

bool AcquireSockets() { return true; }
void ReleaseSockets() {}
void CloseNative(NativeSocket s) { close(s); }

bool SetNonBlocking(NativeSocket s) {
  const int flags = fcntl(s, F_GETFL, 0);
  return flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
}

void IgnoreConnReset(NativeSocket) {}
#endif

}  // namespace

bool ParseIPv4(std::string_view text, uint32_t& address) {
  if (text == "localhost") {
    address = 0x7F000001;
    return true;
  }
  uint32_t value = 0;
  size_t pos = 0;
  for (int octet = 0; octet < 4; ++octet) {
    if (pos >= text.size()) return false;
    uint32_t part = 0;
    size_t digits = 0;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
      part = part * 10 + uint32_t(text[pos] - '0');
      if (part > 255 || ++digits > 3) return false;
      ++pos;
    }
    if (digits == 0) return false;
    value = (value << 8) | part;
    if (octet < 3) {
      if (pos >= text.size() || text[pos] != '.') return false;
      ++pos;
    }
  }
  if (pos != text.size()) return false;
  address = value;
  return true;
}

const char* PeerAddressKindName(PeerAddressKind kind) {
  switch (kind) {
    case PeerAddressKind::kEmpty: return "empty";
    case PeerAddressKind::kIPv4: return "ipv4";
    case PeerAddressKind::kIPv4MappedIPv6: return "ipv4_mapped_ipv6";
    case PeerAddressKind::kHostPort: return "host_port";
    case PeerAddressKind::kIPv6: return "ipv6";
    default: return "invalid";
  }
}

namespace {

std::string_view Trim(std::string_view text) {
  size_t begin = 0, end = text.size();
  while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) ++begin;
  while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) --end;
  return text.substr(begin, end - begin);
}

bool StartsWithIgnoreCase(std::string_view text, std::string_view prefix) {
  if (text.size() < prefix.size()) return false;
  for (size_t i = 0; i < prefix.size(); ++i) {
    if ((text[i] | 0x20) != (prefix[i] | 0x20)) return false;
  }
  return true;
}

}  // namespace

PeerAddressKind ParsePeerAddress(std::string_view text, uint32_t& address) {
  address = 0;
  std::string_view value = Trim(text);
  if (value.empty()) return PeerAddressKind::kEmpty;

  bool had_port_suffix = false;
  if (value.front() == '[') {
    const size_t close = value.find(']');
    if (close == std::string_view::npos) return PeerAddressKind::kInvalid;
    value = value.substr(1, close - 1);
    if (value.empty()) return PeerAddressKind::kEmpty;
  } else if (const size_t colons = size_t(std::count(value.begin(), value.end(), ':')); colons == 1) {
    value = value.substr(0, value.find(':'));
    had_port_suffix = true;
  } else if (colons >= 2 && !StartsWithIgnoreCase(value, "::ffff:")) {
    return PeerAddressKind::kIPv6;
  }

  if (ParseIPv4(value, address)) return had_port_suffix ? PeerAddressKind::kHostPort : PeerAddressKind::kIPv4;
  if (StartsWithIgnoreCase(value, "::ffff:") && ParseIPv4(value.substr(7), address)) {
    return PeerAddressKind::kIPv4MappedIPv6;
  }
  if (std::count(value.begin(), value.end(), ':') >= 2) return PeerAddressKind::kIPv6;
  return PeerAddressKind::kInvalid;
}

UdpTransport::~UdpTransport() { Close(); }

bool UdpTransport::Open(uint16_t port, bool loopback_only) {
  Close();
  if (!AcquireSockets()) return false;
  const NativeSocket s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == kInvalid) {
    ReleaseSockets();
    return false;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(loopback_only ? INADDR_LOOPBACK : INADDR_ANY);
  if (!SetNonBlocking(s) || bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
    CloseNative(s);
    ReleaseSockets();
    return false;
  }
  IgnoreConnReset(s);
  sockaddr_in bound{};
#if defined(_WIN32)
  int length = sizeof bound;
#else
  socklen_t length = sizeof bound;
#endif
  if (getsockname(s, reinterpret_cast<sockaddr*>(&bound), &length) != 0) {
    CloseNative(s);
    ReleaseSockets();
    return false;
  }
  socket_ = static_cast<intptr_t>(s);
  bound_port_ = ntohs(bound.sin_port);
  open_ = true;
  return true;
}

void UdpTransport::Close() {
  if (!open_) return;
  CloseNative(static_cast<NativeSocket>(socket_));
  ReleaseSockets();
  socket_ = -1;
  open_ = false;
  bound_port_ = 0;
}

bool UdpTransport::Send(const Endpoint& to, const uint8_t* data, size_t size) {
  if (!open_ || size == 0) return false;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(to.port);
  addr.sin_addr.s_addr = htonl(to.address);
  const auto sent = sendto(static_cast<NativeSocket>(socket_), reinterpret_cast<const char*>(data),
                           static_cast<int>(size), 0, reinterpret_cast<const sockaddr*>(&addr), sizeof addr);
  return sent == static_cast<decltype(sent)>(size);
}

size_t UdpTransport::Receive(uint8_t* buffer, size_t capacity, Endpoint& from) {
  if (!open_ || capacity == 0) return 0;
  sockaddr_in addr{};
#if defined(_WIN32)
  int length = sizeof addr;
#else
  socklen_t length = sizeof addr;
#endif
  const auto n = recvfrom(static_cast<NativeSocket>(socket_), reinterpret_cast<char*>(buffer),
                          static_cast<int>(capacity), 0, reinterpret_cast<sockaddr*>(&addr), &length);
  if (n <= 0) return 0;
  from.address = ntohl(addr.sin_addr.s_addr);
  from.port = ntohs(addr.sin_port);
  return static_cast<size_t>(n);
}

}  // namespace ssf2t::netplay
