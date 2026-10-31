#include "online/stun.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "online/random.h"

#ifndef _WIN32
#include <poll.h>
#endif

namespace ssf2t::online::net {
namespace {

std::string Trim(std::string text) {
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.pop_back();
  size_t start = 0;
  while (start < text.size() && (text[start] == ' ' || text[start] == '\t')) ++start;
  return text.substr(start);
}

}  // namespace

std::vector<std::pair<std::string, uint16_t>> StunServers(const std::string& list) {
  std::vector<std::pair<std::string, uint16_t>> out;
  if (Trim(list) == "off") return out;
  size_t start = 0;
  while (start <= list.size()) {
    const size_t comma = list.find(',', start);
    const std::string item = Trim(list.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
    if (!item.empty()) {
      const size_t colon = item.rfind(':');
      uint16_t port = 3478;
      std::string host = item;
      if (colon != std::string::npos) {
        const long value = std::strtol(item.c_str() + colon + 1, nullptr, 10);
        host = item.substr(0, colon);
        if (value > 0 && value <= 65535) port = uint16_t(value);
      }
      if (!host.empty()) out.emplace_back(host, port);
    }
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return out;
}

}  // namespace ssf2t::online::net

namespace ssf2t::online::net::stun {
namespace {

constexpr uint32_t kCookie = 0x2112a442;
constexpr uint16_t kBindingRequest = 0x0001, kBindingSuccess = 0x0101;
constexpr uint16_t kMappedAddress = 0x0001, kXorMappedAddress = 0x0020;

uint16_t Be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

bool LooksLikeStun(const uint8_t* data, size_t size) {
  return size >= 20 && (data[0] & 0xc0) == 0 && Be32(data + 4) == kCookie && (Be16(data + 2) & 3) == 0;
}

}  // namespace

void MakeRequest(const TransactionId& id, uint8_t out[kRequestSize]) {
  out[0] = kBindingRequest >> 8, out[1] = kBindingRequest & 0xff;
  out[2] = out[3] = 0;
  out[4] = kCookie >> 24, out[5] = (kCookie >> 16) & 0xff, out[6] = (kCookie >> 8) & 0xff, out[7] = kCookie & 0xff;
  std::memcpy(out + 8, id.data(), id.size());
}

bool ParseResponse(const uint8_t* data, size_t size, const TransactionId& id, Endpoint& mapped) {
  if (!LooksLikeStun(data, size) || Be16(data) != kBindingSuccess || std::memcmp(data + 8, id.data(), id.size())) {
    return false;
  }
  const size_t length = Be16(data + 2);
  if (20 + length > size) return false;
  bool found_plain = false, found_xor = false;
  Endpoint plain, xored;
  for (size_t at = 20; at + 4 <= 20 + length;) {
    const uint16_t type = Be16(data + at), attribute = Be16(data + at + 2);
    const uint8_t* value = data + at + 4;
    if (at + 4 + attribute > 20 + length) return false;
    if ((type == kMappedAddress || type == kXorMappedAddress) && attribute >= 8 && value[1] == 0x01) {
      uint16_t port = Be16(value + 2);
      uint32_t ip = Be32(value + 4);
      if (type == kXorMappedAddress) {
        port ^= uint16_t(kCookie >> 16);
        ip ^= kCookie;
        xored = {ip, port}, found_xor = true;
      } else {
        plain = {ip, port}, found_plain = true;
      }
    }
    at += 4 + ((attribute + 3u) & ~3u);
  }
  if (found_xor) mapped = xored;
  else if (found_plain) mapped = plain;
  return (found_xor || found_plain) && mapped.valid();
}

bool Query(SocketHandle socket_handle, const Endpoint& server, int timeout_ms, Endpoint& mapped) {
  TransactionId id;
  if (!RandomBytes(id.data(), id.size())) return false;
  uint8_t request[kRequestSize];
  MakeRequest(id, request);
  const sockaddr_in to = ToSockaddr(server);
  const uint64_t deadline = NowMs() + uint64_t(timeout_ms);
  uint64_t next_send = 0;
  int attempt = 0;
  while (NowMs() < deadline) {
    if (NowMs() >= next_send) {
      sendto(socket_handle, reinterpret_cast<const char*>(request), int(sizeof(request)), 0,
             reinterpret_cast<const sockaddr*>(&to), sizeof(to));
      next_send = NowMs() + uint64_t(250 << std::min(attempt++, 3));
    }
    const uint64_t now = NowMs(), until = std::min(next_send, deadline);
    const int wait = until > now ? int(until - now) : 0;
#ifdef _WIN32
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(socket_handle, &read_set);
    timeval timeout{wait / 1000, (wait % 1000) * 1000};
    if (select(0, &read_set, nullptr, nullptr, &timeout) <= 0) continue;
#else
    pollfd pfd{socket_handle, POLLIN, 0};
    if (poll(&pfd, 1, wait) <= 0) continue;
#endif
    uint8_t buffer[1500];
    sockaddr_in from{};
#ifdef _WIN32
    int length = sizeof(from);
#else
    socklen_t length = sizeof(from);
#endif
    const int n = int(recvfrom(socket_handle, reinterpret_cast<char*>(buffer), int(sizeof(buffer)), 0,
                               reinterpret_cast<sockaddr*>(&from), &length));
    if (n > 0 && ParseResponse(buffer, size_t(n), id, mapped)) return true;
  }
  return false;
}

}  // namespace ssf2t::online::net::stun
