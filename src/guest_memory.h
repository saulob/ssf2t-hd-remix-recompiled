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

// Matches REX_PHYS_HOST_OFFSET: these hosts map the physical heap (0xE0000000 and up) 4 KiB higher.
#if defined(_WIN32) || (defined(__APPLE__) && defined(__aarch64__))
inline constexpr uint32_t kPhysicalHeapHostOffset = 0x1000;
#else
inline constexpr uint32_t kPhysicalHeapHostOffset = 0;
#endif

constexpr uint32_t GuestHostOffset(uint32_t addr) {
  return addr >= 0xE0000000u ? kPhysicalHeapHostOffset : 0;
}

// For guest pointers read at runtime; fixed image addresses are below the physical heap.
inline const uint8_t* GuestHost(const uint8_t* base, uint32_t addr) { return base + addr + GuestHostOffset(addr); }

inline void StoreBE16(uint8_t* base, uint32_t addr, uint16_t v) { StoreBE<uint16_t>(base, addr, v); }
inline void StoreBE32(uint8_t* base, uint32_t addr, uint32_t v) { StoreBE<uint32_t>(base, addr, v); }
inline void StoreBE64(uint8_t* base, uint32_t addr, uint64_t v) { StoreBE<uint64_t>(base, addr, v); }

}  // namespace ssf2t
