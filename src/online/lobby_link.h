#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ssf2t::online {

struct ChannelEvent {
  enum class Kind { kOpen, kText, kBinary, kClosed };
  Kind kind = Kind::kText;
  std::string text;  // kText, kBinary: one message; kClosed: why
};

// One connection to the online service. The owner polls it from a single thread; SendBinary may come from any thread.
class LobbyChannel {
 public:
  virtual ~LobbyChannel() = default;
  virtual bool Poll(ChannelEvent& out) = 0;
  virtual void Send(const std::string& text) = 0;
  // False when the connection cannot take it now (closed, or its queue is full).
  virtual bool SendBinary(std::vector<uint8_t>&& bytes) {
    (void)bytes;
    return false;
  }
  virtual void Close() = 0;
};

using ChannelFactory = std::function<std::unique_ptr<LobbyChannel>(const std::string& url)>;

// A WebSocket on its own thread: connects, keeps the link alive with pings, and reports a silent or broken link as
// kClosed. Closing it never blocks the caller.
std::unique_ptr<LobbyChannel> OpenLobbyLink(const std::string& url);

}  // namespace ssf2t::online
