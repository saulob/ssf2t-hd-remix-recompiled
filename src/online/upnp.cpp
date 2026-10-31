#include "online/upnp.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <vector>

#ifndef _WIN32
#include <poll.h>
#endif

namespace ssf2t::online::net::upnp {
namespace {

constexpr char kDescription[] = "Super Street Fighter II Turbo HD Remix";

struct Gateway {
  Endpoint http;        // the router's description server
  std::string control;  // control URL path
  std::string service;  // service type
  uint32_t lan_ip = 0;  // our address towards it
};

struct HttpUrl {
  Endpoint at;
  std::string path;
};

bool ParseHttpUrl(const std::string& url, HttpUrl& out) {
  if (url.rfind("http://", 0) != 0) return false;
  const std::string rest = url.substr(7);
  const size_t slash = rest.find('/');
  const std::string authority = rest.substr(0, slash);
  out.path = slash == std::string::npos ? "/" : rest.substr(slash);
  const size_t colon = authority.find(':');
  out.at.ip = ParseIp(authority.substr(0, colon));
  out.at.port = colon == std::string::npos ? 80 : uint16_t(std::strtoul(authority.c_str() + colon + 1, nullptr, 10));
  return out.at.valid();
}

bool WaitSocket(SocketHandle socket_handle, bool write, int timeout_ms) {
#ifdef _WIN32
  fd_set set;
  FD_ZERO(&set);
  FD_SET(socket_handle, &set);
  timeval timeout{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
  return select(0, write ? nullptr : &set, write ? &set : nullptr, nullptr, &timeout) == 1;
#else
  pollfd pfd{socket_handle, short(write ? POLLOUT : POLLIN), 0};
  return poll(&pfd, 1, timeout_ms) == 1;
#endif
}

// One HTTP/1.1 request to a LAN address; the status code, or 0 when it failed.
int Http(const Endpoint& to, const std::string& request, std::string& body) {
  body.clear();
  const SocketHandle socket_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (socket_handle == kNoSocket) return 0;
  SetNonBlocking(socket_handle);
  const sockaddr_in address = ToSockaddr(to);
  int status = 0;
  if (connect(socket_handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0 || WouldBlock(LastSocketError())) {
    if (WaitSocket(socket_handle, true, 3000)) {
      size_t sent = 0;
      while (sent < request.size() && WaitSocket(socket_handle, true, 3000)) {
        const int n = int(send(socket_handle, request.data() + sent, int(request.size() - sent), 0));
        if (n <= 0) break;
        sent += size_t(n);
      }
      std::string response;
      const uint64_t deadline = NowMs() + 5000;
      while (sent == request.size() && response.size() < 256 * 1024 && NowMs() < deadline &&
             WaitSocket(socket_handle, false, 3000)) {
        char chunk[4096];
        const int n = int(recv(socket_handle, chunk, sizeof(chunk), 0));
        if (n <= 0) break;
        response.append(chunk, size_t(n));
      }
      unsigned code = 0;
      if (std::sscanf(response.c_str(), "HTTP/1.%*u %u", &code) == 1) status = int(code);
      const size_t end = response.find("\r\n\r\n");
      if (end != std::string::npos) body = response.substr(end + 4);
    }
  }
  CloseSocket(socket_handle);
  return status;
}

std::string Tag(const std::string& xml, const std::string& name, size_t from = 0, size_t* at = nullptr) {
  const std::string open = "<" + name + ">", close = "</" + name + ">";
  const size_t start = xml.find(open, from);
  if (start == std::string::npos) return {};
  const size_t end = xml.find(close, start);
  if (end == std::string::npos) return {};
  if (at) *at = end;
  std::string value = xml.substr(start + open.size(), end - start - open.size());
  while (!value.empty() && (value.back() == ' ' || value.back() == '\n' || value.back() == '\r')) value.pop_back();
  while (!value.empty() && (value.front() == ' ' || value.front() == '\n' || value.front() == '\r')) value.erase(0, 1);
  return value;
}

// Routers answer SSDP with the URL of their device description; only LAN routers that answered themselves count.
std::vector<HttpUrl> Discover(uint32_t lan_ip) {
  std::vector<HttpUrl> found;
  const SocketHandle socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (socket_handle == kNoSocket) return found;
  const sockaddr_in local = ToSockaddr({lan_ip, 0});
  bind(socket_handle, reinterpret_cast<const sockaddr*>(&local), sizeof(local));
  const sockaddr_in group = ToSockaddr({ParseIp("239.255.255.250"), 1900});
  for (const char* target : {"urn:schemas-upnp-org:device:InternetGatewayDevice:1",
                             "urn:schemas-upnp-org:service:WANIPConnection:1",
                             "urn:schemas-upnp-org:device:InternetGatewayDevice:2"}) {
    const std::string search =
        std::string("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: ") + target +
        "\r\n\r\n";
    sendto(socket_handle, search.data(), int(search.size()), 0, reinterpret_cast<const sockaddr*>(&group), sizeof(group));
  }
  const uint64_t deadline = NowMs() + 2200;
  while (NowMs() < deadline && found.size() < 4) {
    if (!WaitSocket(socket_handle, false, int(deadline - std::min(deadline, NowMs())))) break;
    char buffer[2048];
    sockaddr_in from{};
#ifdef _WIN32
    int length = sizeof(from);
#else
    socklen_t length = sizeof(from);
#endif
    const int n = int(recvfrom(socket_handle, buffer, sizeof(buffer) - 1, 0, reinterpret_cast<sockaddr*>(&from), &length));
    if (n <= 0) continue;
    buffer[n] = 0;
    std::string reply(buffer, size_t(n)), lower = reply;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    const size_t at = lower.find("\nlocation:");
    if (at == std::string::npos) continue;
    size_t start = at + 10;
    while (start < reply.size() && reply[start] == ' ') ++start;
    const std::string location = reply.substr(start, reply.find('\r', start) - start);
    HttpUrl url;
    if (!ParseHttpUrl(location, url) || url.at.ip != FromSockaddr(from).ip || !IsLan(url.at.ip)) continue;
    if (std::none_of(found.begin(), found.end(), [&](const HttpUrl& u) { return u.at == url.at && u.path == url.path; })) {
      found.push_back(url);
    }
  }
  CloseSocket(socket_handle);
  return found;
}

bool Describe(const HttpUrl& location, Gateway& out) {
  std::string xml;
  const std::string request =
      "GET " + location.path + " HTTP/1.1\r\nHost: " + EndpointText(location.at) + "\r\nConnection: close\r\n\r\n";
  if (Http(location.at, request, xml) != 200) return false;
  for (size_t at = 0;;) {
    const size_t service = xml.find("<service>", at);
    if (service == std::string::npos) return false;
    size_t end = 0;
    const std::string block = xml.substr(service, xml.find("</service>", service) - service);
    at = service + 9;
    const std::string type = Tag(block, "serviceType", 0, &end);
    if (type.find(":WANIPConnection:") == std::string::npos && type.find(":WANPPPConnection:") == std::string::npos) continue;
    std::string control = Tag(block, "controlURL");
    if (control.rfind("http://", 0) == 0) {
      HttpUrl absolute;
      if (!ParseHttpUrl(control, absolute) || absolute.at.ip != location.at.ip) continue;
      out.http = absolute.at;
      control = absolute.path;
    } else {
      out.http = location.at;
      if (control.empty() || control[0] != '/') control = "/" + control;
    }
    out.control = control;
    out.service = type;
    return true;
  }
}

// A SOAP action; the response body, and the UPnP error code (0 = success, -1 = no answer).
int Soap(const Gateway& gateway, const std::string& action, const std::string& args, std::string& response) {
  const std::string body =
      "<?xml version=\"1.0\"?>\r\n<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
      "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:" +
      action + " xmlns:u=\"" + gateway.service + "\">" + args + "</u:" + action + "></s:Body></s:Envelope>\r\n";
  const std::string request = "POST " + gateway.control + " HTTP/1.1\r\nHost: " + EndpointText(gateway.http) +
                              "\r\nContent-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"" + gateway.service + "#" +
                              action + "\"\r\nContent-Length: " + std::to_string(body.size()) +
                              "\r\nConnection: close\r\n\r\n" + body;
  const int status = Http(gateway.http, request, response);
  if (status == 200) return 0;
  if (status == 0) return -1;
  const std::string code = Tag(response, "errorCode");
  return code.empty() ? 500 : std::atoi(code.c_str());
}

std::string MappingArgs(uint16_t external, uint16_t internal, uint32_t lan_ip, uint32_t lease) {
  return "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(external) +
         "</NewExternalPort><NewProtocol>UDP</NewProtocol><NewInternalPort>" + std::to_string(internal) +
         "</NewInternalPort><NewInternalClient>" + IpText(lan_ip) +
         "</NewInternalClient><NewEnabled>1</NewEnabled><NewPortMappingDescription>" + kDescription +
         "</NewPortMappingDescription><NewLeaseDuration>" + std::to_string(lease) + "</NewLeaseDuration>";
}

bool FindGateway(Gateway& gateway, std::string& detail) {
  const uint32_t lan = DefaultRouteIp();
  if (!lan || !IsLan(lan)) {
    detail = lan ? "this PC is not behind a home router" : "no network route";
    return false;
  }
  for (const HttpUrl& location : Discover(lan)) {
    if (Describe(location, gateway)) {
      gateway.lan_ip = lan;
      return true;
    }
  }
  detail = "no UPnP router answered";
  return false;
}

// Adds the mapping (trying a few external ports on conflicts); the external port, or 0.
uint16_t AddMapping(const Gateway& gateway, uint16_t local_port, uint32_t& lease, std::string& detail) {
  std::string response;
  for (uint16_t offset = 0; offset < 5; ++offset) {
    const uint16_t external = uint16_t(local_port + offset);
    int error = Soap(gateway, "AddPortMapping", MappingArgs(external, local_port, gateway.lan_ip, lease), response);
    if (error == 725) {
      lease = 0;
      error = Soap(gateway, "AddPortMapping", MappingArgs(external, local_port, gateway.lan_ip, lease), response);
    }
    if (error == 0) return external;
    if (error != 718 && error != 727) {
      detail = error < 0 ? "the router did not answer" : "the router refused the mapping (error " + std::to_string(error) + ")";
      return 0;
    }
  }
  detail = "the router's ports are taken by other mappings";
  return 0;
}

void RemoveMapping(const Gateway& gateway, uint16_t external) {
  std::string response;
  Soap(gateway, "DeletePortMapping",
       "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(external) +
           "</NewExternalPort><NewProtocol>UDP</NewProtocol>",
       response);
}

uint32_t ExternalAddress(const Gateway& gateway) {
  std::string response;
  if (Soap(gateway, "GetExternalIPAddress", "", response) != 0) return 0;
  return ParseIp(Tag(response, "NewExternalIPAddress"));
}

}  // namespace

Status Probe(uint16_t port) {
  if (!SocketsReady()) return {State::kFailed, {}, "no sockets"};
  Gateway gateway;
  std::string detail;
  if (!FindGateway(gateway, detail)) return {State::kFailed, {}, detail};
  uint32_t lease = 60;
  const uint16_t external = AddMapping(gateway, port, lease, detail);
  if (!external) return {State::kFailed, {}, detail};
  const uint32_t wan = ExternalAddress(gateway);
  RemoveMapping(gateway, external);
  return {State::kMapped, {wan, external}, "the router accepts port mappings"};
}

}  // namespace ssf2t::online::net::upnp
