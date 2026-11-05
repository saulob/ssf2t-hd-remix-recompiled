#pragma once

#include <array>
#include <cstdint>

#include "character/evil_ryu_actions.h"
#include "character/violent_ken_data.h"

namespace ssf2t::character {

inline constexpr uint32_t kViolentActionBytes = 0x8000;
inline constexpr uint32_t kViolentPackBytes = 0x2000;

// The stance sprite's own placement: USF2 pose 280 centres at (31, 2) and fills 128x80 on the HD grid.
inline constexpr EvilSpritePlacement kViolentStancePlacement = {-33, -78, 128, 80};

// The guest keys a frame's HD art by its byte offset past the first frame of the character's action data.
uint32_t FirstActionFrame(const uint8_t* base, uint32_t table);

// A copy of the guest Ken actions the USF2 data changes, so ordinary Ken is never touched. Patches are checked against
// the guest record first: an action whose bytes differ from USF2 Ken stays as the guest has it.
class ViolentActionTable {
 public:
  bool Build(uint8_t* base, uint32_t table, uint32_t memory, uint32_t capacity, const ViolentKenGameplay& gameplay);

  uint32_t table() const { return table_; }
  uint32_t Find(uint8_t action, uint8_t sub) const;
  uint32_t SourceFrame(uint32_t record) const;
  bool StanceAt(uint32_t record) const;
  int skipped() const { return skipped_; }

 private:
  struct Entry {
    uint8_t action = 0;
    uint8_t sub = 0;
    uint32_t address = 0;
  };
  struct SourceRange {
    uint32_t start = 0;
    uint32_t frames = 0;
    uint32_t source = 0;
  };
  friend struct ViolentActionWriter;

  uint32_t table_ = 0;
  int skipped_ = 0;
  int entries_ = 0;
  int stances_ = 0;
  int sources_ = 0;
  std::array<Entry, 96> entry_{};
  std::array<uint32_t, 16> stance_{};
  std::array<SourceRange, 256> source_{};
};

// Copies the fighter box pack at `pack` to `memory`, moves its attack table past the copy and applies the USF2 attacks.
// Returns the attack records written.
int BuildViolentPack(uint8_t* base, uint32_t pack, uint32_t memory, const ViolentKenGameplay& gameplay);

std::array<ViolentActionTable, 2>& GlobalViolentActionTables();
bool ViolentStanceAtRecord(uint32_t record);
uint32_t ViolentSourceFrame(uint32_t record);

}  // namespace ssf2t::character
