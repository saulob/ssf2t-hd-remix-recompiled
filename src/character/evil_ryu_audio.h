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

// Null on success. Every USF2 character package shares this layout and differs in its magic and clip count.
const char* ParseClipPackage(const uint8_t* file, size_t size, const char* magic, EvilClipData* clips, size_t clip_count);
std::vector<uint8_t> ReadClipPackage(const std::filesystem::path& path);

EvilAudioPackage ParseEvilAudio(const uint8_t* file, size_t size);

void SetEvilAudioDirectory(const std::filesystem::path& directory);
// Null when the package is missing or invalid; the guest's own Ryu sounds then play untouched.
const EvilAudioPackage* LoadedEvilAudio();

// Ryu's voices have one sound id per side. See KoSideForSound for the one call that shares an id.
inline constexpr uint16_t kRyuShoryukenSound = 96;
inline constexpr uint16_t kRyuHadokenSound = 97;
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
    {kEvilVoiceP1 + 0, kRyuShoryukenSound, EvilClip::kShoryuken},
    {kEvilVoiceP1 + 1, kRyuHadokenSound, EvilClip::kHadoken},
    {kEvilVoiceP1 + 2, kRyuTatsumakiSound, EvilClip::kTatsumaki},
    {kEvilVoiceP1 + kEvilRagingDemonOffset, kRyuHadokenSound, EvilClip::kShunGokuSatsu},
    {kEvilVoiceP2 + 0, kRyuShoryukenSound + kPlayerTwoVoiceOffset, EvilClip::kShoryuken},
    {kEvilVoiceP2 + 1, kRyuHadokenSound + kPlayerTwoVoiceOffset, EvilClip::kHadoken},
    {kEvilVoiceP2 + 2, kRyuTatsumakiSound + kPlayerTwoVoiceOffset, EvilClip::kTatsumaki},
    {kEvilVoiceP2 + kEvilRagingDemonOffset, kRyuHadokenSound + kPlayerTwoVoiceOffset, EvilClip::kShunGokuSatsu},
    {kEvilKoP1, kRyuKoSound, EvilClip::kKo},
    {kEvilKoP2, kRyuKoSound + kPlayerTwoVoiceOffset, EvilClip::kKo},
    {kEvilAnnouncerName, kRyuAnnouncerNameSound, EvilClip::kAnnouncerName},
}};

struct EvilSoundState {
  std::array<bool, 2> evil{};
  std::array<bool, 2> raging_demon{};
  int round_end_ko_side = -1;
  bool announce_name = false;
};

// A hit KO's id names the fallen side: the P1 id on P1, the P1 id + kPlayerTwoVoiceOffset on P2. The end-of-round call
// uses the P1 id for either side, so only it carries a side of its own, read from the loser byte as it was queued.
// Negative when `sound` is not this character's KO.
constexpr int KoSideForSound(uint16_t sound, uint16_t p1_sound, int round_end_side) {
  if (sound == p1_sound + kPlayerTwoVoiceOffset) return 1;
  if (sound != p1_sound) return -1;
  return round_end_side < 0 ? 0 : round_end_side;
}

// A replayed frame queues no sound, so a mark set during one would swap the next real voice instead.
constexpr bool HostVoiceQueues(bool installed, int side, bool replay) {
  return installed && !replay && side >= 0 && side <= 1;
}

// The sound to play instead of `sound`, or 0 to keep it. Only ever one or the other plays.
uint16_t EvilSoundFor(uint16_t sound, const EvilSoundState& state);

// Frames the "wins" call must wait for the name call before it, with the gap the guest leaves after a short name.
int EvilAnnouncerFrames(const EvilClipData& name);

}  // namespace ssf2t::character
