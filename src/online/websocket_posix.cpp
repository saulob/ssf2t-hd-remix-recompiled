// WebSocket client on BSD sockets, with the system's OpenSSL for wss:// (its certificate store, host name checked).
#include "online/websocket.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <type_traits>
#include <vector>

#include <dlfcn.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "online/random.h"
#include "online/websocket_frame.h"

namespace ssf2t::online {
namespace {

using ws::Base64;

// libssl is loaded at run time: an AppImage cannot carry a GPL-compatible copy, and distributions ship either
// libssl.so.3 or libssl.so.1.1. Both export these functions with the same signatures.
struct SSL_CTX;
struct SSL;
struct SSL_METHOD;

constexpr int SSL_ERROR_WANT_READ = 2;
constexpr int SSL_ERROR_WANT_WRITE = 3;
constexpr int SSL_VERIFY_PEER = 1;
constexpr int SSL_CTRL_SET_TLSEXT_HOSTNAME = 55;
constexpr int SSL_CTRL_SET_MIN_PROTO_VERSION = 123;
constexpr long TLSEXT_NAMETYPE_host_name = 0;
constexpr long TLS1_2_VERSION = 0x0303;

struct OpenSsl {
  const SSL_METHOD* (*TLS_client_method)();
  SSL_CTX* (*SSL_CTX_new)(const SSL_METHOD*);
  void (*SSL_CTX_free)(SSL_CTX*);
  long (*SSL_CTX_ctrl)(SSL_CTX*, int, long, void*);
  int (*SSL_CTX_set_default_verify_paths)(SSL_CTX*);
  void (*SSL_CTX_set_verify)(SSL_CTX*, int, int (*)(int, void*));
  SSL* (*SSL_new)(SSL_CTX*);
  void (*SSL_free)(SSL*);
  long (*SSL_ctrl)(SSL*, int, long, void*);
  int (*SSL_set_fd)(SSL*, int);
  int (*SSL_set1_host)(SSL*, const char*);
  int (*SSL_connect)(SSL*);
  int (*SSL_read)(SSL*, void*, int);
  int (*SSL_write)(SSL*, const void*, int);
  int (*SSL_get_error)(const SSL*, int);
};

const OpenSsl* LoadOpenSsl() {
  static const OpenSsl* loaded = [] () -> const OpenSsl* {
    void* library = nullptr;
    for (const char* name : {"libssl.so.3", "libssl.so.1.1", "libssl.so"}) {
      if ((library = dlopen(name, RTLD_NOW | RTLD_LOCAL))) break;
    }
    if (!library) return nullptr;
    static OpenSsl table;
    bool complete = true;
    const auto take = [&](auto& function, const char* name) {
      function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(dlsym(library, name));
      complete = complete && function;
    };
    take(table.TLS_client_method, "TLS_client_method");
    take(table.SSL_CTX_new, "SSL_CTX_new");
    take(table.SSL_CTX_free, "SSL_CTX_free");
    take(table.SSL_CTX_ctrl, "SSL_CTX_ctrl");
    take(table.SSL_CTX_set_default_verify_paths, "SSL_CTX_set_default_verify_paths");
    take(table.SSL_CTX_set_verify, "SSL_CTX_set_verify");
    take(table.SSL_new, "SSL_new");
    take(table.SSL_free, "SSL_free");
    take(table.SSL_ctrl, "SSL_ctrl");
    take(table.SSL_set_fd, "SSL_set_fd");
    take(table.SSL_set1_host, "SSL_set1_host");
    take(table.SSL_connect, "SSL_connect");
    take(table.SSL_read, "SSL_read");
    take(table.SSL_write, "SSL_write");
    take(table.SSL_get_error, "SSL_get_error");
    return complete ? &table : nullptr;
  }();
  return loaded;
}

// Only called once a TLS stream exists, so the library is loaded.
const OpenSsl& Ssl() { return *LoadOpenSsl(); }

int Connect(const WsUrl& url, int timeout_ms, std::string* error) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* found = nullptr;
  const std::string port = std::to_string(url.port);
  if (getaddrinfo(url.host.c_str(), port.c_str(), &hints, &found) != 0 || !found) {
    if (error) *error = "the online service's host name does not resolve";
    return -1;
  }
  int fd = -1;
  for (const addrinfo* p = found; p && fd < 0; p = p->ai_next) {
    fd = socket(p->ai_family, p->ai_socktype | SOCK_CLOEXEC | SOCK_NONBLOCK, p->ai_protocol);
    if (fd < 0) continue;
    if (::connect(fd, p->ai_addr, p->ai_addrlen) != 0 && errno != EINPROGRESS) {
      ::close(fd), fd = -1;
      continue;
    }
    pollfd pfd{fd, POLLOUT, 0};
    int so_error = 0;
    socklen_t length = sizeof(so_error);
    if (poll(&pfd, 1, timeout_ms) != 1 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &length) != 0 || so_error) {
      ::close(fd), fd = -1;
    }
  }
  freeaddrinfo(found);
  if (fd < 0) {
    if (error) *error = "the online service refused the connection or is not running";
    return -1;
  }
  const int flags = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flags, sizeof(flags));
  return fd;
}

