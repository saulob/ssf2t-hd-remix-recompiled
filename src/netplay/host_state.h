#pragma once

#include <cstdint>

namespace ssf2t::netplay {

// State the port keeps outside the guest but that the guest's replayed frames read and write (the Evil Ryu cell, the
// move trackers). It rides at the end of every saved simulation blob, so a rollback restores it with the guest and both
// peers replay the same frames to the same result. The frame-0 agreement never looks at it.
struct HostStateCodec {
  uint32_t bytes = 0;
  void (*save)(uint8_t* out) = nullptr;
  void (*load)(const uint8_t* in) = nullptr;
};

inline HostStateCodec g_host_state;

inline void SetHostStateCodec(const HostStateCodec& codec) { g_host_state = codec; }
inline uint32_t HostStateBytes() { return g_host_state.save && g_host_state.load ? g_host_state.bytes : 0; }
inline void SaveHostState(uint8_t* out) {
  if (HostStateBytes()) g_host_state.save(out);
}
inline void LoadHostState(const uint8_t* in) {
  if (HostStateBytes()) g_host_state.load(in);
}

}  // namespace ssf2t::netplay
