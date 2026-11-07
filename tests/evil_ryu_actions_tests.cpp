#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "character/evil_ryu_actions.h"
#include "character/evil_ryu_sprites.h"
#include "guest_memory.h"

namespace {

using namespace ssf2t;
using namespace ssf2t::character;

int failures = 0;

void Check(bool condition, const char* what) {
  if (condition) return;
  std::printf("FAIL %s\n", what);
  ++failures;
}

constexpr uint32_t kTable = 0x1000;
constexpr uint32_t kMemory = 0x40000;
constexpr uint32_t kPack = 0x60000;
constexpr int kActions = 0x56 / 2;
constexpr int kSubs = 10;
constexpr int kFrames = 6;
constexpr uint32_t kFramesStart = kTable + 0x100;
constexpr uint32_t kActionBytes = (kFrames * kActionFrameBytes + 4) * kSubs + kSubs * 2;
constexpr uint32_t kPoseBase = 0x8C670000;

struct Fake {
  std::vector<uint8_t> memory = std::vector<uint8_t>(0x80000, 0);
  uint8_t* base() { return memory.data(); }

  uint32_t Frame(uint8_t action, uint8_t sub, int frame) const {
    const uint32_t subs = kFramesStart + uint32_t(action / 2) * kActionBytes;
    return subs + kSubs * 2 + (kFrames * kActionFrameBytes + 4) * sub + frame * kActionFrameBytes;
  }

  void Duration(uint8_t action, uint8_t sub, int frame, uint16_t value) {
    StoreBE16(base(), Frame(action, sub, frame), value);
  }

  void Hurt(uint8_t action, uint8_t sub, int frame, int box, uint8_t value) {
    memory[Frame(action, sub, frame) + 8 + box] = value;
  }

