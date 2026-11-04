#include <array>
#include <cstdint>
#include <cstring>
#include <unordered_map>

#include <rex/hook.h>
#include <rex/logging.h>

#include "character/akuma_overlay.h"
#include "character/ultra_fighters.h"
#include "character/ultra_navigation.h"
#include "guest_memory.h"
#include "title_update_image.h"

namespace ssf2t::character {
namespace {

struct Base013Name {
  static constexpr uint32_t kPlanePointer = 0x83E09D08;
};

struct TitleUpdate023Name {
  static constexpr uint32_t kPlanePointer = 0x83E0A220;
};

constexpr uint32_t kPlaneBytes = 0x4000;

thread_local int t_name_side = -1;
uint32_t g_session = 0;
std::array<std::unordered_map<uint32_t, uint32_t>, 2> g_background;

void BeginSessionIfNew() {
  const uint32_t session = CharacterSelectSession();
  if (session == g_session) return;
  g_session = session;
  for (auto& cells : g_background) cells.clear();
}

template <typename Original>
void DrawPlayerName(int side, PPCContext& ctx, uint8_t* base, Original original) {
  if (!UltraFightersEnabled()) return original(ctx, base);
  BeginSessionIfNew();
  t_name_side = side;
  original(ctx, base);
  t_name_side = -1;
}

// The guest names are tile blits over the background: remember what each blit replaced for the first time and put
// it back for a host-owned player, so nothing of the guest name remains.
template <typename P>
void BlitName(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  const int side = t_name_side;
  if (side < 0 || !UltraFightersEnabled()) return original(ctx, base);
  const uint32_t plane = LoadBE32(GuestHost(base, P::kPlanePointer), 0) & ~0xFFu;
  if (plane < 0x10000 || plane >= 0xFFFF0000u - kPlaneBytes) return original(ctx, base);
  uint8_t* cells = base + plane;
  std::array<uint8_t, kPlaneBytes> before;
  std::memcpy(before.data(), cells, kPlaneBytes);
  original(ctx, base);
  auto& background = g_background[side];
  for (uint32_t at = 0; at < kPlaneBytes; at += 4) {
    if (std::memcmp(before.data() + at, cells + at, 4) == 0) continue;
    uint32_t raw;
    std::memcpy(&raw, before.data() + at, 4);
    background.try_emplace(at, raw);
  }
  if (!GlobalUltraNavigation().OnEvilRyu(side)) return;
  for (const auto& [at, raw] : background) std::memcpy(cells + at, &raw, 4);
}

}  // namespace
}  // namespace ssf2t::character

#define SSF2T_NAME_PLAYER_HOOK(name, side) \
  REX_EXTERN(__imp__##name); \
  REX_HOOK_RAW(name) { \
    ssf2t::character::DrawPlayerName(side, ctx, base, __imp__##name); \
  }

#define SSF2T_NAME_BLIT_HOOK(name, P) \
  REX_EXTERN(__imp__##name); \
  REX_HOOK_RAW(name) { \
    ssf2t::character::BlitName<ssf2t::character::P>(ctx, base, __imp__##name); \
  }

SSF2T_NAME_PLAYER_HOOK(base013_sub_82236D38, 0)
SSF2T_NAME_PLAYER_HOOK(base013_sub_822366E8, 1)
SSF2T_NAME_BLIT_HOOK(base013_sub_8238E9A8, Base013Name)
SSF2T_NAME_BLIT_HOOK(base013_sub_8238ECD8, Base013Name)

#if SSF2T_TITLE_UPDATE_IMAGE
SSF2T_NAME_PLAYER_HOOK(tu023_sub_82236E60, 0)
SSF2T_NAME_PLAYER_HOOK(tu023_sub_82236810, 1)
SSF2T_NAME_BLIT_HOOK(tu023_sub_8238E998, TitleUpdate023Name)
SSF2T_NAME_BLIT_HOOK(tu023_sub_8238ECC8, TitleUpdate023Name)
#endif
