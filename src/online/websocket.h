// A WebSocket client for the online service: ws:// and wss:// (WinHTTP on Windows, sockets and the system OpenSSL elsewhere). Text
// and binary messages. One thread may receive while others send; close() from any thread ends a blocked receive().
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace ssf2t::online {

struct WsUrl {
  bool tls = false;
  std::string host;
  uint16_t port = 0;
  std::string path = "/";
};

// False with the reason for anything but ws://host[:port][/path] or wss://...
bool parse_ws_url(const std::string& url, WsUrl& out, std::string* error);

class WebSocket {
public:
  virtual ~WebSocket() = default;

  // nullptr with the reason when the server cannot be reached or refuses the upgrade.
  static std::unique_ptr<WebSocket> connect(const std::string& url, int timeout_ms, std::string* error);

  virtual bool send_text(const std::string& text) = 0;
  virtual bool send_binary(const uint8_t* data, size_t size) = 0;
  // A whole message, and whether it was binary; false once the connection is closed or broken.
  virtual bool receive(std::string& data, bool& binary) = 0;
  virtual void close() = 0;
};

struct HttpResult {
  int status = 0;
  std::string body;
};

// One GET of a ws:// or wss:// address as http(s)://, with `path` instead of the address's own. Blocks up to about
// `timeout_ms` per step; the body is capped at 16 KB.
bool http_get(const std::string& ws_url, const std::string& path, int timeout_ms, HttpResult& out, std::string* error);

}  // namespace ssf2t::online
