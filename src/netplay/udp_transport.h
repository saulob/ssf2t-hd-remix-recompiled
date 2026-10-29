#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ssf2t::netplay {

struct Endpoint {
  uint32_t address = 0;  // IPv4, host byte order
  uint16_t port = 0;
  friend bool operator==(const Endpoint&, const Endpoint&) = default;
};

// Dotted quad or "localhost".
bool ParseIPv4(std::string_view text, uint32_t& address);

class Transport {
 public:
  virtual ~Transport() = default;
  virtual bool Send(const Endpoint& to, const uint8_t* data, size_t size) = 0;
  // Never blocks. Returns the datagram size, or 0 when none is waiting.
  virtual size_t Receive(uint8_t* buffer, size_t capacity, Endpoint& from) = 0;
};

// IPv4 UDP on a non-blocking socket. Not thread-safe: the owner calls it from one thread.
class UdpTransport final : public Transport {
 public:
  UdpTransport() = default;
  ~UdpTransport() override;
  UdpTransport(const UdpTransport&) = delete;
  UdpTransport& operator=(const UdpTransport&) = delete;

  // Port 0 picks a free one; `loopback_only` binds 127.0.0.1 instead of every interface.
  bool Open(uint16_t port, bool loopback_only);
  void Close();
  bool is_open() const { return open_; }
  uint16_t bound_port() const { return bound_port_; }

  bool Send(const Endpoint& to, const uint8_t* data, size_t size) override;
  size_t Receive(uint8_t* buffer, size_t capacity, Endpoint& from) override;

 private:
  intptr_t socket_ = -1;
  bool open_ = false;
  uint16_t bound_port_ = 0;
};

}  // namespace ssf2t::netplay
