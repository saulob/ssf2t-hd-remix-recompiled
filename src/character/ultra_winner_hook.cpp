#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

#include <rex/hook.h>
#include <rex/system/kernel_state.h>

#include "character/character_moves.h"
#include "character/fighter_identity.h"
#include "character/ultra_fight.h"
#include "character/ultra_quote.h"
#include "guest_memory.h"
#include "netplay/guest_netplay.h"
#include "title_update_image.h"

REX_EXTERN(base013_sub_824308A0);
REX_EXTERN(base013_sub_82439990);
REX_EXTERN(base013_sub_823C2158);
#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(tu023_sub_82430FE0);
REX_EXTERN(tu023_sub_8243A100);
REX_EXTERN(tu023_sub_823C2700);
#endif

namespace ssf2t::character {
namespace {

struct Base013Quote {
  static constexpr uint32_t kTaunts = 0x83C27380;
  static constexpr std::array<uint32_t, 2> kWinnerReturn = {0x82238920, 0x82238C28};
  static void HashName(PPCContext& ctx, uint8_t* base) { base013_sub_824308A0(ctx, base); }
  static void FindText(PPCContext& ctx, uint8_t* base) { base013_sub_82439990(ctx, base); }
  static void TextPointer(PPCContext& ctx, uint8_t* base) { base013_sub_823C2158(ctx, base); }
};

#if SSF2T_TITLE_UPDATE_IMAGE
struct TitleUpdate023Quote {
  static constexpr uint32_t kTaunts = 0x83C276F8;
  static constexpr std::array<uint32_t, 2> kWinnerReturn = {0x82238A48, 0x82238D50};
  static void HashName(PPCContext& ctx, uint8_t* base) { tu023_sub_82430FE0(ctx, base); }
  static void FindText(PPCContext& ctx, uint8_t* base) { tu023_sub_8243A100(ctx, base); }
  static void TextPointer(PPCContext& ctx, uint8_t* base) { tu023_sub_823C2700(ctx, base); }
};
#endif

constexpr std::string_view kRyuWinsText = "WINS_RYU";
constexpr std::string_view kKenWinsText = "WINS_KEN";
constexpr uint32_t kFightWorkWinner = 1412;
constexpr uint32_t kTauntsPerCharacter = 6;
constexpr uint32_t kTauntRowBytes = kTauntsPerCharacter * 4;

uint32_t GuestName(uint8_t* base, std::string_view text) {
  const uint32_t name = REX_KERNEL_MEMORY()->SystemHeapAlloc(uint32_t(text.size() + 1));
  if (name) {
    char* out = reinterpret_cast<char*>(GuestHost(base, name));
    std::memcpy(out, text.data(), text.size());
    out[text.size()] = '\0';
  }
  return name;
}

uint32_t WinsTextName(uint8_t* base, bool violent) {
  static std::array<uint32_t, 2> names{};
  uint32_t& name = names[violent ? 1 : 0];
  if (!name) name = GuestName(base, violent ? kViolentKenWinsId : kEvilRyuWinsId);
  return name;
}

// The winner banner takes a text id and fills the eight wintext layers from it, so a different id keeps font, layers
// and animation.
template <typename G>
void ShowWinText(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  const uint32_t text = ctx.r3.u32;
  if (CharacterMovesPageReady() && text >= 0x10000) {
    const char* id = reinterpret_cast<const char*>(GuestHost(base, text));
    const bool ryu = id == kRyuWinsText;
    const bool ken = id == kKenWinsText;
    if (ryu || ken) {
      const uint32_t work = LoadBE32(GuestHost(base, G::kFightWork), 0);
      const int winner = work >= 0x10000 ? int(*GuestHost(base, work + kFightWorkWinner)) - 1 : -1;
      if (ryu ? EvilRyuOnSide<G>(base, winner) : ViolentKenOnSide<G>(base, winner)) {
        const uint32_t name = WinsTextName(base, ken);
        if (name) ctx.r3.u64 = name;
      }
    }
  }
  original(ctx, base);
}

// The guest's own TAUNT_ lookup for a text id of the patched table; 0 when the table has no such id.
template <typename H>
uint32_t QuoteText(PPCContext& ctx, uint8_t* base, uint32_t name) {
  const PPCContext saved = ctx;
  ctx.r3.u64 = name;
  H::HashName(ctx, base);
  H::FindText(ctx, base);
  uint32_t text = 0;
  if (ctx.r3.u32 != 0xFFFFFFFFu) {
    H::TextPointer(ctx, base);
    text = ctx.r3.u32;
  }
  ctx = saved;
  return text;
}

// The dialog reads one of the six pointers of a character's taunt row while it runs. The carrier's row holds the Ultra
// fighter's USF2 quotes for that call only, so the carrier's own quotes stay for every other fight.
template <typename H>
void ShowQuote(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  const int side = ctx.lr == H::kWinnerReturn[0] ? 0 : ctx.lr == H::kWinnerReturn[1] ? 1 : -1;
  if (side < 0 || !CharacterMovesPageReady() || !UltraFightersEnabled()) return original(ctx, base);
  const QuoteSet* found = QuoteSetFor(int(ctx.r3.u32), RequestedIdentity(side));
  if (!found) return original(ctx, base);
  const QuoteSet& set = *found;
  const size_t index = size_t(found - kQuoteSets.data());
  static std::array<std::array<uint32_t, kMaxQuotes>, kQuoteSets.size()> names{};
  std::array<uint32_t, kMaxQuotes> texts{};
  for (size_t i = 0; i < set.ids.size(); ++i) {
    if (!names[index][i]) names[index][i] = GuestName(base, set.ids[i]);
    texts[i] = names[index][i] ? QuoteText<H>(ctx, base, names[index][i]) : 0;
    if (!texts[i]) return original(ctx, base);
  }
  const uint32_t row = H::kTaunts + uint32_t(set.carrier) * kTauntRowBytes;
  std::array<uint32_t, kTauntsPerCharacter> kept{};
  for (uint32_t i = 0; i < kTauntsPerCharacter; ++i) {
    kept[i] = LoadBE32(GuestHost(base, row + i * 4), 0);
    GuestStoreBE32(base, row + i * 4, texts[QuoteForSlot(i, set.ids.size())]);
  }
  original(ctx, base);
  for (uint32_t i = 0; i < kTauntsPerCharacter; ++i) GuestStoreBE32(base, row + i * 4, kept[i]);
}

}  // namespace
}  // namespace ssf2t::character

REX_EXTERN(__imp__base013_sub_823CE430);
REX_HOOK_RAW(base013_sub_823CE430) {
  ssf2t::character::ShowQuote<ssf2t::character::Base013Quote>(ctx, base, __imp__base013_sub_823CE430);
}

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_823CEA10);
REX_HOOK_RAW(tu023_sub_823CEA10) {
  ssf2t::character::ShowQuote<ssf2t::character::TitleUpdate023Quote>(ctx, base, __imp__tu023_sub_823CEA10);
}
#endif

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
