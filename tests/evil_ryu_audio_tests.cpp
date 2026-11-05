#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <vector>

#include "character/evil_ryu_audio.h"

namespace {

using namespace ssf2t::character;

int failures = 0;

void Check(bool condition, const char* what) {
  if (condition) return;
  std::printf("FAIL %s\n", what);
  ++failures;
}

void PutLE32(std::vector<uint8_t>& out, uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) out.push_back(uint8_t(value >> shift));
}

std::vector<uint8_t> Package(size_t clips, uint32_t samples) {
  std::vector<uint8_t> out = {'S', 'S', 'F', '2', 'E', 'R', 'V', '1'};
  PutLE32(out, uint32_t(clips));
  for (size_t clip = 1; clip <= clips; ++clip) {
    PutLE32(out, uint32_t(clip));
    PutLE32(out, 48000);
    PutLE32(out, samples);
    for (uint32_t s = 0; s < samples; ++s) {
      const int16_t value = s == 0 ? int16_t(-2) : int16_t(s);
      out.push_back(uint8_t(uint16_t(value)));
      out.push_back(uint8_t(uint16_t(value) >> 8));
    }
  }
  return out;
}

void TestParser() {
  const std::vector<uint8_t> file = Package(kEvilClipCount, 8);
  const EvilAudioPackage package = ParseEvilAudio(file.data(), file.size());
  Check(package.error == nullptr, "a whole package parses");
  Check(package.Clip(EvilClip::kKo).rate == 48000 && package.Clip(EvilClip::kKo).samples.size() == 8, "clip records");
  Check(package.Clip(EvilClip::kHadoken).samples[0] == -2 && package.Clip(EvilClip::kHadoken).samples[7] == 7,
        "samples are signed little endian");

  std::vector<uint8_t> bad = file;
  bad[0] = 'X';
  Check(ParseEvilAudio(bad.data(), bad.size()).error != nullptr, "another file is refused");
  Check(ParseEvilAudio(file.data(), file.size() - 1).error != nullptr, "a cut file is refused");
  Check(ParseEvilAudio(file.data(), 4).error != nullptr, "a header fragment is refused");
  const std::vector<uint8_t> partial = Package(kEvilClipCount - 1, 8);
  Check(ParseEvilAudio(partial.data(), partial.size()).error != nullptr, "a package without every clip is refused");
  std::vector<uint8_t> huge = Package(1, 8);
  huge[kEvilClipCount * 0 + 12 + 8] = 0xFF;
  huge[12 + 9] = 0xFF;
  huge[12 + 10] = 0xFF;
  Check(ParseEvilAudio(huge.data(), huge.size()).error != nullptr, "an impossible sample count is refused");
}

EvilSoundState State(bool p1, bool p2) {
  EvilSoundState state;
  state.evil = {p1, p2};
  return state;
}

