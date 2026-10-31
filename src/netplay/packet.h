#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "netplay/frame.h"

namespace ssf2t::netplay {

inline constexpr uint32_t kPacketMagic = 0x53465254;  // "SFRT"
inline constexpr uint8_t kProtocolVersion = 1;
// A peer without the frame-0 setup agreement would leave the other waiting at frame 0.
inline constexpr uint16_t kNetplayBuild = 3;
inline constexpr size_t kHeaderSize = 16;
inline constexpr size_t kMaxInputsPerPacket = 32;
inline constexpr size_t kMaxPacketSize = 256;
inline constexpr size_t kStateRegions = 7;
inline constexpr size_t kMaxBlockHashesPerPacket = 48;
inline constexpr size_t kMaxStateBytesPerPacket = 128;
inline constexpr size_t kMaxSetupWords = 16;

enum class PacketType : uint8_t {
  kHello = 1,
  kHelloAck = 2,
  kInput = 3,
  kChecksum = 4,
  kBye = 5,
  kReady = 6,
  kStateDigest = 7,
  kStateBlocks = 8,
  kStateBytes = 9,
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

// `fight` is the sender's count of fights begun this session: inputs and checksums of any other fight are dropped.
struct Header {
  uint32_t session = 0;
  uint16_t seq = 0;
  uint16_t ack = 0;
  uint16_t fight = 0;
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

// The fight-ready barrier: each side reports its own local readiness until both are ready. `start_frame` is set
// only by the host, once it has seen both sides ready, and repeated so a joiner that missed it still gets it.
struct Ready {
  bool ready = false;
  bool has_start_frame = false;
  Frame start_frame = 0;
};

// The frame-0 state of fight `fight` (the count of fights this session began before it), one hash per region, and
// the setup words both peers must share. `round` is 1 once the joiner took the host's state.
struct StateDigest {
  uint32_t fight = 0;
  std::array<uint32_t, kStateRegions> regions{};
  std::vector<uint32_t> setup;
  uint8_t round = 0;
};

// Hashes of consecutive fixed-size blocks of the frame-0 state, sent only once the digests disagree.
struct StateBlocks {
  uint32_t fight = 0;
  uint16_t first = 0;
  std::vector<uint32_t> hashes;
  uint8_t round = 0;
};

// Raw frame-0 state bytes at `offset`, for the blocks whose hashes disagree.
struct StateBytes {
  uint32_t fight = 0;
  uint32_t offset = 0;
  std::vector<uint8_t> bytes;
  uint8_t round = 0;
};

using Body = std::variant<Hello, HelloAck, Input, Checksum, Bye, Ready, StateDigest, StateBlocks, StateBytes>;

struct Packet {
  Header header;
  Body body;
};

PacketType TypeOf(const Body& body);

// Returns the encoded size, or 0 when the body does not fit `capacity` or is invalid (too many inputs, hashes or
// bytes).
size_t Encode(const Header& header, const Body& body, uint8_t* out, size_t capacity);

// Strict: wrong magic, version, type, length or counts reject the datagram.
std::optional<Packet> Decode(const uint8_t* data, size_t size);

}  // namespace ssf2t::netplay