  Fake() {
    for (int action = 0; action < kActions; ++action) {
      const uint32_t subs = kFramesStart + uint32_t(action) * kActionBytes;
      StoreBE16(base(), kTable + action * 2, uint16_t(subs - kTable));
      for (int sub = 0; sub < kSubs; ++sub) {
        const uint32_t first = Frame(uint8_t(action * 2), uint8_t(sub), 0);
        StoreBE16(base(), subs + sub * 2, uint16_t(first - subs));
        for (int frame = 0; frame < kFrames; ++frame) {
          const uint32_t at = first + frame * kActionFrameBytes;
          StoreBE16(base(), at, 4);
          StoreBE16(base(), at + 2, frame == kFrames - 1 ? 0x8000 : 0);
          StoreBE32(base(), at + 4, kPoseBase + uint32_t(action) * 0x100 + uint32_t(sub) * 0x10 + frame);
        }
        StoreBE32(base(), first + kFrames * kActionFrameBytes, first);
      }
    }
    for (const uint8_t sub : {0, 1}) {
      for (int frame = 0; frame < 6; ++frame) {
        const uint16_t walk[] = {3, 6, 4, 4, 4, 6};
        Duration(0x00, sub, frame, walk[frame]);
      }
    }
    Hurt(0x42, 5, 1, 0, 41);
    Hurt(0x42, 5, 2, 0, 42);
    Hurt(0x46, 0, 1, 2, 32);
    Hurt(0x4A, 3, 2, 2, 30);
    Hurt(0x4A, 5, 2, 2, 30);
  }
};

uint16_t Duration(Fake& fake, uint32_t record, int frame) {
  return LoadBE16(fake.base(), record + frame * kActionFrameBytes);
}

uint16_t Flags(Fake& fake, uint32_t record, int frame) {
  return LoadBE16(fake.base(), record + frame * kActionFrameBytes + 2);
}

uint8_t Byte(Fake& fake, uint32_t record, int frame, int offset) {
  return fake.memory[record + frame * kActionFrameBytes + offset];
}

void TestActions() {
  Fake fake;
  const std::vector<uint8_t> original(fake.memory.begin() + kTable, fake.memory.begin() + kMemory);
  EvilActionTable table;
  Check(table.Build(fake.base(), kTable, kMemory, kEvilActionBytes), "the table builds");
  Check(table.skipped() == 0, "every frame patch finds the Ryu frame it expects");
  Check(std::memcmp(original.data(), fake.memory.data() + kTable, original.size()) == 0, "ordinary Ryu data is untouched");

  const uint32_t teleport = table.Teleport();
  Check(teleport >= kMemory, "the teleport action is in Evil Ryu's own memory");
  int startup = 0;
  for (int frame = 0; frame < 8; ++frame) startup += Duration(fake, teleport, frame);
  Check(startup == 15, "the teleport startup lasts 15 ticks like USF2 E55");
  Check(Duration(fake, teleport, 8) + Duration(fake, teleport, 9) == 9, "the teleport recovery lasts 9 ticks");
  for (int frame = 0; frame < 8; ++frame) {
    Check(Byte(fake, teleport, frame, 8) == 0 && Byte(fake, teleport, frame, 9) == 0 && Byte(fake, teleport, frame, 10) == 0,
          "the teleport startup has no hurtbox");
    Check(Byte(fake, teleport, frame, 13) == 0, "the teleport startup has no pushbox");
  }
  Check(Byte(fake, teleport, 8, 8) == 1 && Byte(fake, teleport, 8, 13) == 1, "the teleport recovery is hittable");
  Check((Flags(fake, teleport, 7) & 0xFF) == 0xFF, "the dash starts on the negative event");
  Check((Flags(fake, teleport, 1) & 0xFF) == 4 && (Flags(fake, teleport, 5) & 0xFF) == 20, "the slide events are USF2's");
  Check((Flags(fake, teleport, 9) & 0x8000) != 0, "the teleport ends");
  Check(table.PoseAt(teleport) == EvilPose::kPowerStance, "the teleport startup uses the power stance art");
  Check(table.PoseAt(teleport + 7 * kActionFrameBytes) == EvilPose::kNone, "the dash pose is ordinary Ryu art");
  const uint32_t last = teleport + 9 * kActionFrameBytes;
  Check(LoadBE32(fake.base(), last + kActionFrameBytes) == last, "a one-shot action holds its last frame");

  const uint32_t demon = table.Find(kRagingDemonAction, 0, 0);
  Check(demon != 0, "action 0x5E exists for Evil Ryu");
  Check(Duration(fake, demon, 0) + Duration(fake, demon, 1) == 21, "the raging demon startup lasts 21 ticks");
  Check((Flags(fake, demon, 2) & 0xFF) == 2, "the dash frame carries USF2's event");
  Check(Duration(fake, demon, kDemonCaughtFrame) == 20, "the catch shows USF2's 20 tick pose");
  const uint32_t finish = table.Find(kRagingDemonAction, 1, 0);
  Check(finish != 0 && table.Find(kRagingDemonAction, 2, 0) != 0, "both raging demon finishes exist");
  Check(table.PoseAt(finish) == EvilPose::kBackTurned, "the finish uses the back turned art");

  const uint32_t shakunetsu = table.Find(kShakunetsuAction, kShakunetsuSub, 0);
  Check(shakunetsu != 0 && table.Find(kShakunetsuAction, kShakunetsuSub + 2, 0) != 0,
        "the half circle fireball is replaced for all strengths");
  Check(table.Find(kShakunetsuAction, 0, 0) == 0 && table.Find(kShakunetsuAction, 2, 0) == 0, "236P stays Ryu's");
  int windup = 0;
  for (int frame = 0; frame < 4; ++frame) windup += Duration(fake, shakunetsu, frame);
  Check(windup == 20, "the Shakunetsu fires after USF2's 20 ticks, on frame index 4");
  Check((Flags(fake, shakunetsu, 3) & 0x8000) == 0 && (Flags(fake, shakunetsu, 4) & 0x8000) != 0,
        "the guest fires on the end frame, so frame index 4 carries the end flag");

  Check(table.Find(kUniqueAction, 8, 0) != 0, "the unique becomes Senpu Kyaku");
  const uint32_t senpu = table.Find(kUniqueAction, 8, 0);
  Check(Byte(fake, senpu, 4, 12) == kAttackSenpu, "Senpu Kyaku hits with its own box");
  Check((Flags(fake, senpu, 1) & 0xFF) == 1, "frame 1 starts the hop");
  Check((Flags(fake, senpu, 6) & 0xFF) == 1, "frame 6 waits for the landing");
  Check(Duration(fake, senpu, 7) == 3 && (Flags(fake, senpu, 8) & 0x8000) != 0, "the landing plays frames 7 and 8");
  Check(table.Find(kUniqueAction, 7, 0) == 0, "the other unique stays Ryu's");

  Check(table.Find(kTatsuAction, 0, 0) == 0, "light Tatsu keeps Ryu's startup");
  const uint32_t tatsu = table.Find(kTatsuAction, 0, 2);
  Check(tatsu != 0 && tatsu == table.Find(kTatsuAction, 0, 4), "medium and heavy Tatsu use the USF2 startup");
  Check(Byte(fake, tatsu, 1, 12) == kAttackTatsuStart, "the Tatsu startup has its own hit");
  Check((Flags(fake, tatsu, 4) & 0x8000) != 0 && Duration(fake, tatsu, 4) == 1,
        "the spins are counted from E44 frame index 4, so that frame is the end frame");

  const uint32_t idle = table.Find(0x00, 2, 0);
  Check(idle != 0 && Duration(fake, idle, 0) == 3 && Duration(fake, idle, 1) == 4, "idle frames speed up like USF2");
  const uint32_t walk = table.Find(0x00, 0, 0);
  Check(walk != 0 && Duration(fake, walk, 1) == 5 && Duration(fake, walk, 2) == 3, "walk frames speed up like USF2");
  Check(LoadBE32(fake.base(), walk + kFrames * kActionFrameBytes) == walk, "a looping action loops inside the copy");
  Check(LoadBE32(fake.base(), walk + 4) == LoadBE32(fake.base(), fake.Frame(0x00, 0, 0) + 4), "copied frames keep Ryu's pose");
  Check(Duration(fake, table.Find(0x42, 4, 0), 1) == 2, "the far kick starts faster");
  Check(Byte(fake, table.Find(0x42, 5, 0), 1, 8) == 1, "the round kick loses its extended hurtbox");
  Check(Byte(fake, table.Find(0x46, 0, 0), 1, 10) == 8, "the crouching kick hurtbox is USF2's");
  Check(table.Find(0x40, 0, 0) == 0, "unchanged actions use the guest table");

  EvilActionTable small;
  Check(!small.Build(fake.base(), kTable, kMemory, 64), "running out of room fails");

  Fake other;
  other.Duration(0x42, 4, 1, 9);
  EvilActionTable partial;
  Check(partial.Build(other.base(), kTable, kMemory, kEvilActionBytes), "a mismatch does not fail the build");
  Check(partial.skipped() == 1, "a frame that differs from Ryu's USF2 data is skipped");
  Check(partial.Find(0x42, 4, 0) == 0, "the skipped action stays the guest's");

  auto& tables = GlobalEvilActionTables();
  tables[0] = table;
  Check(EvilPoseAtRecord(teleport) == EvilPose::kPowerStance, "the global lookup finds the record");
  Check(EvilPoseAtRecord(0x1234) == EvilPose::kNone, "other records have no Evil art");
}

constexpr uint32_t kHeadTable = 0x20;
constexpr uint32_t kBodyTable = 0x120;
constexpr uint32_t kLegsTable = 0x1C0;
constexpr uint32_t kAttackTable = 0x260;

void PutBox(uint8_t* at, std::array<int, 4> box) {
  for (int i = 0; i < 4; ++i) at[i] = uint8_t(box[i]);
}

void TestPack() {
  Fake fake;
  uint8_t* pack = fake.base() + kPack;
  StoreBE16(pack, 0, kHeadTable);
  StoreBE16(pack, 2, kBodyTable);
  StoreBE16(pack, 4, kLegsTable);
  StoreBE16(pack, 6, kAttackTable);
  PutBox(pack + kLegsTable + 13 * 4, {-32, 11, 44, 12});
  PutBox(pack + kAttackTable + 2 * 16, {-35, 69, 25, 3});
  PutBox(pack + kAttackTable + 16 * 16, {-16, 83, 33, 14});
  PutBox(pack + kAttackTable + 26 * 16, {-30, 45, 42, 14});
  PutBox(pack + kAttackTable + 29 * 16, {-24, 61, 49, 12});
  for (const int id : {31, 32, 37, 38, 39, 40, 41, 42, 43, 44, 48, 49, 50, 51, 52, 53}) pack[kAttackTable + id * 16 + 11] = 0x1C;
  pack[kAttackTable + 54 * 16 + 11] = 0x4A;
  pack[kAttackTable + 55 * 16 + 11] = 0x4A;
  pack[kAttackTable + 16 * 16 + 6] = 0x2C;
  pack[kAttackTable + 37 * 16 + 6] = 0x2D;
  pack[kAttackTable + 37 * 16 + 4] = 0x87;
  pack[kAttackTable + 37 * 16 + 13] = 1;
  const std::vector<uint8_t> original(pack, pack + 0x1200);

  constexpr uint32_t copy = kPack + 0x2000;
  Check(BuildEvilPack(fake.base(), kPack, copy) == 23, "every box and move class patch applies to matching data");
  Check(std::memcmp(original.data(), pack, original.size()) == 0, "ordinary Ryu boxes are untouched");

  uint8_t* out = fake.base() + copy;
  const uint32_t attacks = LoadBE16(out, 6);
  Check(attacks >= 0x1200 && attacks + 76 * 16 <= kEvilPackBytes, "the attack table moved inside Evil Ryu's pack");
  Check(int8_t(out[attacks + 2 * 16]) == -39 && out[attacks + 2 * 16 + 1] == 75, "attack 2 is USF2's");
  Check(out[attacks + 26 * 16 + 3] == 25, "attack 26 is USF2's");
  Check(int8_t(out[kLegsTable + 13 * 4]) == -22 && out[kLegsTable + 13 * 4 + 2] == 34, "the legs box is USF2's");
  Check(int8_t(out[attacks + kAttackSenpu * 16]) == -28 && out[attacks + kAttackSenpu * 16 + 4] == 7, "the Senpu box exists");
  Check(out[attacks + kAttackSenpu * 16 + 6] == 0x2C, "the Senpu box inherits its strength");
  Check(out[attacks + kAttackSenpu * 16 + 11] == 12, "the Senpu box uses USF2's effect");
  Check(int8_t(out[attacks + kAttackTatsuStart * 16]) == -18, "the Tatsu startup box exists");
  Check(out[attacks + kAttackTatsuStart * 16 + 6] == 0x2D, "the Tatsu startup box inherits its strength");
  Check(out[attacks + kAttackTatsuStart * 16 + 4] == 0x80, "USF2 attack 61 is a special with no stun: flags 1 at record +5");
  Check(out[attacks + kAttackTatsuStart * 16 + 13] == 0, "USF2 attack 61 has packed 0, its template has 1");

  PutBox(pack + kAttackTable + 16 * 16, {1, 2, 3, 4});
  Check(out[attacks + 31 * 16 + 11] == 0x4C && out[attacks + 44 * 16 + 11] == 0x4C, "ids 31, 32, 41-44 take USF2's class 76");
  Check(out[attacks + 37 * 16 + 11] == 0x4E && out[attacks + 40 * 16 + 11] == 0x4E, "ids 37-40 take USF2's class 78");
  Check(out[attacks + 48 * 16 + 11] == 0x3E && out[attacks + 53 * 16 + 11] == 0x3E, "ids 48-53 take USF2's class 62");
  Check(out[attacks + 54 * 16 + 11] == 0x50 && out[attacks + 55 * 16 + 11] == 0x50, "ids 54 and 55 take USF2's class 80");
  Check(out[attacks + kAttackTatsuStart * 16 + 11] == 0x3E, "USF2 attack 61 is class 62");
  Check(out[attacks + kAttackSenpu * 16 + 11] == 12, "USF2 attack 60 is class 12");
  Check(pack[kAttackTable + 31 * 16 + 11] == 0x1C, "ordinary Ryu keeps his move classes");
  Check(BuildEvilPack(fake.base(), kPack, copy) == 22, "a renumbered record is skipped");
  Check(out[attacks + 16 * 16] == 1, "a skipped record keeps the guest values");
  StoreBE16(pack, 6, 0);
  Check(BuildEvilPack(fake.base(), kPack, copy) == 0, "an unusable pack applies nothing");
}

void TestWalk() {
  Fake fake;
  constexpr uint32_t kRow = 0x70000;
  constexpr uint32_t kCopy = 0x70100;
  StoreBE16(fake.base(), kRow, uint16_t(int16_t(-0x300)));
  StoreBE16(fake.base(), kRow + 2, 0x11);
  StoreBE16(fake.base(), kRow + 4, 0x200);
  StoreBE16(fake.base(), kRow + 6, 0x22);
  StoreBE16(fake.base(), kRow + 12, 0x33);
  Check(BuildEvilWalk(fake.base(), kRow, kCopy), "the walk row builds");
  Check(int16_t(LoadBE16(fake.base(), kCopy)) == -0x398, "forward keeps the row's sign at USF2's 3.59375");
  Check(int16_t(LoadBE16(fake.base(), kCopy + 4)) == 0x268, "back keeps the row's sign at USF2's 2.40625");
  Check(LoadBE16(fake.base(), kCopy + 2) == 0x11 && LoadBE16(fake.base(), kCopy + 12) == 0x33, "the rest of the row is copied");
  Check(int16_t(LoadBE16(fake.base(), kRow)) == -0x300, "the shared row is untouched");

  StoreBE16(fake.base(), kRow, 0x200);
  StoreBE16(fake.base(), kRow + 4, uint16_t(int16_t(-0x300)));
  Check(BuildEvilWalk(fake.base(), kRow, kCopy), "the other order builds");
  Check(LoadBE16(fake.base(), kCopy) == 0x268 && int16_t(LoadBE16(fake.base(), kCopy + 4)) == -0x398,
        "the faster entry is forward in either order");

  StoreBE16(fake.base(), kRow, 0);
  Check(!BuildEvilWalk(fake.base(), kRow, kCopy), "a row without speeds is left alone");
}

void Append32(std::vector<uint8_t>& out, uint32_t value) {
  for (int i = 0; i < 4; ++i) out.push_back(uint8_t(value >> (i * 8)));
}

void AppendRecord(std::vector<uint8_t>& out, uint32_t id, uint32_t width, uint32_t height, bool palette, uint8_t fill) {
  Append32(out, id);
  Append32(out, width);
  Append32(out, height);
  Append32(out, palette ? 1 : 0);
  if (palette) out.insert(out.end(), kEvilPaletteBytes, uint8_t(fill + 1));
  out.insert(out.end(), size_t(width) * height, fill);
}

bool FillsTextureEdgeToEdge(const EvilSpriteImage& image) {
  const auto column_has_art = [&](int x) {
    for (int y = 0; y < image.height; ++y) {
      if (image.indices[size_t(y) * size_t(image.width) + size_t(x)] != 0) return true;
    }
    return false;
  };
  return column_has_art(0) && column_has_art(image.width - 1);
}

void TestSprites() {
  std::vector<uint8_t> file{'S', 'S', 'F', '2', 'E', 'R', 'S', '2'};
  Append32(file, 5);
  AppendRecord(file, 224, 2, 3, false, 1);
  AppendRecord(file, 225, 1, 2, false, 2);
  AppendRecord(file, 1, 3, 4, true, 3);
  AppendRecord(file, 2, 4, 1, true, 4);
  AppendRecord(file, 3, 5, 1, true, 5);
  EvilSprites sprites;
  Check(ParseEvilSprites(file.data(), file.size(), &sprites), "the art package parses");
  Check(sprites.Get(EvilArt::kBackTurned).width == 2 && sprites.Get(EvilArt::kBackTurned).height == 3, "the back turned size");
  Check(!sprites.Get(EvilArt::kBackTurned).has_palette, "fight sprites use the fighter palette");
  Check(sprites.Get(EvilArt::kPowerStance).indices[1] == 2, "the power stance pixels");
  Check(sprites.Get(EvilArt::kLargePortrait).has_palette, "the portrait carries its palette");
  Check(sprites.Get(EvilArt::kLargePortrait).palette[1023] == 4, "the portrait palette is read");
  Check(sprites.Get(EvilArt::kLargePortrait).indices[11] == 3, "the portrait pixels follow its palette");
  Check(sprites.Get(EvilArt::kName).width == 4 && sprites.Get(EvilArt::kHudName).width == 5, "both names are present");
  Check(ArtFor(EvilPose::kBackTurned) == EvilArt::kBackTurned, "pose 224 maps to its art");
  Check(ArtFor(EvilPose::kPowerStance) == EvilArt::kPowerStance, "pose 225 maps to its art");
  EvilSprites broken;
  Check(!ParseEvilSprites(file.data(), file.size() - 1, &broken), "a truncated package fails");
  std::vector<uint8_t> missing{'S', 'S', 'F', '2', 'E', 'R', 'S', '2'};
  Append32(missing, 1);
  AppendRecord(missing, 224, 2, 3, false, 1);
  Check(!ParseEvilSprites(missing.data(), missing.size(), &broken), "a package without every image fails");
  file[0] = 'X';
  Check(!ParseEvilSprites(file.data(), file.size(), &broken), "a wrong magic fails");
  Check(!GuestNameDrawnRecently(0), "no guest name was drawn yet");
  NoteGuestNameDrawn(0);
  Check(GuestNameDrawnRecently(0) && !GuestNameDrawnRecently(1), "the guest name note is per side");

  Check(SpritePlacement(EvilPose::kPowerStance).x + SpritePlacement(EvilPose::kPowerStance).width / 2 == 26,
        "the power stance centre is USF2's");
  Check(SpritePlacement(EvilPose::kBackTurned).y + SpritePlacement(EvilPose::kBackTurned).height == 4,
        "the back turned baseline is USF2's");
}

constexpr uint32_t kPhysicalFighter = 0xFC2A0700;

constexpr uint32_t kWindowLead = 0x2000;

// A host pointer that maps `guest_start` to window + kWindowLead as the runtime's guest base does, leaving room below it
// for a store that omits the physical heap offset.
uint8_t* BaseFor(std::vector<uint8_t>& window, uint32_t guest_start) {
  return reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(window.data()) + kWindowLead - uintptr_t(guest_start) -
                                    kPhysicalHeapHostOffset);
}

