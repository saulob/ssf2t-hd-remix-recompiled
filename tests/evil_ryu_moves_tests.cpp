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
  Check(fp.frames == 46 && std::fabs(fp.pixels - 244.7) < 1.0, "the forward punch dash is USF2 Evil Ryu's (46 frames, 245 px)");
  Check(fk.frames == 31 && std::fabs(fk.pixels - 116.3) < 1.0, "the forward kick dash is USF2 Evil Ryu's (31 frames, 116 px)");
  Check(bp.frames == 46 && std::fabs(bp.pixels + 244.7) < 1.0, "back punch dash goes the other way");
  Check(bk.frames == 31 && std::fabs(bk.pixels + 116.3) < 1.0, "back kick dash goes the other way");
  {
    const DashParameters far = DashFor(0, true);
    const DashParameters near = DashFor(1, true);
    const DashParameters back = DashFor(2, true);
    Check(far.velocity == 0x8A0 && far.deceleration == 0x24 && far.threshold == 0x240, "0xb9c288 row 0 and 0xb9c2a8[0]");
    Check(near.velocity == 0x600 && near.deceleration == 0x24 && near.threshold == 0x1B0, "0xb9c288 row 1 and 0xb9c2a8[1]");
    Check(back.velocity == -0x8A0 && back.deceleration == -0x24 && back.threshold == 0x240, "0xb9c288 row 2 and 0xb9c2a8[2]");
    Check(kTeleportTailFrames == 5, "0x591e8 stores 5 ticks of dash tail");
  }
  const DashResult flipped = RunDash(0, false);
  Check(flipped.pixels < -240.0, "the facing flag mirrors the dash");
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

  Check(DemonDashSpent(0x400) && DemonDashSpent(-0x400), "the raging demon whiffs at 4 pixels per tick");
  Check(!DemonDashSpent(0x401) && !DemonDashSpent(-0x401), "the raging demon keeps dashing above that");
  Check(kDemonDashVelocity == 0x8A0 && kDemonDashDeceleration == 0x24, "0xb9c1d0 8.625 and the -0.140625 at 0x59ba4");
  {
    int16_t velocity = kDemonDashVelocity;
    int frames = 0;
    while (!DemonDashSpent(velocity) && frames < 200) {
      velocity = int16_t(velocity - kDemonDashDeceleration);
      ++frames;
    }
    Check(frames == 33, "the raging demon dash runs out in 33 frames");
    Check(frames < kDemonDashTimeout, "the guest timeout never ends the dash first");
  }

  Check(TatsuSpins(0) == 0 && TatsuSpins(2) == 1 && TatsuSpins(4) == 2, "Evil Ryu's Tatsu spins 0, 1, 2 where Ryu's spins 0, 3, 4");
  Check(ShakunetsuRecovery(0) == 42 && ShakunetsuRecovery(2) == 50 && ShakunetsuRecovery(4) == 60, "0xb9c318 row 1");
  Check(ShakunetsuRecovery(40) == 42, "an out of range strength stays in the table");

  Check(SenpuInput(kNibbleForward, kMk, 0), "forward + medium kick is Senpu Kyaku");
  Check(!SenpuInput(kNibbleForward, kMk, kMk), "a held kick does not repeat it");
  Check(!SenpuInput(0, kMk, 0), "medium kick alone is the normal");
  Check(!SenpuInput(kNibbleForward, kHp, 0), "forward + heavy punch is not a unique for Evil Ryu");
  Check(!SenpuInput(kNibbleDownForward, kMk, 0), "the crouching kick stays a normal");
  Check(kSenpuHopVelocityX == 0x480 && kSenpuHopDecelerationX == 0x20, "0x58cb4 4.5 and 0x58cc4 -0.125");
  Check(kSenpuHopVelocityY == 0x300 && kSenpuHopDecelerationY == 0x50, "0x58cb8 3.0 and 0x58cc8 -0.3125");

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

    HostMove senpu;
    senpu.kind = HostMoveKind::kSenpuKyaku;
    Check(RenderStateFor(senpu, true, false) == EvilRenderState::kNormal, "Senpu Kyaku draws the ordinary body");
    Check(never_hidden(RenderStateFor(senpu, true, false)), "Senpu Kyaku never hides the body");
  }

  {
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

  {
    constexpr int32_t kFloor = 0x120000;
    const auto stand = [](GroundHeight& ground, int32_t y, int ticks) {
      for (int i = 0; i < ticks; ++i) ObserveGround(ground, y, false);
    };
    GroundHeight ground;
    stand(ground, kFloor, 20);
    Check(ground.y == kFloor && !AboveGround(ground, kFloor), "a fighter standing still is on the ground");
    Check(!AboveGround(ground, kFloor + kAirborneMargin), "the margin is still the ground");
    Check(AboveGround(ground, kFloor + kAirborneMargin + 1), "above the margin is the air");
    for (int y = kFloor; y < kFloor + (60 << 16); y += 3 << 16) ObserveGround(ground, y, false);
    Check(ground.y == kFloor, "a jump never moves the floor");
    ObserveGround(ground, kFloor - (12 << 16), false);
    stand(ground, kFloor, 20);
    Check(ground.y == kFloor && !AboveGround(ground, kFloor), "a landing that dips below the floor does not poison it");
    GroundHeight later = ground;
    stand(later, kFloor + (30 << 16), 20);
    Check(!AboveGround(later, kFloor + (30 << 16)), "a higher floor in the next fight heals once the fighter stands");
    GroundHeight frozen;
    stand(frozen, kFloor, 20);
    for (int i = 0; i < 40; ++i) ObserveGround(frozen, kFloor + (50 << 16), true);
    Check(frozen.y == kFloor && AboveGround(frozen, kFloor + (50 << 16)), "a frozen frame in mid air is not a floor");
    GroundHeight fresh;
    ObserveGround(fresh, kFloor, false);
    Check(fresh.known && fresh.y == kFloor, "the first height seeds the floor");
  }

  if (failures == 0) std::printf("evil ryu moves tests passed\n");
  return failures == 0 ? 0 : 1;
}
