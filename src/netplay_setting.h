#pragma once

#include <cstdint>

namespace ssf2t {

inline constexpr char kNetplayDeterminismSetting[] = "ssf2t_netplay_determinism_test";
inline constexpr bool kDefaultNetplayDeterminism = false;
inline constexpr char kNetplayDeterminismLabel[] = "Netplay Determinism Test";
inline constexpr char kNetplayDeterminismHelp[] =
    "Developer: runs rollback save/load/resimulation checks during Local Versus matches.";

// Developer: two instances of the game play one Local Versus fight over UDP. Off leaves gameplay untouched; Host
// waits for one peer on a fixed local port; Join connects to Peer Address on the host's fixed port.
inline constexpr char kNetplayRoleSetting[] = "ssf2t_netplay_role";
inline constexpr char kDefaultNetplayRole[] = "off";
inline constexpr char kNetplayRoleLabel[] = "Netplay Test";
inline constexpr char kNetplayRoleHelp[] =
    "Developer: plays one Local Versus fight against another instance of the game over UDP. Host waits for a "
    "peer; Join connects to Peer Address. Both instances must be the same game version.";
inline constexpr char kNetplayRoleOff[] = "off";
inline constexpr char kNetplayRoleHost[] = "host";
inline constexpr char kNetplayRoleJoin[] = "join";

inline constexpr char kNetplayPeerAddressSetting[] = "ssf2t_netplay_peer_address";
inline constexpr char kDefaultNetplayPeerAddress[] = "127.0.0.1";
inline constexpr char kNetplayPeerAddressLabel[] = "Peer Address";
inline constexpr char kNetplayPeerAddressHelp[] = "IPv4 address of the Host instance. Shown only when Join is selected.";

// Fixed developer ports so two instances on the same PC can both bind without any manual configuration.
inline constexpr uint16_t kNetplayHostPort = 7001;
inline constexpr uint16_t kNetplayJoinPort = 7002;

}  // namespace ssf2t
