#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include "netplay/udp_transport.h"

namespace ssf2t::netplay {

// One datagram of the game's own Player Match lobby traffic, between two guest VDP ports.
struct LobbyDatagram {
  uint16_t from_port = 0;
  uint16_t to_port = 0;
  std::vector<uint8_t> data;
};

inline constexpr size_t kLobbyHeaderBytes = 12;
inline constexpr size_t kMaxLobbyPayload = 1400;

std::vector<uint8_t> EncodeLobby(uint32_t tag, const LobbyDatagram& datagram);
// Nothing for anything that is not a lobby datagram.
std::optional<std::pair<uint32_t, LobbyDatagram>> DecodeLobby(const uint8_t* data, size_t size);

inline constexpr size_t kMaxHeldLobby = 32;

// Lobby datagrams the game sends before its match has a transport are held rather than lost, so the game's own resend
// timer is not what delays the lobby once the transport is selected. Holds one match's datagrams at a time.
class LobbyHold {
 public:
  // False when there is no match (`tag` 0) or the hold is full.
  bool Hold(uint32_t tag, const LobbyDatagram& datagram);
  // The held datagrams of `tag`, oldest first; the hold is emptied, and datagrams of any other match are dropped.
  std::vector<LobbyDatagram> Take(uint32_t tag);
  void Clear();
  size_t size() const { return held_.size(); }

 private:
  uint32_t tag_ = 0;
  std::deque<LobbyDatagram> held_;
};

// Carries lobby datagrams on the gameplay socket. Gameplay traffic passes through unchanged; lobby datagrams are
// accepted only with the current match's tag, and their sender becomes the reply address. Thread-safe: the guest's
// network thread and the presenting thread may both use it.
class LobbyMux final : public Transport {
 public:
  explicit LobbyMux(Transport& inner) : inner_(inner) {}

  void SetRoute(uint32_t tag, const Endpoint& peer);
  bool SendLobby(const LobbyDatagram& datagram);
  bool TakeLobby(LobbyDatagram& out);

  bool Send(const Endpoint& to, const uint8_t* data, size_t size) override;
  size_t Receive(uint8_t* buffer, size_t capacity, Endpoint& from) override;

  uint32_t lobby_sent() const;
  uint32_t lobby_received() const;
  uint32_t lobby_rejected() const;

 private:
  void DrainLocked();

  Transport& inner_;
  mutable std::mutex mutex_;
  uint32_t tag_ = 0;
  Endpoint peer_{};
  std::deque<LobbyDatagram> lobby_;
  std::deque<std::pair<Endpoint, std::vector<uint8_t>>> game_;
  uint32_t sent_ = 0;
  uint32_t received_ = 0;
  uint32_t rejected_ = 0;
};

}  // namespace ssf2t::netplay