void TestPhysicalFighter() {
  Fake fake;
  EvilActionTable table;
  Check(table.Build(fake.base(), kTable, kMemory, kEvilActionBytes), "the table builds for the fighter test");
  const uint32_t record = table.Find(0x02, 0, 0);
  Check(record != 0, "the fighter's init action resolves");

  constexpr uint32_t kRegion = 0xFC2A0000;
  std::vector<uint8_t> window(0x4000, 0);
  uint8_t* base = BaseFor(window, kRegion);
  const uint32_t fighter = kPhysicalFighter;
  const uint32_t at = kWindowLead + (fighter - kRegion);

  GuestStoreBE32(base, fighter + 0x1C, record);
  GuestStoreBE16(base, fighter + 0x18, 0x1234);
  GuestStore8(base, fighter + 0x03, 0x56);
  Check(LoadBE32(window.data(), at + 0x1C) == record, "a 32 bit store lands where recompiled code reads it");
  Check(LoadBE16(window.data(), at + 0x18) == 0x1234, "a 16 bit store lands where recompiled code reads it");
  Check(window[at + 0x03] == 0x56, "a byte store lands where recompiled code reads it");
  if (kPhysicalHeapHostOffset != 0) {
    StoreBE32(base, fighter + 0x40, 0xABCD);
    Check(LoadBE32(window.data(), at + 0x40) == 0, "the plain store misses physical memory, which the guest crashed on");
  }
}

