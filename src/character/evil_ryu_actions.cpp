#include "character/evil_ryu_actions.h"

#include <cstring>

#include "guest_memory.h"

namespace ssf2t::character {
namespace {

constexpr uint16_t kEndFlag = 0x8000;
constexpr int kMaxFrames = 64;
constexpr uint32_t kFlagsOffset = 2;
constexpr uint32_t kHurtOffset = 8;
constexpr uint32_t kAttackOffset = 12;
constexpr uint32_t kPushOffset = 13;
constexpr uint32_t kShapeOffset = 14;
constexpr uint32_t kFighterAnimationFlags = 0x14;
constexpr uint32_t kFighterAnimationCountdown = 0x18;
constexpr uint32_t kFighterAnimationRecord = 0x1C;

struct FrameRef {
  uint8_t action;
  uint8_t sub;
  uint8_t frame;
};

constexpr FrameRef kPose15{0x08, 0, 1};
constexpr FrameRef kPose21{0x08, 0, 0};
constexpr FrameRef kPose32{0x50, 9, 0};
constexpr FrameRef kPose41{0x4C, 0, 2};
constexpr FrameRef kPose42{0x4C, 0, 3};
constexpr FrameRef kPose56{0x40, 0, 0};
constexpr FrameRef kPose63{0x42, 2, 0};
constexpr FrameRef kPose146{0x4A, 4, 1};
constexpr FrameRef kPose147{0x4A, 4, 2};
constexpr FrameRef kPose148{0x4A, 4, 3};
constexpr FrameRef kPose154{0x4A, 4, 0};
constexpr FrameRef kPose155{0x50, 0, 1};
constexpr FrameRef kPose156{0x50, 0, 2};
constexpr FrameRef kPose157{0x50, 0, 3};
constexpr FrameRef kPose164{0x50, 1, 2};
constexpr FrameRef kPose165{0x50, 1, 3};

constexpr EvilPose kStance = EvilPose::kPowerStance;
constexpr EvilPose kBack = EvilPose::kBackTurned;
constexpr EvilPose kNoArt = EvilPose::kNone;

struct NewFrame {
  uint8_t duration;  // 0 keeps the duration of the referenced guest frame.
  uint8_t event;
  FrameRef pose;
  EvilPose art;
  uint8_t head, body, legs;
  uint8_t attack;
  uint8_t push;
  uint8_t shape;
};

constexpr NewFrame kTeleportFrames[] = {
    {2, 2, kPose41, kStance, 0, 0, 0, 0, 0, 221},  {2, 4, kPose41, kStance, 0, 0, 0, 0, 0, 221},
    {2, 2, kPose41, kStance, 0, 0, 0, 0, 0, 221},  {2, 0, kPose41, kStance, 0, 0, 0, 0, 0, 221},
    {2, 18, kPose41, kStance, 0, 0, 0, 0, 0, 221}, {2, 20, kPose41, kStance, 0, 0, 0, 0, 0, 221},
    {2, 18, kPose41, kStance, 0, 0, 0, 0, 0, 221}, {1, 255, kPose15, kNoArt, 0, 0, 0, 0, 0, 27},
    {8, 0, kPose21, kNoArt, 1, 1, 1, 0, 1, 27},    {1, 0, kPose21, kNoArt, 1, 1, 1, 0, 1, 27},
};

constexpr NewFrame kDemonStartFrames[] = {
    {3, 0, kPose41, kStance, 0, 0, 0, 0, 1, 221}, {18, 1, kPose41, kStance, 0, 0, 0, 0, 1, 221},
    {5, 2, kPose15, kNoArt, 1, 1, 1, 0, 1, 27},   {1, 0, kPose15, kNoArt, 1, 1, 1, 0, 1, 27},
    {20, 0, kPose63, kNoArt, 0, 0, 0, 0, 1, 27},  {1, 0, kPose63, kNoArt, 0, 0, 0, 0, 1, 27},
};

// USF2 holds the back pose for 109 ticks from the catch; the guest shows it after its 80 tick blackout.
constexpr NewFrame kDemonFinishFrames[] = {
    {41, 0, kPose56, kBack, 0, 0, 0, 0, 1, 27},  {8, 0, kPose56, kNoArt, 0, 0, 0, 0, 1, 27},
    {1, 0, kPose56, kNoArt, 0, 0, 0, 0, 1, 27},  {8, 0, kPose21, kNoArt, 1, 1, 1, 0, 1, 27},
    {1, 0, kPose21, kNoArt, 1, 1, 1, 0, 1, 27},
};

constexpr NewFrame kShakunetsuFrames[] = {
    {4, 0, kPose41, kStance, 1, 1, 1, 0, 1, 221}, {4, 0, kPose41, kStance, 1, 1, 1, 0, 1, 221},
    {6, 0, kPose41, kStance, 1, 1, 1, 0, 1, 221}, {6, 0, kPose41, kNoArt, 17, 10, 9, 0, 1, 222},
    {0, 0, kPose42, kNoArt, 35, 26, 22, 0, 1, 219},
};

constexpr NewFrame kSenpuFrames[] = {
    {4, 0, kPose21, kNoArt, 1, 1, 1, 0, 1, 27},    {5, 1, kPose164, kNoArt, 36, 27, 0, 0, 5, 26},
    {5, 0, kPose165, kNoArt, 36, 27, 0, 0, 5, 26}, {5, 0, kPose146, kNoArt, 11, 2, 7, 0, 2, 27},
    {6, 0, kPose147, kNoArt, 11, 2, 31, kAttackSenpu, 2, 27},
    {2, 0, kPose148, kNoArt, 11, 2, 7, 0, 2, 27},  {1, 1, kPose148, kNoArt, 11, 2, 7, 0, 2, 27},
    {3, 0, kPose32, kNoArt, 2, 1, 1, 0, 1, 27},    {1, 0, kPose32, kNoArt, 2, 1, 1, 0, 1, 27},
};

constexpr NewFrame kStrongTatsuFrames[] = {
    {1, 0, kPose154, kNoArt, 0, 0, 0, 0, 2, 26},
    {4, 0, kPose154, kNoArt, 0, 0, 0, kAttackTatsuStart, 2, 26},
    {4, 0, kPose155, kNoArt, 36, 27, 0, 0, 5, 26},
    {4, 0, kPose156, kNoArt, 36, 27, 0, 0, 5, 26},
    {4, 0, kPose157, kNoArt, 36, 27, 0, 0, 5, 26},
    {1, 0, kPose157, kNoArt, 36, 27, 0, 0, 5, 26},
};

constexpr NewFrame kWinBackFrames[] = {{8, 0, kPose56, kBack, 0, 0, 0, 0, 1, 27}};
constexpr NewFrame kWinStanceFrames[] = {{8, 0, kPose32, kNoArt, 0, 0, 0, 0, 1, 27},
                                         {8, 0, kPose41, kStance, 0, 0, 0, 0, 1, 221}};

struct NewAction {
  uint8_t action;
  uint8_t sub;
  const NewFrame* frames;
  int count;
};

template <size_t N>
constexpr NewAction Action(uint8_t action, uint8_t sub, const NewFrame (&frames)[N]) {
  return {action, sub, frames, int(N)};
}

constexpr NewAction kNewActions[] = {
    Action(kRagingDemonAction, 0, kDemonStartFrames), Action(kRagingDemonAction, 1, kDemonFinishFrames),
    Action(kRagingDemonAction, 2, kDemonFinishFrames), Action(kShakunetsuAction, 0, kShakunetsuFrames),
    Action(kShakunetsuAction, 1, kShakunetsuFrames),  Action(kShakunetsuAction, 2, kShakunetsuFrames),
    Action(kUniqueAction, kSenpuSub, kSenpuFrames),   Action(0x38, 0, kWinBackFrames),
    Action(0x52, 0, kWinBackFrames),                  Action(0x3A, 0, kWinStanceFrames),
    Action(0x52, 1, kWinStanceFrames),
};

enum class FrameField : uint8_t { kDuration, kHead, kLegs };

struct FramePatch {
  FrameField field;
  uint8_t frame;
  uint8_t expected;
  uint8_t value;
};

struct ActionPatch {
  uint8_t action;
  uint8_t sub;
  const FramePatch* patches;
  int count;
};

constexpr FramePatch D(uint8_t frame, uint8_t expected, uint8_t value) {
  return {FrameField::kDuration, frame, expected, value};
}

constexpr FramePatch kFarKick[] = {D(1, 4, 2)};
constexpr FramePatch kRoundKick[] = {D(1, 4, 2),
                                     {FrameField::kHead, 1, 41, 1},
                                     {FrameField::kHead, 2, 42, 41}};
constexpr FramePatch kCrouchKick[] = {{FrameField::kLegs, 1, 32, 8}};
constexpr FramePatch kAirKick[] = {{FrameField::kLegs, 2, 30, 7}};
constexpr FramePatch kIdle[] = {D(0, 4, 3), D(2, 4, 3), D(3, 4, 3), D(5, 4, 3)};
constexpr FramePatch kWalk[] = {D(1, 6, 5), D(2, 4, 3), D(3, 4, 3), D(4, 4, 3), D(5, 6, 5)};
constexpr FramePatch kPreJump[] = {D(0, 4, 3)};

template <size_t N>
constexpr ActionPatch Patch(uint8_t action, uint8_t sub, const FramePatch (&patches)[N]) {
  return {action, sub, patches, int(N)};
}

constexpr ActionPatch kActionPatches[] = {
    Patch(0x42, 4, kFarKick), Patch(0x42, 5, kRoundKick), Patch(0x46, 0, kCrouchKick), Patch(0x4A, 3, kAirKick),
    Patch(0x4A, 5, kAirKick), Patch(0x00, 2, kIdle),      Patch(0x00, 3, kIdle),       Patch(0x02, 0, kIdle),
    Patch(0x00, 0, kWalk),    Patch(0x00, 1, kWalk),      Patch(0x08, 0, kPreJump),    Patch(0x08, 1, kPreJump),
    Patch(0x08, 2, kPreJump), Patch(0x08, 3, kPreJump),   Patch(0x0A, 0, kPreJump),
};

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

uint8_t* Host(uint8_t* base, uint32_t address) { return const_cast<uint8_t*>(GuestHost(base, address)); }

bool PatchMatches(const uint8_t* base, uint32_t record, const FramePatch& patch) {
  const uint32_t frame = record + uint32_t(patch.frame) * kActionFrameBytes;
  switch (patch.field) {
    case FrameField::kDuration: return LoadBE16(GuestHost(base, frame), 0) == patch.expected;
    case FrameField::kHead: return *GuestHost(base, frame + kHurtOffset) == patch.expected;
    case FrameField::kLegs: return *GuestHost(base, frame + kHurtOffset + 2) == patch.expected;
  }
  return false;
}

void ApplyPatch(uint8_t* base, uint32_t record, const FramePatch& patch) {
  const uint32_t frame = record + uint32_t(patch.frame) * kActionFrameBytes;
  switch (patch.field) {
    case FrameField::kDuration: StoreBE16(Host(base, frame), 0, patch.value); break;
    case FrameField::kHead: *Host(base, frame + kHurtOffset) = patch.value; break;
    case FrameField::kLegs: *Host(base, frame + kHurtOffset + 2) = patch.value; break;
  }
}

constexpr uint32_t kPackCopyBytes = 0x1200;
constexpr uint32_t kPackLegsOffset = 4;
constexpr uint32_t kPackAttackOffset = 6;
constexpr uint32_t kAttackRecordBytes = 16;
constexpr uint32_t kAttackRecords = 76;
constexpr uint32_t kBoxBytes = 4;
constexpr uint8_t kSenpuTemplate = 16;
constexpr uint8_t kTatsuTemplate = 37;
constexpr uint8_t kSenpuStun = 7;
constexpr uint8_t kSenpuEffect = 12;
constexpr uint8_t kTatsuStun = 0;

struct AttackPatch {
  uint8_t id;
  int8_t before[4];
  int8_t after[4];
};

constexpr AttackPatch kAttackPatches[] = {
    {2, {-35, 69, 25, 3}, {-39, 75, 32, 9}},
    {16, {-16, 83, 33, 14}, {-16, 84, 46, 15}},
    {26, {-30, 45, 42, 14}, {-34, 56, 46, 25}},
    {29, {-24, 61, 49, 12}, {-24, 55, 49, 18}},
};

constexpr uint8_t kLegsBoxId = 13;
constexpr int8_t kLegsBoxBefore[4] = {-32, 11, 44, 12};
constexpr int8_t kLegsBoxAfter[4] = {-22, 11, 34, 12};
constexpr int8_t kSenpuBox[4] = {-28, 45, 34, 14};
constexpr int8_t kTatsuStartBox[4] = {-18, 52, 21, 15};

bool ReplaceBox(uint8_t* record, const int8_t* before, const int8_t* after) {
  if (std::memcmp(record, before, 4) != 0) return false;
  std::memcpy(record, after, 4);
  return true;
}

}  // namespace

struct EvilActionWriter {
  EvilActionTable& out;
  uint8_t* base;
  uint32_t cursor;
  uint32_t end;

