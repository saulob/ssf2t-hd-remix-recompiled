#include <cstring>
#include <string>
#include <string_view>
#include <utility>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#include "character/character_moves.h"
#include "character/ultra_fighters.h"
#include "guest_memory.h"
#include "localization/baked_texture.h"
#include "localization/active_language.h"
#include "localization/arcade_quotes.h"
#include "localization/game_text.h"
#include "localization/game_text_guest.h"
#include "localization/ui_text.h"
#include "title_update_image.h"

namespace {

constexpr uint32_t kEnglishSlot = 0;

template <typename G>
void ReadTextTable(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  const uint32_t size_address = ctx.r4.u32;
  const bool text_table = ctx.lr == G::kTextTableReadReturn;
  const bool english_table =
      text_table && ctx.r28.u32 == kEnglishSlot && ssf2t::localization::PortugueseBrazilSelected();
  const bool moves_text = text_table && ssf2t::character::UltraFightersEnabled();
  original(ctx, base);
  const uint32_t buffer = ctx.r3.u32;
  if ((!english_table && !moves_text) || buffer == 0) return;

  const uint32_t size = ssf2t::LoadBE32(base, size_address);
  std::string text(reinterpret_cast<const char*>(ssf2t::GuestHost(base, buffer)), size);
  const uint32_t moves = moves_text ? ssf2t::character::PatchMoveListText(text) : 0;
  if (!english_table && moves == 0) return;
  ssf2t::localization::LocalizedTable table;
  if (english_table) {
    table = ssf2t::localization::BuildLocalizedTable(text, ssf2t::localization::PortugueseBrazilGameText().rows);
  } else {
    table.text = std::move(text);
  }
  const uint32_t copy = REX_KERNEL_MEMORY()->SystemHeapAlloc(uint32_t(table.text.size() + 1));
  if (copy == 0) {
    REXLOG_WARN("Language: game text not applied, out of guest memory guest={}", G::kName);
    return;
  }
  uint8_t* destination = const_cast<uint8_t*>(ssf2t::GuestHost(base, copy));
  std::memcpy(destination, table.text.data(), table.text.size());
  destination[table.text.size()] = '\0';
  ssf2t::StoreBE32(base, size_address, uint32_t(table.text.size()));
  ctx.r3.u64 = copy;
  if (english_table) {
    REXLOG_INFO("Language: pt-BR game text replaced={} kept={} stale={} guest={}", table.replaced, table.kept,
                table.stale, G::kName);
  }
  if (moves != 0) {
    REXLOG_INFO("DEBUG_SAULO EVIL_RYU character-moves text lines={} guest={}", moves, G::kName);
  }
}

template <typename G>
void ParseTexture(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (ssf2t::localization::PortugueseBrazilSelected() && ctx.r4.u32 != 0) {
    uint8_t* data = const_cast<uint8_t*>(ssf2t::GuestHost(base, ctx.r4.u32));
    if (const auto* texture = ssf2t::localization::LocalizeBakedTexture(data, ctx.r5.u32)) {
      REXLOG_INFO("Language: pt-BR texture {} '{}' guest={}", texture->name, texture->text, G::kName);
    }
  }
  original(ctx, base);
}

uint32_t PressStartActivateCopy(uint8_t* base) {
  static uint32_t copy = 0;
  if (copy != 0) return copy;
  for (const auto& row : ssf2t::localization::PortugueseBrazilGameText().rows) {
    if (row.id != "PRESS_START_ACTIVATE" || row.keep_english) continue;
    copy = REX_KERNEL_MEMORY()->SystemHeapAlloc(uint32_t(row.text.size() + 1));
    if (copy != 0) {
      uint8_t* destination = const_cast<uint8_t*>(ssf2t::GuestHost(base, copy));
      std::memcpy(destination, row.text.c_str(), row.text.size() + 1);
    }
    break;
  }
  return copy;
}

template <typename G>
void SetPromptText(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (ctx.r4.u32 == ssf2t::kPressStartActivateLiteral && ssf2t::localization::PortugueseBrazilSelected()) {
    if (const uint32_t copy = PressStartActivateCopy(base)) ctx.r4.u64 = copy;
  }
  original(ctx, base);
}

// The helper writes a fixed English line into the second cell (32 bytes) of the leaderboard screen's cell table.
constexpr uint32_t kLeaderboardPendingFlag = 3276;
constexpr uint32_t kLeaderboardCellPointers = 3292;
constexpr size_t kLeaderboardCellSize = 32;

template <typename G>
void ShowLeaderboardError(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  const uint32_t screen = ctx.r3.u32;
  const bool pending = *ssf2t::GuestHost(base, screen + kLeaderboardPendingFlag) != 0;
  original(ctx, base);
  if (!pending || !ssf2t::localization::PortugueseBrazilSelected()) return;
  const std::string_view text = ssf2t::Tr("Error reading entries.");
  const uint32_t cell = ssf2t::LoadBE32(base, screen + kLeaderboardCellPointers);
  if (cell == 0 || text.size() >= kLeaderboardCellSize) return;
  uint8_t* destination = const_cast<uint8_t*>(ssf2t::GuestHost(base, cell));
  std::memcpy(destination, text.data(), text.size());
  destination[text.size()] = '\0';
}

template <typename G>
void InitArcadeQuotes(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  original(ctx, base);
  if (!ssf2t::localization::PortugueseBrazilSelected()) return;
  uint8_t* pool = const_cast<uint8_t*>(ssf2t::GuestHost(base, G::kArcadeQuotePool));
  const size_t lines = ssf2t::localization::PatchArcadeQuotes(pool, G::kArcadeQuotePoolSize);
  REXLOG_INFO("Language: pt-BR arcade quotes lines={} guest={}", lines, G::kName);
}

}  // namespace