void TestSetFighterAnimation() {
  Fake fake;
  constexpr uint32_t kFighter = 0x70000;
  EvilActionTable table;
  Check(table.Build(fake.base(), kTable, kMemory, kEvilActionBytes), "the table builds for the animation test");
  const uint32_t record = table.Find(0x02, 0, 0);
  const uint8_t ryu_before = fake.memory[kTable];
  SetFighterAnimation(fake.base(), kFighter, record);
  Check(LoadBE32(fake.base(), kFighter + 0x1C) == record, "the record reaches the fighter");
  Check(LoadBE16(fake.base(), kFighter + 0x18) == LoadBE16(fake.base(), record), "the countdown is the frame duration");
  Check(LoadBE16(fake.base(), kFighter + 0x14) == LoadBE16(fake.base(), record + 2), "the flags are the frame flags");
  Check(fake.memory[kTable] == ryu_before, "selecting an action leaves Ryu's table alone");

  constexpr uint16_t kRyuTableHeader = 0x56;
  StoreBE16(fake.base(), kTable, kRyuTableHeader);
  Check(GuestHasAction(fake.base(), kTable, 0x54), "the last Ryu action is in the table");
  Check(!GuestHasAction(fake.base(), kTable, kRagingDemonAction), "the raging demon id is past Ryu's table header");
}

