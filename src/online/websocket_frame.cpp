#include "online/websocket_frame.h"

namespace ssf2t::online::ws {

std::string Base64(const uint8_t* data, size_t size) {
  static constexpr char kDigits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  for (size_t i = 0; i < size; i += 3) {
    const uint32_t n = uint32_t(data[i]) << 16 | (i + 1 < size ? uint32_t(data[i + 1]) << 8 : 0) |
                       (i + 2 < size ? data[i + 2] : 0);
    out += kDigits[n >> 18 & 63];
    out += kDigits[n >> 12 & 63];
    out += i + 1 < size ? kDigits[n >> 6 & 63] : '=';
    out += i + 2 < size ? kDigits[n & 63] : '=';
  }
  return out;
}

std::array<uint8_t, 20> Sha1(const uint8_t* data, size_t size) {
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  std::vector<uint8_t> message(data, data + size);
  message.push_back(0x80);
  while (message.size() % 64 != 56) message.push_back(0);
  const uint64_t bits = uint64_t(size) * 8;
  for (int i = 7; i >= 0; --i) message.push_back(uint8_t(bits >> (i * 8)));
  const auto rotl = [](uint32_t x, int n) { return x << n | x >> (32 - n); };
  for (size_t block = 0; block < message.size(); block += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
      const uint8_t* p = &message[block + size_t(i) * 4];
      w[i] = uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
    }
    for (int i = 16; i < 80; ++i) w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; ++i) {
      uint32_t f, k;
      if (i < 20) f = (b & c) | (~b & d), k = 0x5A827999;
      else if (i < 40) f = b ^ c ^ d, k = 0x6ED9EBA1;
      else if (i < 60) f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDC;
      else f = b ^ c ^ d, k = 0xCA62C1D6;
      const uint32_t t = rotl(a, 5) + f + e + k + w[i];
      e = d, d = c, c = rotl(b, 30), b = a, a = t;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e;
  }
  std::array<uint8_t, 20> out;
  for (int i = 0; i < 20; ++i) out[size_t(i)] = uint8_t(h[i / 4] >> (24 - 8 * (i % 4)));
  return out;
}

std::string AcceptFor(const std::string& key) {
  const std::string source = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  const auto digest = Sha1(reinterpret_cast<const uint8_t*>(source.data()), source.size());
  return Base64(digest.data(), digest.size());
}

std::vector<uint8_t> ClientFrame(uint8_t opcode, const uint8_t* payload, size_t size, const uint8_t mask[4]) {
  std::vector<uint8_t> frame;
  frame.push_back(uint8_t(0x80 | opcode));
  if (size < 126) {
    frame.push_back(uint8_t(0x80 | size));
  } else if (size < 0x10000) {
    frame.push_back(0x80 | 126);
    frame.push_back(uint8_t(size >> 8));
    frame.push_back(uint8_t(size));
  } else {
    frame.push_back(0x80 | 127);
    for (int i = 7; i >= 0; --i) frame.push_back(uint8_t(uint64_t(size) >> (8 * i)));
  }
  frame.insert(frame.end(), mask, mask + 4);
  for (size_t i = 0; i < size; ++i) frame.push_back(payload[i] ^ mask[i & 3]);
  return frame;
}

}  // namespace ssf2t::online::ws