REX_EXTERN(__imp__base013_sub_8247E4B0);
REX_HOOK_RAW(base013_sub_8247E4B0) {
  ReadTextTable<ssf2t::Base013GameText>(ctx, base, __imp__base013_sub_8247E4B0);
}

REX_EXTERN(__imp__base013_sub_823FFDC8);
REX_HOOK_RAW(base013_sub_823FFDC8) {
  ParseTexture<ssf2t::Base013GameText>(ctx, base, __imp__base013_sub_823FFDC8);
}

REX_EXTERN(__imp__base013_sub_823CFB10);
REX_HOOK_RAW(base013_sub_823CFB10) {
  SetPromptText<ssf2t::Base013GameText>(ctx, base, __imp__base013_sub_823CFB10);
}

REX_EXTERN(__imp__base013_sub_823DB3D0);
REX_HOOK_RAW(base013_sub_823DB3D0) {
  ShowLeaderboardError<ssf2t::Base013GameText>(ctx, base, __imp__base013_sub_823DB3D0);
}

REX_EXTERN(__imp__base013_sub_8362C0B0);
REX_HOOK_RAW(base013_sub_8362C0B0) {
  InitArcadeQuotes<ssf2t::Base013GameText>(ctx, base, __imp__base013_sub_8362C0B0);
}

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_8247EFE0);
REX_HOOK_RAW(tu023_sub_8247EFE0) {
  ReadTextTable<ssf2t::TitleUpdate023GameText>(ctx, base, __imp__tu023_sub_8247EFE0);
}

REX_EXTERN(__imp__tu023_sub_82400478);
REX_HOOK_RAW(tu023_sub_82400478) {
  ParseTexture<ssf2t::TitleUpdate023GameText>(ctx, base, __imp__tu023_sub_82400478);
}

REX_EXTERN(__imp__tu023_sub_823D00F0);
REX_HOOK_RAW(tu023_sub_823D00F0) {
  SetPromptText<ssf2t::TitleUpdate023GameText>(ctx, base, __imp__tu023_sub_823D00F0);
}

REX_EXTERN(__imp__tu023_sub_823DBAB0);
REX_HOOK_RAW(tu023_sub_823DBAB0) {
  ShowLeaderboardError<ssf2t::TitleUpdate023GameText>(ctx, base, __imp__tu023_sub_823DBAB0);
}

REX_EXTERN(__imp__tu023_sub_8362C828);
REX_HOOK_RAW(tu023_sub_8362C828) {
  InitArcadeQuotes<ssf2t::TitleUpdate023GameText>(ctx, base, __imp__tu023_sub_8362C828);
}
#endif