void TestSourceFrames() {
  Fake fake;
  EvilActionTable table;
  Check(table.Build(fake.base(), kTable, kMemory, kEvilActionBytes), "the table builds for the source test");
  const uint32_t idle = table.Find(0x02, 0, 0);
  Check(idle >= kMemory, "the idle action is a copy");
  const uint32_t source = table.SourceFrame(idle);
  Check(source >= kTable && source < kMemory, "a copied frame maps back to the guest action data");
  Check(table.SourceFrame(idle + kActionFrameBytes) == source + kActionFrameBytes, "a copied run stays in step");
  Check(table.SourceFrame(idle + 1) == 0, "an address inside a frame is not a frame");
  Check(table.SourceFrame(source) == 0, "a guest frame is not a copy");
  const uint32_t teleport = table.Teleport();
  Check(table.SourceFrame(teleport) != 0, "a new frame keeps the guest frame it draws as");
  Check(LoadBE32(fake.base(), teleport + 4) == LoadBE32(fake.base(), table.SourceFrame(teleport) + 4),
        "a new frame shows the pose of that guest frame");
  Check(table.SourceFrame(0) == 0, "no record maps to nothing");
  EvilActionTable empty;
  Check(empty.SourceFrame(idle) == 0, "an unbuilt table owns no frames");
}

