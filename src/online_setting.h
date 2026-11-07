#pragma once

#include <cstdlib>

namespace ssf2t {

inline constexpr char kOnlineEnabledSetting[] = "ssf2t_online_enabled";
inline constexpr bool kDefaultOnlineEnabled = false;
inline constexpr char kOnlineEnabledLabel[] = "Online play";
inline constexpr char kOnlineEnabledHelp[] =
    "Lets the game's own Online Match menu create and join matches. Players meet through the online service; the "
    "fight itself runs directly between your PCs.";

inline constexpr char kOnlineNameSetting[] = "ssf2t_online_name";
inline constexpr char kDefaultOnlineNameValue[] = "Player";
inline constexpr char kOnlineNameLabel[] = "Online name";
inline constexpr char kOnlineNameHelp[] = "What other players see: up to 16 letters, digits, - or _.";

inline constexpr char kOnlineLobbySetting[] = "ssf2t_online_lobby_visible";
inline constexpr bool kDefaultOnlineLobby = false;
inline constexpr char kOnlineLobbyLabel[] = "Show me in Lobby";
inline constexpr char kOnlineLobbyOffHelp[] =
    "Turn on to let other players of this game see your Online name in the Lobby and send you a friend request.";
inline constexpr char kOnlineLobbyOnHelp[] =
    "Other players of this game can see your Online name, status, platform and game version in the Lobby and send you "
    "a friend request. Nothing else about you is shown.";

inline constexpr char kOnlinePingSetting[] = "ssf2t_online_ping_display";
inline constexpr bool kDefaultOnlinePing = false;
inline constexpr char kOnlinePingLabel[] = "Ping display";
inline constexpr char kOnlinePingHelp[] =
    "Shows the round trip to the other player in game. F3 switches it while playing.";

// Infinite Time changes the simulation both players run, so it cannot be used while Online play is on.
inline constexpr char kInfiniteTimeOnlineNotice[] = "Infinite Time is disabled while Online play is enabled.";
constexpr bool InfiniteTimeActive(bool setting, bool online_enabled) { return setting && !online_enabled; }
constexpr bool InfiniteTimeToggleAllowed(bool online_enabled) { return !online_enabled; }

// Developer only: another online service, for example the local one (server/ssf2t-lobby/local/server.js).
inline constexpr char kOnlineServiceEnv[] = "SSF2T_LOBBY";
// Developer only: STUN servers ("host:port,host:port" or "off") and UPnP ("0" or "1") of the Connection test.
inline constexpr char kStunServersEnv[] = "SSF2T_STUN";
inline constexpr char kUpnpEnv[] = "SSF2T_UPNP";
// Developer only: shows the Netplay Test and Netplay Determinism Test controls in the launcher.
inline constexpr char kDeveloperControlsEnv[] = "SSF2T_DEVELOPER";

inline bool DeveloperControlsEnabled() {
  const char* value = std::getenv(kDeveloperControlsEnv);
  return value && *value && *value != '0';
}

}  // namespace ssf2t
