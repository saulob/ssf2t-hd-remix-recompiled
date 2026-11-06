#include "character/select_activity.h"

#include <atomic>
#include <chrono>

#include <rex/hook.h>

#include "character/akuma.h"
#include "character/ultra_fighters.h"
#include "guest_memory.h"
#include "netplay/guest_netplay.h"
#include "title_update_image.h"

namespace ssf2t::character {
namespace {

constexpr int64_t kStaleMilliseconds = 3000;

struct Base013Select {
  static constexpr uint32_t kMode = 0x83CA294F;
};

struct TitleUpdate023Select {
  static constexpr uint32_t kMode = 0x83CA2E67;
};

constexpr uint32_t kWorkJoinedSidesOffset = 1168;

std::atomic<int64_t> g_last_touch_ms{0};
std::atomic<uint32_t> g_session{0};
std::atomic<uint8_t> g_joined_sides{0};

int64_t NowMilliseconds() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

template <typename S>
void TouchSelect(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  original(ctx, base);
  const uint8_t mode = *GuestHost(base, S::kMode);
  if (!AkumaAvailable(mode) && !EvilRyuAvailable(mode) && !ViolentKenAvailable(mode)) return;
  if (!CharacterSelectActive()) g_session.fetch_add(1, std::memory_order_relaxed);
  g_last_touch_ms.store(NowMilliseconds(), std::memory_order_relaxed);
}

// This state runs once per Character Select entry and clears the guest's join bits, so they are read before it.
template <typename G, typename S>
void EnterSelect(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  const uint32_t work = LoadBE32(GuestHost(base, G::kFightWork), 0);
  const bool valid = work >= 0x10000 && work < 0xFFFF0000u;
  g_joined_sides.store(valid ? *GuestHost(base, work + kWorkJoinedSidesOffset) & 3 : 0, std::memory_order_relaxed);
  TouchSelect<S>(ctx, base, original);
}

}  // namespace

bool CharacterSelectActive() {
  const int64_t last = g_last_touch_ms.load(std::memory_order_relaxed);
  return last != 0 && NowMilliseconds() - last < kStaleMilliseconds;
}

uint32_t CharacterSelectSession() { return g_session.load(std::memory_order_relaxed); }

uint8_t CharacterSelectJoinedSides() { return g_joined_sides.load(std::memory_order_relaxed); }

}  // namespace ssf2t::character

#define SSF2T_SELECT_TOUCH_HOOK(name, S) \
  REX_EXTERN(__imp__##name); \
  REX_HOOK_RAW(name) { ssf2t::character::TouchSelect<ssf2t::character::S>(ctx, base, __imp__##name); }

REX_EXTERN(__imp__base013_sub_8223DF78);
REX_HOOK_RAW(base013_sub_8223DF78) {
  ssf2t::character::EnterSelect<ssf2t::netplay::Base013Netplay, ssf2t::character::Base013Select>(
      ctx, base, __imp__base013_sub_8223DF78);
}
SSF2T_SELECT_TOUCH_HOOK(base013_sub_82240F78, Base013Select)
SSF2T_SELECT_TOUCH_HOOK(base013_sub_82240870, Base013Select)
SSF2T_SELECT_TOUCH_HOOK(base013_sub_82245310, Base013Select)

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_8223E0A8);
REX_HOOK_RAW(tu023_sub_8223E0A8) {
  ssf2t::character::EnterSelect<ssf2t::netplay::TitleUpdate023Netplay, ssf2t::character::TitleUpdate023Select>(
      ctx, base, __imp__tu023_sub_8223E0A8);
}
SSF2T_SELECT_TOUCH_HOOK(tu023_sub_822410A8, TitleUpdate023Select)
SSF2T_SELECT_TOUCH_HOOK(tu023_sub_822409A0, TitleUpdate023Select)
SSF2T_SELECT_TOUCH_HOOK(tu023_sub_82245378, TitleUpdate023Select)
#endif