void TestUploadLayout() {
  EvilSpriteImage image;
  image.width = 3;
  image.height = 2;
  image.indices = {1, 2, 3, 4, 5, 6};

  std::vector<uint8_t> surface(16 * 2, 0xEE);
  WriteIndexedRows(surface.data(), 16, image, false);
  Check(surface[0] == 1 && surface[2] == 3, "the first row is unchanged");
  Check(surface[3] == 0xEE, "bytes past the row width stay padding");
  Check(surface[16] == 4 && surface[18] == 6, "the next row starts one pitch later, not one width later");

  std::fill(surface.begin(), surface.end(), uint8_t(0xEE));
  WriteIndexedRows(surface.data(), 16, image, true);
  Check(surface[0] == 3 && surface[1] == 2 && surface[2] == 1, "a mirrored row is reversed");
  Check(surface[16] == 6 && surface[18] == 4, "every mirrored row uses the pitch");

  std::array<uint8_t, 12> palette = {0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0x10, 0x20, 0x30, 0x80, 0xFF, 0x00, 0xFF};
  ClearPaletteColourKey(palette.data(), palette.size());
  Check(palette[0] == 0 && palette[3] == 0, "the colour key becomes fully transparent");
  Check(palette[4] == 0xFF && palette[6] == 0x20, "other colours are kept");
  Check(palette[8] == 0 && palette[11] == 0, "the key matches on colour alone, as the guest's does");
}

std::vector<uint8_t> ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) return {};
  std::vector<uint8_t> bytes(static_cast<size_t>(in.tellg()));
  in.seekg(0);
  in.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
  return bytes;
}

uint32_t Le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
uint16_t Le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

constexpr uint32_t kRealTable = 0x10000;
constexpr uint32_t kRealMemory = 0x40000;
constexpr uint32_t kRealFileHeader = 16;

// The disk tables are little endian with absolute pointers; the guest loader swaps them and rebases them.
struct RealTable {
  std::vector<uint8_t> memory = std::vector<uint8_t>(0x80000, 0);
  uint32_t size = 0;
  std::array<bool, 0x10000> frame_done{};

  uint8_t* base() { return memory.data(); }

  uint32_t Rebase(uint32_t pointer, uint32_t disk_base) const { return pointer - disk_base + kRealTable; }

  // Sub tables also hold entries that are not records; a record's every pose and its link point into the file.
  bool IsRecord(uint32_t record, uint32_t disk_base) const {
    const uint8_t* table = memory.data() + kRealTable;
    const auto inside = [&](uint32_t pointer) { return pointer >= disk_base && pointer - disk_base < size; };
    for (uint32_t frame = 0; frame < 64; ++frame) {
      const uint32_t at = record + frame * kActionFrameBytes;
      if (at + kActionFrameBytes + 4 > size) return false;
      if (frame_done[at]) return true;
      if (!inside(Le32(table + at + 4))) return false;
      if (Le16(table + at + 2) & 0x8000) return inside(Le32(table + at + kActionFrameBytes));
    }
    return false;
  }

  // Records share frame runs, so each frame converts once and a shared tail ends the walk.
  void ConvertFrames(uint32_t record, uint32_t disk_base) {
    if (!IsRecord(record, disk_base)) return;
    for (uint32_t frame = 0; frame < 64; ++frame) {
      const uint32_t at = record + frame * kActionFrameBytes;
      if (at + kActionFrameBytes + 4 > size || frame_done[at]) return;
      frame_done[at] = true;
      uint8_t* p = memory.data() + kRealTable + at;
      const uint16_t flags = Le16(p + 2);
      StoreBE16(p, 0, Le16(p));
      StoreBE16(p, 2, flags);
      StoreBE32(p, 4, Rebase(Le32(p + 4), disk_base));
      if (flags & 0x8000) {
        uint8_t* link = p + kActionFrameBytes;
        StoreBE32(link, 0, Rebase(Le32(link), disk_base));
        return;
      }
    }
  }

  bool Load(const std::vector<uint8_t>& file) {
    if (file.size() <= kRealFileHeader) return false;
    const uint32_t disk_base = Le32(file.data());
    size = uint32_t(file.size()) - kRealFileHeader;
    std::memcpy(memory.data() + kRealTable, file.data() + kRealFileHeader, size);
    uint8_t* table = memory.data() + kRealTable;
    const uint32_t header = Le16(table);
    std::vector<uint32_t> actions;
    for (uint32_t i = 0; i < header / 2; ++i) actions.push_back(Le16(table + i * 2));
    // Sub tables abut, so each one ends where the next begins; the last one ends at the first frame.
    std::vector<uint32_t> starts = actions;
    std::sort(starts.begin(), starts.end());
    starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
    uint32_t first_frame = size;
    for (const uint32_t subs : starts) first_frame = std::min<uint32_t>(first_frame, subs + Le16(table + subs));
    starts.push_back(first_frame);
    for (size_t i = 0; i + 1 < starts.size(); ++i) {
      for (uint32_t at = starts[i]; at + 2 <= starts[i + 1]; at += 2) {
        const uint16_t offset = Le16(table + at);
        ConvertFrames(starts[i] + offset, disk_base);
        StoreBE16(table, at, offset);
      }
    }
    for (uint32_t i = 0; i < header / 2; ++i) StoreBE16(table, i * 2, uint16_t(actions[i]));
    return true;
  }

