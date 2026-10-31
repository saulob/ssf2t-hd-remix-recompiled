#include "online/random.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#else
#include <sys/random.h>
#endif

namespace ssf2t::online {

bool RandomBytes(void* out, size_t size) {
#if defined(_WIN32)
  return BCRYPT_SUCCESS(BCryptGenRandom(nullptr, static_cast<PUCHAR>(out), ULONG(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG));
#else
  auto* p = static_cast<uint8_t*>(out);
  while (size) {
    const ssize_t n = getrandom(p, size, 0);
    if (n <= 0) return false;
    p += n;
    size -= size_t(n);
  }
  return true;
#endif
}

std::string RandomHex(size_t bytes) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string raw(bytes, 0);
  if (!RandomBytes(raw.data(), raw.size())) return {};
  std::string text;
  text.reserve(bytes * 2);
  for (const char c : raw) {
    text += kDigits[(uint8_t(c) >> 4) & 0x0F];
    text += kDigits[uint8_t(c) & 0x0F];
  }
  return text;
}

}  // namespace ssf2t::online