class PosixSocket final : public WebSocket {
public:
  PosixSocket(int fd, SSL_CTX* context, SSL* ssl) : fd_(fd), context_(context), ssl_(ssl) {}

  ~PosixSocket() override {
    close();
    if (ssl_) Ssl().SSL_free(ssl_);
    if (context_) Ssl().SSL_CTX_free(context_);
    ::close(fd_);
  }

  // Blocking I/O with a deadline; nonblocking sockets underneath so close() can end a wait.
  bool Write(const void* data, size_t size) {
    const auto* p = static_cast<const uint8_t*>(data);
    while (size) {
      if (closed_) return false;
      int n;
      if (ssl_) {
        n = Ssl().SSL_write(ssl_, p, int(size));
        if (n <= 0) {
          const int e = Ssl().SSL_get_error(ssl_, n);
          if (e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE) return false;
          if (!Wait(e == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, 10000)) return false;
          continue;
        }
      } else {
        n = int(::send(fd_, p, size, MSG_NOSIGNAL));
        if (n < 0) {
          if (errno != EAGAIN && errno != EWOULDBLOCK) return false;
          if (!Wait(POLLOUT, 10000)) return false;
          continue;
        }
      }
      p += n, size -= size_t(n);
    }
    return true;
  }

  // Some bytes; false once the connection ends.
  bool Read(uint8_t* out, size_t size, size_t& got, int timeout_ms) {
    for (;;) {
      if (closed_) return false;
      if (ssl_) {
        const int n = Ssl().SSL_read(ssl_, out, int(size));
        if (n > 0) return got = size_t(n), true;
        const int e = Ssl().SSL_get_error(ssl_, n);
        if (e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE) return false;
        if (!Wait(e == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, timeout_ms)) return false;
      } else {
        const ssize_t n = ::recv(fd_, out, size, 0);
        if (n > 0) return got = size_t(n), true;
        if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) return false;
        if (!Wait(POLLIN, timeout_ms)) return false;
      }
    }
  }

  bool ReadExact(uint8_t* out, size_t size) {
    while (size) {
      if (pending_.empty()) {
        uint8_t chunk[16384];
        size_t got = 0;
        if (!Read(chunk, sizeof(chunk), got, 75000)) return false;
        pending_.insert(pending_.end(), chunk, chunk + got);
      }
      const size_t take = std::min(size, pending_.size());
      std::memcpy(out, pending_.data(), take);
      pending_.erase(pending_.begin(), pending_.begin() + ptrdiff_t(take));
      out += take, size -= take;
    }
    return true;
  }

  bool Frame(uint8_t opcode, const void* payload, size_t size) {
    uint8_t mask[4];
    RandomBytes(mask, sizeof(mask));
    const std::vector<uint8_t> frame = ws::ClientFrame(opcode, static_cast<const uint8_t*>(payload), size, mask);
    std::lock_guard<std::mutex> lock(send_mutex_);
    return Write(frame.data(), frame.size());
  }

  bool send_text(const std::string& text) override { return !closed_ && Frame(1, text.data(), text.size()); }
  bool send_binary(const uint8_t* data, size_t size) override { return !closed_ && Frame(2, data, size); }

  bool receive(std::string& text, bool& binary) override {
    text.clear();
    binary = false;
    for (;;) {
      uint8_t head[2];
      if (!ReadExact(head, 2)) return false;
      const uint8_t opcode = head[0] & 0x0f;
      uint64_t length = head[1] & 0x7f;
      if (head[1] & 0x80) return false; // servers do not mask
      if (length >= 126) {
        uint8_t ext[8];
        const size_t n = length == 126 ? 2 : 8;
        if (!ReadExact(ext, n)) return false;
        length = 0;
        for (size_t i = 0; i < n; ++i) length = length << 8 | ext[i];
      }
      if (length > 4 * 1024 * 1024 || text.size() + length > 4 * 1024 * 1024) return false;
      std::string payload(size_t(length), '\0');
      if (length && !ReadExact(reinterpret_cast<uint8_t*>(payload.data()), size_t(length))) return false;
      if (opcode == 8) {
        Frame(8, payload.data(), std::min<size_t>(payload.size(), 2));
        return false;
      }
      if (opcode == 9) {
        Frame(10, payload.data(), payload.size());
        continue;
      }
      if (opcode == 10) continue;
      if (opcode != 0 && opcode != 1 && opcode != 2) return false;
      if (opcode == 2) binary = true;
      text += payload;
      if (head[0] & 0x80) return true;
    }
  }

  // Like the Windows client, ends the session with a Close frame so the service sees the player leave at once.
  void close() override {
    if (closing_.exchange(true)) return;
    {
      std::unique_lock<std::mutex> lock(send_mutex_, std::try_to_lock);
      if (lock.owns_lock()) SendCloseFrame();
    }
    closed_ = true;
    ::shutdown(fd_, SHUT_RDWR);
  }

  void SendCloseFrame() {
    uint8_t mask[4];
    RandomBytes(mask, sizeof(mask));
    const uint8_t normal_closure[2] = {0x03, 0xE8};
    const std::vector<uint8_t> frame = ws::ClientFrame(8, normal_closure, sizeof(normal_closure), mask);
    if (ssl_) {
      Ssl().SSL_write(ssl_, frame.data(), int(frame.size()));
    } else {
      ::send(fd_, frame.data(), frame.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
    }
  }

  bool Wait(short events, int timeout_ms) {
    pollfd pfd{fd_, events, 0};
    return poll(&pfd, 1, timeout_ms) == 1 && !closed_ && !(pfd.revents & (POLLERR | POLLNVAL));
  }

private:
  int fd_;
  SSL_CTX* context_;
  SSL* ssl_;
  std::atomic<bool> closing_{false};
  std::atomic<bool> closed_{false};
  std::mutex send_mutex_;
  std::vector<uint8_t> pending_;

  friend std::unique_ptr<WebSocket> WebSocket::connect(const std::string&, int, std::string*);
};