  bool InsideRyu(uint32_t address) const { return address >= kRealTable && address < kRealTable + size; }
  bool InsideEvil(uint32_t address) const { return address >= kRealMemory && address < kRealMemory + kEvilActionBytes; }
};

void CheckRecordChain(RealTable& real, uint32_t record, const char* what) {
  Check(real.InsideEvil(record) || real.InsideRyu(record), what);
  for (int frame = 0; frame < 64; ++frame) {
    const uint32_t at = record + uint32_t(frame) * kActionFrameBytes;
    const uint32_t pose = LoadBE32(real.base(), at + 4);
    Check(real.InsideRyu(pose), "a pose pointer stays inside Ryu's data");
    if (LoadBE16(real.base(), at + 2) & 0x8000) {
      const uint32_t link = LoadBE32(real.base(), at + kActionFrameBytes);
      Check(link >= 0x10000, "no link holds a small integer");
      Check(real.InsideEvil(link) || real.InsideRyu(link), "a link stays inside known action data");
      return;
    }
  }
  Check(false, "the action has an end frame");
}

constexpr uint32_t kRyuFirstFrame = kRealTable + 0x1BC;

// The HD art of a Ryu frame is keyed by its offset from the first frame: the second column of Ryu.tsv under code ca00.
std::set<int> ReadSpriteKeys(const std::filesystem::path& path) {
  std::set<int> keys;
  std::ifstream in(path);
  std::string code;
  std::string rest;
  while (in >> code) {
    if (code == "ca00") {
      int key = 0;
      if (in >> key) keys.insert(key);
    }
    std::getline(in, rest);
  }
  return keys;
}

int SpriteKey(uint32_t record) { return int(int16_t(uint16_t(record - kRyuFirstFrame))); }

void CheckRecordArt(RealTable& real, const EvilActionTable& table, const std::set<int>& keys, uint32_t record) {
  for (int frame = 0; frame < 64; ++frame) {
    const uint32_t at = record + uint32_t(frame) * kActionFrameBytes;
    const uint32_t source = table.SourceFrame(at);
    if (real.InsideEvil(at)) {
      Check(source != 0, "every frame of the Evil Ryu copy knows its guest frame");
      Check(keys.count(SpriteKey(source)) == 1, "every Evil Ryu frame draws through existing Ryu HD art");
    } else {
      Check(source == 0, "an ordinary Ryu frame never enters the Evil Ryu path");
      Check(keys.count(SpriteKey(at)) == 1, "a shared Ryu frame keeps its art");
    }
    if (LoadBE16(real.base(), at + 2) & 0x8000) return;
  }
}

