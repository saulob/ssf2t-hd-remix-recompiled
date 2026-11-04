#include <cmath>
#include <cstdio>

#include "character/evil_ryu_moves.h"

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
  if (condition) return;
  std::printf("FAIL %s\n", what);
  ++failures;
}

constexpr uint16_t kLp = 0x10, kMp = 0x20, kHp = 0x40, kLk = 0x100, kMk = 0x200, kHk = 0x400;

int Feed(ssf2t::character::TeleportRecognizer& r, uint8_t nibble, uint16_t current = 0, uint16_t previous = 0) {
  return r.Update(nibble, current, previous);
}

int Motion(ssf2t::character::TeleportRecognizer& r, uint8_t first, uint8_t third, uint16_t buttons) {
  Feed(r, first);
  Feed(r, 0);
  Feed(r, 4);
  Feed(r, 0);
  Feed(r, third);
  return Feed(r, third, buttons, 0);
}

struct DashResult {
  int frames = 0;
  double pixels = 0;
};

DashResult RunDash(int variant, bool facing_flag, uint8_t wall = 0) {
  using namespace ssf2t::character;
  const DashParameters p = DashFor(variant, facing_flag);
  DashState dash{p.velocity, p.deceleration, p.threshold};
  int32_t position = 0;
  DashResult result;
  while (result.frames < 200) {
    ++result.frames;
    if (dash.Step(position, wall)) break;
  }
  result.pixels = double(position) / 65536.0;
  return result;
}

}  // namespace

