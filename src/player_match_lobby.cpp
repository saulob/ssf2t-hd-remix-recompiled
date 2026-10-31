#include "player_match_lobby.h"

#include <cstdio>

#include "guest_memory.h"

namespace ssf2t {

LobbyTable ReadLobbyTable(const uint8_t* table) {
  LobbyTable t;
  t.public_slots = LoadBE32(table, 80);
  t.private_slots = LoadBE32(table, 84);
  t.public_used = LoadBE32(table, 88);
  t.private_used = LoadBE32(table, 92);
  t.players = LoadBE32(table, 904);
  return t;
}

const char* LobbyPacketName(uint8_t type) {
  switch (type) {
    case 0: return "player_ready";
    case 1: return "player_unready";
    case 6: return "player_set";
    case 7: return "player_change";
    case 8: return "player_list";
    case 9: return "host_migrated";
    case 10: return "player_value";
    case 12: return "arbitration";
    case 14: return "player_update";
    default: return type <= 14 ? "game" : "unknown";
  }
}

const char* OnlineOpName(uint32_t op) {
  static constexpr const char* kNames[] = {
      "none", "none", "CREATE", "ANSWER_JOINER_AND_PROPAGATE_HIS_JOIN", "JOIN_LOCAL", "JOIN_REMOTE",
      "WAIT_FOR_PLAYER_LIST", "VERIFY_FULL_CONNECTIVITY_ON_JOIN", "VERIFY_FULL_CONNECTIVITY", "START", "END",
      "INVITE", "LEAVE_LOCAL", "LEAVE_REMOTE", "DELETE", "DISCONNECT", "REMOVE_ALL_PLAYERS", "START_LEAVE_LOBBY",
      "CONTINUE_LEAVE_LOBBY", "COMPLETE_LEAVE_LOBBY", "READ_STATS", "READ_FRIENDS", "READ_ACHIEVEMENTS",
      "WRITE_ACHIEVEMENTS", "SEARCH", "QOS_PROBE_LOBBIES", "QOS_PROBE_INVITE", "HOST_START_ARBIT",
      "REGISTER_ARBIT", "HOST_SEND_ARBITED_LIST", "CLIENT_WAIT_FOR_ARBITED_LIST", "CLIENT_ACK_ARBITED_LIST",
      "RETRIEVE_GAMERPIC_KEY", "RETRIEVE_GAMERPIC", "MIGRATE_HOST", "WAIT_FOR_HOST_MIGRATION", "MIGRATE_HOST_TO_ME"};
  return op < sizeof(kNames) / sizeof(kNames[0]) ? kNames[op] : "unknown";
}

const char* LobbyFatalReason(uint32_t call_site, bool title_update) {
  for (const LobbyFatalSite& site : kLobbyFatalSites) {
    if ((title_update ? site.tu023 : site.base013) == call_site) return site.reason;
  }
  return "other";
}

int RepeatFilter::Next(std::string_view text) {
  if (text == last_) {
    ++dropped_;
    return -1;
  }
  last_ = std::string(text);
  const int dropped = dropped_;
  dropped_ = 0;
  return dropped;
}

bool LineBudget::Allow(double now_s) {
  if (now_s - window_s_ >= 1.0) {
    window_s_ = now_s;
    used_ = 0;
  }
  if (used_ < per_second_) {
    ++used_;
    return true;
  }
  ++dropped_;
  return false;
}

uint32_t LineBudget::TakeDropped() {
  const uint32_t dropped = dropped_;
  dropped_ = 0;
  return dropped;
}

bool SearchLogGate::ShouldLog(uint32_t results, double now_s) {
  ++pending_calls_;
  if (int64_t(results) == last_results_ && now_s - last_s_ < interval_s_) return false;
  last_results_ = results;
  last_s_ = now_s;
  calls_since_log_ = pending_calls_;
  pending_calls_ = 0;
  return true;
}

std::string HexHead(const uint8_t* data, uint32_t size, uint32_t max_bytes) {
  std::string out;
  char byte[3];
  for (uint32_t i = 0; i < size && i < max_bytes; ++i) {
    std::snprintf(byte, sizeof byte, "%02X", data[i]);
    out += byte;
  }
  return out;
}

}  // namespace ssf2t
