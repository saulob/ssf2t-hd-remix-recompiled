#include "character/evil_ryu_audio.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <mutex>

namespace ssf2t::character {
namespace {

constexpr char kMagic[] = "SSF2ERV1";
constexpr size_t kMagicSize = 8;
constexpr size_t kHeaderSize = kMagicSize + 4;
constexpr size_t kRecordHeaderSize = 12;
constexpr uint32_t kMaxRate = 48000;
constexpr uint32_t kMaxSamples = 10 * kMaxRate;
constexpr std::streamoff kMaxFileSize = 16 << 20;
constexpr int kFramesPerSecond = 60;
constexpr int kAnnouncerGapFrames = 4;
constexpr int kMaxAnnouncerFrames = 127;

uint32_t ReadLE32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }

std::mutex g_mutex;
std::filesystem::path g_directory;
bool g_tried = false;
EvilAudioPackage g_package;

}  // namespace

EvilAudioPackage ParseEvilAudio(const uint8_t* file, size_t size) {
  EvilAudioPackage package;
  if (size < kHeaderSize || std::memcmp(file, kMagic, kMagicSize) != 0) {
    package.error = "decode";
    return package;
  }
  const uint32_t count = ReadLE32(file + kMagicSize);
  size_t at = kHeaderSize;
  for (uint32_t i = 0; i < count; ++i) {
    if (size - at < kRecordHeaderSize) {
      package.error = "truncated";
      return package;
    }
    const uint32_t id = ReadLE32(file + at);
    const uint32_t rate = ReadLE32(file + at + 4);
    const uint32_t samples = ReadLE32(file + at + 8);
    at += kRecordHeaderSize;
    if (id == 0 || id > kEvilClipCount || rate == 0 || rate > kMaxRate || samples == 0 || samples > kMaxSamples ||
        size - at < size_t(samples) * 2) {
      package.error = "truncated";
      return package;
    }
    EvilClipData& clip = package.clips[id - 1];
    clip.rate = rate;
    clip.samples.resize(samples);
    for (uint32_t s = 0; s < samples; ++s) {
      clip.samples[s] = int16_t(uint16_t(file[at + s * 2] | (file[at + s * 2 + 1] << 8)));
    }
    at += size_t(samples) * 2;
  }
  for (const EvilClipData& clip : package.clips) {
    if (clip.samples.empty()) package.error = "missing clip";
  }
  return package;
}

void SetEvilAudioDirectory(const std::filesystem::path& directory) {
  std::lock_guard lock(g_mutex);
  g_directory = directory;
}

const EvilAudioPackage* LoadedEvilAudio() {
  std::lock_guard lock(g_mutex);
  if (!g_tried) {
    g_tried = true;
    g_package.error = "open file";
    std::ifstream in(g_directory / kEvilRyuAudioFile, std::ios::binary | std::ios::ate);
    const std::streamoff size = in ? std::streamoff(in.tellg()) : std::streamoff(0);
    if (size > 0 && size <= kMaxFileSize) {
      std::vector<uint8_t> bytes(static_cast<size_t>(size));
      in.seekg(0);
      if (in.read(reinterpret_cast<char*>(bytes.data()), size)) g_package = ParseEvilAudio(bytes.data(), bytes.size());
    }
  }
  return g_package.error ? nullptr : &g_package;
}

uint16_t EvilSoundFor(uint16_t sound, const EvilSoundState& state) {
  const auto voice = [&](int side, uint16_t first, uint16_t base) -> uint16_t {
    if (!state.evil[side]) return 0;
    if (sound == first && state.raging_demon[side]) return uint16_t(base + kEvilRagingDemonOffset);
    return uint16_t(base + (sound - first));
  };
  if (sound >= kRyuHadokenSound && sound <= kRyuTatsumakiSound) return voice(0, kRyuHadokenSound, kEvilVoiceP1);
  const uint16_t p2 = kRyuHadokenSound + kPlayerTwoVoiceOffset;
  if (sound >= p2 && sound <= p2 + (kRyuTatsumakiSound - kRyuHadokenSound)) return voice(1, p2, kEvilVoiceP2);
  if (sound == kRyuKoSound) {
    if (state.ko_side < 0 || state.ko_side > 1 || !state.evil[state.ko_side]) return 0;
    return state.ko_side == 0 ? kEvilKoP1 : kEvilKoP2;
  }
  if (sound == kRyuAnnouncerNameSound && state.announce_name) return kEvilAnnouncerName;
  return 0;
}

int EvilAnnouncerFrames(const EvilClipData& name) {
  if (name.rate == 0) return 0;
  const size_t whole = name.samples.size() * kFramesPerSecond;
  const int frames = int((whole + name.rate - 1) / name.rate) + kAnnouncerGapFrames;
  return std::min(frames, kMaxAnnouncerFrames);
}

}  // namespace ssf2t::character
