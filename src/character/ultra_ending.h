#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "character/character_moves.h"
#include "character/evil_ryu_ending_text.h"
#include "character/fighter_identity.h"

namespace ssf2t::character {

// USF2 scheduler ed_chr19: 3000 frames at 60 Hz, a new text line every 300, cuts 1, 2, 4 and 3 at 0, 1200, 2100 and 2400.
inline constexpr int kEndingCuts = 4;
inline constexpr std::array<float, kEndingCuts> kEndingCutSeconds = {0.0f, 20.0f, 35.0f, 40.0f};
inline constexpr float kEndingSeconds = 50.0f;
inline constexpr float kEndingLineSeconds = 5.0f;

inline constexpr std::string_view kViolentKenEndingGroup = "end_violentken";
inline constexpr char kViolentKenEndingFile[] = "violent_ken_ending.bin";

// ed_chr18: 2700 frames, a new line every 300, the same cuts at 0, 1200, 1800 and 2400.
inline constexpr std::array<float, kEndingCuts> kEvilRyuEndingCutSeconds = {0.0f, 20.0f, 30.0f, 40.0f};
inline constexpr float kEvilRyuEndingSeconds = 45.0f;
inline constexpr std::string_view kEvilRyuEndingGroup = "end_evilryu";
inline constexpr char kEvilRyuEndingFile[] = "evil_ryu_ending.bin";

// Everything that differs between the Ultra fighters' endings. Each plays in its carrier's ending screen, in a group of its
// own beside the carrier's.
struct UltraEnding {
  FighterIdentity identity;
  int carrier;
  std::string_view group;
  std::string_view screen_tag;
  std::string_view carrier_group_tag;
  const char* file;
  std::array<float, kEndingCuts> cut_seconds;
  float seconds;
  int lines;
  const std::string_view* line_ids;
  // The literals and music of the carrier's case in the guest's ending switch, shared by both images.
  uint32_t guest_set_name;
  uint32_t guest_screen_name;
  uint32_t guest_music;
  const char* log_name;
};

inline constexpr UltraEnding kViolentKenEnding = {
    FighterIdentity::kViolentKen,
    kKenGuestCharacter,
    kViolentKenEndingGroup,
    "<name>EndKen</name>",
    "<name>end_ken</name>",
    kViolentKenEndingFile,
    kEndingCutSeconds,
    kEndingSeconds,
    kViolentKenEndingLines,
    kViolentKenEndingIds.data(),
    0x82009030,
    0x82009038,
    0x21,
    "VIOLENT_KEN"};

inline constexpr UltraEnding kEvilRyuEnding = {
    FighterIdentity::kEvilRyu,
    kRyuGuestCharacter,
    kEvilRyuEndingGroup,
    "<name>EndRyu</name>",
    "<name>end_ryu</name>",
    kEvilRyuEndingFile,
    kEvilRyuEndingCutSeconds,
    kEvilRyuEndingSeconds,
    kEvilRyuEndingLines,
    kEvilRyuEndingIds.data(),
    0x82009088,
    0x82009090,
    0x23,
    "EVIL_RYU"};

inline constexpr std::array<const UltraEnding*, 2> kUltraEndings = {&kEvilRyuEnding, &kViolentKenEnding};

// The cuts in playing order, as A,R,G,B textures in the size of the guest's End_ art.
inline constexpr uint32_t kEndingCutWidth = 512;
inline constexpr uint32_t kEndingCutHeight = 256;
inline constexpr size_t kEndingCutBytes = size_t(kEndingCutWidth) * kEndingCutHeight * 4;

struct UltraEndingArt {
  std::vector<uint8_t> pixels;
  const uint8_t* Cut(int index) const { return pixels.data() + size_t(index) * kEndingCutBytes; }
};

bool ParseUltraEndingArt(const uint8_t* file, size_t size, UltraEndingArt* out);
void SetUltraEndingDirectory(const std::filesystem::path& directory);
// Read once from the game data folder; null when the package is missing or invalid.
const UltraEndingArt* LoadUltraEndingArt(const UltraEnding& ending);
inline const UltraEndingArt* LoadUltraEndingArt() { return LoadUltraEndingArt(kViolentKenEnding); }

// The name of the frame element that shows the cut at `index` of the playing order.
std::string EndingFrameName(const UltraEnding& ending, int index);
inline std::string EndingFrameName(int index) { return EndingFrameName(kViolentKenEnding, index); }

// The guest stores the winner's character and the ending plays whatever that is, so the carrier's ending is replaced only
// for the player who chose the Ultra fighter.
constexpr int UltraEndingSide(uint32_t ending_character, FighterIdentity identity, int carrier,
                              const std::array<FighterIdentity, 2>& requested, const std::array<bool, 2>& on_cell,
                              const std::array<uint32_t, 2>& hover) {
  if (ending_character != uint32_t(carrier)) return -1;
  for (int side = 0; side < 2; ++side) {
    if (requested[size_t(side)] == identity && on_cell[size_t(side)] && hover[size_t(side)] == uint32_t(carrier)) return side;
  }
  return -1;
}

struct UltraEndingChoice {
  const UltraEnding* ending = nullptr;
  size_t slot = 0;
  int side = -1;
};

// The ending to play for the guest's winner, if one of the Ultra fighters earned it. `on_cell` is per kUltraEndings slot.
constexpr UltraEndingChoice ChooseUltraEnding(uint32_t ending_character, const std::array<FighterIdentity, 2>& requested,
                                              const std::array<std::array<bool, 2>, kUltraEndings.size()>& on_cell,
                                              const std::array<uint32_t, 2>& hover) {
  for (size_t slot = 0; slot < kUltraEndings.size(); ++slot) {
    const UltraEnding& ending = *kUltraEndings[slot];
    const int side = UltraEndingSide(ending_character, ending.identity, ending.carrier, requested, on_cell[slot], hover);
    if (side >= 0) return {&ending, slot, side};
  }
  return {};
}

// A carrier's screen document gains the Ultra fighter's group beside the carrier's, and ending_animations gains a timeline
// per fighter. False and `xml` unchanged for any other document or layout; the carriers' own groups and animations stay as
// they are. The overloads without a fighter patch whichever of them the document is for.
bool PatchEndingScreen(std::string& xml, const UltraEnding& ending);
bool PatchEndingAnimations(std::string& xml, const UltraEnding& ending);
bool PatchEndingScreen(std::string& xml);
bool PatchEndingAnimations(std::string& xml);

}  // namespace ssf2t::character
