#pragma once

#include <filesystem>
#include <string>

namespace ssf2t::online {

inline constexpr char kIdentityFileName[] = "online_identity.ini";
inline constexpr char kDefaultOnlineName[] = "Player";
inline constexpr size_t kMaxOnlineNameChars = 16;

// `token` is private and only ever sent to the lobby service; `player_id` (XXXX-XXXX) is derived from it and is the
// public name of this installation.
struct Identity {
  std::string token;
  std::string player_id;
  bool ephemeral = false;
  std::string error;
  bool ok() const { return token.size() == 64 && player_id.size() == 9; }
};

// Must match playerIdFor in server/ssf2t-lobby/src/protocol.js.
std::string DerivePlayerId(const std::string& token);

// Reads <user_dir>/online_identity.ini, creating it on first use. An existing file is never overwritten.
Identity LoadOrCreateIdentity(const std::filesystem::path& user_dir);

// Reads <user_dir>/online_identity.ini without creating it; not ok() when there is none.
Identity LoadExistingIdentity(const std::filesystem::path& user_dir);

// Random, never written: the identity of a second instance on the same PC.
Identity MakeEphemeralIdentity();

// Letters, digits, '-' and '_' only, up to 16 characters.
constexpr bool OnlineNameChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}
std::string CleanOnlineName(const std::string& wanted);

}  // namespace ssf2t::online
