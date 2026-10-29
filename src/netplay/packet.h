#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "netplay/frame.h"

namespace ssf2t::netplay {

inline constexpr uint32_t kPacketMagic = 0x53465254;  // "SFRT"
inline constexpr uint8_t kProtocolVersion = 1;
inline constexpr uint16_t kNetplayBuild = 1;
inline constexpr size_t kHeaderSize = 16;
inline constexpr size_t kMaxInputsPerPacket = 32;
inline constexpr size_t kMaxPacketSize = 256;

enum class PacketType : uint8_t {
  kHello = 1,
  kHelloAck = 2,
  kInput = 3,
  kChecksum = 4,
  kBye = 5,
};

enum class GuestId : uint8_t {
  kBase013 = 1,
  kTitleUpdate023 = 2,
};

enum class RejectReason : uint8_t {
  kNone = 0,
  kGuestMismatch = 1,
  kBuildMismatch = 2,
  kBusy = 3,
};

enum class ByeReason : uint8_t {
  kQuit = 0,
  kDesync = 1,
  kError = 2,
};

struct Header {
  uint32_t session = 0;
  uint16_t seq = 0;
  uint16_t ack = 0;
};

struct Hello {
  GuestId guest = GuestId::kBase013;
  uint16_t build = kNetplayBuild;
  uint32_t nonce = 0;
};

struct HelloAck {
  RejectReason reject = RejectReason::kNone;
  uint8_t joiner_slot = 1;
  uint8_t input_delay = 1;
  uint32_t nonce = 0;
  Frame start_frame = 0;
};

// Inputs [first, first + count) of the sender; `next_wanted` is the first of the receiver's inputs the sender lacks.
struct Input {
  Frame next_wanted = 0;
  Frame first = 0;
  std::vector<uint16_t> words;
};

struct Checksum {
  Frame frame = 0;
  uint32_t value = 0;
};

struct Bye {
  ByeReason reason = ByeReason::kQuit;
};

using Body = std::variant<Hello, HelloAck, Input, Checksum, Bye>;

struct Packet {
  Header header;
  Body body;
};

PacketType TypeOf(const Body& body);

// Returns the encoded size, or 0 when the body does not fit `capacity` or is invalid (too many inputs).
size_t Encode(const Header& header, const Body& body, uint8_t* out, size_t capacity);

// Strict: wrong magic, version, type, length or counts reject the datagram.
std::optional<Packet> Decode(const uint8_t* data, size_t size);

}  // namespace ssf2t::netplay