  bool Reserve(uint32_t bytes) { return end - cursor >= bytes; }

  void Remember(uint8_t action, uint8_t sub, bool strong, uint32_t address) {
    if (out.entries_ < int(out.entry_.size())) out.entry_[out.entries_++] = {action, sub, strong, address};
  }

  void RememberSource(uint32_t start, uint32_t frames, uint32_t source) {
    if (out.sources_ < int(out.source_.size())) out.source_[out.sources_++] = {start, frames, source};
  }

  uint32_t Write(const NewFrame* frames, int count) {
    if (!Reserve(uint32_t(count) * kActionFrameBytes + 4)) return 0;
    const uint32_t start = cursor;
    for (int i = 0; i < count; ++i) {
      const NewFrame& frame = frames[i];
      const uint32_t source = ActionAddress(base, out.table_, frame.pose.action, frame.pose.sub) +
                              uint32_t(frame.pose.frame) * kActionFrameBytes;
      uint8_t* target = Host(base, cursor);
      std::memcpy(target, GuestHost(base, source), kActionFrameBytes);
      RememberSource(cursor, 1, source);
      const uint16_t duration = frame.duration ? frame.duration : LoadBE16(GuestHost(base, source), 0);
      const uint16_t flags = uint16_t(frame.event | (i == count - 1 ? kEndFlag : 0));
      StoreBE16(target, 0, duration);
      StoreBE16(target, kFlagsOffset, flags);
      target[kHurtOffset] = frame.head;
      target[kHurtOffset + 1] = frame.body;
      target[kHurtOffset + 2] = frame.legs;
      target[kAttackOffset] = frame.attack;
      target[kPushOffset] = frame.push;
      target[kShapeOffset] = frame.shape;
      if (frame.art != EvilPose::kNone && out.poses_ < int(out.pose_.size())) {
        out.pose_[out.poses_++] = {cursor, frame.art};
      }
      cursor += kActionFrameBytes;
    }
    StoreBE32(Host(base, cursor), 0, cursor - kActionFrameBytes);
    cursor += 4;
    return start;
  }

