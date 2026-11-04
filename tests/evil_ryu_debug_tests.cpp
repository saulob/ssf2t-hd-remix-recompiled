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

DebugFightStatus Fight(bool online = false, bool p1_evil = true, bool ticking = true) {
  DebugFightStatus status;
  status.ticking = ticking;
  status.online = online;
  status.p1_evil = p1_evil;
  return status;
}

void TestMapping() {
  Check(DebugMoveFor(DebugKey::kF5, false) == DebugMove::kTeleportForwardFar, "F5 is the forward far teleport");
  Check(DebugMoveFor(DebugKey::kF6, false) == DebugMove::kTeleportBackwardFar, "F6 is the backward far teleport");
  Check(DebugMoveFor(DebugKey::kF7, false) == DebugMove::kSenpuKyaku, "F7 is Senpu Kyaku");
  Check(DebugMoveFor(DebugKey::kF8, false) == DebugMove::kRagingDemon, "F8 is Shun Goku Satsu");
  Check(DebugMoveFor(DebugKey::kF5, true) == DebugMove::kTeleportForwardNear, "Shift+F5 is the forward near teleport");
  Check(DebugMoveFor(DebugKey::kF6, true) == DebugMove::kTeleportBackwardNear, "Shift+F6 is the backward near teleport");
  Check(DebugMoveFor(DebugKey::kF7, true) == DebugMove::kNone, "Shift+F7 has no move");
  Check(DebugMoveFor(DebugKey::kF8, true) == DebugMove::kNone, "Shift+F8 has no move");
  Check(DebugMoveFor(DebugKey::kNone, false) == DebugMove::kNone, "other keys have no move");
  Check(std::strcmp(DebugKeyLabel(DebugMove::kTeleportForwardFar), "F5") == 0, "the F5 label");
  Check(std::strcmp(DebugMoveName(DebugMove::kTeleportBackwardFar), "teleport-backward-far") == 0, "the F6 move name");
  Check(std::strcmp(DebugMoveName(DebugMove::kSenpuKyaku), "senpu-kyaku") == 0, "the F7 move name");
  Check(std::strcmp(DebugMoveName(DebugMove::kRagingDemon), "raging-demon") == 0, "the F8 move name");
  Check(std::strcmp(DebugRejectName(DebugReject::kNotEvilRyu), "not-evil-ryu") == 0, "the not-evil-ryu reason");
  Check(std::strcmp(DebugRejectName(DebugReject::kNotInFight), "not-in-fight") == 0, "the not-in-fight reason");
}

void TestEdge() {
  Check(DebugMoveForPress(DebugKey::kF5, false, false) == DebugMove::kTeleportForwardFar, "a fresh press activates");
  Check(DebugMoveForPress(DebugKey::kF5, false, true) == DebugMove::kNone, "a held key does not repeat");
  int activations = 0;
  bool down = false;
  for (int event = 0; event < 30; ++event) {
    if (DebugMoveForPress(DebugKey::kF8, false, down) != DebugMove::kNone) ++activations;
    down = true;
  }
  Check(activations == 1, "one physical press is one activation");
  down = false;
  if (DebugMoveForPress(DebugKey::kF8, false, down) != DebugMove::kNone) ++activations;
  Check(activations == 2, "releasing and pressing again activates again");
}

void TestHostGate() {
  Check(HostGate(true, false, Fight()) == DebugReject::kNone, "an Evil Ryu P1 in a local fight is accepted");
  Check(HostGate(false, false, Fight()) == DebugReject::kUltraOff, "Ultra off rejects");
  Check(HostGate(true, false, Fight(false, false)) == DebugReject::kNotEvilRyu, "ordinary Ryu rejects");
  Check(HostGate(true, false, Fight(false, false)) == DebugReject::kNotEvilRyu, "Akuma rejects, it is never Evil Ryu");
  Check(HostGate(true, false, Fight(false, true, false)) == DebugReject::kNotInFight, "outside a fight rejects");
  Check(HostGate(true, true, Fight()) == DebugReject::kNotInFight, "Character Select rejects");
  Check(HostGate(true, false, Fight(true)) == DebugReject::kOnline, "online rejects");
  Check(HostGate(true, false, Fight(true, false)) == DebugReject::kOnline, "online rejects before the fighter is read");
  Check(HostGate(true, false, DebugFightStatus{}) == DebugReject::kNotInFight, "no fighter state rejects");
}

void TestBridge() {
  DebugBridge bridge;
  Check(!bridge.Status(1000).ticking, "no fight tick before the guest reports");
  bridge.PublishFight(1000, false, true);
  Check(bridge.Status(1100).ticking && bridge.Status(1100).p1_evil, "a recent tick reports the fight");
  Check(!bridge.Status(1000 + kFightTickWindowMs).ticking, "a stale tick is not a fight");
  bridge.PublishFight(2000, true, false);
  Check(bridge.Status(2001).online && !bridge.Status(2001).p1_evil, "the online and identity flags follow the guest");

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
  TestMapping();
  TestEdge();
  TestHostGate();
  TestBridge();
  TestConvergence();
  TestRenderState();
  if (failures == 0) std::printf("evil ryu debug tests passed\n");
  return failures == 0 ? 0 : 1;
}