std::unique_ptr<PosixSocket> OpenStream(const WsUrl& parts, int timeout_ms, std::string* error) {
  const int fd = Connect(parts, timeout_ms, error);
  if (fd < 0) return nullptr;
  SSL_CTX* context = nullptr;
  SSL* ssl = nullptr;
  if (parts.tls) {
    const OpenSsl* lib = LoadOpenSsl();
    if (!lib) {
      if (error) *error = "TLS is unavailable: this system has no libssl.so.3 or libssl.so.1.1";
      ::close(fd);
      return nullptr;
    }
    context = lib->SSL_CTX_new(lib->TLS_client_method());
    if (context) {
      lib->SSL_CTX_ctrl(context, SSL_CTRL_SET_MIN_PROTO_VERSION, TLS1_2_VERSION, nullptr);
      lib->SSL_CTX_set_default_verify_paths(context);
      lib->SSL_CTX_set_verify(context, SSL_VERIFY_PEER, nullptr);
      ssl = lib->SSL_new(context);
    }
    if (!ssl) {
      if (error) *error = "TLS is unavailable";
      if (context) lib->SSL_CTX_free(context);
      ::close(fd);
      return nullptr;
    }
    lib->SSL_set_fd(ssl, fd);
    lib->SSL_ctrl(ssl, SSL_CTRL_SET_TLSEXT_HOSTNAME, TLSEXT_NAMETYPE_host_name, const_cast<char*>(parts.host.c_str()));
    lib->SSL_set1_host(ssl, parts.host.c_str());
  }
  auto socket = std::make_unique<PosixSocket>(fd, context, ssl);
  if (ssl) {
    for (;;) {
      const int r = Ssl().SSL_connect(ssl);
      if (r == 1) break;
      const int e = Ssl().SSL_get_error(ssl, r);
      if ((e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE) || !socket->Wait(e == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, timeout_ms)) {
        if (error) *error = "the online service's TLS certificate was not accepted or the handshake failed";
        return nullptr;
      }
    }
  }
  return socket;
}

}  // namespace