void TestRouting() {
  const uint16_t p2_hadoken = kRyuHadokenSound + kPlayerTwoVoiceOffset;
  const uint16_t p2_tatsumaki = kRyuTatsumakiSound + kPlayerTwoVoiceOffset;

  EvilSoundState state = State(false, false);
  for (const uint16_t sound : {kRyuHadokenSound, kRyuShoryukenSound, kRyuTatsumakiSound, kRyuKoSound, p2_hadoken}) {
    Check(EvilSoundFor(sound, state) == 0, "Ryu vs Ryu keeps every Ryu sound");
  }

  state = State(true, false);
  Check(EvilSoundFor(kRyuShoryukenSound, state) == kEvilVoiceP1, "P1 Evil Ryu: P1 Shoryuken swaps");
  Check(EvilSoundFor(kRyuHadokenSound, state) == kEvilVoiceP1 + 1, "P1 Evil Ryu: P1 Hadoken swaps");
  Check(EvilSoundFor(kRyuTatsumakiSound, state) == kEvilVoiceP1 + 2, "P1 Evil Ryu: P1 Tatsumaki swaps");
  Check(EvilSoundFor(p2_hadoken, state) == 0, "P1 Evil / P2 Ryu: P2 keeps Ryu's voice");
  Check(EvilSoundFor(p2_tatsumaki, state) == 0, "P1 Evil / P2 Ryu: P2 keeps Ryu's Tatsumaki");

  state = State(false, true);
  Check(EvilSoundFor(kRyuHadokenSound, state) == 0, "P1 Ryu / P2 Evil: P1 keeps Ryu's voice");
  Check(EvilSoundFor(p2_hadoken, state) == kEvilVoiceP2 + 1, "P1 Ryu / P2 Evil: P2 Hadoken swaps");
  Check(EvilSoundFor(p2_tatsumaki, state) == kEvilVoiceP2 + 2, "P1 Ryu / P2 Evil: P2 Tatsumaki swaps");

  state = State(true, true);
  Check(EvilSoundFor(kRyuHadokenSound, state) != EvilSoundFor(p2_hadoken, state),
        "Evil vs Evil: each side has its own source voice");

  const uint16_t p2_ko = kRyuKoSound + kPlayerTwoVoiceOffset;
  const struct {
    const char* what;
    bool p1_evil, p2_evil;
    uint16_t sound;
    int round_end_side;
    uint16_t expected;
  } kKoCases[] = {
      {"P2 Evil, hit KO of P2", false, true, p2_ko, -1, kEvilKoP2},
      {"P2 Evil, round end KO of P2", false, true, kRyuKoSound, 1, kEvilKoP2},
      {"P2 Evil, hit KO of the P1 Ryu or Dhalsim", false, true, kRyuKoSound, -1, 0},
      {"P2 Evil, round end KO of the P1 Ryu or Dhalsim", false, true, kRyuKoSound, 0, 0},
      {"P1 Evil, hit KO of P1", true, false, kRyuKoSound, -1, kEvilKoP1},
      {"P1 Evil, round end KO of P1", true, false, kRyuKoSound, 0, kEvilKoP1},
      {"P1 Evil, hit KO of the P2 Ryu or Dhalsim", true, false, p2_ko, -1, 0},
      {"P1 Evil, round end KO of the P2 Ryu or Dhalsim", true, false, kRyuKoSound, 1, 0},
      {"Evil vs Evil, hit KO of P1", true, true, kRyuKoSound, -1, kEvilKoP1},
      {"Evil vs Evil, hit KO of P2", true, true, p2_ko, -1, kEvilKoP2},
      {"Evil vs Evil, round end KO of P2", true, true, kRyuKoSound, 1, kEvilKoP2},
      {"an impossible loser changes nothing", true, true, kRyuKoSound, 7, 0},
      {"Ryu vs Ryu keeps the P2 scream", false, false, p2_ko, -1, 0},
  };
  for (const auto& test : kKoCases) {
    state = State(test.p1_evil, test.p2_evil);
    state.round_end_ko_side = test.round_end_side;
    Check(EvilSoundFor(test.sound, state) == test.expected, test.what);
  }

  state = State(true, false);
  Check(EvilSoundFor(kRyuAnnouncerNameSound, state) == 0, "the name call only swaps for an Evil Ryu winner");
  state.announce_name = true;
  Check(EvilSoundFor(kRyuAnnouncerNameSound, state) == kEvilAnnouncerName, "Evil Ryu winner: name call swaps");
  Check(EvilSoundFor(175, state) == 0 && EvilSoundFor(484, state) == 0, "the native wins call stays");

  state = State(true, false);
  state.raging_demon = {true, false};
  Check(EvilSoundFor(kRyuHadokenSound, state) == kEvilVoiceP1 + kEvilRagingDemonOffset, "the marked voice is the super");
  Check(EvilSoundFor(kRyuShoryukenSound, state) == kEvilVoiceP1, "the mark only carries on the Hadoken id");

  Check(HostVoiceQueues(true, 1, false), "a real frame queues the Shun Goku Satsu voice");
  Check(!HostVoiceQueues(true, 1, true), "a replayed frame leaves no mark for the next real voice");
}

