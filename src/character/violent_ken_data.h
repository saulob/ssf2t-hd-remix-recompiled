#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "character/evil_ryu_palette.h"
#include "character/evil_ryu_sprites.h"

namespace ssf2t::character {

inline constexpr char kViolentKenGameplayFile[] = "violent_ken_gameplay.bin";
inline constexpr char kViolentKenSpriteFile[] = "violent_ken_sprites.bin";
inline constexpr char kViolentKenPaletteFile[] = "violent_ken_palette.bin";
inline constexpr char kViolentKenHdPaletteFile[] = "violent_ken_hd_palette.bin";
inline constexpr char kViolentKenHudIconFile[] = "violent_ken_hud_icon.bin";

// Actions with no guest slot; the ids are past the end of every guest action header.
inline constexpr uint8_t kInazumaAction = 0xE0;
inline constexpr uint8_t kRasetsuAction = 0xE2;
inline constexpr uint8_t kShinbuAction = 0xE4;
inline constexpr uint8_t kShinbuRushSub = 1;
inline constexpr uint8_t kShinbuRiseSub = 2;
inline constexpr uint8_t kShinbuLandSub = 3;
inline constexpr uint8_t kShinbuRecoverySub = 4;

enum class PackagedField : uint8_t { kDuration = 0, kHead = 1, kBody = 2, kLegs = 3, kAttack = 4 };

struct PackagedFrame {
  uint16_t duration = 0;
  uint16_t event = 0;
  uint16_t source_key = 0;
  uint8_t head = 0, body = 0, legs = 0, attack = 0, push = 0, shape = 0, weight = 0;
  bool stance_art = false;
};

struct PackagedAction {
  uint8_t action = 0;
  uint8_t sub = 0;
  std::vector<PackagedFrame> frames;
};

struct PackagedPatch {
  uint8_t action = 0;
  uint8_t sub = 0;
  uint8_t frame = 0;
  PackagedField field = PackagedField::kDuration;
  uint8_t expected = 0;
  uint8_t value = 0;
};

inline constexpr int kAttackRecordBytes = 16;

struct PackagedAttack {
  uint8_t id = 0;
  bool replace = false;
  uint8_t record[kAttackRecordBytes] = {};
};

struct ViolentKenGameplay {
  std::vector<PackagedAction> actions;
  std::vector<PackagedPatch> patches;
  std::vector<PackagedAttack> attacks;
};

bool ParseViolentKenGameplay(const uint8_t* file, size_t size, ViolentKenGameplay* out);

void SetViolentKenDirectory(const std::filesystem::path& directory);
// Null when the package is missing or invalid. Without it Violent Ken would play as Ken, so nothing offers him.
const ViolentKenGameplay* LoadedViolentKenGameplay();
bool ViolentKenGameplayReady();

const EvilSpriteImage* LoadedViolentKenArt(EvilArt art);
const EvilRyuPalette* LoadedViolentKenPalette();
const EvilRyuHdPalette* LoadedViolentKenHdPalette();
const EvilHudIcon* LoadedViolentKenHudIcon();

}  // namespace ssf2t::character
