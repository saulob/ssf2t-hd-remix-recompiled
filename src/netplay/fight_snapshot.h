#pragma once

#include <cstdint>
#include <span>
#include <utility>

#include "guest_memory.h"
#include "netplay/guest_netplay.h"
#include "online/fight_telemetry.h"

namespace ssf2t::netplay {

// Read-only view of the running fight for the online telemetry. Offsets are inside a fighter and inside the fight work
// object; the same in both images, which the code check below confirms on each.
inline constexpr uint32_t kFighterOffsets[2] = {0x700, 0xB00};
inline constexpr uint32_t kRoundWinsOffset = 0x39C;
inline constexpr uint32_t kCharacterOffset = 0x39D;
inline constexpr uint32_t kHiddenCharacterOffset = 0x3C1;
inline constexpr uint32_t kWinsToBeatOffset = 0x6DA;
inline constexpr uint32_t kMatchOverFlagOffset = 0x494;

struct GuestMemory {
  const uint8_t* base = nullptr;
  uint8_t U8(uint32_t address) const { return *GuestHost(base, address); }
  uint16_t U16(uint32_t address) const { return LoadBE16(GuestHost(base, address), 0); }
  uint32_t U32(uint32_t address) const { return LoadBE32(GuestHost(base, address), 0); }
};

inline bool PlausibleGuestPointer(uint32_t address) { return address >= 0x10000 && address < 0xFFFF0000u; }

// The character a fighter plays: a set hidden-character flag is Akuma, otherwise the stored id.
template <typename Memory>
int FighterCharacter(const Memory& memory, uint32_t fighter) {
  if (memory.U8(fighter + kHiddenCharacterOffset) != 0) return online::kAkumaCharacter;
  const int id = int(int8_t(memory.U8(fighter + kCharacterOffset)));
  return online::IsCharacterId(id) ? id : -1;
}

// Invalid until both fighters hold a real character.
template <typename G, typename Memory>
online::FightSnapshot ReadFightSnapshot(const Memory& memory) {
  online::FightSnapshot snapshot;
  const uint32_t block = memory.U32(G::kFighterBlockPointer);
  const uint32_t work = memory.U32(G::kFightWork);
  if (!PlausibleGuestPointer(block) || !PlausibleGuestPointer(work)) return snapshot;
  for (int side = 0; side < 2; ++side) {
    const uint32_t fighter = block + kFighterOffsets[side];
    snapshot.character[side] = FighterCharacter(memory, fighter);
    const int wins = int(int8_t(memory.U8(fighter + kRoundWinsOffset)));
    snapshot.wins[side] = wins < 0 ? 0 : wins;
  }
  snapshot.wins_to_beat = int(memory.U16(work + kWinsToBeatOffset) & 0xFF);
  snapshot.valid = online::IsCharacterId(snapshot.character[0]) && online::IsCharacterId(snapshot.character[1]);
  return snapshot;
}

// A D-form instruction (lbz, stb, lhz, lwz) with this opcode and displacement.
inline bool HasDForm(std::span<const uint32_t> words, uint32_t opcode, uint32_t displacement) {
  for (const uint32_t word : words) {
    if ((word >> 26) == opcode && (word & 0xFFFF) == displacement) return true;
  }
  return false;
}

inline constexpr uint32_t kOpLwz = 32;
inline constexpr uint32_t kOpLbz = 34;
inline constexpr uint32_t kOpStb = 38;
inline constexpr uint32_t kOpLhz = 40;
inline constexpr uint32_t kRoundWinWords = 235;
inline constexpr uint32_t kFightInitWords = 80;

template <typename Memory>
bool ReadWordsAt(const Memory& memory, uint32_t address, uint32_t* out, uint32_t count) {
  for (uint32_t i = 0; i < count; ++i) out[i] = memory.U32(address + 4 * i);
  return true;
}

// The analysed code must be the code that is running: the two round-win routines (the one a fighter's round wins and
// the match-over flag are written by) and the fight init that copies the characters.
template <typename G, typename Memory>
bool FightTelemetryCodeMatches(const Memory& memory) {
  uint32_t first[kRoundWinWords], second[kRoundWinWords], init[kFightInitWords];
  ReadWordsAt(memory, G::kRoundWinPlayer1, first, kRoundWinWords);
  ReadWordsAt(memory, G::kRoundWinPlayer2, second, kRoundWinWords);
  ReadWordsAt(memory, G::kFightCharacterInit, init, kFightInitWords);
  const uint32_t p1 = kFighterOffsets[0];
  const uint32_t p2 = kFighterOffsets[1];
  const uint32_t block_low = G::kFighterBlockPointer & 0xFFFF;
  const uint32_t work_low = G::kFightWork & 0xFFFF;
  const bool round_win =
      HasDForm(first, kOpLbz, p1 + kRoundWinsOffset) && HasDForm(first, kOpStb, p1 + kRoundWinsOffset) &&
      HasDForm(first, kOpLhz, kWinsToBeatOffset) && HasDForm(first, kOpStb, kMatchOverFlagOffset) &&
      HasDForm(first, kOpLwz, block_low) && HasDForm(first, kOpLwz, work_low) &&
      HasDForm(second, kOpLbz, p2 + kRoundWinsOffset) && HasDForm(second, kOpStb, p2 + kRoundWinsOffset) &&
      HasDForm(second, kOpLhz, kWinsToBeatOffset) && HasDForm(second, kOpStb, kMatchOverFlagOffset) &&
      HasDForm(second, kOpLwz, block_low) && HasDForm(second, kOpLwz, work_low);
  const bool characters =
      HasDForm(init, kOpLbz, p1 + kCharacterOffset) && HasDForm(init, kOpLbz, p1 + kHiddenCharacterOffset) &&
      HasDForm(init, kOpLbz, p2 + kCharacterOffset) && HasDForm(init, kOpLbz, p2 + kHiddenCharacterOffset) &&
      HasDForm(init, kOpLwz, block_low);
  return round_win && characters;
}

}  // namespace ssf2t::netplay
