#include "character/violent_ken_actions.h"

#include <algorithm>
#include <cstring>

#include "guest_memory.h"

namespace ssf2t::character {
namespace {

constexpr uint16_t kEndFlag = 0x8000;
constexpr int kMaxFrames = 80;
constexpr uint32_t kFlagsOffset = 2;
constexpr uint32_t kHurtOffset = 8;
constexpr uint32_t kAttackOffset = 12;
constexpr uint32_t kPushOffset = 13;
constexpr uint32_t kShapeOffset = 14;
constexpr uint32_t kWeightOffset = 15;

constexpr uint32_t kPackCopyBytes = 0x1200;
constexpr uint32_t kPackAttackOffset = 6;
constexpr uint32_t kPackAfterAttackOffset = 8;
constexpr uint32_t kPackRecordBytes = 16;
constexpr uint32_t kGuestAttackRecords = 74;
constexpr uint32_t kAttackTableRecords = 128;

uint8_t* Host(uint8_t* base, uint32_t address) { return const_cast<uint8_t*>(GuestHost(base, address)); }

uint32_t ActionAddress(const uint8_t* base, uint32_t table, uint8_t action, uint8_t sub) {
  const uint32_t subs = table + LoadBE16(GuestHost(base, table + action), 0);
  return subs + LoadBE16(GuestHost(base, subs + uint32_t(sub) * 2), 0);
}

int FrameCount(const uint8_t* base, uint32_t record) {
  for (int count = 1; count <= kMaxFrames; ++count) {
    if (LoadBE16(GuestHost(base, record + uint32_t(count - 1) * kActionFrameBytes + kFlagsOffset), 0) & kEndFlag) {
      return count;
    }
  }
  return 0;
}

bool PatchMatches(const uint8_t* base, uint32_t frame, const PackagedPatch& patch) {
  switch (patch.field) {
    case PackagedField::kDuration: return LoadBE16(GuestHost(base, frame), 0) == patch.expected;
    case PackagedField::kHead: return *GuestHost(base, frame + kHurtOffset) == patch.expected;
    case PackagedField::kBody: return *GuestHost(base, frame + kHurtOffset + 1) == patch.expected;
    case PackagedField::kLegs: return *GuestHost(base, frame + kHurtOffset + 2) == patch.expected;
    case PackagedField::kAttack: return *GuestHost(base, frame + kAttackOffset) == patch.expected;
  }
  return false;
}

void ApplyPatch(uint8_t* base, uint32_t frame, const PackagedPatch& patch) {
  switch (patch.field) {
    case PackagedField::kDuration: StoreBE16(Host(base, frame), 0, patch.value); break;
    case PackagedField::kHead: *Host(base, frame + kHurtOffset) = patch.value; break;
    case PackagedField::kBody: *Host(base, frame + kHurtOffset + 1) = patch.value; break;
    case PackagedField::kLegs: *Host(base, frame + kHurtOffset + 2) = patch.value; break;
    case PackagedField::kAttack: *Host(base, frame + kAttackOffset) = patch.value; break;
  }
}

}  // namespace

uint32_t FirstActionFrame(const uint8_t* base, uint32_t table) {
  const uint32_t header = LoadBE16(GuestHost(base, table), 0);
  uint32_t first = 0xFFFFFFFFu;
  for (uint32_t action = 0; action < header; action += 2) {
    const uint32_t subs = table + LoadBE16(GuestHost(base, table + action), 0);
    first = std::min(first, subs + LoadBE16(GuestHost(base, subs), 0));
  }
  return first;
}

struct ViolentActionWriter {
  ViolentActionTable& out;
  uint8_t* base;
  uint32_t first_frame;
  uint32_t cursor;
  uint32_t end;

  void Remember(uint8_t action, uint8_t sub, uint32_t address) {
    if (out.entries_ < int(out.entry_.size())) out.entry_[out.entries_++] = {action, sub, address};
  }

  void RememberSource(uint32_t start, uint32_t frames, uint32_t source) {
    if (out.sources_ < int(out.source_.size())) out.source_[out.sources_++] = {start, frames, source};
  }

  bool Write(const PackagedAction& action) {
    const uint32_t bytes = uint32_t(action.frames.size()) * kActionFrameBytes + 4;
    if (end - cursor < bytes) return false;
    const uint32_t start = cursor;
    for (size_t i = 0; i < action.frames.size(); ++i) {
      const PackagedFrame& frame = action.frames[i];
      const uint32_t source = first_frame + frame.source_key;
      uint8_t* target = Host(base, cursor);
      std::memcpy(target, GuestHost(base, source), kActionFrameBytes);
      RememberSource(cursor, 1, source);
      StoreBE16(target, 0, frame.duration);
      StoreBE16(target, kFlagsOffset, uint16_t(frame.event | (i + 1 == action.frames.size() ? kEndFlag : 0)));
      target[kHurtOffset] = frame.head;
      target[kHurtOffset + 1] = frame.body;
      target[kHurtOffset + 2] = frame.legs;
      target[kAttackOffset] = frame.attack;
      target[kPushOffset] = frame.push;
      target[kShapeOffset] = frame.shape;
      target[kWeightOffset] = frame.weight;
      if (frame.stance_art && out.stances_ < int(out.stance_.size())) out.stance_[out.stances_++] = cursor;
      cursor += kActionFrameBytes;
    }
    StoreBE32(Host(base, cursor), 0, cursor - kActionFrameBytes);
    cursor += 4;
    Remember(action.action, action.sub, start);
    return true;
  }

