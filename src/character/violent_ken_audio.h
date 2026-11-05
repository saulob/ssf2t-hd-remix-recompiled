#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>

#include "character/evil_ryu_audio.h"

namespace ssf2t::character {

inline constexpr char kViolentKenAudioFile[] = "violent_ken_audio.bin";

enum class ViolentClip : uint32_t {
  kHadoken = 1,
  kShoryuken = 2,
  kTatsumaki = 3,
  kThrow = 4,
  kKo = 5,
  kRasetsu = 6,
  kShinbu = 7,
  kAnnouncerName = 8,
};
inline constexpr size_t kViolentClipCount = 8;

struct ViolentKenAudioPackage {
  std::array<EvilClipData, kViolentClipCount> clips;
  const char* error = nullptr;

  const EvilClipData& Clip(ViolentClip clip) const { return clips[size_t(clip) - 1]; }
};

ViolentKenAudioPackage ParseViolentKenAudio(const uint8_t* file, size_t size);

void SetViolentKenAudioDirectory(const std::filesystem::path& directory);
// Null when the package is missing or invalid; the guest's own Ken sounds then play untouched.
const ViolentKenAudioPackage* LoadedViolentKenAudio();

inline constexpr uint16_t kKenShoryukenSound = 101;
inline constexpr uint16_t kKenHadokenSound = 102;
inline constexpr uint16_t kKenTatsumakiSound = 103;
inline constexpr uint16_t kKenThrowSound = 104;
inline constexpr uint16_t kKenKoSound = 105;
inline constexpr uint16_t kKenAnnouncerNameSound = 465;
// The Continue screen calls this P2 voice for a loser on either side, so its side comes from the loser, like the end
// of round KO. USF2 plays the fighter's Shoryuken voice there too.
inline constexpr uint16_t kKenContinueSound = kKenShoryukenSound + kPlayerTwoVoiceOffset;

struct ViolentSound {
  uint16_t id;
  uint16_t carrier;
  ViolentClip clip;
};

inline constexpr uint16_t kViolentVoiceP1 = 0xD0;
inline constexpr uint16_t kViolentVoiceP2 = 0xD6;
inline constexpr uint16_t kViolentRasetsuOffset = 4;
inline constexpr uint16_t kViolentShinbuOffset = 5;
inline constexpr uint16_t kViolentKoP1 = 0xDC;
inline constexpr uint16_t kViolentKoP2 = 0xDD;
inline constexpr uint16_t kViolentAnnouncerName = 0xDE;

inline constexpr std::array<ViolentSound, 15> kViolentSounds = {{
    {kViolentVoiceP1 + 0, kKenShoryukenSound, ViolentClip::kShoryuken},
    {kViolentVoiceP1 + 1, kKenHadokenSound, ViolentClip::kHadoken},
    {kViolentVoiceP1 + 2, kKenTatsumakiSound, ViolentClip::kTatsumaki},
    {kViolentVoiceP1 + 3, kKenThrowSound, ViolentClip::kThrow},
    {kViolentVoiceP1 + kViolentRasetsuOffset, kKenHadokenSound, ViolentClip::kRasetsu},
    {kViolentVoiceP1 + kViolentShinbuOffset, kKenHadokenSound, ViolentClip::kShinbu},
    {kViolentVoiceP2 + 0, kKenShoryukenSound + kPlayerTwoVoiceOffset, ViolentClip::kShoryuken},
    {kViolentVoiceP2 + 1, kKenHadokenSound + kPlayerTwoVoiceOffset, ViolentClip::kHadoken},
    {kViolentVoiceP2 + 2, kKenTatsumakiSound + kPlayerTwoVoiceOffset, ViolentClip::kTatsumaki},
    {kViolentVoiceP2 + 3, kKenThrowSound + kPlayerTwoVoiceOffset, ViolentClip::kThrow},
    {kViolentVoiceP2 + kViolentRasetsuOffset, kKenHadokenSound + kPlayerTwoVoiceOffset, ViolentClip::kRasetsu},
    {kViolentVoiceP2 + kViolentShinbuOffset, kKenHadokenSound + kPlayerTwoVoiceOffset, ViolentClip::kShinbu},
    {kViolentKoP1, kKenKoSound, ViolentClip::kKo},
    {kViolentKoP2, kKenKoSound + kPlayerTwoVoiceOffset, ViolentClip::kKo},
    {kViolentAnnouncerName, kKenAnnouncerNameSound, ViolentClip::kAnnouncerName},
}};

enum class ViolentMoveVoice : uint8_t { kNone, kRasetsu, kShinbu };

struct ViolentSoundState {
  std::array<bool, 2> violent{};
  std::array<ViolentMoveVoice, 2> move_voice{};
  int round_end_ko_side = -1;
  bool announce_name = false;
};

// The sound to play instead of `sound`, or 0 to keep it. Only ever one or the other plays.
uint16_t ViolentSoundFor(uint16_t sound, const ViolentSoundState& state);

}  // namespace ssf2t::character