// Runs only on a machine with the user's game data: SSF2T_FIGHT_DATA holds pl00tbl*.bin and s_data00.bin, and
// SSF2T_GAME_DATA holds evil_ryu_sprites.bin. SSF2T_SPRITE_KEYS is Ryu.tsv. The data must never enter the repository.
void TestRealData() {
  std::set<int> sprite_keys;
  if (const char* keys = std::getenv("SSF2T_SPRITE_KEYS")) {
    sprite_keys = ReadSpriteKeys(keys);
    Check(sprite_keys.count(0) == 1 && sprite_keys.count(24) == 1, "the real sprite keys are frame offsets");
  }
  if (const char* folder = std::getenv("SSF2T_FIGHT_DATA")) {
    for (const char* name : {"pl00tbl0.bin", "pl00tbl1.bin", "pl00tbl2.bin", "pl00tbl3.bin"}) {
      const std::vector<uint8_t> file = ReadFile(std::filesystem::path(folder) / name);
      if (file.empty()) {
        Check(false, "a real Ryu table is readable");
        continue;
      }
      auto real = std::make_unique<RealTable>();
      Check(real->Load(file), "the real table converts");
      const std::vector<uint8_t> before(real->memory.begin() + kRealTable,
                                        real->memory.begin() + kRealTable + real->size);
      EvilActionTable table;
      Check(table.Build(real->base(), kRealTable, kRealMemory, kEvilActionBytes), "the real table builds");
      std::printf("real %s skipped=%d\n", name, table.skipped());
      Check(table.skipped() == 0, "every patch matches the real Ryu table");
      Check(std::memcmp(before.data(), real->memory.data() + kRealTable, before.size()) == 0,
            "the real Ryu table stays byte identical");
      {
        const std::vector<uint8_t> data = ReadFile(std::filesystem::path(folder) / "s_data00.bin");
        Check(data.size() > kRealFileHeader, "the real Ryu box pack is readable");
        if (data.size() > kRealFileHeader) {
          auto fake = std::make_unique<Fake>();
          uint8_t* pack = fake->base() + kPack;
          std::memcpy(pack, data.data() + kRealFileHeader, data.size() - kRealFileHeader);
          for (const uint32_t offset : {0u, 2u, 4u, 6u}) StoreBE16(pack, offset, Le16(data.data() + kRealFileHeader + offset));
          Check(BuildEvilPack(fake->base(), kPack, kPack + 0x2000) == 23, "every patch matches the real Ryu box pack");
        }
      }
      Check(GuestHasAction(real->base(), kRealTable, 0x54), "the real table has action 0x54");
      Check(!GuestHasAction(real->base(), kRealTable, kRagingDemonAction), "the real table has no action 0x5E");
      CheckRecordChain(*real, table.Teleport(), "the real teleport record is valid");
      struct Key {
        uint8_t action, sub, strong;
      };
      for (const Key key : std::initializer_list<Key>{
               {0x00, 0, 0}, {0x00, 1, 0}, {0x00, 2, 0}, {0x00, 3, 0}, {0x02, 0, 0}, {0x08, 0, 0}, {0x08, 1, 0},
               {0x08, 2, 0}, {0x08, 3, 0}, {0x0A, 0, 0}, {0x38, 0, 0}, {0x3A, 0, 0}, {0x40, 8, 0}, {0x42, 4, 0},
               {0x42, 5, 0}, {0x46, 0, 0}, {0x4A, 3, 0}, {0x4A, 5, 0}, {0x4C, 3, 0}, {0x4C, 4, 0}, {0x4C, 5, 0},
               {0x50, 0, 1}, {0x52, 0, 0}, {0x52, 1, 0}, {0x5E, 0, 0}, {0x5E, 1, 0}, {0x5E, 2, 0}}) {
        const uint32_t record = table.Find(key.action, key.sub, key.strong);
        Check(record != 0, "every Evil Ryu action resolves on the real table");
        if (record != 0) CheckRecordChain(*real, record, "a real Evil Ryu record is valid");
        if (record != 0 && !sprite_keys.empty()) CheckRecordArt(*real, table, sprite_keys, record);
      }
      if (!sprite_keys.empty()) CheckRecordArt(*real, table, sprite_keys, table.Teleport());
    }
  }
  if (!sprite_keys.empty()) {
    constexpr uint32_t kLoggedRecord = 0x30083A10;
    constexpr uint32_t kLoggedTable = 0x848E55D8;
    const uint16_t relocated = uint16_t(kLoggedRecord - (kLoggedTable + kRyuFirstFrame - kRealTable));
    Check(sprite_keys.count(relocated & 0x7FFF) == 0, "the copied idle record had no art, which hid the fighter");
  }
  if (const char* folder = std::getenv("SSF2T_GAME_DATA")) {
    const std::vector<uint8_t> file = ReadFile(std::filesystem::path(folder) / kEvilRyuSpriteFile);
    auto sprites = std::make_unique<EvilSprites>();
    Check(ParseEvilSprites(file.data(), file.size(), sprites.get()), "the real sprite file parses");
    const EvilSpriteImage& portrait = sprites->Get(EvilArt::kLargePortrait);
    const EvilSpriteImage& name = sprites->Get(EvilArt::kName);
    Check(portrait.width == 371 && portrait.height == 416 && portrait.has_palette, "the portrait matches ryu_large");
    Check(FillsTextureEdgeToEdge(portrait), "the Evil Ryu portrait is full bleed like the carrier's, no blank strip");
    const std::vector<uint8_t> violent_file = ReadFile(std::filesystem::path(folder) / "violent_ken_sprites.bin");
    if (!violent_file.empty()) {
      auto violent = std::make_unique<EvilSprites>();
      Check(ParseEvilSprites(violent_file.data(), violent_file.size(), violent.get()), "the Violent Ken sprite file parses");
      for (const EvilArt art : {EvilArt::kLargePortrait, EvilArt::kLosePortrait}) {
        const EvilSpriteImage& image = violent->Get(art);
        Check(image.width == portrait.width && image.height == portrait.height, "Violent Ken's portraits match Ken's texture");
        Check(FillsTextureEdgeToEdge(image), "the Violent Ken portraits are full bleed like the carrier's");
      }
    }
    Check(name.width == 384 && name.height == 64 && name.has_palette, "the name is 4x its 96x16 quad");
    Check(sprites->Get(EvilArt::kHudName).width == 360, "the HUD name is 4x its 90 wide quad");
    constexpr uint32_t kGuestPitch = 512;
    std::vector<uint8_t> surface(size_t(kGuestPitch) * portrait.height, 0);
    WriteIndexedRows(surface.data(), kGuestPitch, portrait, true);
    int matching = 0;
    for (int y = 0; y < portrait.height; ++y) {
      matching += surface[size_t(y) * kGuestPitch] == portrait.indices[size_t(y) * portrait.width + portrait.width - 1];
    }
    Check(matching == portrait.height, "each mirrored row starts with the art's right edge");
    std::array<uint8_t, kEvilPaletteBytes> palette = portrait.palette;
    Check(palette[0] == 0xFF && palette[1] == 0xFF && palette[2] == 0 && palette[3] == 0xFF,
          "the portrait palette holds the magenta key");
    ClearPaletteColourKey(palette.data(), palette.size());
    Check(palette[0] == 0 && palette[3] == 0, "the key the guest would show as magenta is transparent");
  }
}

}  // namespace

int main() {
  TestActions();
  TestPack();
  TestWalk();
  TestSprites();
  TestPhysicalFighter();
  TestSetFighterAnimation();
  TestSourceFrames();
  TestUploadLayout();
  TestRealData();
  if (failures == 0) std::printf("evil ryu actions tests passed\n");
  return failures == 0 ? 0 : 1;
}
