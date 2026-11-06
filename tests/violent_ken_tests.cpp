#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "character/evil_ryu_moves.h"
#include "character/fighter_identity.h"
#include "character/secret_select.h"
#include "character/ultra_gameplay.h"
#include "character/ultra_navigation.h"
#include "character/ultra_select_presentation.h"
#include "character/violent_ken_actions.h"
#include "character/violent_ken_data.h"
#include "guest_memory.h"
#include "online/secret_characters.h"

namespace {

using namespace ssf2t;
using namespace ssf2t::character;

int failures = 0;

void Check(bool condition, const char* what) {
  if (condition) return;
  std::printf("FAIL %s\n", what);
  ++failures;
}

constexpr uint8_t kBottomEnd = kViolentKenNeighbourLive;
constexpr uint8_t kTopEnd = kEvilRyuNeighbourLive;

void TestIdentity() {
  ResetIdentities();
  Check(ResolveFighterIdentity(kKenGuestCharacter, false, true, FighterIdentity::kGuest).identity == FighterIdentity::kKen,
        "the Ken carrier alone is Ken");
  Check(ResolveFighterIdentity(kKenGuestCharacter, false, true, FighterIdentity::kViolentKen).identity ==
            FighterIdentity::kViolentKen,
        "a Violent Ken request on the carrier is Violent Ken");
  Check(ResolveFighterIdentity(kKenGuestCharacter, false, false, FighterIdentity::kViolentKen).identity ==
            FighterIdentity::kKen,
        "the option Off keeps Ken");
  Check(ResolveFighterIdentity(kRyuGuestCharacter, false, true, FighterIdentity::kViolentKen).identity ==
            FighterIdentity::kRyu,
        "a Violent Ken request does not touch Ryu");
  Check(HudShowsViolentKen(true, FighterIdentity::kViolentKen, kKenGuestCharacter), "HUD shows Violent Ken");
  Check(!HudShowsViolentKen(true, FighterIdentity::kGuest, kKenGuestCharacter), "HUD keeps Ken");
  Check(!HudShowsEvilRyu(true, FighterIdentity::kViolentKen, kRyuGuestCharacter), "HUD identities never mix");
  Check(!CanStartShinkuHadoken(FighterIdentity::kViolentKen), "the guest's Shoryureppa start is refused");
  Check(CanStartShinkuHadoken(FighterIdentity::kKen), "Ken keeps his super");
}

void TestNavigation() {
  ResetIdentities();
  UltraNavigation nav;
  Check(nav.Resolve(false, false, 0, kBottomEnd, PadDirection::kRight).action == UltraMoveAction::kNone,
        "Off: the bottom row stays with the guest");
  const UltraMove enter = nav.Resolve(true, true, 0, kBottomEnd, PadDirection::kRight);
  Check(enter.action == UltraMoveAction::kEnter && enter.live == kKenGuestCharacter, "Right from id 8 enters on Ken");
  Check(nav.OnViolentKen(0) && !nav.OnEvilRyu(0) && !nav.OnViolentKen(1), "P1 alone is on Violent Ken");
  Check(RequestedIdentity(0) == FighterIdentity::kViolentKen, "P1 requests Violent Ken");

  const UltraMove up = nav.Resolve(true, true, 0, kKenGuestCharacter, PadDirection::kUp);
  Check(up.action == UltraMoveAction::kSwitch && up.live == kRyuGuestCharacter, "Up moves to Evil Ryu on the Ryu carrier");
  Check(nav.OnEvilRyu(0) && !nav.OnViolentKen(0), "Up leaves Violent Ken");
  const UltraMove down = nav.Resolve(true, true, 0, kRyuGuestCharacter, PadDirection::kDown);
  Check(down.action == UltraMoveAction::kSwitch && down.live == kKenGuestCharacter, "Down moves back");
  const UltraMove left = nav.Resolve(true, true, 0, kKenGuestCharacter, PadDirection::kLeft);
  Check(left.action == UltraMoveAction::kLeave && left.live == kBottomEnd, "Left returns to id 8");
  Check(!nav.OnUltraCell(0) && RequestedIdentity(0) == FighterIdentity::kGuest, "Left clears the request");

  Check(nav.Resolve(true, false, 0, kBottomEnd, PadDirection::kRight).action == UltraMoveAction::kNone,
        "Violent Ken unavailable: the guest keeps Right from id 8");
  Check(nav.Resolve(false, true, 0, kTopEnd, PadDirection::kRight).action == UltraMoveAction::kNone,
        "Evil Ryu unavailable: the guest keeps Right from id 9");
  nav.Resolve(true, false, 0, kTopEnd, PadDirection::kRight);
  Check(nav.Resolve(true, false, 0, kRyuGuestCharacter, PadDirection::kDown).action == UltraMoveAction::kHold,
        "Down holds when Violent Ken is unavailable");
  nav.Reset();
  nav.Resolve(false, true, 0, kBottomEnd, PadDirection::kRight);
  Check(nav.Resolve(false, true, 0, kKenGuestCharacter, PadDirection::kUp).action == UltraMoveAction::kHold,
        "Up holds when Evil Ryu is unavailable");
  nav.Reset();

  nav.Resolve(true, true, 0, kTopEnd, PadDirection::kRight);
  nav.Resolve(true, true, 1, kBottomEnd, PadDirection::kRight);
  Check(nav.OnEvilRyu(0) && nav.OnViolentKen(1), "the sides are independent");
  Check(RequestedIdentity(0) == FighterIdentity::kEvilRyu && RequestedIdentity(1) == FighterIdentity::kViolentKen,
        "both requests stand");

  const uint8_t saved = nav.Mask();
  nav.Reset();
  nav.Restore(saved);
  Check(nav.OnEvilRyu(0) && nav.OnViolentKen(1), "rollback restores each cell");
  Check(RequestedIdentity(1) == FighterIdentity::kViolentKen, "rollback restores the request");

  nav.Resolve(false, false, 1, kKenGuestCharacter, PadDirection::kLeft);
  Check(!nav.OnViolentKen(1), "losing availability clears the cell");
  nav.Reset();
}

void TestNegotiation() {
  using online::SecretCapabilities;
  using online::SecretCharacter;
  using online::SecretSet;
  const SecretCapabilities on{{true, true, true}, {true, true, true}};
  const SecretCapabilities off{{true, true, true}, {true, true, false}};
  const SecretCapabilities old_peer{{true, true, false}, {true, true, false}};
  Check(online::EffectiveSecrets(on, on).violent_ken, "On/On is available");
  Check(!online::EffectiveSecrets(on, off).violent_ken, "On/Off is unavailable");
  Check(!online::EffectiveSecrets(off, on).violent_ken, "Off/On is unavailable");
  Check(!online::EffectiveSecrets(off, off).violent_ken, "Off/Off is unavailable");
  Check(!online::EffectiveSecrets(on, old_peer).violent_ken, "an old peer is unavailable");
  Check(online::EffectiveSecrets(on, old_peer).evil_ryu, "Evil Ryu stays available beside an old peer");
  Check(online::SecretStatusOf(SecretCharacter::kViolentKen, on, old_peer) ==
            online::SecretStatus::kUnsupportedByOpponent,
        "an old peer reads as unsupported");
  Check(online::SecretStatusOf(SecretCharacter::kViolentKen, on, off) == online::SecretStatus::kDisabledByOpponent,
        "Off reads as disabled by the opponent");
  const SecretSet all = SecretSet::FromBits(7);
  Check(all.akuma && all.evil_ryu && all.violent_ken && all.Bits() == 7, "setup bits round trip");
  Check(SecretSet::FromBits(4).Bits() == 4, "bit 2 alone is Violent Ken");
  Check(online::SecretKey(SecretCharacter::kViolentKen) == std::string("violent_ken"), "wire key");
  Check(online::SecretLabel(SecretCharacter::kViolentKen) == std::string("Violent Ken"), "label");

  const SecretSet none{};
  Check(ValidateSecretSelection(kKenGuestCharacter, FighterIdentity::kViolentKen, none) ==
            SelectionVerdict::kViolentKen,
        "an unnegotiated Violent Ken is refused");
  Check(ValidateSecretSelection(kKenGuestCharacter, FighterIdentity::kGuest, none) == SelectionVerdict::kAccepted,
        "Ken is always accepted");
  Check(ValidateSecretSelection(kKenGuestCharacter, FighterIdentity::kViolentKen, all) == SelectionVerdict::kAccepted,
        "a negotiated Violent Ken is accepted");
  Check(ReportsViolentKen(kKenGuestCharacter, FighterIdentity::kViolentKen, true), "telemetry names Violent Ken");
  Check(!ReportsViolentKen(kKenGuestCharacter, FighterIdentity::kGuest, true), "telemetry keeps Ken");
  Check(!ReportsViolentKen(kKenGuestCharacter, FighterIdentity::kViolentKen, false), "telemetry needs the negotiation");
}

void Motion(RasetsuRecognizer& recognizer, std::initializer_list<uint8_t> nibbles, int& result, uint16_t kick) {
  for (uint8_t nibble : nibbles) result = recognizer.Update(nibble, 0, 0);
  result = recognizer.Update(0, kick, 0);
}

void TestMoves() {
  RasetsuRecognizer rasetsu;
  int result = -1;
  Motion(rasetsu, {2, 4, 6}, result, kLightKick);
  Check(result == 0, "623 LK is the light Rasetsukyaku");
  Motion(rasetsu, {2, 4, 6}, result, kMediumKick);
  Check(result == 1, "623 MK is the medium Rasetsukyaku");
  Motion(rasetsu, {2, 4, 6}, result, kHeavyKick);
  Check(result == 2, "623 HK is the heavy Rasetsukyaku");
  Motion(rasetsu, {2, 4, 6}, result, 0x40);
  Check(result == -1, "623 with a punch is not Rasetsukyaku");
  rasetsu.Reset();
  Motion(rasetsu, {4, 6, 2}, result, kHeavyKick);
  Check(result == -1, "236 K is not Rasetsukyaku");
  rasetsu.Reset();
  Check(rasetsu.Update(2, 0, 0) == -1, "motion start");
  rasetsu.Update(4, 0, 0);
  rasetsu.Update(6, 0, 0);
  for (int i = 0; i < 20; ++i) rasetsu.Update(0, 0, 0);
  Check(rasetsu.Update(0, kLightKick, 0) == -1, "the motion expires");

  ShinbuRecognizer shinbu;
  for (uint8_t nibble : {4, 6, 2, 4, 6, 2}) Check(!shinbu.Update(nibble, 0, 0), "no super during the motion");
  Check(shinbu.Update(2, kLightKick, 0), "236236 K is Shinbu Messatsu");
  for (uint8_t nibble : {4, 6, 2, 4, 6, 2}) shinbu.Update(nibble, 0, 0);
  Check(!shinbu.Update(2, 0x10, 0), "236236 P is not Shinbu Messatsu");

  Check(InazumaInput(2, kMediumKick, 0), "forward MK is the Inazuma Kick");
  Check(!InazumaInput(0, kMediumKick, 0), "neutral MK stays a normal");
  Check(!InazumaInput(2, kHeavyKick, 0), "forward HK stays a normal");
  Check(!InazumaInput(2, kMediumKick, kMediumKick), "a held MK does not repeat");

  const RasetsuDash dash = RasetsuDashFor(2, true);
  Check(dash.velocity == 0xA00 && dash.deceleration == 0x24 && dash.threshold == 0x740, "heavy Rasetsu table");
  Check(RasetsuDashFor(0, false).velocity == -0x680, "the dash follows the facing");
  Check(DecayTowardZero(10, 4) == 6 && DecayTowardZero(3, 4) == 0, "speed decays to zero");

  HostMove move;
  move.kind = HostMoveKind::kRasetsu;
  move.phase = HostMovePhase::kDash;
  Check(RenderStateFor(move, true, false) == EvilRenderState::kTeleportGhost, "the dash is drawn as a ghost");
  Check(RenderStateFor(move, false, false) == EvilRenderState::kNormal, "out of the move the body is normal");
}

void TestRemovedMoves() {
  for (uint8_t id : {kShoryureppaMoveId, uint8_t(8), uint8_t(10), uint8_t(12)}) {
    Check(ViolentMoveRemoved(id), "Shoryureppa and the kick specials cannot run");
  }
  for (uint8_t id : {uint8_t(0), uint8_t(2), uint8_t(4), uint8_t(14)}) {
    Check(!ViolentMoveRemoved(id), "Hadoken, Tatsumaki, Shoryuken and host moves stay");
  }
  Check(ViolentCpuSpecialRemoved(8) && ViolentCpuSpecialRemoved(10), "the CPU loses the same two specials");
  for (uint8_t id : {uint8_t(0), uint8_t(2), uint8_t(4), uint8_t(6), uint8_t(12), uint8_t(14), uint8_t(16)}) {
    Check(!ViolentCpuSpecialRemoved(id), "the other CPU specials stay");
  }
}

void TestProvenMovement() {
  Check(kViolentShoryukenVelocity[0] == 0x400 && kViolentShoryukenVelocity[1] == 0x900 &&
            kViolentShoryukenVelocity[2] == 0xC00,
        "USF2 0xb9c47c: Shoryuken vx 4, 9, 12");
  Check(kViolentAirTatsuVelocity[0] == 0x280 && kViolentAirTatsuVelocity[1] == 0x320 &&
            kViolentAirTatsuVelocity[2] == 0x3C0,
        "USF2 0xb9c470: air Tatsumaki vx 2.5, 3.125, 3.75");
  Check(IsViolentAirTatsu(0x50, 5) && IsViolentAirTatsu(0x50, 7), "air Tatsumaki subs");
  Check(!IsViolentAirTatsu(0x50, 4) && !IsViolentAirTatsu(0x50, 8) && !IsViolentAirTatsu(0x4E, 5),
        "ground Tatsumaki and landing stay guest");
  Check(kShinbuRushVelocity == 0x600 && kShinbuRushDeceleration == 0x28, "rush 6.0, -0.15625");
  Check(kShinbuRiseVelocityX == 0xC00 && kShinbuRiseVelocityY == 0xA00, "rise 12, 10");
  Check(kShinbuRiseDecelerationX == 0x78 && kShinbuRiseDecelerationY == 0x60, "rise -0.46875, -0.375");
  Check(kShinbuRecoverySub == 4, "anim 63 follows the landing");
  Check(kViolentWalkForward == 0x400 && kViolentWalkBack == 0x300, "USF2 0xb9c420: walk 4.0, 3.0");
  Check(ViolentJumpRow(0x280).vy == 0xB00 && ViolentJumpRow(0x280).vx == 0x380 && ViolentJumpRow(0x280).ax == 5,
        "forward jump 3.5, 11.0");
  Check(ViolentJumpRow(-0x200).vy == 0xB40 && ViolentJumpRow(-0x200).vx == -0x398 && ViolentJumpRow(-0x200).ax == -5,
        "back jump -3.59375, 11.25");
  Check(ViolentJumpRow(0).vx == 0 && ViolentJumpRow(0).vy == 0xB20, "neutral jump 11.125");
  Check(kViolentJumpForward.ay == 0x90 && kViolentJumpBack.ay == 0x90 && kViolentJumpNeutral.ay == 0x90,
        "gravity 0.5625");
  Check(HostMoves::kBytes == 2 * sizeof(HostMove), "the rollback blob covers every host field");
}

void TestPresentation() {
  const std::array<FighterIdentity, 2> mixed = {FighterIdentity::kEvilRyu, FighterIdentity::kViolentKen};
  const SelectPresentation both =
      ResolveSelectPresentation(mixed, {true, true}, {uint32_t(kRyuGuestCharacter), uint32_t(kKenGuestCharacter)});
  Check(both.evil[0] && !both.violent[0] && both.violent[1] && !both.evil[1], "each side keeps its own portrait");
  const SelectPresentation ken = ResolveSelectPresentation({FighterIdentity::kGuest, FighterIdentity::kViolentKen},
                                                           {false, true},
                                                           {uint32_t(kKenGuestCharacter), uint32_t(kKenGuestCharacter)});
  Check(!ken.violent[0] && ken.violent[1], "Ken beside Violent Ken stays Ken");
  const SelectPresentation slot = ResolveSelectPresentation({FighterIdentity::kGuest, FighterIdentity::kViolentKen},
                                                            {false, true}, {kEvilPortraitSlot, uint32_t(kKenGuestCharacter)});
  Check(slot.hide[0], "a side not on the cell never shares the slot");
  Check(KenNameSide(kKenNameFirstTile, kNameTileColour, 10) == 0, "name tile on the left is P1");
  Check(KenNameSide(kKenNameLastTile, kNameTileColour, 300) == 1, "name tile on the right is P2");
  Check(KenNameSide(kKenHudNameTile, kHudNameTileColour, 300) == 1, "HUD name tile");
  Check(KenNameSide(kRyuNameFirstTile, kNameTileColour, 10) == -1, "Ryu's tiles are not Ken's");
  Check(RyuNameSide(kKenNameFirstTile, kNameTileColour, 10) == -1, "Ken's tiles are not Ryu's");
}

std::vector<uint8_t> SampleGameplayPackage() {
  std::vector<uint8_t> file;
  const auto put = [&](std::initializer_list<int> bytes) {
    for (int b : bytes) file.push_back(uint8_t(b));
  };
  file.insert(file.end(), {'S', 'S', 'F', '2', 'V', 'K', 'G', '1'});
  put({1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0});
  put({kRasetsuAction, 1, 2, 0});
  put({2, 0, 7, 0, 24, 0, 1, 2, 3, 4, 5, 6, 150, 1});
  put({3, 0, 0, 0, 48, 0, 9, 8, 7, 0, 1, 27, 150, 0});
  put({0x10, 0, 0, 1, 4, 2, 0, 0});
  put({74, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});
  return file;
}

void TestPackage() {
  const std::vector<uint8_t> file = SampleGameplayPackage();
  ViolentKenGameplay gameplay;
  Check(ParseViolentKenGameplay(file.data(), file.size(), &gameplay), "the package parses");
  Check(gameplay.actions.size() == 1 && gameplay.actions[0].frames.size() == 2, "action count");
  Check(gameplay.actions[0].frames[0].event == 7 && gameplay.actions[0].frames[0].stance_art, "frame fields");
  Check(gameplay.patches[0].field == PackagedField::kHead && gameplay.patches[0].value == 2, "patch fields");
  Check(gameplay.attacks[0].id == 74 && gameplay.attacks[0].record[15] == 16, "attack fields");
  Check(!ParseViolentKenGameplay(file.data(), file.size() - 1, &gameplay), "a short file is refused");
  std::vector<uint8_t> bad = file;
  bad[0] = 'X';
  Check(!ParseViolentKenGameplay(bad.data(), bad.size(), &gameplay), "a wrong magic is refused");
}

// Action header, one sub table and frames laid out as the guest keeps them, big endian.
constexpr uint32_t kTable = 0x1000;
constexpr uint32_t kCopy = 0x40000;
constexpr uint32_t kPack = 0x60000;
constexpr uint32_t kPackCopy = 0x70000;
constexpr uint32_t kFirstFrame = kTable + 0x20;

void PutFrame(uint8_t* base, uint32_t at, uint16_t duration, uint16_t flags, uint8_t head) {
  StoreBE16(base + at, 0, duration);
  StoreBE16(base + at, 2, flags);
  StoreBE32(base + at, 4, 0x8C000000u + at);
  base[at + 8] = head;
}

void BuildGuest(uint8_t* base) {
  StoreBE16(base + kTable, 0, 4);
  StoreBE16(base + kTable, 2, 0x10);
  StoreBE16(base + kTable + 4, 0, uint16_t(kFirstFrame - (kTable + 4)));
  StoreBE16(base + kTable + 0x10, 0, 0x10);
  StoreBE16(base + kTable + 0x10, 2, uint16_t(0x10 + 2 * kActionFrameBytes + 4));
  PutFrame(base, kFirstFrame, 4, 0, 1);
  PutFrame(base, kFirstFrame + kActionFrameBytes, 6, 0x8000, 37);
  StoreBE32(base + kFirstFrame + 2 * kActionFrameBytes, 0, kFirstFrame);
  PutFrame(base, kFirstFrame + 2 * kActionFrameBytes + 4, 9, 0x8000, 5);
}

void TestActionTable() {
  auto memory = std::make_unique<uint8_t[]>(0x100000);
  uint8_t* base = memory.get();
  std::memset(base, 0, 0x100000);
  BuildGuest(base);
  Check(FirstActionFrame(base, kTable) == kFirstFrame, "the first frame is where art keys start");

  ViolentKenGameplay gameplay;
  PackagedFrame stance;
  stance.duration = 2;
  stance.source_key = uint16_t(kActionFrameBytes);
  stance.head = 11;
  stance.stance_art = true;
  gameplay.actions.push_back({kRasetsuAction, 0, {stance}});
  gameplay.patches.push_back({0x02, 0, 1, PackagedField::kHead, 37, 0});
  gameplay.patches.push_back({0x02, 1, 0, PackagedField::kDuration, 99, 1});

  const std::vector<uint8_t> before(base + kTable, base + kTable + 0x100);
  ViolentActionTable table;
  Check(table.Build(base, kTable, kCopy, kViolentActionBytes, gameplay), "the table builds");
  Check(std::memcmp(before.data(), base + kTable, before.size()) == 0, "guest Ken data is never written");

  const uint32_t rasetsu = table.Find(kRasetsuAction, 0);
  Check(rasetsu != 0, "a host action is found");
  Check(table.SourceFrame(rasetsu) == kFirstFrame + kActionFrameBytes, "a host frame draws its source frame");
  Check(table.StanceAt(rasetsu), "the stance frame is remembered");
  Check(LoadBE16(base + rasetsu, 2) == 0x8000, "a one frame action ends on it");

  const uint32_t patched = table.Find(0x02, 0);
  Check(patched != 0, "a matching patch copies the action");
  Check(base[patched + kActionFrameBytes + 8] == 0, "the Shoryuken frame loses its hurt box");
  Check(base[patched + 8] == 1, "the other frames keep theirs");
  Check(LoadBE32(base + patched + 2 * kActionFrameBytes, 0) == patched, "the link follows the copy");
  Check(table.SourceFrame(patched + kActionFrameBytes) == kFirstFrame + kActionFrameBytes, "copies keep their art key");
  Check(table.Find(0x02, 1) == 0 && table.skipped() == 1, "a mismatched patch leaves the guest action alone");

  StoreBE16(base + kPack, 6, 0x100);
  StoreBE16(base + kPack, 8, uint16_t(0x100 + 74 * kAttackRecordBytes));
  base[kPack + 0x100 + 3 * kAttackRecordBytes] = 0x55;
  PackagedAttack added;
  added.id = 74;
  added.record[0] = 0x77;
  PackagedAttack changed;
  changed.id = 2;
  changed.replace = true;
  changed.record[0] = 0x66;
  gameplay.attacks = {added, changed};
  Check(BuildViolentPack(base, kPack, kPackCopy, gameplay) == 2, "attacks are applied");
  const uint32_t attacks = kPackCopy + LoadBE16(base + kPackCopy, 6);
  Check(attacks == kPackCopy + 0x1200, "the attack table moves past the copy");
  Check(base[attacks + 74 * kAttackRecordBytes] == 0x77, "a new attack is appended");
  Check(base[attacks + 2 * kAttackRecordBytes] == 0x66, "a changed attack is replaced");
  Check(base[attacks + 3 * kAttackRecordBytes] == 0x55, "other attacks stay");
  Check(base[kPack + 0x100 + 2 * kAttackRecordBytes] == 0, "the guest pack is never written");
}

void TestRealPackage() {
  const char* folder = std::getenv("SSF2T_GAME_DATA");
  if (!folder) return;
  std::ifstream in(std::filesystem::path(folder) / kViolentKenGameplayFile, std::ios::binary);
  if (!in) return;
  std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  ViolentKenGameplay gameplay;
  Check(ParseViolentKenGameplay(file.data(), file.size(), &gameplay), "the generated package parses");
  bool rasetsu = false, shinbu = false, inazuma = false, stance = false;
  for (const PackagedAction& action : gameplay.actions) {
    rasetsu = rasetsu || action.action == kRasetsuAction;
    shinbu = shinbu || action.action == kShinbuAction;
    inazuma = inazuma || action.action == kInazumaAction;
    for (const PackagedFrame& frame : action.frames) stance = stance || frame.stance_art;
  }
  Check(rasetsu && shinbu && inazuma && stance, "the generated package holds the unique moves and the stance");
  bool true_push = false;
  for (const PackagedAction& action : gameplay.actions) {
    if (action.action == 0x42 && action.sub == 4 && action.frames.size() >= 3) {
      true_push = action.frames[0].push == 8 && action.frames[1].push == 9 && action.frames[2].push == 10;
    }
  }
  Check(true_push, "close HK keeps USF2 push ids 8, 9, 10");
  int classes = 0;
  for (const PackagedAttack& attack : gameplay.attacks) {
    Check(attack.record[11] <= 0x50 && (attack.record[11] & 1) == 0, "attack byte 11 is a guest move class");
    if (attack.id >= 41 && attack.id <= 44 && (attack.record[11] == 0x4E || attack.record[11] == 0x48)) ++classes;
  }
  Check(classes == 4, "the Shoryuken records keep USF2 classes 0x4E and 0x48");
}

}  // namespace

int main() {
  TestIdentity();
  TestNavigation();
  TestNegotiation();
  TestMoves();
  TestRemovedMoves();
  TestProvenMovement();
  TestPresentation();
  TestPackage();
  TestActionTable();
  TestRealPackage();
  if (failures == 0) std::printf("violent ken tests passed\n");
  return failures == 0 ? 0 : 1;
}
