#pragma once

#include <cstdint>
#include <cstring>

#include <rex/hook.h>
#include <rex/system/kernel_state.h>

#include "guest_memory.h"

namespace ssf2t::character {

inline constexpr uint32_t kArgbTextureFormat = 0x16;
inline constexpr uint32_t kArgbTextureDummyName = 0x82000A1C;
inline constexpr uint32_t kArgbTextureWrapperSize = 84;
inline constexpr uint32_t kArgbTextureObject = 4;
inline constexpr uint32_t kArgbTextureObjectBytes = 20;
inline constexpr uint32_t kArgbTextureFileFlags = 104;
inline constexpr uint32_t kArgbStreamHeader = 5;
inline constexpr uint32_t kArgbStreamRetile = 1;

inline bool PlausibleGuestAddress(uint32_t address) { return address >= 0x10000 && address < 0xFFFF0000u; }

// Built by the guest's own texture constructor and file reader from the stream layout of an ARGB texture file, so the
// result is the same kind of texture the screen sets load. `pixels` are A,R,G,B, top row first. 0 on failure.
template <typename H>
uint32_t BuildArgbTexture(const uint8_t* pixels, uint32_t width, uint32_t height, uint8_t file_flags, PPCContext& ctx,
                          uint8_t* base) {
  const uint32_t bytes = width * height * 4;
  const PPCContext saved = ctx;
  ctx.r3.u64 = width;
  ctx.r4.u64 = height;
  ctx.r5.u64 = kArgbTextureFormat;
  ctx.r6.u64 = 0;
  ctx.r7.u64 = 0;
  ctx.r8.u64 = kArgbTextureDummyName;
  H::CreateTexture(ctx, base);
  const uint32_t wrapper = ctx.r3.u32;
  ctx = saved;
  if (!PlausibleGuestAddress(wrapper) || LoadBE32(GuestHost(base, wrapper), 0) != kArgbTextureWrapperSize) return 0;
  const uint32_t object = wrapper + kArgbTextureObject;
  if (LoadBE32(GuestHost(base, object + kArgbTextureObjectBytes), 0) != bytes) return 0;
  const uint32_t stream = REX_KERNEL_MEMORY()->SystemHeapAlloc(kArgbStreamHeader + bytes);
  const uint32_t cursor = REX_KERNEL_MEMORY()->SystemHeapAlloc(4);
  if (stream == 0 || cursor == 0) return 0;
  GuestStore8(base, stream, 0);
  GuestStoreBE32(base, stream + 1, kArgbStreamRetile);
  std::memcpy(GuestHost(base, stream + kArgbStreamHeader), pixels, bytes);
  GuestStoreBE32(base, cursor, stream);
  ctx.r3.u64 = object;
  ctx.r4.u64 = cursor;
  H::ReadTexture(ctx, base);
  ctx = saved;
  REX_KERNEL_MEMORY()->SystemHeapFree(stream);
  REX_KERNEL_MEMORY()->SystemHeapFree(cursor);
  GuestStore8(base, wrapper + kArgbTextureFileFlags, file_flags);
  return wrapper;
}

}  // namespace ssf2t::character
