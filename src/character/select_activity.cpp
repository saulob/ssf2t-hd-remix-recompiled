#include "character/select_activity.h"

#include <atomic>
#include <chrono>

#include <rex/hook.h>
#include <rex/logging.h>

#include "character/akuma_navigation.h"
#include "character/secret_select.h"
#include "guest_memory.h"
#include "netplay/guest_netplay.h"
#include "title_update_image.h"

namespace ssf2t::character {
namespace {

constexpr int64_t kStaleMilliseconds = 3000;

struct Base013Select {
  static constexpr uint32_t kMode = 0x83CA294F;
  static constexpr uint32_t kHdStyle = 0x83A3876D;
  static constexpr uint32_t kLive = 0x83CA6C9C;
  static constexpr uint32_t kSpecial = 0x83C133A8;
  static constexpr uint32_t kConfirmResult = 0x83CD52A8;
  static constexpr uint32_t kOnlineAkumaGate = 0x83C24DB0;
};

struct TitleUpdate023Select {
  static constexpr uint32_t kMode = 0x83CA2E67;
  static constexpr uint32_t kHdStyle = 0x83A387E5;
  static constexpr uint32_t kLive = 0x83CA71B4;
  static constexpr uint32_t kSpecial = 0x83C13428;
  static constexpr uint32_t kConfirmResult = 0x83CD57C0;
  static constexpr uint32_t kOnlineAkumaGate = 0x83C25128;
};

constexpr uint32_t kWorkJoinedSidesOffset = 1168;
constexpr uint32_t kFighterWindowTimerOffset = 161;
constexpr uint32_t kFighterWindowOffset = 162;
constexpr uint32_t kFighterSideOffset = 897;
constexpr uint32_t kFighterPadOffset = 912;
constexpr uint32_t kFighterCharacterOffset = 925;
constexpr uint32_t kFighterHiddenOffset = 961;

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
  if (!SecretCellsFor(*GuestHost(base, S::kMode)).any()) return;
  if (!CharacterSelectActive()) g_session.fetch_add(1, std::memory_order_relaxed);
  g_last_touch_ms.store(NowMilliseconds(), std::memory_order_relaxed);
}

template <typename G>
bool SelectingFighter(const uint8_t* base, uint32_t& fighter, int& side) {
  fighter = LoadBE32(GuestHost(base, G::kFightWork + 4), 0);
  if (fighter < 0x10000 || fighter >= 0xFFFF0000u) return false;
  side = *GuestHost(base, fighter + kFighterSideOffset);
  return side == 0 || side == 1;
}

// The guest's own confirm code runs next with this carrier, so nothing after the hook sees id 32 as a character.
template <typename G, typename S>
void ConfirmSelect(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  TouchSelect<S>(ctx, base, original);
  uint32_t fighter = 0;
  int side = 0;
  if (!SelectingFighter<G>(base, fighter, side)) return;
  const bool classic = ClassicStyle(*GuestHost(base, S::kHdStyle));
  const bool available = SecretCellsFor(*GuestHost(base, S::kMode)).akuma;
  const bool confirmed = LoadBE32(base, S::kConfirmResult) != 0;
  if (!ClassicAkumaConfirmed(classic, available, *GuestHost(base, S::kLive + side), confirmed)) return;
  GuestStore8(base, fighter + kFighterCharacterOffset, kAkumaClassicCarrier);
  GuestStore8(base, fighter + kFighterWindowOffset, kAkumaWindowOpen);
  GuestStore8(base, fighter + kFighterWindowTimerOffset, 0);
}

// The commit reads the three punches from the pad word, and online in Classic it also wants a session word the
// negotiated secrets already overrule. Both are put back around the call, so nothing else sees them.
template <typename G, typename S>
void CommitAkuma(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  uint32_t fighter = 0;
  int side = 0;
  const bool classic = ClassicStyle(*GuestHost(base, S::kHdStyle));
  const bool due = SelectingFighter<G>(base, fighter, side) &&
                   ClassicAkumaCommitDue(classic, *GuestHost(base, S::kLive + side),
                                         *GuestHost(base, fighter + kFighterWindowOffset),
                                         *GuestHost(base, fighter + kFighterHiddenOffset));
  if (!due) return TouchSelect<S>(ctx, base, original);
  const uint16_t pad = LoadBE16(GuestHost(base, fighter + kFighterPadOffset), 0);
  const uint32_t online_gate = LoadBE32(base, S::kOnlineAkumaGate);
  GuestStoreBE16(base, fighter + kFighterPadOffset, uint16_t(pad | kAkumaCommitButtons));
  StoreBE32(base, S::kOnlineAkumaGate, 1);
  TouchSelect<S>(ctx, base, original);
  StoreBE32(base, S::kOnlineAkumaGate, online_gate);
  GuestStoreBE16(base, fighter + kFighterPadOffset, pad);
  const bool committed = *GuestHost(base, fighter + kFighterHiddenOffset) != 0;
  if (committed) *(base + S::kSpecial + side) = 0;
  REXLOG_INFO("DEBUG_SAULO ULTRA classic-akuma confirm side={} native_commit={} mode={}", side + 1, committed,
              GameModeName(*GuestHost(base, S::kMode)));
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

#define SSF2T_SELECT_AKUMA_HOOK(name, Handler, G, S) \
  REX_EXTERN(__imp__##name); \
  REX_HOOK_RAW(name) { \
    ssf2t::character::Handler<ssf2t::netplay::G, ssf2t::character::S>(ctx, base, __imp__##name); \
  }

REX_EXTERN(__imp__base013_sub_8223DF78);
REX_HOOK_RAW(base013_sub_8223DF78) {
  ssf2t::character::EnterSelect<ssf2t::netplay::Base013Netplay, ssf2t::character::Base013Select>(
      ctx, base, __imp__base013_sub_8223DF78);
}
SSF2T_SELECT_AKUMA_HOOK(base013_sub_82240F78, ConfirmSelect, Base013Netplay, Base013Select)
SSF2T_SELECT_AKUMA_HOOK(base013_sub_82240870, CommitAkuma, Base013Netplay, Base013Select)
SSF2T_SELECT_TOUCH_HOOK(base013_sub_82245310, Base013Select)

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_8223E0A8);
REX_HOOK_RAW(tu023_sub_8223E0A8) {
  ssf2t::character::EnterSelect<ssf2t::netplay::TitleUpdate023Netplay, ssf2t::character::TitleUpdate023Select>(
      ctx, base, __imp__tu023_sub_8223E0A8);
}
SSF2T_SELECT_AKUMA_HOOK(tu023_sub_822410A8, ConfirmSelect, TitleUpdate023Netplay, TitleUpdate023Select)
SSF2T_SELECT_AKUMA_HOOK(tu023_sub_822409A0, CommitAkuma, TitleUpdate023Netplay, TitleUpdate023Select)
SSF2T_SELECT_TOUCH_HOOK(tu023_sub_82245378, TitleUpdate023Select)
#endif
