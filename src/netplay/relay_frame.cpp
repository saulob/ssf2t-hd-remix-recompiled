#include "netplay/relay_frame.h"

#include <cstring>

namespace ssf2t::netplay {
namespace {

constexpr uint8_t kMagic[2] = {'S', 'R'};

void Put16(uint8_t* at, uint16_t v) {
  at[0] = uint8_t(v >> 8);
  at[1] = uint8_t(v);
}
void Put32(uint8_t* at, uint32_t v) {
  Put16(at, uint16_t(v >> 16));
  Put16(at + 2, uint16_t(v));
}
uint16_t Get16(const uint8_t* at) { return uint16_t(at[0] << 8 | at[1]); }
uint32_t Get32(const uint8_t* at) { return uint32_t(Get16(at)) << 16 | Get16(at + 2); }

}  // namespace

const char* RelayFrameErrorName(RelayFrameError error) {
  switch (error) {
    case RelayFrameError::kNone: return "none";
    case RelayFrameError::kTruncated: return "truncated";
    case RelayFrameError::kBadMagic: return "bad_magic";
    case RelayFrameError::kBadVersion: return "bad_version";
    case RelayFrameError::kBadType: return "bad_type";
    case RelayFrameError::kOversized: return "oversized";
    default: return "bad_length";
  }
}

std::vector<uint8_t> EncodeRelayFrame(uint32_t token, uint32_t seq, const uint8_t* payload, size_t size) {
  if (size == 0 || size > kRelayMaxPayload) return {};
  std::vector<uint8_t> out(kRelayHeaderBytes + size);
  out[0] = kMagic[0];
  out[1] = kMagic[1];
  out[2] = kRelayVersion;
  out[3] = kRelayData;
  Put32(out.data() + 4, token);
  Put32(out.data() + 8, seq);
  Put16(out.data() + 12, uint16_t(size));
  std::memcpy(out.data() + kRelayHeaderBytes, payload, size);
  return out;
}

std::optional<RelayFrame> DecodeRelayFrame(const uint8_t* data, size_t size, RelayFrameError* error) {
  const auto fail = [error](RelayFrameError why) {
    if (error) *error = why;
    return std::nullopt;
  };
  if (size < 4) return fail(RelayFrameError::kTruncated);
  if (data[0] != kMagic[0] || data[1] != kMagic[1]) return fail(RelayFrameError::kBadMagic);
  if (data[2] != kRelayVersion) return fail(RelayFrameError::kBadVersion);
  if (data[3] != kRelayData) return fail(RelayFrameError::kBadType);
  if (size < kRelayHeaderBytes) return fail(RelayFrameError::kTruncated);
  const size_t length = Get16(data + 12);
  if (length > kRelayMaxPayload || size > kRelayHeaderBytes + kRelayMaxPayload) return fail(RelayFrameError::kOversized);
  if (length == 0 || size != kRelayHeaderBytes + length) return fail(RelayFrameError::kBadLength);
  RelayFrame frame;
  frame.token = Get32(data + 4);
  frame.seq = Get32(data + 8);
  frame.payload.assign(data + kRelayHeaderBytes, data + size);
  if (error) *error = RelayFrameError::kNone;
  return frame;
}

}  // namespace ssf2t::netplay
