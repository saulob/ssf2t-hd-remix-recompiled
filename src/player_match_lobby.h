#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace ssf2t {

// XGI message of XSessionLeaveLocal / XSessionLeaveRemote: {session, user count, XUIDs or user indices, 0, 0}.
// ReXGlue does not implement it and fails the request, so the game never gets OnlMessage_LOBBY_LEFT.
inline constexpr uint32_t kXgiSessionLeave = 0x000B0013;
inline constexpr uint32_t kErrorIoPending = 997;

// The game's online player table (net manager + kPlayerTableOffset): slot limits and use, then eight 80-byte players
// {slot, ..., XUID at +8, XNADDR at +16, private slot flag at +54}.
inline constexpr uint32_t kPlayerTableOffset = 18256;
inline constexpr uint32_t kCurrentOpOffset = 1832;
inline constexpr uint32_t kPlayerEntryBytes = 80;

struct LobbyTable {
  uint32_t players = 0;
  uint32_t public_slots = 0;
  uint32_t private_slots = 0;
  uint32_t public_used = 0;
  uint32_t private_used = 0;
};

// `table` is the host view of the guest table (big-endian words).
LobbyTable ReadLobbyTable(const uint8_t* table);

const char* LobbyPacketName(uint8_t type);
const char* OnlineOpName(uint32_t op);

// The game's lobby fatal error (code -2000) call sites that matter for a join, by image.
struct LobbyFatalSite {
  uint32_t base013;
  uint32_t tu023;
  const char* reason;
};
inline constexpr LobbyFatalSite kLobbyFatalSites[] = {
    {0x82428EB0, 0x82429610, "player_set_without_remote_players"},
    {0x82428F48, 0x824296A8, "player_change_rejected"},
    {0x82428F5C, 0x824296BC, "player_set_count_mismatch_or_empty_lobby"},
    {0x824284AC, 0x82428C0C, "remote_xnaddr_not_mapped"},
    {0x824281C4, 0x82428924, "authoritative_list_mismatch"},
    {0x824281E4, 0x82428944, "authoritative_list_empty_lobby"},
    {0x824273A4, 0x82427B04, "leave_local_failed"},
    {0x824273F4, 0x82427B54, "leave_remote_failed"},
};
// `call_site` is the address of the `bl` (link register - 4).
const char* LobbyFatalReason(uint32_t call_site, bool title_update);

// Lets a repeated line through once, then reports how many identical lines were dropped when the text changes.
class RepeatFilter {
 public:
  // Returns the number of dropped repeats to report before `text`, or -1 to drop `text` itself.
  int Next(std::string_view text);

 private:
  std::string last_;
  int dropped_ = 0;
};

// At most `per_second` lines in any one-second window; the rest are counted.
class LineBudget {
 public:
  explicit LineBudget(uint32_t per_second) : per_second_(per_second) {}
  bool Allow(double now_s);
  // Lines dropped since the last call.
  uint32_t TakeDropped();

 private:
  uint32_t per_second_;
  double window_s_ = -1e9;
  uint32_t used_ = 0;
  uint32_t dropped_ = 0;
};

// Search telemetry: a line when the result count changes, otherwise at most one summary per interval.
class SearchLogGate {
 public:
  explicit SearchLogGate(double interval_s = 1.0) : interval_s_(interval_s) {}
  bool ShouldLog(uint32_t results, double now_s);
  uint32_t calls_since_log() const { return calls_since_log_; }

 private:
  double interval_s_;
  double last_s_ = -1e9;
  int64_t last_results_ = -1;
  uint32_t calls_since_log_ = 0;
  uint32_t pending_calls_ = 0;
};

std::string HexHead(const uint8_t* data, uint32_t size, uint32_t max_bytes = 16);

// Starts the bounded lobby packet log over for a new session.
void ResetLobbyTelemetry();

}  // namespace ssf2t
