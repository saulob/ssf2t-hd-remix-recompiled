#pragma once

#include <array>
#include <cstdint>

namespace ssf2t::character {

inline constexpr uint32_t kActionFrameBytes = 24;
inline constexpr uint32_t kEvilActionBytes = 0x4000;
inline constexpr uint32_t kEvilPackBytes = 0x1800;

inline constexpr uint8_t kShakunetsuAction = 0x4C;
inline constexpr uint8_t kTatsuAction = 0x50;
inline constexpr uint8_t kRagingDemonAction = 0x5E;
inline constexpr uint8_t kUniqueAction = 0x40;
inline constexpr uint8_t kSenpuSub = 8;

// First frame of the raging demon action shown once the victim is caught.
inline constexpr uint32_t kDemonCaughtFrame = 4;

// The two poses only USF2 Evil Ryu has. The guest draws a carrier Ryu pose; the sprite hook swaps the art.
enum class EvilPose : uint8_t { kNone, kBackTurned, kPowerStance };

struct EvilSpritePlacement {
  int16_t x = 0;
  int16_t y = 0;
  uint16_t width = 0;
  uint16_t height = 0;
};

constexpr EvilSpritePlacement SpritePlacement(EvilPose pose) {
  if (pose == EvilPose::kBackTurned) return {-48, -108, 96, 112};
  if (pose == EvilPose::kPowerStance) return {-30, -94, 112, 96};
  return {};
}

// A copy of the guest Ryu action data with the USF2 Evil Ryu changes applied, so ordinary Ryu is never touched.
class EvilActionTable {
 public:
  // Reads the live table at guest address `table` and writes the result to `memory`. False when out of room.
  bool Build(uint8_t* base, uint32_t table, uint32_t memory, uint32_t capacity);

  uint32_t table() const { return table_; }
  uint32_t Find(uint8_t action, uint8_t sub, uint8_t tatsu_strength) const;
  uint32_t Teleport() const { return teleport_; }
  EvilPose PoseAt(uint32_t record) const;
  // The guest frame `record` was copied from, or 0 for an address outside this table's copies.
  uint32_t SourceFrame(uint32_t record) const;
  int skipped() const { return skipped_; }

 private:
  struct Entry {
    uint8_t action = 0;
    uint8_t sub = 0;
    bool strong_tatsu = false;
    uint32_t address = 0;
  };
  struct PoseEntry {
    uint32_t record = 0;
    EvilPose pose = EvilPose::kNone;
  };
  struct SourceRange {
    uint32_t start = 0;
    uint32_t frames = 0;
    uint32_t source = 0;
  };
  friend struct EvilActionWriter;

  uint32_t table_ = 0;
  uint32_t teleport_ = 0;
  int skipped_ = 0;
  int entries_ = 0;
  int poses_ = 0;
  int sources_ = 0;
  std::array<Entry, 40> entry_{};
  std::array<PoseEntry, 32> pose_{};
  std::array<SourceRange, 128> source_{};
};

// The guest's action selection writes these three fighter fields together; fighters are in the physical heap.
void SetFighterAnimation(uint8_t* base, uint32_t fighter, uint32_t record);

// False for an action id past the table header, such as the raging demon id Ryu's table does not have.
bool GuestHasAction(const uint8_t* base, uint32_t table, uint8_t action);

// Copies the fighter box pack at `pack` to `memory` and applies the Evil Ryu box changes. A patch whose original bytes
// differ from Ryu's USF2 values is skipped, because the HD ruleset renumbers some records. Returns the patches applied.
int BuildEvilPack(uint8_t* base, uint32_t pack, uint32_t memory);

inline constexpr uint8_t kAttackSenpu = 74;
inline constexpr uint8_t kAttackTatsuStart = 75;

// Per ruleset table storage, shared by both sides: the two fighters may use the same table.
std::array<EvilActionTable, 2>& GlobalEvilActionTables();
EvilPose EvilPoseAtRecord(uint32_t record);

// The guest keys HD sprites by a frame's byte offset inside its character's action data, so a frame in Evil Ryu's copy
// has no art of its own; it draws as the guest frame it came from. 0 for a record that is not in a copy.
uint32_t EvilSourceFrame(uint32_t record);

}  // namespace ssf2t::character
