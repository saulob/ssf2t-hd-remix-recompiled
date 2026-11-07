#include "character/violent_ken_audio.h"

#include <mutex>
#include <vector>

namespace ssf2t::character {
namespace {

constexpr char kMagic[] = "SSF2VKV1";

std::mutex g_mutex;
std::filesystem::path g_directory;
bool g_tried = false;
ViolentKenAudioPackage g_package;

}  // namespace

ViolentKenAudioPackage ParseViolentKenAudio(const uint8_t* file, size_t size) {
  ViolentKenAudioPackage package;
  package.error = ParseClipPackage(file, size, kMagic, package.clips.data(), package.clips.size());
  return package;
}

void SetViolentKenAudioDirectory(const std::filesystem::path& directory) {
  std::lock_guard lock(g_mutex);
  g_directory = directory;
}

const ViolentKenAudioPackage* LoadedViolentKenAudio() {
  std::lock_guard lock(g_mutex);
  if (!g_tried) {
    g_tried = true;
    g_package.error = "open file";
    const std::vector<uint8_t> bytes = ReadClipPackage(g_directory / kViolentKenAudioFile);
    if (!bytes.empty()) g_package = ParseViolentKenAudio(bytes.data(), bytes.size());
  }
  return g_package.error ? nullptr : &g_package;
}

uint16_t ViolentSoundFor(uint16_t sound, const ViolentSoundState& state) {
  const auto voice = [&](int side, uint16_t first, uint16_t base) -> uint16_t {
    if (!state.violent[side]) return 0;
    const bool carrier = sound == first + (kKenHadokenSound - kKenShoryukenSound);
    if (carrier && state.move_voice[side] == ViolentMoveVoice::kRasetsu) {
      return uint16_t(base + kViolentRasetsuOffset);
    }
    if (carrier && state.move_voice[side] == ViolentMoveVoice::kShinbu) {
      return uint16_t(base + kViolentShinbuOffset);
    }
    return uint16_t(base + (sound - first));
  };
  if (sound == kKenContinueSound && state.round_end_ko_side >= 0) {
    const int side = state.round_end_ko_side;
    if (side > 1 || !state.violent[side]) return 0;
    return side == 0 ? kViolentVoiceP1 : kViolentVoiceP2;
  }
  if (sound >= kKenShoryukenSound && sound <= kKenThrowSound) return voice(0, kKenShoryukenSound, kViolentVoiceP1);
  const uint16_t p2 = kKenShoryukenSound + kPlayerTwoVoiceOffset;
  if (sound >= p2 && sound <= p2 + (kKenThrowSound - kKenShoryukenSound)) return voice(1, p2, kViolentVoiceP2);
  const int ko_side = KoSideForSound(sound, kKenKoSound, state.round_end_ko_side);
  if (ko_side >= 0) {
    if (ko_side > 1 || !state.violent[ko_side]) return 0;
    return ko_side == 0 ? kViolentKoP1 : kViolentKoP2;
  }
  if (sound == kKenAnnouncerNameSound && state.announce_name) return kViolentAnnouncerName;
  return 0;
}

}  // namespace ssf2t::character
