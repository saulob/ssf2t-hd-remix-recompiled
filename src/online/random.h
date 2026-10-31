#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ssf2t::online {

// False only when the system generator fails.
bool RandomBytes(void* out, size_t size);
std::string RandomHex(size_t bytes);

}  // namespace ssf2t::online