  bool Copy(uint8_t action, uint8_t sub, const std::vector<PackagedPatch>& patches) {
    if (!GuestHasAction(base, out.table_, action)) return false;
    const uint32_t source = ActionAddress(base, out.table_, action, sub);
    const int frames = FrameCount(base, source);
    if (frames == 0) return false;
    const uint32_t bytes = uint32_t(frames) * kActionFrameBytes;
    if (end - cursor < bytes + 4) return false;
    for (const PackagedPatch& patch : patches) {
      if (patch.action != action || patch.sub != sub) continue;
      if (patch.frame >= frames || !PatchMatches(base, source + patch.frame * kActionFrameBytes, patch)) return false;
    }
    std::memcpy(Host(base, cursor), GuestHost(base, source), bytes);
    RememberSource(cursor, uint32_t(frames), source);
    for (const PackagedPatch& patch : patches) {
      if (patch.action == action && patch.sub == sub) ApplyPatch(base, cursor + patch.frame * kActionFrameBytes, patch);
    }
    uint32_t link = LoadBE32(GuestHost(base, source + bytes), 0);
    if (link >= source && link < source + bytes) link = link - source + cursor;
    StoreBE32(Host(base, cursor + bytes), 0, link);
    Remember(action, sub, cursor);
    cursor += bytes + 4;
    return true;
  }
};

bool ViolentActionTable::Build(uint8_t* base, uint32_t table, uint32_t memory, uint32_t capacity,
                               const ViolentKenGameplay& gameplay) {
  *this = ViolentActionTable{};
  table_ = table;
  ViolentActionWriter writer{*this, base, FirstActionFrame(base, table), memory, memory + capacity};
  for (const PackagedAction& action : gameplay.actions) {
    if (!writer.Write(action)) return false;
  }
  uint32_t done_action = 0xFFFFFFFFu;
  for (const PackagedPatch& patch : gameplay.patches) {
    const uint32_t slot = uint32_t(patch.action) << 8 | patch.sub;
    if (slot == done_action || Find(patch.action, patch.sub) != 0) continue;
    done_action = slot;
    if (!writer.Copy(patch.action, patch.sub, gameplay.patches)) ++skipped_;
  }
  return true;
}

uint32_t ViolentActionTable::Find(uint8_t action, uint8_t sub) const {
  for (int i = 0; i < entries_; ++i) {
    if (entry_[i].action == action && entry_[i].sub == sub) return entry_[i].address;
  }
  return 0;
}

uint32_t ViolentActionTable::SourceFrame(uint32_t record) const {
  for (int i = 0; i < sources_; ++i) {
    const SourceRange& range = source_[i];
    const uint32_t offset = record - range.start;
    if (record >= range.start && offset < range.frames * kActionFrameBytes && offset % kActionFrameBytes == 0) {
      return range.source + offset;
    }
  }
  return 0;
}

bool ViolentActionTable::StanceAt(uint32_t record) const {
  for (int i = 0; i < stances_; ++i) {
    if (stance_[i] == record) return true;
  }
  return false;
}

int BuildViolentPack(uint8_t* base, uint32_t pack, uint32_t memory, const ViolentKenGameplay& gameplay) {
  uint8_t* copy = Host(base, memory);
  std::memcpy(copy, GuestHost(base, pack), kPackCopyBytes);
  const uint32_t attacks = LoadBE16(copy, kPackAttackOffset);
  const uint32_t after = LoadBE16(copy, kPackAfterAttackOffset);
  if (attacks == 0 || after <= attacks || after > kPackCopyBytes) return 0;
  const uint32_t records = std::min((after - attacks) / kPackRecordBytes, kGuestAttackRecords);

  // The new records do not fit the guest table, whose neighbour follows it directly.
  uint8_t* table = copy + kPackCopyBytes;
  std::memset(table, 0, kAttackTableRecords * kPackRecordBytes);
  std::memcpy(table, GuestHost(base, pack + attacks), records * kPackRecordBytes);
  StoreBE16(copy, kPackAttackOffset, uint16_t(kPackCopyBytes));

  int applied = 0;
  for (const PackagedAttack& attack : gameplay.attacks) {
    if (attack.id >= kAttackTableRecords) continue;
    std::memcpy(table + attack.id * kPackRecordBytes, attack.record, kPackRecordBytes);
    ++applied;
  }
  return applied;
}

std::array<ViolentActionTable, 2>& GlobalViolentActionTables() {
  static std::array<ViolentActionTable, 2> tables;
  return tables;
}

bool ViolentStanceAtRecord(uint32_t record) {
  for (const ViolentActionTable& table : GlobalViolentActionTables()) {
    if (table.StanceAt(record)) return true;
  }
  return false;
}

uint32_t ViolentSourceFrame(uint32_t record) {
  for (const ViolentActionTable& table : GlobalViolentActionTables()) {
    if (const uint32_t source = table.SourceFrame(record)) return source;
  }
  return 0;
}

}  // namespace ssf2t::character
