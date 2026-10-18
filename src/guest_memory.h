#pragma once

#include <bit>
#include <cstdint>
#include <cstring>

namespace ssf2t {

template <typename T>
inline T LoadBE(const uint8_t* base, uint32_t addr) {
  T v;
  std::memcpy(&v, base + addr, sizeof(v));
  return std::byteswap(v);
}

template <typename T>
inline void StoreBE(uint8_t* base, uint32_t addr, T v) {
  v = std::byteswap(v);
  std::memcpy(base + addr, &v, sizeof(v));
}

inline uint16_t LoadBE16(const uint8_t* base, uint32_t addr) { return LoadBE<uint16_t>(base, addr); }
inline uint32_t LoadBE32(const uint8_t* base, uint32_t addr) { return LoadBE<uint32_t>(base, addr); }
inline uint64_t LoadBE64(const uint8_t* base, uint32_t addr) { return LoadBE<uint64_t>(base, addr); }

inline void StoreBE16(uint8_t* base, uint32_t addr, uint16_t v) { StoreBE<uint16_t>(base, addr, v); }
inline void StoreBE32(uint8_t* base, uint32_t addr, uint32_t v) { StoreBE<uint32_t>(base, addr, v); }
inline void StoreBE64(uint8_t* base, uint32_t addr, uint64_t v) { StoreBE<uint64_t>(base, addr, v); }

}  // namespace ssf2t
