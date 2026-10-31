// RFC 6455 pieces of the sockets WebSocket client that need no socket, so every platform's tests check them.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ssf2t::online::ws {

std::string Base64(const uint8_t* data, size_t size);
std::array<uint8_t, 20> Sha1(const uint8_t* data, size_t size);

// The Sec-WebSocket-Accept a server must answer `key` with.
std::string AcceptFor(const std::string& key);

// One final client frame: clients always mask.
std::vector<uint8_t> ClientFrame(uint8_t opcode, const uint8_t* payload, size_t size, const uint8_t mask[4]);

}  // namespace ssf2t::online::ws