void TestVoiceIds() {
  Check(kRyuShoryukenSound == 96 && kRyuHadokenSound == 97 && kRyuTatsumakiSound == 98, "Ryu's P1 voice ids");
  const EvilSoundState state = State(true, true);
  const struct {
    uint16_t sound, slot;
    EvilClip clip;
  } kVoices[] = {
      {96, kEvilVoiceP1 + 0, EvilClip::kShoryuken}, {97, kEvilVoiceP1 + 1, EvilClip::kHadoken},
      {98, kEvilVoiceP1 + 2, EvilClip::kTatsumaki}, {310, kEvilVoiceP2 + 0, EvilClip::kShoryuken},
      {311, kEvilVoiceP2 + 1, EvilClip::kHadoken},  {312, kEvilVoiceP2 + 2, EvilClip::kTatsumaki},
  };
  for (const auto& voice : kVoices) {
    Check(EvilSoundFor(voice.sound, state) == voice.slot, "each Ryu voice reaches its slot");
    bool found = false;
    for (const EvilSound& sound : kEvilSounds) {
      if (sound.id != voice.slot) continue;
      found = sound.clip == voice.clip && sound.carrier == voice.sound;
    }
    Check(found, "the slot holds the clip of its event and copies its id's flags");
  }
  for (const EvilSound& sound : kEvilSounds) {
    if (sound.clip != EvilClip::kShunGokuSatsu) continue;
    Check(sound.carrier == 97 || sound.carrier == 311, "Shun Goku Satsu rides a real Hadoken voice");
  }
}

void TestTable() {
  std::set<uint16_t> ids;
  for (const EvilSound& sound : kEvilSounds) {
    Check(ids.insert(sound.id).second, "no two sounds share a slot");
    Check(sound.id >= 0xD0 && sound.id <= 0xFF, "every sound uses the guest's unused id range");
    Check(sound.carrier != sound.id, "a sound copies another id's flags");
  }
  for (const uint16_t swapped : {kEvilVoiceP1, uint16_t(kEvilVoiceP1 + 2), kEvilVoiceP2, kEvilKoP1, kEvilKoP2,
                                 kEvilAnnouncerName}) {
    Check(ids.count(swapped) == 1, "every routed id has a slot");
  }
}

void TestAnnouncerTiming() {
  EvilClipData clip;
  clip.rate = 48000;
  clip.samples.resize(48000);
  Check(EvilAnnouncerFrames(clip) == 64, "a one second name waits 60 frames and the gap");
  clip.samples.resize(800 * 3 / 2);
  Check(EvilAnnouncerFrames(clip) == 6, "a partial frame rounds up");
  clip.samples.resize(48000 * 4);
  Check(EvilAnnouncerFrames(clip) == 127, "the delay fits the guest's signed byte");
  Check(EvilAnnouncerFrames(EvilClipData{}) == 0, "no clip, no delay");
}

void TestRealPackage() {
  std::ifstream in("game/extracted/evil_ryu_audio.bin", std::ios::binary);
  if (!in) {
    std::printf("note: game/extracted/evil_ryu_audio.bin not found, real-data checks skipped\n");
    return;
  }
  const std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const EvilAudioPackage package = ParseEvilAudio(file.data(), file.size());
  Check(package.error == nullptr, "the extracted package parses");
  for (const EvilClipData& clip : package.clips) {
    Check(clip.rate == 48000, "USF2 clips are 48 kHz");
    int peak = 0;
    for (const int16_t sample : clip.samples) peak = std::max(peak, sample < 0 ? -int(sample) : int(sample));
    Check(peak > 8000, "a clip holds sound, not silence");
  }
  Check(package.Clip(EvilClip::kKo).samples.size() > package.Clip(EvilClip::kHadoken).samples.size(),
        "the KO scream is the long clip");
  Check(EvilAnnouncerFrames(package.Clip(EvilClip::kAnnouncerName)) > 38, "the name outlasts Ryu's 38 frame delay");
}

}  // namespace

int main() {
  TestParser();
  TestRouting();
  TestVoiceIds();
  TestTable();
  TestAnnouncerTiming();
  TestRealPackage();
  if (failures == 0) std::printf("evil ryu audio tests passed\n");
  return failures == 0 ? 0 : 1;
}
