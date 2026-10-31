#pragma once

#include <cstdint>

#include "netplay/lobby_mux.h"

namespace ssf2t {

// Pumps the Netplay Test connection/ready-barrier traffic for whichever guest is active. Call once per presented
// frame, independent of game state: the fight-step hook only runs during an active fight, but the handshake and
// ready exchange must also progress while still in menus, and a fight's end is only seen here. A no-op when Netplay
// Test is Off.
void PumpNetplayNetwork(const uint8_t* base);

// The running guest's Player Match lobby datagrams, carried on its gameplay socket. False without an active match.
bool SendPlayerMatchLobby(const netplay::LobbyDatagram& datagram);
bool TakePlayerMatchLobby(netplay::LobbyDatagram& out);

}  // namespace ssf2t
