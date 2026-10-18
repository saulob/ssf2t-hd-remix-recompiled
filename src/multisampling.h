#pragma once

#include <cstdint>

namespace ssf2t {

// The launcher's Multisampling choice: native 2x MSAA, or 4x compatibility (hcu_msaa_compatibility).
// Neither can turn off multisampling the game itself asks for.
inline bool Native2xMsaaFor(bool compatibility) {
  return !compatibility;
}

// Host samples per pixel ReXGlue gives a guest render target with guest_samples samples, as both the
// Direct3D 12 and the Vulkan render target caches choose them.
inline uint32_t HostMsaaSamples(uint32_t guest_samples, bool native_2x_msaa, bool host_2x_supported) {
  if (guest_samples == 2 && !(native_2x_msaa && host_2x_supported)) {
    return 4;
  }
  return guest_samples;
}

}  // namespace ssf2t
