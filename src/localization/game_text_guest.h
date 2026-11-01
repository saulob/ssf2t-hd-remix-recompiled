#pragma once

#include <cstdint>

namespace ssf2t {

// The literal the Training mode prompt passes to the UI text helper; both images hold it at this address.
inline constexpr uint32_t kPressStartActivateLiteral = 0x820064A8;

// The call that reads a whole string table file (`TEXT/STR_<language>.TXT`) into a buffer, and the instruction after
// its only call site; r28 holds the language slot there. The texture parse takes r4 = texture file, r5 = size; the prompt helper takes r4 = text; the leaderboard error helper takes r3 = the leaderboard screen; the arcade quote init copies the quote text from the read-only image into a writable pool, whose quote area is patched after the init.
struct Base013GameText {
  static constexpr const char* kName = "base013";
  static constexpr uint32_t kTextTableRead = 0x8247E4B0;
  static constexpr uint32_t kTextTableReadReturn = 0x82439EA4;
  static constexpr uint32_t kTextureParse = 0x823FFDC8;
  static constexpr uint32_t kPromptText = 0x823CFB10;
  static constexpr uint32_t kLeaderboardError = 0x823DB3D0;
  static constexpr uint32_t kArcadeQuoteInit = 0x8362C0B0;
  static constexpr uint32_t kArcadeQuotePool = 0x84A4AFA0;
  static constexpr uint32_t kArcadeQuotePoolSize = 0x486;
};

struct TitleUpdate023GameText {
  static constexpr const char* kName = "tu023";
  static constexpr uint32_t kTextTableRead = 0x8247EFE0;
  static constexpr uint32_t kTextTableReadReturn = 0x8243A614;
  static constexpr uint32_t kTextureParse = 0x82400478;
  static constexpr uint32_t kPromptText = 0x823D00F0;
  static constexpr uint32_t kLeaderboardError = 0x823DBAB0;
  static constexpr uint32_t kArcadeQuoteInit = 0x8362C828;
  static constexpr uint32_t kArcadeQuotePool = 0x84A4B4C0;
  static constexpr uint32_t kArcadeQuotePoolSize = 0x486;
};

}  // namespace ssf2t