int main() {
  using namespace ssf2t::character;

  {
    TeleportRecognizer r;
    Check(Motion(r, kNibbleForward, kNibbleDownForward, kLp | kMp | kHp) == 0, "623+PPP is the forward punch teleport");
  }
  {
    TeleportRecognizer r;
    Check(Motion(r, kNibbleForward, kNibbleDownForward, kLk | kMk | kHk) == 1, "623+KKK is the forward kick teleport");
  }
  {
    TeleportRecognizer r;
    Check(Motion(r, kNibbleBack, kNibbleDownBack, kLp | kMp | kHp) == 2, "421+PPP is the back punch teleport");
  }
  {
    TeleportRecognizer r;
    Check(Motion(r, kNibbleBack, kNibbleDownBack, kLk | kMk | kHk) == 3, "421+KKK is the back kick teleport");
  }
  {
    TeleportRecognizer r;
    Check(Motion(r, kNibbleForward, kNibbleDownForward, kLp | kMp) == -1, "two punches are not USF2's PPP");
  }
  {
    TeleportRecognizer r;
    Check(Motion(r, kNibbleForward, kNibbleDownForward, kLk | kHk) == -1, "two kicks are not USF2's KKK");
  }
  {
    TeleportRecognizer r;
    Feed(r, kNibbleForward);
    Feed(r, kNibbleDown);
    Feed(r, kNibbleDownForward);
    Check(Feed(r, kNibbleDownForward, 0, kLp | kMp | kHp) == 0, "releasing all three punches counts like the guest");
  }
  {
    TeleportRecognizer r;
    Feed(r, kNibbleForward);
    Feed(r, kNibbleDownForward);
    Feed(r, kNibbleForward);
    Check(Feed(r, kNibbleForward, kLp | kMp | kHp, 0) == 0, "the USF2 F DF F path of 623 is accepted");
  }
  {
    TeleportRecognizer r;
    Feed(r, kNibbleDown);
    Feed(r, kNibbleDownBack);
    Feed(r, kNibbleDown);
    Check(Feed(r, kNibbleDown, kLk | kMk | kHk, 0) == 3, "the USF2 D DB D path of 421 is accepted");
  }
  {
    TeleportRecognizer r;
    Check(Motion(r, kNibbleForward, kNibbleDownForward, kLp) == -1, "623+P alone is not a teleport");
  }
  {
    TeleportRecognizer r;
    Check(Motion(r, kNibbleBack, kNibbleDownForward, kLp | kMp | kHp) == -1, "421 needs down-back as its third direction");
  }
  {
    TeleportRecognizer r;
    Feed(r, kNibbleForward);
    for (int i = 0; i < 20; ++i) Feed(r, 0);
    Feed(r, kNibbleDown);
    Check(Feed(r, kNibbleDownForward, kLp | kMp | kHp, 0) == -1, "a stale motion times out");
  }
  {
    TeleportRecognizer r;
    Motion(r, kNibbleForward, kNibbleDownForward, kLp | kMp | kHp);
    Check(Feed(r, kNibbleDownForward, kLp | kMp | kHp, 0) == -1, "the recogniser resets after completing");
  }
  {
    TeleportRecognizer r;
    Feed(r, kNibbleForward);
    Feed(r, kNibbleDown);
    Feed(r, kNibbleDownForward);
    Feed(r, kNibbleDownForward, kLp | kMp | kHp, 0);
    TeleportRecognizer other;
    Check(Feed(other, kNibbleDownForward, kLp | kMp | kHp, 0) == -1, "recognisers are independent instances");
  }

  const DashResult fp = RunDash(0, true);
  const DashResult fk = RunDash(1, true);
  const DashResult bp = RunDash(2, true);
  const DashResult bk = RunDash(3, true);
  Check(fp.frames == 46 && std::fabs(fp.pixels - 326.3) < 1.0, "forward punch dash matches the guest Ashura (46 frames, 326 px)");
  Check(fk.frames == 31 && std::fabs(fk.pixels - 155.0) < 1.0, "forward kick dash matches the guest Ashura (31 frames, 155 px)");
  Check(bp.frames == 46 && std::fabs(bp.pixels + 326.3) < 1.0, "back punch dash goes the other way");
  Check(bk.frames == 31 && std::fabs(bk.pixels + 155.0) < 1.0, "back kick dash goes the other way");
  const DashResult flipped = RunDash(0, false);
  Check(flipped.pixels < -300.0, "the facing flag mirrors the dash");
  const DashResult walled = RunDash(0, true, 2);
  Check(walled.frames == 1, "the right wall flag ends a rightward dash at once");
  const DashResult not_walled = RunDash(0, true, 1);
  Check(not_walled.frames == 46, "the left wall flag does not stop a rightward dash");

  {
    RagingDemonRecognizer r;
    bool done = false;
    uint16_t held = 0;
    auto press = [&](uint16_t buttons) {
      const uint16_t before = held;
      held = buttons;
      return r.Update(held, before);
    };
    press(kLp);
    press(0);
    press(kLp);
    press(0);
    press(0x02);
    press(0);
    press(kLk);
    press(0);
    done = press(kHp);
    Check(done, "LP LP F LK HP completes the raging demon command");
  }
  {
    RagingDemonRecognizer r;
    uint16_t held = 0;
    bool done = false;
    auto press = [&](uint16_t buttons) {
      const uint16_t before = held;
      held = buttons;
      return r.Update(held, before);
    };
    press(kLp);
    press(0);
    press(kLp);
    for (int i = 0; i < 40; ++i) press(0);
    press(0x02);
    press(0);
    press(kLk);
    press(0);
    done = press(kHp);
    Check(!done, "the raging demon command times out between steps");
  }
  {
    RagingDemonRecognizer r;
    uint16_t held = 0;
    auto press = [&](uint16_t buttons) {
      const uint16_t before = held;
      held = buttons;
      return r.Update(held, before);
    };
    press(kLp);
    press(0);
    press(kHp);
    press(0);
    press(0x02);
    press(0);
    press(kLk);
    press(0);
    Check(!press(kHp), "a wrong press does not complete the raging demon command");
  }

  {
    RagingDemonRecognizer r;
    uint16_t held = 0;
    auto press = [&](uint16_t buttons) {
      const uint16_t before = held;
      held = buttons;
      return r.Update(held, before);
    };
    press(kLp);
    press(0);
    press(kLp);
    press(0);
    press(0x02 | kLk);
    press(0);
    Check(press(kHp), "LP LP F+LK HP is the USF2 short path");
  }

  Check(TeleportSlide(0) == 0 && TeleportSlide(2) == 2 && TeleportSlide(4) == 4, "low events slide forward");
  Check(TeleportSlide(18) == -2 && TeleportSlide(20) == -4, "high events slide backward");
  Check(TeleportBodyVisible(1) && !TeleportBodyVisible(2), "the dash ghost flickers on alternate frames");

  Check(DemonDashSpent(0x300) && DemonDashSpent(-0x300), "the raging demon whiffs at 3 pixels per tick");
  Check(!DemonDashSpent(0x301) && !DemonDashSpent(-0x301), "the raging demon keeps dashing above that");
  {
    int16_t velocity = kDemonDashVelocity;
    int frames = 0;
    while (!DemonDashSpent(velocity) && frames < 200) {
      velocity = int16_t(velocity - kDemonDashDeceleration);
      ++frames;
    }
    Check(frames == 46, "the raging demon dash runs out in 46 frames, like the far teleport");
    Check(frames < kDemonDashTimeout, "the guest timeout never ends the dash first");
  }

  HostMoves moves;
  moves.For(0).kind = HostMoveKind::kTeleport;
  Check(moves.Active(0) && !moves.Active(1), "host move state is per player");
  moves.Reset();
  Check(!moves.Active(0) && !moves.Active(1), "reset clears both players");
  Check(!moves.Active(2) && !moves.Active(-1), "invalid sides are ignored");

  {
    const auto never_hidden = [](EvilRenderState state) {
      for (uint32_t ticks = 0; ticks < 64; ++ticks) {
        if (BodySuppressed(state, ticks)) return false;
      }
      return true;
    };
    HostMove idle;
    Check(RenderStateFor(idle, false, false) == EvilRenderState::kNormal, "a fighter with no move is normal");
    Check(never_hidden(EvilRenderState::kNormal), "the normal state never hides the body");
    Check(RenderStateFor(idle, false, true) == EvilRenderState::kUniquePose, "a unique pose is its own state");
    Check(never_hidden(EvilRenderState::kUniquePose), "a unique pose never hides the body");

    HostMove teleport;
    teleport.kind = HostMoveKind::kTeleport;
    teleport.phase = HostMovePhase::kStartup;
    Check(RenderStateFor(teleport, true, true) == EvilRenderState::kTeleportStartup, "teleport startup");
    Check(never_hidden(EvilRenderState::kTeleportStartup), "the stance stays visible");
    teleport.phase = HostMovePhase::kDash;
    Check(RenderStateFor(teleport, true, false) == EvilRenderState::kTeleportGhost, "the dash is the ghost phase");
    Check(BodySuppressed(EvilRenderState::kTeleportGhost, 2) && !BodySuppressed(EvilRenderState::kTeleportGhost, 3),
          "the ghost phase flickers");
    teleport.phase = HostMovePhase::kTail;
    Check(RenderStateFor(teleport, true, false) == EvilRenderState::kTeleportRecovery, "the tail is visible again");
    teleport.phase = HostMovePhase::kRecovery;
    Check(RenderStateFor(teleport, true, false) == EvilRenderState::kTeleportRecovery, "recovery is visible");
    Check(never_hidden(EvilRenderState::kTeleportRecovery), "the fighter comes back after a teleport");

    teleport.phase = HostMovePhase::kDash;
    Check(RenderStateFor(teleport, false, false) == EvilRenderState::kNormal,
          "a dash cut off by leaving the special class cannot leave the fighter hidden");

    HostMove demon;
    demon.kind = HostMoveKind::kRagingDemon;
    Check(RenderStateFor(demon, true, false) == EvilRenderState::kShunGokuSatsu, "the raging demon state");
    Check(never_hidden(EvilRenderState::kShunGokuSatsu), "the raging demon never hides the body");
    Check(RenderStateFor(demon, false, false) == EvilRenderState::kNormal, "the fighter returns after the raging demon");

    HostMoves sides;
    sides.For(0).kind = HostMoveKind::kTeleport;
    sides.For(0).phase = HostMovePhase::kDash;
    Check(RenderStateFor(sides.For(0), true, false) == EvilRenderState::kTeleportGhost &&
              RenderStateFor(sides.For(1), true, false) == EvilRenderState::kNormal,
          "render state is per player");
  }

  if (failures == 0) std::printf("evil ryu moves tests passed\n");
  return failures == 0 ? 0 : 1;
}
