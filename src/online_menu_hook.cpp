#include <algorithm>
#include <cstring>
#include <string_view>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#include "guest_memory.h"
#include "online_menu.h"
#include "player_match_guest.h"
#include "title_update_image.h"

namespace {

uint8_t* GuestPtr(uint8_t* base, uint32_t address) { return const_cast<uint8_t*>(ssf2t::GuestHost(base, address)); }

bool g_multiplayer_patched = false;
bool g_in_multiplayer = false;
bool g_in_game_lobby = false;

bool StartsLikeScreenSet(const char* text) {
  for (int i = 0; i < 8 && text[i]; ++i) {
    if (text[i] == '<') return std::strncmp(text + i, "<UIScreenSet>", 13) == 0;
  }
  return false;
}

// A script document opens with the XML declaration, then <scripts>.
bool StartsLikeScripts(const char* text) {
  constexpr size_t kHead = 128;
  const void* end = std::memchr(text, '\0', kHead);
  const size_t size = end ? size_t(static_cast<const char*>(end) - text) : kHead;
  return std::string_view(text, size).find("<scripts>") != std::string_view::npos;
}

void LogMultiplayerItems(const char* guest) {
  const int removed = g_multiplayer_patched ? 0 : 1;
  REXLOG_INFO("DEBUG_SAULO multiplayer menu items local=1 online=1 ranked={} tournament={} guest={}", removed, removed,
              guest);
}

template <typename G>
struct OnlineMenuHooks {
  // TiXmlDocument::Parse(this, text, location, encoding). The loader frees `text` right after parsing, so the
  // document holds no pointer into it and a patched copy can be parsed instead. The returned text position is mapped
  // back onto the guest's buffer.
  static void ParseXml(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const uint32_t text = ctx.r4.u32;
    const char* guest_text = text ? reinterpret_cast<const char*>(GuestPtr(base, text)) : nullptr;
    if (guest_text && StartsLikeScreenSet(guest_text)) {
      const auto patch = ssf2t::PatchScreenSet(guest_text);
      if (!patch || !ParsePatched(ctx, base, original, patch->xml)) {
        REXLOG_INFO("DEBUG_SAULO multiplayer menu layout original guest={}", G::kName);
        original(ctx, base);
        return;
      }
      g_multiplayer_patched = patch->multiplayer;
      LogMultiplayerItems(G::kName);
      REXLOG_INFO("DEBUG_SAULO online_match menu items find=1 custom={} create=1 guest={}",
                  patch->player_match ? 0 : 1, G::kName);
      REXLOG_INFO("DEBUG_SAULO create_player_match players control removed={} guest={}", patch->create_match,
                  G::kName);
      return;
    }
    if (guest_text && StartsLikeScripts(guest_text)) {
      const auto patch = ssf2t::PatchLobbyScripts(guest_text);
      if (patch && ParsePatched(ctx, base, original, patch->xml)) {
        REXLOG_INFO("DEBUG_SAULO game_lobby actions removed gamer_card={} players_list={} slot_help={} guest={}",
                    patch->gamer_card, patch->players_list, patch->slot_help, G::kName);
        return;
      }
    }
    original(ctx, base);
  }

  // Parses `xml` in place of the guest's text; the returned text position is mapped back onto the guest's buffer.
  static bool ParsePatched(PPCContext& ctx, uint8_t* base, PPCFunc* original, const std::string& xml) {
    const uint32_t text = ctx.r4.u32;
    const uint32_t original_size = uint32_t(std::strlen(reinterpret_cast<const char*>(GuestPtr(base, text))));
    const uint32_t size = uint32_t(xml.size() + 1);
    const uint32_t copy = REX_KERNEL_MEMORY()->SystemHeapAlloc(size);
    if (!copy) return false;
    std::memcpy(GuestPtr(base, copy), xml.c_str(), size);
    ctx.r4.u64 = copy;
    original(ctx, base);
    const uint32_t end = ctx.r3.u32;
    if (end >= copy && end < copy + size) {
      const uint32_t left = std::min<uint32_t>(copy + size - 1 - end, original_size);
      ctx.r3.u64 = text + original_size - left;
    }
    REX_KERNEL_MEMORY()->SystemHeapFree(copy);
    return true;
  }

  static void LoadHelpText(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    original(ctx, base);
    const ssf2t::HelpTextPatch patch = ssf2t::PatchHelpText(base + G::kHelpTextTable);
    REXLOG_INFO("DEBUG_SAULO multiplayer menu help rows multiplayer={} player_match={} create_player_match={} guest={}",
                patch.multiplayer, patch.player_match, patch.create_match, G::kName);
  }

  // Runs on each help-bar refresh; logs only when the Multiplayer or Game Lobby screen becomes the current one.
  static void ScreenMainText(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    original(ctx, base);
    const uint32_t multiplayer = ssf2t::HelpTextScreenText(base + G::kHelpTextTable, ssf2t::kMultiplayerScreen);
    const bool in_multiplayer = multiplayer && ctx.r3.u32 == multiplayer;
    if (in_multiplayer && !g_in_multiplayer) {
      REXLOG_INFO("DEBUG_SAULO multiplayer menu enter guest={}", G::kName);
      LogMultiplayerItems(G::kName);
    }
    g_in_multiplayer = in_multiplayer;
    const uint32_t lobby = ssf2t::HelpTextScreenText(base + G::kHelpTextTable, ssf2t::kGameLobbyScreen);
    const bool in_lobby = lobby && ctx.r3.u32 == lobby;
    if (in_lobby != g_in_game_lobby) {
      REXLOG_INFO("DEBUG_SAULO post_fight game_lobby {} guest={}", in_lobby ? "entered" : "left", G::kName);
    }
    g_in_game_lobby = in_lobby;
  }
};

using Base = OnlineMenuHooks<ssf2t::Base013PlayerMatch>;
using Update = OnlineMenuHooks<ssf2t::TitleUpdate023PlayerMatch>;

}  // namespace

REX_EXTERN(__imp__base013_sub_82416E80);
REX_EXTERN(__imp__base013_sub_823D4150);
REX_EXTERN(__imp__base013_sub_823D44F8);
REX_HOOK_RAW(base013_sub_82416E80) { Base::ParseXml(ctx, base, __imp__base013_sub_82416E80); }
REX_HOOK_RAW(base013_sub_823D4150) { Base::LoadHelpText(ctx, base, __imp__base013_sub_823D4150); }
REX_HOOK_RAW(base013_sub_823D44F8) { Base::ScreenMainText(ctx, base, __imp__base013_sub_823D44F8); }

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_82417548);
REX_EXTERN(__imp__tu023_sub_823D4820);
REX_EXTERN(__imp__tu023_sub_823D4BC8);
REX_HOOK_RAW(tu023_sub_82417548) { Update::ParseXml(ctx, base, __imp__tu023_sub_82417548); }
REX_HOOK_RAW(tu023_sub_823D4820) { Update::LoadHelpText(ctx, base, __imp__tu023_sub_823D4820); }
REX_HOOK_RAW(tu023_sub_823D4BC8) { Update::ScreenMainText(ctx, base, __imp__tu023_sub_823D4BC8); }
#endif
