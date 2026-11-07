#include "character/akuma_bug_fixes.h"

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include "guest_memory.h"
#include "netplay/guest_netplay.h"
#include "netplay_setting.h"
#include "title_update_image.h"

REX_EXTERN(base013_sub_821C4FF0);
#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(tu023_sub_821C5110);
#endif

namespace ssf2t::character {

REXCVAR_DEFINE_BOOL(ssf2t_akuma_bug_fixes, false, "SSF2T",
                    "Fix the unintended HD Remix Akuma behaviors documented by David Sirlin");

namespace {

constexpr uint32_t kFighterState = 3;
constexpr uint32_t kFighterStep = 4;
constexpr uint32_t kFighterMove = 0x88;
constexpr uint32_t kFighterJumpLocked = 0x1AD;
constexpr uint32_t kFighterInput = 0x390;
constexpr uint32_t kFighterOpponent = 0x39E;
constexpr uint32_t kFighterAkuma = 0x3C1;

struct Base013Throw {
  using Fixes = Base013AkumaFixes;
  using Netplay = netplay::Base013Netplay;
  static void Refuse(PPCContext& ctx, uint8_t* base) { base013_sub_821C4FF0(ctx, base); }
};

#if SSF2T_TITLE_UPDATE_IMAGE
struct TitleUpdate023Throw {
  using Fixes = TitleUpdate023AkumaFixes;
  using Netplay = netplay::TitleUpdate023Netplay;
  static void Refuse(PPCContext& ctx, uint8_t* base) { tu023_sub_821C5110(ctx, base); }
};
#endif

struct GuestFrames {
  uint8_t* base;
  uint8_t U8(uint32_t address) const { return *GuestHost(base, address); }
  uint16_t U16(uint32_t address) const { return LoadBE16(GuestHost(base, address), 0); }
  uint32_t U32(uint32_t address) const { return LoadBE32(GuestHost(base, address), 0); }
  void SetU8(uint32_t address, uint8_t value) { GuestStore8(base, address, value); }
};

enum class Reported : uint8_t { kNone, kOff, kOnline, kEnabled, kRejected };

Reported g_reported = Reported::kNone;
bool g_fixes_active = false;

void Report(Reported what) {
  if (g_reported == what) return;
  g_reported = what;
  const AkumaFixesInstalled installed = kAkumaFixesWhenApplied;
  switch (what) {
    case Reported::kOff: REXLOG_INFO("Akuma bug fixes: Off"); break;
    case Reported::kOnline: REXLOG_INFO("Akuma bug fixes: On, inactive for online play"); break;
    case Reported::kEnabled:
      REXLOG_INFO("Akuma bug fixes enabled raging_demon={} red_fireball={} dragon_punch={} air_fireball={}",
                  installed.raging_demon, installed.red_fireball, installed.dragon_punch, installed.air_fireball);
      break;
    case Reported::kRejected: REXLOG_WARN("Akuma bug fixes rejected: Remix Akuma frames are not the expected bytes"); break;
    case Reported::kNone: break;
  }
}

template <typename H, typename G>
void SyncAkumaFixes(uint8_t* base) {
  const bool option = REXCVAR_GET(ssf2t_akuma_bug_fixes);
  if (!option && !g_fixes_active) return Report(Reported::kOff);
  const bool developer_netplay = NetplayTestRole(rex::cvar::GetFlagByName(kNetplayRoleSetting));
  const bool active = AkumaBugFixesActive(option, *GuestHost(base, G::kGameMode), developer_netplay);
  GuestFrames frames{base};
  const uint32_t actions = RemixAkumaActions(frames, H::kRemixFighterData);
  if (!SetAkumaFrames(frames, actions, active)) {
    g_fixes_active = false;
    return Report(Reported::kRejected);
  }
  g_fixes_active = active;
  Report(active ? Reported::kEnabled : option ? Reported::kOnline : Reported::kOff);
}

template <typename T>
void TryThrow(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  using H = typename T::Fixes;
  using G = typename T::Netplay;
  if (!g_fixes_active || ClassicStyle(*GuestHost(base, H::kHdStyle))) return original(ctx, base);
  const GuestFrames guest{base};
  const uint32_t fighter = guest.U32(G::kFightWork - 4);
  const DemonThrower thrower{guest.U8(fighter + kFighterAkuma) != 0, guest.U8(fighter + kFighterState),
                             guest.U8(fighter + kFighterStep), guest.U8(fighter + kFighterMove)};
  if (!thrower.akuma) return original(ctx, base);
  const uint32_t opponent = guest.U32(H::kFighterBlock) + guest.U16(fighter + kFighterOpponent);
  const DemonTarget target{guest.U8(opponent + kFighterState), guest.U8(opponent + kFighterJumpLocked),
                           guest.U16(opponent + kFighterInput)};
  if (!DemonWaitsForJump(thrower, target)) return original(ctx, base);
  T::Refuse(ctx, base);
}

}  // namespace
}  // namespace ssf2t::character

namespace ac = ssf2t::character;
namespace an = ssf2t::netplay;

REX_EXTERN(__imp__base013_sub_82389310);
REX_HOOK_RAW(base013_sub_82389310) {
  ac::SyncAkumaFixes<ac::Base013AkumaFixes, an::Base013Netplay>(base);
  __imp__base013_sub_82389310(ctx, base);
}

REX_EXTERN(__imp__base013_sub_8238D028);
REX_HOOK_RAW(base013_sub_8238D028) {
  ac::TryThrow<ac::Base013Throw>(ctx, base, __imp__base013_sub_8238D028);
}

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_823893E0);
REX_HOOK_RAW(tu023_sub_823893E0) {
  ac::SyncAkumaFixes<ac::TitleUpdate023AkumaFixes, an::TitleUpdate023Netplay>(base);
  __imp__tu023_sub_823893E0(ctx, base);
}

REX_EXTERN(__imp__tu023_sub_8238D018);
REX_HOOK_RAW(tu023_sub_8238D018) {
  ac::TryThrow<ac::TitleUpdate023Throw>(ctx, base, __imp__tu023_sub_8238D018);
}
#endif
