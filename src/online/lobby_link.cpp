#include "online/lobby_link.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#include "online/websocket.h"

namespace ssf2t::online {
namespace {

constexpr int kConnectTimeoutMs = 8000;
constexpr int kKeepaliveMs = 20000;
constexpr int kSilenceMs = 45000;
// Relay frames waiting for the socket; a link this far behind is not carrying a fight any more.
constexpr size_t kMaxQueuedBinary = 512;

using Clock = std::chrono::steady_clock;

struct Outgoing {
  std::string data;
  bool binary = false;
};

struct Shared {
  std::mutex mutex;
  std::condition_variable wake;
  std::deque<Outgoing> outgoing;
  size_t queued_binary = 0;
  std::deque<ChannelEvent> incoming;
  std::shared_ptr<WebSocket> socket;
  bool stop = false;
};

void Push(Shared& s, ChannelEvent::Kind kind, std::string text = {}) {
  std::lock_guard<std::mutex> lock(s.mutex);
  s.incoming.push_back({kind, std::move(text)});
}

void Run(std::shared_ptr<Shared> s, std::string url) {
  std::string error;
  std::shared_ptr<WebSocket> socket(WebSocket::connect(url, kConnectTimeoutMs, &error));
  {
    std::lock_guard<std::mutex> lock(s->mutex);
    if (!socket || s->stop) {
      if (socket) socket->close();
      if (!s->stop) s->incoming.push_back({ChannelEvent::Kind::kClosed, error.empty() ? "unreachable" : error});
      return;
    }
    s->socket = socket;
    s->incoming.push_back({ChannelEvent::Kind::kOpen, {}});
  }

  std::atomic<bool> reading{true};
  std::atomic<int64_t> last_rx{std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count()};
  const auto now_ms = [] {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
  };
  std::thread reader([&] {
    std::string text;
    bool binary = false;
    while (socket->receive(text, binary)) {
      last_rx = now_ms();
      if (binary) Push(*s, ChannelEvent::Kind::kBinary, std::move(text));
      else if (text != "pong") Push(*s, ChannelEvent::Kind::kText, text);
    }
    reading = false;
    s->wake.notify_all();
  });

  std::string reason = "closed";
  int64_t last_ping = now_ms();
  std::unique_lock<std::mutex> lock(s->mutex);
  while (!s->stop && reading) {
    while (!s->outgoing.empty()) {
      const Outgoing message = std::move(s->outgoing.front());
      s->outgoing.pop_front();
      if (message.binary) --s->queued_binary;
      lock.unlock();
      const bool sent = message.binary ? socket->send_binary(reinterpret_cast<const uint8_t*>(message.data.data()),
                                                             message.data.size())
                                       : socket->send_text(message.data);
      lock.lock();
      if (!sent) break;
    }
    const int64_t now = now_ms();
    if (now - last_ping >= kKeepaliveMs) {
      last_ping = now;
      lock.unlock();
      socket->send_text("ping");
      lock.lock();
    }
    if (now - last_rx > kSilenceMs) {
      reason = "timeout";
      break;
    }
    s->wake.wait_for(lock, std::chrono::milliseconds(250));
  }
  const bool stopped = s->stop;
  s->socket.reset();
  lock.unlock();
  socket->close();
  reader.join();
  if (!stopped) Push(*s, ChannelEvent::Kind::kClosed, reason);
}

class LobbyLink final : public LobbyChannel {
 public:
  explicit LobbyLink(const std::string& url) : shared_(std::make_shared<Shared>()) {
    std::thread(Run, shared_, url).detach();
  }

  ~LobbyLink() override { Close(); }

  bool Poll(ChannelEvent& out) override {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    if (shared_->incoming.empty()) return false;
    out = std::move(shared_->incoming.front());
    shared_->incoming.pop_front();
    return true;
  }

  void Send(const std::string& text) override {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    shared_->outgoing.push_back({text, false});
    shared_->wake.notify_all();
  }

  bool SendBinary(std::vector<uint8_t>&& bytes) override {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    if (shared_->stop || !shared_->socket || shared_->queued_binary >= kMaxQueuedBinary) return false;
    shared_->outgoing.push_back({std::string(bytes.begin(), bytes.end()), true});
    ++shared_->queued_binary;
    shared_->wake.notify_all();
    return true;
  }

  void Close() override {
    std::shared_ptr<WebSocket> socket;
    {
      std::lock_guard<std::mutex> lock(shared_->mutex);
      shared_->stop = true;
      socket = shared_->socket;
    }
    shared_->wake.notify_all();
    if (socket) socket->close();
  }

 private:
  std::shared_ptr<Shared> shared_;
};

}  // namespace

std::unique_ptr<LobbyChannel> OpenLobbyLink(const std::string& url) { return std::make_unique<LobbyLink>(url); }

}  // namespace ssf2t::online
