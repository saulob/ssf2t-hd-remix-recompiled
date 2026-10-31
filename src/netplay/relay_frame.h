#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "netplay/udp_transport.h"

namespace ssf2t::netplay {

// One gameplay datagram carried over the online service's WebSocket as a binary message:
// "SR", version, type, token (u32), sequence (u32), payload length (u16), payload; big-endian. The service routes it by
// the sender's own match, so it carries no address.
inline constexpr uint8_t kRelayVersion = 1;
inline constexpr uint8_t kRelayData = 1;
inline constexpr size_t kRelayHeaderBytes = 14;
// The largest datagram the game sends is a 1412-byte lobby datagram; rollback packets are at most 256 bytes.
inline constexpr size_t kRelayMaxPayload = 1472;

// Where the fight session and the lobby see a relayed peer. Loopback, so a datagram that misses the relay is harmless.
inline constexpr Endpoint kRelayPeer{0x7F000002, 1};

struct RelayFrame {
  uint32_t token = 0;
  uint32_t seq = 0;
  std::vector<uint8_t> payload;
};

enum class RelayFrameError { kNone, kTruncated, kBadMagic, kBadVersion, kBadType, kOversized, kBadLength };

const char* RelayFrameErrorName(RelayFrameError error);

// Empty for a payload that is empty or over kRelayMaxPayload.
std::vector<uint8_t> EncodeRelayFrame(uint32_t token, uint32_t seq, const uint8_t* payload, size_t size);
std::optional<RelayFrame> DecodeRelayFrame(const uint8_t* data, size_t size, RelayFrameError* error = nullptr);

}  // namespace ssf2t::netplay