  bool Copy(const ActionPatch& patch) {
    const uint32_t source = ActionAddress(base, out.table_, patch.action, patch.sub);
    const int frames = FrameCount(base, source);
    if (frames == 0) return false;
    const uint32_t bytes = uint32_t(frames) * kActionFrameBytes;
    if (!Reserve(bytes + 4)) return false;
    for (int i = 0; i < patch.count; ++i) {
      if (patch.patches[i].frame >= frames || !PatchMatches(base, source, patch.patches[i])) return false;
    }
    std::memcpy(Host(base, cursor), GuestHost(base, source), bytes);
    RememberSource(cursor, uint32_t(frames), source);
    for (int i = 0; i < patch.count; ++i) ApplyPatch(base, cursor, patch.patches[i]);
    uint32_t link = LoadBE32(GuestHost(base, source + bytes), 0);
    if (link >= source && link < source + bytes) link = link - source + cursor;
    StoreBE32(Host(base, cursor + bytes), 0, link);
    Remember(patch.action, patch.sub, false, cursor);
    cursor += bytes + 4;
    return true;
  }
};

bool EvilActionTable::Build(uint8_t* base, uint32_t table, uint32_t memory, uint32_t capacity) {
  *this = EvilActionTable{};
  table_ = table;
  EvilActionWriter writer{*this, base, memory, memory + capacity};
  teleport_ = writer.Write(kTeleportFrames, int(std::size(kTeleportFrames)));
  if (teleport_ == 0) return false;
  for (const NewAction& action : kNewActions) {
    const uint32_t address = writer.Write(action.frames, action.count);
    if (address == 0) return false;
    writer.Remember(action.action, action.sub, false, address);
  }
  const uint32_t tatsu = writer.Write(kStrongTatsuFrames, int(std::size(kStrongTatsuFrames)));
  if (tatsu == 0) return false;
  writer.Remember(kTatsuAction, 0, true, tatsu);
  for (const ActionPatch& patch : kActionPatches) {
    if (!writer.Copy(patch)) ++skipped_;
  }
  return true;
}

uint32_t EvilActionTable::Find(uint8_t action, uint8_t sub, uint8_t tatsu_strength) const {
  const bool strong = action == kTatsuAction && sub == 0 && tatsu_strength != 0;
  for (int i = 0; i < entries_; ++i) {
    const Entry& entry = entry_[i];
    if (entry.action == action && entry.sub == sub && entry.strong_tatsu == strong) return entry.address;
  }
  return 0;
}

EvilPose EvilActionTable::PoseAt(uint32_t record) const {
  for (int i = 0; i < poses_; ++i) {
    if (pose_[i].record == record) return pose_[i].pose;
  }
  return EvilPose::kNone;
}

uint32_t EvilActionTable::SourceFrame(uint32_t record) const {
  for (int i = 0; i < sources_; ++i) {
    const SourceRange& range = source_[i];
    const uint32_t offset = record - range.start;
    if (record >= range.start && offset < range.frames * kActionFrameBytes && offset % kActionFrameBytes == 0) {
      return range.source + offset;
    }
  }
  return 0;
}

void SetFighterAnimation(uint8_t* base, uint32_t fighter, uint32_t record) {
  GuestStoreBE32(base, fighter + kFighterAnimationRecord, record);
  GuestStoreBE16(base, fighter + kFighterAnimationCountdown, LoadBE16(GuestHost(base, record), 0));
  GuestStoreBE16(base, fighter + kFighterAnimationFlags, LoadBE16(GuestHost(base, record + kFlagsOffset), 0));
}

bool GuestHasAction(const uint8_t* base, uint32_t table, uint8_t action) {
  return action < LoadBE16(GuestHost(base, table), 0);
}

int BuildEvilPack(uint8_t* base, uint32_t pack, uint32_t memory) {
  uint8_t* copy = Host(base, memory);
  std::memcpy(copy, GuestHost(base, pack), kPackCopyBytes);
  const uint32_t attacks = LoadBE16(copy, kPackAttackOffset);
  const uint32_t legs = LoadBE16(copy, kPackLegsOffset);
  if (attacks == 0 || attacks >= kPackCopyBytes || legs == 0 || legs >= kPackCopyBytes) return 0;

  // The attack table moves past the copy: the new records do not fit the classic table.
  uint8_t* table = copy + kPackCopyBytes;
  std::memcpy(table, GuestHost(base, pack + attacks), kAttackRecordBytes * (kAttackRecords - 2));
  StoreBE16(copy, kPackAttackOffset, uint16_t(kPackCopyBytes));

  int applied = 0;
  for (const AttackPatch& patch : kAttackPatches) {
    if (ReplaceBox(table + patch.id * kAttackRecordBytes, patch.before, patch.after)) ++applied;
  }
  if (ReplaceBox(copy + legs + kLegsBoxId * kBoxBytes, kLegsBoxBefore, kLegsBoxAfter)) ++applied;

  uint8_t* senpu = table + kAttackSenpu * kAttackRecordBytes;
  std::memcpy(senpu, table + kSenpuTemplate * kAttackRecordBytes, kAttackRecordBytes);
  std::memcpy(senpu, kSenpuBox, 4);
  senpu[4] = kSenpuStun;
  senpu[11] = kSenpuEffect;
  uint8_t* tatsu = table + kAttackTatsuStart * kAttackRecordBytes;
  std::memcpy(tatsu, table + kTatsuTemplate * kAttackRecordBytes, kAttackRecordBytes);
  std::memcpy(tatsu, kTatsuStartBox, 4);
  tatsu[4] = kTatsuStun;
  return applied;
}

std::array<EvilActionTable, 2>& GlobalEvilActionTables() {
  static std::array<EvilActionTable, 2> tables;
  return tables;
}

EvilPose EvilPoseAtRecord(uint32_t record) {
  for (const EvilActionTable& table : GlobalEvilActionTables()) {
    const EvilPose pose = table.PoseAt(record);
    if (pose != EvilPose::kNone) return pose;
  }
  return EvilPose::kNone;
}

uint32_t EvilSourceFrame(uint32_t record) {
  for (const EvilActionTable& table : GlobalEvilActionTables()) {
    if (const uint32_t source = table.SourceFrame(record)) return source;
  }
  return 0;
}

}  // namespace ssf2t::character
