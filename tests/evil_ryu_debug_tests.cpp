#include <cstdio>
#include <cstring>

#include "character/evil_ryu_debug.h"
#include "character/evil_ryu_moves.h"

namespace {

using namespace ssf2t::character;

int failures = 0;

void Check(bool condition, const char* what) {
  if (condition) return;
  std::printf("FAIL %s\n", what);
  ++failures;
}

constexpr uint16_t kPunches = 0x70;
constexpr uint16_t kKicks = 0x700;

int Motion(TeleportRecognizer& r, uint8_t first, uint8_t third, uint16_t buttons) {
  r.Update(first, 0, 0);
  r.Update(0, 0, 0);
  r.Update(kNibbleDown, 0, 0);
  r.Update(0, 0, 0);
  r.Update(third, 0, 0);
  return r.Update(third, buttons, 0);
}

DebugFightStatus Fight(bool online = false, DebugFighter p1 = DebugFighter::kEvilRyu, bool ticking = true) {
  DebugFightStatus status;
  status.ticking = ticking;
  status.online = online;
  status.p1 = p1;
  return status;
}

void TestMapping() {
  Check(EvilDebugMoveFor(DebugKey::kF5, false, false) == DebugMove::kTeleportForwardFar, "F5 is the forward far teleport");
  Check(EvilDebugMoveFor(DebugKey::kF6, false, false) == DebugMove::kTeleportBackwardFar, "F6 is the backward far teleport");
  Check(EvilDebugMoveFor(DebugKey::kF7, false, false) == DebugMove::kSenpuKyaku, "F7 is Senpu Kyaku");
  Check(EvilDebugMoveFor(DebugKey::kF8, false, false) == DebugMove::kRagingDemon, "F8 is Shun Goku Satsu");
  Check(EvilDebugMoveFor(DebugKey::kF5, true, false) == DebugMove::kTeleportForwardNear, "Shift+F5 is the forward near teleport");
  Check(EvilDebugMoveFor(DebugKey::kF6, true, false) == DebugMove::kTeleportBackwardNear, "Shift+F6 is the backward near teleport");
  Check(EvilDebugMoveFor(DebugKey::kF7, true, false) == DebugMove::kNone, "Shift+F7 has no move");
  Check(EvilDebugMoveFor(DebugKey::kF8, true, false) == DebugMove::kNone, "Shift+F8 has no move");
  Check(EvilDebugMoveFor(DebugKey::kNone, false, false) == DebugMove::kNone, "other keys have no move");
  Check(std::strcmp(DebugKeyLabel(DebugMove::kTeleportForwardFar), "F5") == 0, "the F5 label");
  Check(std::strcmp(DebugMoveName(DebugMove::kTeleportBackwardFar), "teleport-backward-far") == 0, "the F6 move name");
  Check(std::strcmp(DebugMoveName(DebugMove::kSenpuKyaku), "senpu-kyaku") == 0, "the F7 move name");
  Check(std::strcmp(DebugMoveName(DebugMove::kRagingDemon), "raging-demon") == 0, "the F8 move name");
  Check(std::strcmp(DebugRejectName(DebugReject::kWrongFighter), "wrong-fighter") == 0, "the wrong-fighter reason");
  Check(std::strcmp(DebugRejectName(DebugReject::kNotInFight), "not-in-fight") == 0, "the not-in-fight reason");
}

void TestFighterRouting() {
  using F = DebugFighter;
  using K = DebugKey;
  using M = DebugMove;
  Check(DebugMoveFor(F::kEvilRyu, K::kF4, false, false) == M::kShakunetsu, "Evil Ryu F4");
  Check(DebugMoveFor(F::kEvilRyu, K::kF5, false, false) == M::kTeleportForwardFar, "Evil Ryu F5");
  Check(DebugMoveFor(F::kEvilRyu, K::kF5, true, false) == M::kTeleportForwardNear, "Evil Ryu Shift+F5");
  Check(DebugMoveFor(F::kEvilRyu, K::kF6, false, false) == M::kTeleportBackwardFar, "Evil Ryu F6");
  Check(DebugMoveFor(F::kEvilRyu, K::kF6, true, false) == M::kTeleportBackwardNear, "Evil Ryu Shift+F6");
  Check(DebugMoveFor(F::kEvilRyu, K::kF7, false, false) == M::kSenpuKyaku, "Evil Ryu F7");
  Check(DebugMoveFor(F::kEvilRyu, K::kF8, false, false) == M::kRagingDemon, "Evil Ryu F8");
  Check(DebugMoveFor(F::kEvilRyu, K::kF4, false, true) == M::kNone, "Evil Ryu has no Ctrl moves");

  Check(DebugMoveFor(F::kViolentKen, K::kF4, false, false) == M::kRasetsuLight, "Violent Ken F4");
  Check(DebugMoveFor(F::kViolentKen, K::kF4, true, false) == M::kRasetsuMedium, "Violent Ken Shift+F4");
  Check(DebugMoveFor(F::kViolentKen, K::kF4, false, true) == M::kRasetsuHeavy, "Violent Ken Ctrl+F4");
  Check(DebugMoveFor(F::kViolentKen, K::kF4, true, true) == M::kNone, "Shift+Ctrl+F4 is nothing");
  Check(DebugMoveFor(F::kViolentKen, K::kF5, false, false) == M::kInazuma, "Violent Ken F5");
  Check(DebugMoveFor(F::kViolentKen, K::kF6, false, false) == M::kShinbu, "Violent Ken F6");
  Check(DebugMoveFor(F::kViolentKen, K::kF7, false, false) == M::kShoryukenHeavy, "Violent Ken F7");
  Check(DebugMoveFor(F::kViolentKen, K::kF8, false, false) == M::kAirTatsumaki, "Violent Ken F8");
  Check(DebugMoveFor(F::kViolentKen, K::kF5, true, false) == M::kNone, "only F4 takes modifiers for Violent Ken");

  for (const K key : {K::kF4, K::kF5, K::kF6, K::kF7, K::kF8}) {
    Check(DebugMoveFor(F::kNone, key, false, false) == M::kNone, "normal Ryu, normal Ken and Akuma have no moves");
    Check(DebugMoveFor(F::kNone, key, true, false) == M::kNone, "not with Shift either");
    Check(DebugMoveFor(F::kNone, key, false, true) == M::kNone, "nor with Ctrl");
  }
  Check(RasetsuStrengthFor(M::kRasetsuLight) == 0 && RasetsuStrengthFor(M::kRasetsuMedium) == 1 &&
            RasetsuStrengthFor(M::kRasetsuHeavy) == 2,
        "the keys carry the real strength");
  Check(RasetsuStrengthFor(M::kInazuma) == -1, "other moves have no strength");
  Check(IsViolentDebugMove(M::kRasetsuLight) && IsViolentDebugMove(M::kAirTatsumaki), "Violent Ken's moves");
  Check(!IsViolentDebugMove(M::kShakunetsu) && !IsViolentDebugMove(M::kRagingDemon), "Evil Ryu's moves");

  Check(HostGate(true, false, Fight(false, F::kViolentKen)) == DebugReject::kNone, "Violent Ken P1 is accepted");
  Check(HostGate(true, false, Fight(true, F::kViolentKen)) == DebugReject::kOnline, "online rejects Violent Ken");
  Check(HostGate(true, false, Fight(false, F::kNone)) == DebugReject::kWrongFighter, "a P1 with no debug moves rejects");
  Check(std::strcmp(DebugRejectName(DebugReject::kNotAirborne), "not-airborne") == 0, "the not-airborne reason");
  Check(std::strcmp(DebugMoveName(M::kRasetsuLight), "rasetsukyaku-lk") == 0, "the F4 move name");
  Check(std::strcmp(DebugMoveName(M::kShinbu), "shinbu-messatsu") == 0, "the F6 move name");
  Check(std::strcmp(DebugKeyLabel(M::kRasetsuHeavy), "Ctrl+F4") == 0, "the Ctrl+F4 label");
  Check(std::strcmp(DebugKeyLabel(M::kShinbu), "F6") == 0, "the F6 label");
  Check(DebugMoveForPress(F::kViolentKen, K::kF6, false, false, true) == M::kNone, "a held key does not repeat");

  Check(MirrorDirections(1) == 2 && MirrorDirections(2) == 1, "back and forward swap");
  Check(MirrorDirections(5) == 6 && MirrorDirections(6) == 5, "the diagonals swap");
  Check(MirrorDirections(4) == 4 && MirrorDirections(0) == 0, "down and neutral stay");
  Check(MirrorDirections(0x72) == 0x71, "buttons are untouched");
  constexpr uint16_t kUsf2HalfCircle[] = {2, 6, 4, 5, 1};
  constexpr uint16_t kGuestChain[] = {1, 5, 4, 6, 2};
  for (int step = 0; step < 5; ++step) {
    Check(MirrorDirections(kUsf2HalfCircle[step]) == kGuestChain[step], "63214 reaches the guest chain as its own order");
  }
}

void TestEdge() {
  Check(DebugMoveForPress(DebugFighter::kEvilRyu, DebugKey::kF5, false, false, false) == DebugMove::kTeleportForwardFar, "a fresh press activates");
  Check(DebugMoveForPress(DebugFighter::kEvilRyu, DebugKey::kF5, false, false, true) == DebugMove::kNone, "a held key does not repeat");
  int activations = 0;
  bool down = false;
  for (int event = 0; event < 30; ++event) {
    if (DebugMoveForPress(DebugFighter::kEvilRyu, DebugKey::kF8, false, false, down) != DebugMove::kNone) ++activations;
    down = true;
  }
  Check(activations == 1, "one physical press is one activation");
  down = false;
  if (DebugMoveForPress(DebugFighter::kEvilRyu, DebugKey::kF8, false, false, down) != DebugMove::kNone) ++activations;
  Check(activations == 2, "releasing and pressing again activates again");
}

void TestHostGate() {
  Check(HostGate(true, false, Fight()) == DebugReject::kNone, "an Evil Ryu P1 in a local fight is accepted");
  Check(HostGate(false, false, Fight()) == DebugReject::kUltraOff, "Ultra off rejects");
  Check(HostGate(true, false, Fight(false, DebugFighter::kNone)) == DebugReject::kWrongFighter, "ordinary Ryu rejects");
  Check(HostGate(true, false, Fight(false, DebugFighter::kNone)) == DebugReject::kWrongFighter, "Akuma rejects, it is never Evil Ryu");
  Check(HostGate(true, false, Fight(false, DebugFighter::kEvilRyu, false)) == DebugReject::kNotInFight, "outside a fight rejects");
  Check(HostGate(true, true, Fight()) == DebugReject::kNotInFight, "Character Select rejects");
  Check(HostGate(true, false, Fight(true)) == DebugReject::kOnline, "online rejects");
  Check(HostGate(true, false, Fight(true, DebugFighter::kNone)) == DebugReject::kOnline, "online rejects before the fighter is read");
  Check(HostGate(true, false, DebugFightStatus{}) == DebugReject::kNotInFight, "no fighter state rejects");
}

void TestBridge() {
  DebugBridge bridge;
  Check(!bridge.Status(1000).ticking, "no fight tick before the guest reports");
  bridge.PublishFight(1000, false, DebugFighter::kEvilRyu);
  Check(bridge.Status(1100).ticking && bridge.Status(1100).p1 == DebugFighter::kEvilRyu, "a recent tick reports the fight");
  Check(!bridge.Status(1000 + kFightTickWindowMs).ticking, "a stale tick is not a fight");
  bridge.PublishFight(2000, true, DebugFighter::kNone);
  Check(bridge.Status(2001).online && bridge.Status(2001).p1 == DebugFighter::kNone, "the online and identity flags follow the guest");

  bridge.Post(DebugMove::kSenpuKyaku, 3000);
  Check(bridge.Take(3100) == DebugMove::kSenpuKyaku, "a posted request is taken");
  Check(bridge.Take(3101) == DebugMove::kNone, "a request is taken once");
  bridge.Post(DebugMove::kRagingDemon, 4000);
  Check(bridge.Take(4000 + kRequestWindowMs) == DebugMove::kNone, "an old request never fires in a later fight");
  bridge.Post(DebugMove::kRagingDemon, 5000);
  bridge.Reset();
  Check(bridge.Take(5001) == DebugMove::kNone && !bridge.Status(5001).ticking, "reset clears the bridge");
}

void TestConvergence() {
  struct Case {
    DebugMove move;
    uint8_t first, third;
    uint16_t buttons;
  };
  const Case cases[] = {
      {DebugMove::kTeleportForwardFar, kNibbleForward, kNibbleDownForward, kPunches},
      {DebugMove::kTeleportForwardNear, kNibbleForward, kNibbleDownForward, kKicks},
      {DebugMove::kTeleportBackwardFar, kNibbleBack, kNibbleDownBack, kPunches},
      {DebugMove::kTeleportBackwardNear, kNibbleBack, kNibbleDownBack, kKicks},
  };
  for (const Case& c : cases) {
    TeleportRecognizer recognizer;
    const int recognised = Motion(recognizer, c.first, c.third, c.buttons);
    Check(recognised == TeleportVariantFor(c.move), "the hotkey and the command start the same teleport variant");
    Check(IsTeleportMove(c.move), "the teleport hotkeys are teleports");
  }
  Check(TeleportVariantFor(DebugMove::kSenpuKyaku) == -1 && TeleportVariantFor(DebugMove::kRagingDemon) == -1,
        "only teleports carry a variant");
  Check(!IsTeleportMove(DebugMove::kRagingDemon) && !IsTeleportMove(DebugMove::kSenpuKyaku), "other moves are not teleports");
}

constexpr int32_t kFloor = 0x120000;

void Stand(GroundHeight& ground, int32_t y, int ticks) {
  for (int i = 0; i < ticks; ++i) ObserveGround(ground, y, false);
}

void TestGround() {
  GroundHeight ground;
  Stand(ground, kFloor, 20);
  Check(ground.y == kFloor && !AboveGround(ground, kFloor), "a fighter standing still is on the ground");
  Check(!AboveGround(ground, kFloor + kAirborneMargin), "the margin is still the ground");
  Check(AboveGround(ground, kFloor + kAirborneMargin + 1), "above the margin is the air");
  for (int y = kFloor; y < kFloor + (60 << 16); y += 3 << 16) ObserveGround(ground, y, false);
  Check(ground.y == kFloor, "a jump never moves the floor");
  ObserveGround(ground, kFloor - (12 << 16), false);
  Stand(ground, kFloor, 20);
  Check(ground.y == kFloor && !AboveGround(ground, kFloor), "a landing that dips below the floor does not poison it");
  GroundHeight later = ground;
  Stand(later, kFloor + (30 << 16), 20);
  Check(!AboveGround(later, kFloor + (30 << 16)), "a higher floor in the next fight heals once the fighter stands");
  GroundHeight frozen;
  Stand(frozen, kFloor, 20);
  for (int i = 0; i < 40; ++i) ObserveGround(frozen, kFloor + (50 << 16), true);
  Check(frozen.y == kFloor && AboveGround(frozen, kFloor + (50 << 16)), "a frozen frame in mid air is not a floor");
  GroundHeight fresh;
  ObserveGround(fresh, kFloor, false);
  Check(fresh.known && fresh.y == kFloor, "the first height seeds the floor");
}

void TestStartRejection() {
  using M = DebugMove;
  using R = DebugReject;
  for (const M move : {M::kShakunetsu, M::kTeleportForwardFar, M::kSenpuKyaku, M::kRagingDemon, M::kRasetsuLight,
                       M::kRasetsuHeavy, M::kInazuma, M::kShinbu, M::kShoryukenHeavy}) {
    Check(StartRejection(move, false, false, false) == R::kNone, "a grounded fighter starts every ground move");
    Check(StartRejection(move, true, false, false) == R::kIncompatibleState, "another host move blocks the start");
    Check(StartRejection(move, false, true, false) == R::kIncompatibleState, "a ground move is refused in the air");
    Check(StartRejection(move, false, false, true) == R::kIncompatibleState, "the guest's own refusal blocks the start");
  }
  Check(StartRejection(M::kAirTatsumaki, false, true, false) == R::kNone, "Air Tatsumaki starts in the air");
  Check(StartRejection(M::kAirTatsumaki, false, false, false) == R::kNotAirborne, "Air Tatsumaki on the ground says so");
  Check(StartRejection(M::kAirTatsumaki, true, true, false) == R::kIncompatibleState, "an active move blocks it too");
}

// The fight state a debug key meets across three fights: each fight publishes its own status, a stale request never
// carries over, and the floor of an earlier fight never makes the fighter airborne.
void TestLifecycle() {
  for (const DebugFighter fighter : {DebugFighter::kEvilRyu, DebugFighter::kViolentKen}) {
    DebugBridge bridge;
    GroundHeight ground;
    int64_t now = 1000;
    for (int fight = 0; fight < 3; ++fight) {
      const int32_t floor = kFloor + fight * (7 << 16);
      Check(!bridge.Status(now).ticking, "between fights no key is claimed");
      for (int tick = 0; tick < 30; ++tick) {
        bridge.PublishFight(now, false, fighter);
        ObserveGround(ground, floor, false);
        now += 16;
      }
      Check(bridge.Status(now).ticking && bridge.Status(now).p1 == fighter, "the fight publishes its fighter");
      Check(!AboveGround(ground, floor), "the fighter stands on the floor of this fight");
      const DebugMove move = DebugMoveFor(fighter, DebugKey::kF5, false, false);
      Check(move != DebugMove::kNone, "F5 maps to a move in every fight");
      Check(StartRejection(move, false, AboveGround(ground, floor), false) == DebugReject::kNone, "F5 starts in every fight");
      bridge.Post(move, now);
      Check(bridge.Take(now) == move, "the guest takes the request");
      Check(bridge.Take(now) == DebugMove::kNone, "a request is taken once");
      bridge.Post(move, now);
      now += kRequestWindowMs + 100;
      Check(bridge.Take(now) == DebugMove::kNone, "a request nobody took expires before the next fight");
      now += kFightTickWindowMs + 100;
      ground = GroundHeight{};
      const int32_t polluted_floor = floor - (9 << 16);
      Stand(ground, polluted_floor, 20);
      Stand(ground, floor, 20);
      Check(!AboveGround(ground, floor), "a floor from before the fight is replaced once the fighter stands");
      ground = GroundHeight{};
    }
  }
}

void TestRenderState() {
  HostMove senpu;
  senpu.kind = HostMoveKind::kSenpuKyaku;
  const EvilRenderState state = RenderStateFor(senpu, true, false);
  Check(state == EvilRenderState::kNormal, "Senpu Kyaku draws the ordinary body");
  bool hidden = false;
  for (uint32_t ticks = 0; ticks < 64; ++ticks) hidden |= BodySuppressed(state, ticks);
  Check(!hidden, "Senpu Kyaku never hides the body");
}

}  // namespace

int main() {
  TestFighterRouting();
  TestMapping();
  TestEdge();
  TestHostGate();
  TestBridge();
  TestConvergence();
  TestRenderState();
  TestGround();
  TestStartRejection();
  TestLifecycle();
  if (failures == 0) std::printf("evil ryu debug tests passed\n");
  return failures == 0 ? 0 : 1;
}
