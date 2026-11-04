#include <cstdint>
#include <cstring>
#include <string_view>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#include "character/character_moves.h"
#include "character/ultra_fight.h"
#include "guest_memory.h"
#include "netplay/guest_netplay.h"
#include "title_update_image.h"

namespace ssf2t::character {
namespace {

constexpr std::string_view kRyuWinsText = "WINS_RYU";
constexpr uint32_t kFightWorkWinner = 1412;

uint32_t EvilWinsTextName(uint8_t* base) {
  static uint32_t name = 0;
  if (name) return name;
  name = REX_KERNEL_MEMORY()->SystemHeapAlloc(uint32_t(kEvilRyuWinsId.size() + 1));
  if (name) {
    char* out = reinterpret_cast<char*>(GuestHost(base, name));
    std::memcpy(out, kEvilRyuWinsId.data(), kEvilRyuWinsId.size());
    out[kEvilRyuWinsId.size()] = '\0';
  }
  return name;
}

// The winner banner takes a text id and fills the eight wintext layers from it, so a different id keeps font, layers
// and animation.
template <typename G>
void ShowWinText(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  const uint32_t text = ctx.r3.u32;
  if (CharacterMovesPageReady() && text >= 0x10000 &&
      reinterpret_cast<const char*>(GuestHost(base, text)) == kRyuWinsText) {
    const uint32_t work = LoadBE32(GuestHost(base, G::kFightWork), 0);
    const int winner = work >= 0x10000 ? int(*GuestHost(base, work + kFightWorkWinner)) - 1 : -1;
    if (EvilRyuOnSide<G>(base, winner)) {
      const uint32_t name = EvilWinsTextName(base);
      if (name) {
        ctx.r3.u64 = name;
        REXLOG_INFO("DEBUG_SAULO EVIL_RYU winner-banner side={}", winner + 1);
      }
    }
  }
  original(ctx, base);
}

}  // namespace
}  // namespace ssf2t::character

REX_EXTERN(__imp__base013_sub_823CDBF8);
REX_HOOK_RAW(base013_sub_823CDBF8) {
  ssf2t::character::ShowWinText<ssf2t::netplay::Base013Netplay>(ctx, base, __imp__base013_sub_823CDBF8);
}

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_823CE1D8);
REX_HOOK_RAW(tu023_sub_823CE1D8) {
  ssf2t::character::ShowWinText<ssf2t::netplay::TitleUpdate023Netplay>(ctx, base, __imp__tu023_sub_823CE1D8);
}
#endif
