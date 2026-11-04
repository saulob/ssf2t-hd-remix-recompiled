#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace ssf2t::character {

inline constexpr char kEvilRyuAudioFile[] = "evil_ryu_audio.bin";

enum class EvilClip : uint32_t {
  kHadoken = 1,
  kShoryuken = 2,
  kTatsumaki = 3,
  kKo = 4,
  kShunGokuSatsu = 5,
  kAnnouncerName = 6,
};
inline constexpr size_t kEvilClipCount = 6;

struct EvilClipData {
  uint32_t rate = 0;
  std::vector<int16_t> samples;
};

struct EvilAudioPackage {
  std::array<EvilClipData, kEvilClipCount> clips;
  const char* error = nullptr;

  const EvilClipData& Clip(EvilClip clip) const { return clips[size_t(clip) - 1]; }
};

EvilAudioPackage ParseEvilAudio(const uint8_t* file, size_t size);

void SetEvilAudioDirectory(const std::filesystem::path& directory);
// Null when the package is missing or invalid; the guest's own Ryu sounds then play untouched.
const EvilAudioPackage* LoadedEvilAudio();

// Ryu's voices have one sound id per side; KO is one id for both, and the fight work names the side that lost.
inline constexpr uint16_t kRyuHadokenSound = 96;
inline constexpr uint16_t kRyuShoryukenSound = 97;
inline constexpr uint16_t kRyuTatsumakiSound = 98;
inline constexpr uint16_t kRyuKoSound = 100;
inline constexpr uint16_t kRyuAnnouncerNameSound = 464;
inline constexpr uint16_t kPlayerTwoVoiceOffset = 214;

// Ids the guest leaves unused. Each takes the flag word of its carrier, which decides when the guest creates its source
// voice and which side's voice channel it cuts.
struct EvilSound {
  uint16_t id;
  uint16_t carrier;
  EvilClip clip;
};

inline constexpr uint16_t kEvilVoiceP1 = 0xE0;
inline constexpr uint16_t kEvilVoiceP2 = 0xE4;
inline constexpr uint16_t kEvilKoP1 = 0xE8;
inline constexpr uint16_t kEvilKoP2 = 0xE9;
inline constexpr uint16_t kEvilAnnouncerName = 0xEA;
inline constexpr uint16_t kEvilRagingDemonOffset = 3;

inline constexpr std::array<EvilSound, 11> kEvilSounds = {{
    {kEvilVoiceP1 + 0, kRyuHadokenSound, EvilClip::kHadoken},
    {kEvilVoiceP1 + 1, kRyuShoryukenSound, EvilClip::kShoryuken},
    {kEvilVoiceP1 + 2, kRyuTatsumakiSound, EvilClip::kTatsumaki},
    {kEvilVoiceP1 + kEvilRagingDemonOffset, kRyuHadokenSound, EvilClip::kShunGokuSatsu},
    {kEvilVoiceP2 + 0, kRyuHadokenSound + kPlayerTwoVoiceOffset, EvilClip::kHadoken},
    {kEvilVoiceP2 + 1, kRyuShoryukenSound + kPlayerTwoVoiceOffset, EvilClip::kShoryuken},
    {kEvilVoiceP2 + 2, kRyuTatsumakiSound + kPlayerTwoVoiceOffset, EvilClip::kTatsumaki},
    {kEvilVoiceP2 + kEvilRagingDemonOffset, kRyuHadokenSound + kPlayerTwoVoiceOffset, EvilClip::kShunGokuSatsu},
    {kEvilKoP1, kRyuKoSound, EvilClip::kKo},
    {kEvilKoP2, kRyuKoSound, EvilClip::kKo},
    {kEvilAnnouncerName, kRyuAnnouncerNameSound, EvilClip::kAnnouncerName},
}};

struct EvilSoundState {
  std::array<bool, 2> evil{};
  std::array<bool, 2> raging_demon{};
  int ko_side = -1;
  bool announce_name = false;
};

// The sound to play instead of `sound`, or 0 to keep it. Only ever one or the other plays.
uint16_t EvilSoundFor(uint16_t sound, const EvilSoundState& state);

// Frames the "wins" call must wait for the name call before it, with the gap the guest leaves after a short name.
int EvilAnnouncerFrames(const EvilClipData& name);

}  // namespace ssf2t::character
