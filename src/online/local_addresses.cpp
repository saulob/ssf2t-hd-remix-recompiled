#include "online/local_addresses.h"

#include <cstdio>

#if defined(_WIN32)
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

namespace ssf2t::online {
namespace {

std::string PrimaryLanAddress() {
#if defined(_WIN32)
  WSADATA data;
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return {};
  const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  const bool valid = s != INVALID_SOCKET;
#else
  const int s = socket(AF_INET, SOCK_DGRAM, 0);
  const bool valid = s >= 0;
#endif
  std::string result;
  if (valid) {
    // Connecting a UDP socket sends nothing; it only makes the system pick the outgoing interface.
    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(9);
    inet_pton(AF_INET, "192.0.2.1", &remote.sin_addr);
    sockaddr_in local{};
#if defined(_WIN32)
    int length = sizeof(local);
#else
    socklen_t length = sizeof(local);
#endif
    char text[INET_ADDRSTRLEN] = {};
    if (connect(s, reinterpret_cast<sockaddr*>(&remote), sizeof(remote)) == 0 &&
        getsockname(s, reinterpret_cast<sockaddr*>(&local), &length) == 0 &&
        inet_ntop(AF_INET, &local.sin_addr, text, sizeof(text))) {
      result = text;
    }
  }
#if defined(_WIN32)
  if (valid) closesocket(s);
  WSACleanup();
#else
  if (valid) close(s);
#endif
  return result;
}

}  // namespace

bool IsPrivateIPv4(const std::string& ip) {
  int a = 0, b = 0, c = 0, d = 0;
  if (std::sscanf(ip.c_str(), "%d.%d.%d.%d", &a, &b, &c, &d) != 4) return false;
  return a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168);
}

std::vector<std::string> LocalCandidates() {
  std::vector<std::string> out = {"127.0.0.1"};
  const std::string lan = PrimaryLanAddress();
  if (IsPrivateIPv4(lan)) out.push_back(lan);
  return out;
}

}  // namespace ssf2t::online
