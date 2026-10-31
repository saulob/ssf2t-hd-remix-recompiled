#include "player_match.h"

#include <algorithm>
#include <cstring>

#include "guest_memory.h"
#include "player_match_net.h"

namespace ssf2t {
namespace {

constexpr uint64_t kOnlineXuidPrefix = 0x0009000000000000ull;
constexpr uint64_t kXuidBits = 0x0000FFFFFFFFFFFFull;
constexpr uint32_t kSessionIdBytes = 8;
constexpr uint32_t kXnAddrOffset = 8;

uint64_t Fnv1a64(std::string_view text) {
  uint64_t hash = 0xCBF29CE484222325ull;
  for (const char c : text) {
    hash ^= static_cast<unsigned char>(c);
    hash *= 0x100000001B3ull;
  }
  return hash;
}

int HexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

void Put32(uint8_t* at, uint32_t value) { StoreBE32(at, 0, value); }

}  // namespace

uint64_t VirtualXuid(std::string_view friend_code) {
  const uint64_t bits = Fnv1a64(std::string("ssf2t-xuid:") + std::string(friend_code)) & kXuidBits;
  return kOnlineXuidPrefix | (bits ? bits : 1);
}

CreateMatchSettings ReadCreateMatchSettings(const uint8_t* block) {
  CreateMatchSettings s;
  uint32_t* fields[] = {&s.rule, &s.rounds, &s.speed, &s.players, &s.private_slots,
                        &s.voice, &s.timer, &s.tournament, &s.akuma, &s.handicap};
  for (uint32_t i = 0; i < 10; ++i) *fields[i] = LoadBE32(block, i * 4);
  return s;
}

online::PlayerMatchRules PlayerMatchRulesFrom(const CreateMatchSettings& s) {
  const auto small = [](uint32_t value) { return int(std::min<uint32_t>(value, 255)); };
  return {{"rule", small(s.rule)},   {"rounds", small(s.rounds)}, {"speed", small(s.speed)},
          {"timer", small(s.timer)}, {"akuma", small(s.akuma)},   {"handicap", small(s.handicap)}};
}

std::vector<uint8_t> SessionIdBytes(std::string_view session) {
  std::vector<uint8_t> bytes(kSessionIdBytes, 0);
  if (session.size() != kSessionIdBytes * 2) return bytes;
  for (uint32_t i = 0; i < kSessionIdBytes; ++i) {
    const int hi = HexDigit(session[i * 2]);
    const int lo = HexDigit(session[i * 2 + 1]);
    if (hi < 0 || lo < 0) return std::vector<uint8_t>(kSessionIdBytes, 0);
    bytes[i] = uint8_t(hi << 4 | lo);
  }
  return bytes;
}

std::string SessionIdText(const uint8_t* bytes) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string text;
  for (uint32_t i = 0; i < kSessionIdBytes; ++i) {
    text += kHex[bytes[i] >> 4];
    text += kHex[bytes[i] & 0xF];
  }
  return text;
}

void WriteInviteInfo(uint8_t* info, uint64_t invitee, uint64_t inviter, std::string_view session) {
  std::memset(info, 0, kInviteInfoBytes);
  StoreBE64(info, 0, invitee);
  StoreBE64(info, 8, inviter);
  StoreBE32(info, 16, kSsf2tTitleId);
  WriteSessionInfo(info + 20, session);
  WriteXnAddr(info + 20 + kXnAddrOffset, inviter);
  StoreBE32(info, 80, 1);
}

void WriteSessionInfo(uint8_t* info, std::string_view session) {
  std::memset(info, 0, kSessionInfoBytes);
  const std::vector<uint8_t> id = SessionIdBytes(session);
  std::memcpy(info, id.data(), id.size());
}

uint32_t WriteSearchResults(uint8_t* header, uint32_t header_address, uint32_t capacity, uint32_t max_results,
                            const std::vector<online::PlayerMatchAd>& ads) {
  const uint32_t count = std::min<uint32_t>(max_results, uint32_t(ads.size()));
  if (capacity < kSearchHeaderBytes + count * kSearchResultBytes) return 0;
  Put32(header, count);
  Put32(header + 4, count ? header_address + kSearchHeaderBytes : 0);
  for (uint32_t i = 0; i < count; ++i) {
    uint8_t* result = header + kSearchHeaderBytes + i * kSearchResultBytes;
    std::memset(result, 0, kSearchResultBytes);
    WriteSessionInfo(result, ads[i].session);
    WriteXnAddr(result + kXnAddrOffset, VirtualXuid(ads[i].player));
    Put32(result + kSessionInfoBytes + 0, 1);   // open public slots
    Put32(result + kSessionInfoBytes + 8, 1);   // filled public slots: the host
  }
  return count;
}

}  // namespace ssf2t
