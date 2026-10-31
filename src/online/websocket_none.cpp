// Stand-in for builds without OpenSSL: online play reports itself unavailable instead of failing to link.
#include "online/websocket.h"

namespace ssf2t::online {

std::unique_ptr<WebSocket> WebSocket::connect(const std::string&, int, std::string* error) {
  if (error) *error = "this build has no secure connection support";
  return nullptr;
}

bool http_get(const std::string&, const std::string&, int, HttpResult&, std::string* error) {
  if (error) *error = "this build has no secure connection support";
  return false;
}

}  // namespace ssf2t::online
