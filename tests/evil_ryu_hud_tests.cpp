#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include "character/evil_ryu_sprites.h"
#include "character/fighter_identity.h"

namespace {

using namespace ssf2t::character;

int failures = 0;

void Check(bool condition, const char* what) {
  if (condition) return;
  std::printf("FAIL %s\n", what);
  ++failures;
}

void TestElementHash() {
  Check(ElementHash("p1_character") == 0xD7E46F0Fu, "the hash matches the guest's p1_character constant");
  Check(ElementHash("p2_character") == 0x4E06090Eu, "the hash matches the guest's p2_character constant");
  Check(ElementHash("p1_healthbar_yellow") == 0x9800AF13u, "the hash matches the guest's health bar constant");
  Check(ElementHash("P1_Character") == ElementHash("p1_character"), "names hash without regard to case");
  Check(ElementHash("p1_portrait_Ryu") != ElementHash("p2_portrait_Ryu"), "each side has its own Ryu portrait element");
}

void TestHudRouting() {
  Check(HudShowsEvilRyu(true, FighterIdentity::kEvilRyu, 0), "the Evil Ryu side shows Evil Ryu");
  Check(!HudShowsEvilRyu(true, FighterIdentity::kGuest, 0), "an ordinary Ryu side keeps Ryu");
  Check(!HudShowsEvilRyu(false, FighterIdentity::kEvilRyu, 0), "Ultra off keeps Ryu");
  Check(!HudShowsEvilRyu(true, FighterIdentity::kEvilRyu, kAkumaGuestCharacter), "Akuma is never routed");
  Check(!HudShowsEvilRyu(true, FighterIdentity::kEvilRyu, -1), "a side without a character is never routed");

  ResetIdentities();
  SetRequestedIdentity(0, FighterIdentity::kEvilRyu);
  Check(HudShowsEvilRyu(true, RequestedIdentity(0), 0), "P1 Evil Ryu routes P1");
  Check(!HudShowsEvilRyu(true, RequestedIdentity(1), 0), "P1 Evil Ryu leaves a P2 Ryu alone");
  ResetIdentities();
  SetRequestedIdentity(1, FighterIdentity::kEvilRyu);
  Check(!HudShowsEvilRyu(true, RequestedIdentity(0), 0), "P2 Evil Ryu leaves a P1 Ryu alone");
  Check(HudShowsEvilRyu(true, RequestedIdentity(1), 0), "P2 Evil Ryu routes P2");
  SetRequestedIdentity(0, FighterIdentity::kEvilRyu);
  Check(HudShowsEvilRyu(true, RequestedIdentity(0), 0) && HudShowsEvilRyu(true, RequestedIdentity(1), 0),
        "Evil vs Evil routes both sides");
  ResetIdentities();
}

void TestPortraitCell() {
  constexpr uint32_t kNative = 0x40000100;
  constexpr uint32_t kEvil = 0x30000200;
  uint32_t saved = 0;
  uint32_t cell = RoutePortraitCell(true, kNative, kEvil, saved);
  Check(cell == kEvil && saved == kNative, "the element takes the Evil cell and remembers its own");
  cell = RoutePortraitCell(true, cell, kEvil, saved);
  Check(cell == kEvil && saved == kNative, "repeating the frame keeps the remembered cell");
  cell = RoutePortraitCell(false, cell, kEvil, saved);
  Check(cell == kNative, "the element returns to its own cell");

  constexpr uint32_t kRebuilt = 0x40000300;
  cell = RoutePortraitCell(true, kRebuilt, kEvil, saved);
  Check(cell == kEvil && saved == kRebuilt, "a rebuilt element is remembered afresh");

  uint32_t other = 0;
  Check(RoutePortraitCell(false, kNative, kEvil, other) == kNative, "an element never routed is left as it is");
  Check(other == 0, "an element never routed remembers nothing");
}

void TestIconFile() {
  auto icon = std::make_unique<EvilHudIcon>();
  std::vector<uint8_t> file(8 + kHudIconBytes, 0x5A);
  std::memcpy(file.data(), "SSF2ERI1", 8);
  Check(ParseEvilHudIcon(file.data(), file.size(), icon.get()), "the icon file parses");
  Check((*icon)[0] == 0x5A && (*icon)[kHudIconBytes - 1] == 0x5A, "the pixels are copied whole");
  Check(!ParseEvilHudIcon(file.data(), file.size() - 1, icon.get()), "a truncated icon is refused");
  file[0] = 'X';
  Check(!ParseEvilHudIcon(file.data(), file.size(), icon.get()), "a wrong magic is refused");
}

void TestGeneratedIcon() {
  const char* root = std::getenv("SSF2T_GAME_DATA");
  if (!root) return;
  const std::filesystem::path path = std::filesystem::path(root) / kEvilRyuHudIconFile;
  std::ifstream in(path, std::ios::binary);
  std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  auto icon = std::make_unique<EvilHudIcon>();
  Check(ParseEvilHudIcon(file.data(), file.size(), icon.get()), "the generated icon parses");
  size_t opaque = 0;
  size_t red = 0;
  for (size_t at = 0; at < kHudIconBytes; at += 4) {
    opaque += (*icon)[at] == 0xFF;
    red += (*icon)[at + 1] > 180 && (*icon)[at + 2] < 80 && (*icon)[at + 3] < 80;
  }
  Check(opaque > size_t(kHudIconSize) * kHudIconSize / 2, "the generated icon is mostly opaque");
  Check(red > 200, "the generated icon shows the red headband");
}

}  // namespace

int main() {
  TestElementHash();
  TestHudRouting();
  TestPortraitCell();
  TestIconFile();
  TestGeneratedIcon();
  if (failures == 0) std::printf("evil ryu hud tests passed\n");
  return failures == 0 ? 0 : 1;
}