std::unique_ptr<WebSocket> WebSocket::connect(const std::string& url, int timeout_ms, std::string* error) {
  WsUrl parts;
  if (!parse_ws_url(url, parts, error)) return nullptr;
  std::unique_ptr<PosixSocket> socket = OpenStream(parts, timeout_ms, error);
  if (!socket) return nullptr;
  uint8_t nonce[16];
  RandomBytes(nonce, sizeof(nonce));
  const std::string key = Base64(nonce, sizeof(nonce));
  const bool default_port = parts.port == (parts.tls ? 443 : 80);
  const std::string request = "GET " + parts.path + " HTTP/1.1\r\nHost: " + parts.host +
                (default_port ? "" : ":" + std::to_string(parts.port)) +
                "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + key +
                "\r\nSec-WebSocket-Version: 13\r\nUser-Agent: SSF2TPC\r\n\r\n";
  if (!socket->Write(request.data(), request.size())) {
    if (error) *error = "the online service closed the connection";
    return nullptr;
  }
  std::string response;
  while (response.find("\r\n\r\n") == std::string::npos) {
    uint8_t chunk[2048];
    size_t got = 0;
    if (response.size() > 16384 || !socket->Read(chunk, sizeof(chunk), got, timeout_ms)) {
      if (error) *error = "the online service did not answer the WebSocket upgrade";
      return nullptr;
    }
    response.append(reinterpret_cast<char*>(chunk), got);
  }
  const size_t end = response.find("\r\n\r\n") + 4;
  socket->pending_.assign(response.begin() + ptrdiff_t(end), response.end());
  response.resize(end);
  const std::string accept = ws::AcceptFor(key);
  std::string lower = response;
  for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
  if (response.rfind("HTTP/1.1 101", 0) != 0) {
    if (error) {
      *error = response.rfind("HTTP/1.1 429", 0) == 0 ? "the online service has too many connections from this address"
                              : "the online service answered " + response.substr(0, response.find('\r'));
    }
    return nullptr;
  }
  if (response.find(accept) == std::string::npos || lower.find("upgrade: websocket") == std::string::npos) {
    if (error) *error = "the online service's WebSocket answer is invalid";
    return nullptr;
  }
  return socket;
}

bool http_get(const std::string& ws_url, const std::string& path, int timeout_ms, HttpResult& out, std::string* error) {
  WsUrl parts;
  if (!parse_ws_url(ws_url, parts, error)) return false;
  std::unique_ptr<PosixSocket> socket = OpenStream(parts, timeout_ms, error);
  if (!socket) return false;
  const std::string request = "GET " + path + " HTTP/1.1\r\nHost: " + parts.host +
                "\r\nConnection: close\r\nUser-Agent: SSF2TPC\r\n\r\n";
  if (!socket->Write(request.data(), request.size())) {
    if (error) *error = "the online service closed the connection";
    return false;
  }
  std::string response;
  uint8_t chunk[2048];
  size_t got = 0;
  while (response.size() < 20 * 1024 && socket->Read(chunk, sizeof(chunk), got, timeout_ms)) {
    response.append(reinterpret_cast<char*>(chunk), got);
  }
  const size_t head_end = response.find("\r\n\r\n");
  if (response.rfind("HTTP/1.", 0) != 0 || head_end == std::string::npos) {
    if (error) *error = "the online service did not answer";
    return false;
  }
  out.status = std::atoi(response.c_str() + 9);
  out.body = response.substr(head_end + 4, 16 * 1024);
  return true;
}

}  // namespace ssf2t::online
