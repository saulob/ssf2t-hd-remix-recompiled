#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "online/protocol.h"

namespace ssf2t {

// The title sees an Xbox LIVE profile only while Online play is on and the online service has signed this player in.
constexpr bool LiveAvailable(bool online_enabled, bool service_signed_in) { return online_enabled && service_signed_in; }

// XamUserGetSigninState: 2 = signed in to LIVE.
inline constexpr uint32_t kSignedInToLive = 2;
// XamUserGetSigninInfo flags: online-enabled profile, never a guest.
inline constexpr uint32_t kSigninInfoOnlineEnabled = 0x1;

// A stable online-style XUID (0x0009 prefix) derived only from the public friend code, never from the token.
uint64_t VirtualXuid(std::string_view friend_code);

// The guest's active user (SetControllerMask, from the pad that accepted the menu item) owns the LIVE profile.
// Users 1-3 are local guests to the SDK (signed in, not LIVE), so a hard-coded user 0 fails the online gate.
constexpr uint32_t LiveUserIndex(uint32_t active_user) { return active_user < 4 ? active_user : 0; }
constexpr bool LocalUser(uint32_t user) { return user < 4; }

enum class OnlineGate { kOriginal, kLive, kServiceUnavailable };

// RequestOnlineSignIn(argument): 1 guards Online Match (and invites, which this port never produces), 0 Leaderboards.
constexpr OnlineGate DecideOnlineGate(int argument, bool online_enabled, bool live_available) {
  if (argument != 1 || !online_enabled) return OnlineGate::kOriginal;
  return live_available ? OnlineGate::kLive : OnlineGate::kServiceUnavailable;
}

// The guards read `if (RequestOnlineSignIn(1)) { return true; }`; every refusal raises its own popup and returns 0.
constexpr bool SignInAllowed(uint32_t result) { return result != 0; }

// SetError code of the guest's CONNECTION REQUIRED popup (sign-in state not LIVE).
inline constexpr uint32_t kConnectionRequiredError = 14;
// SetError codes the online match ends with: NETWORK ERROR "Connection lost.", PLAYER LEFT, and no connection.
inline constexpr uint32_t kNetErrorCode = 1;
inline constexpr uint32_t kPlayerLeftErrorCode = 2;
inline constexpr uint32_t kNoConnectionErrorCode = 3;

inline constexpr char kOnlineUnavailableTitle[] = "ONLINE UNAVAILABLE";
inline constexpr char kOnlineUnavailableMessage[] = "Unable to connect to the online service.";

// Online Match is one against one; the lobby sizes above two (Quarter Match) are not offered.
inline constexpr uint32_t kPlayerMatchSlots = 2;

// The game's create-match settings block, ten big-endian words.
struct CreateMatchSettings {
  uint32_t rule = 0;
  uint32_t rounds = 0;
  uint32_t speed = 0;
  uint32_t players = 0;
  uint32_t private_slots = 0;
  uint32_t voice = 0;
  uint32_t timer = 0;
  uint32_t tournament = 0;
  uint32_t akuma = 0;
  uint32_t handicap = 0;
};
inline constexpr uint32_t kCreateMatchSettingsBytes = 40;
inline constexpr uint32_t kCreateMatchPlayersOffset = 12;
// Slots only an invite can fill; in a two-player match one leaves Find Match nothing to join.
inline constexpr uint32_t kCreateMatchPrivateSlotsOffset = 16;
inline constexpr uint32_t kCreateMatchTournamentOffset = 28;

CreateMatchSettings ReadCreateMatchSettings(const uint8_t* block);

// The settings both players must share; they travel in the advertisement.
online::PlayerMatchRules PlayerMatchRulesFrom(const CreateMatchSettings& settings);

// XSESSION_INFO: XNKID (8), XNADDR (36), XNKEY (16).
inline constexpr uint32_t kSessionInfoBytes = 60;
inline constexpr uint32_t kSearchHeaderBytes = 8;
inline constexpr uint32_t kSearchResultBytes = 92;
// What the game's XSessionSearchEx wrapper requires per requested result.
inline constexpr uint32_t kSearchBytesPerResult = 1326;

// The 8-byte session id from the service's 16 hex digits; zeros when malformed.
std::vector<uint8_t> SessionIdBytes(std::string_view session);
std::string SessionIdText(const uint8_t* bytes);

// Writes an XSESSION_SEARCHRESULT_HEADER and its results at guest address `header_address` (host view `header`),
// at most `max_results` of `ads`, all two-player sessions with one open public slot. Returns the results written;
// nothing is written when `capacity` cannot hold them.
uint32_t WriteSearchResults(uint8_t* header, uint32_t header_address, uint32_t capacity, uint32_t max_results,
                            const std::vector<online::PlayerMatchAd>& ads);

// An XSESSION_INFO with only the session id; the host address is written separately.
void WriteSessionInfo(uint8_t* info, std::string_view session);

// XN_LIVE_INVITE_ACCEPTED; its parameter is the user index that accepted, which the game makes the active one.
inline constexpr uint32_t kInviteAcceptedNotification = 0x02000002;
// XINVITE_INFO: invitee XUID, inviter XUID, title id, the host's XSESSION_INFO, fFromGameInvite.
inline constexpr uint32_t kInviteInfoBytes = 84;
inline constexpr uint32_t kSsf2tTitleId = 0x584108A4;

// The accepted invite the game reads after XN_LIVE_INVITE_ACCEPTED: the inviter's session and address, exactly what a
// search result for that session carries.
void WriteInviteInfo(uint8_t* info, uint64_t invitee, uint64_t inviter, std::string_view session);

}  // namespace ssf2t
