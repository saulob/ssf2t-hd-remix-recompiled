#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
#include <vector>

#include "character/violent_ken_audio.h"

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
  std::vector<uint8_t> out = {'S', 'S', 'F', '2', 'V', 'K', 'V', '1'};
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
  const std::vector<uint8_t> file = Package(kViolentClipCount, 8);
  const ViolentKenAudioPackage package = ParseViolentKenAudio(file.data(), file.size());
  Check(package.error == nullptr, "a whole package parses");
  Check(package.Clip(ViolentClip::kShinbu).rate == 48000 && package.Clip(ViolentClip::kShinbu).samples.size() == 8,
        "clip records");
  Check(package.Clip(ViolentClip::kHadoken).samples[0] == -2, "samples are signed little endian");

  std::vector<uint8_t> evil = file;
  evil[4] = 'E';
  evil[5] = 'R';
  Check(ParseViolentKenAudio(evil.data(), evil.size()).error != nullptr, "the Evil Ryu package is refused");
  Check(ParseViolentKenAudio(file.data(), file.size() - 1).error != nullptr, "a cut file is refused");
  Check(ParseViolentKenAudio(file.data(), 4).error != nullptr, "a header fragment is refused");
  const std::vector<uint8_t> partial = Package(kViolentClipCount - 1, 8);
  Check(ParseViolentKenAudio(partial.data(), partial.size()).error != nullptr, "a package without every clip is refused");
  std::vector<uint8_t> extra = Package(1, 8);
  extra[12] = uint8_t(kViolentClipCount + 1);
  Check(ParseViolentKenAudio(extra.data(), extra.size()).error != nullptr, "an unknown clip id is refused");
}

ViolentSoundState State(bool p1, bool p2) {
  ViolentSoundState state;
  state.violent = {p1, p2};
  return state;
}

constexpr uint16_t kP2 = kPlayerTwoVoiceOffset;

void TestEventMap() {
  const ViolentSoundState state = State(true, true);
  Check(kKenShoryukenSound == 101 && kKenHadokenSound == 102, "the id whose handler spawns the shell is the Hadoken");
  Check(ViolentSoundFor(kKenShoryukenSound, state) == kViolentVoiceP1, "P1 Shoryuken");
  Check(ViolentSoundFor(kKenHadokenSound, state) == kViolentVoiceP1 + 1, "P1 Hadoken");
  Check(ViolentSoundFor(kKenTatsumakiSound, state) == kViolentVoiceP1 + 2, "P1 Tatsumaki");
  Check(ViolentSoundFor(kKenThrowSound, state) == kViolentVoiceP1 + 3, "P1 throw");
  Check(ViolentSoundFor(kKenShoryukenSound + kP2, state) == kViolentVoiceP2, "P2 Shoryuken");
  Check(ViolentSoundFor(kKenHadokenSound + kP2, state) == kViolentVoiceP2 + 1, "P2 Hadoken");
  Check(ViolentSoundFor(kKenTatsumakiSound + kP2, state) == kViolentVoiceP2 + 2, "P2 Tatsumaki");
  Check(ViolentSoundFor(kKenThrowSound + kP2, state) == kViolentVoiceP2 + 3, "P2 throw");

  for (const ViolentSound& sound : kViolentSounds) {
    const uint16_t voice = sound.id >= kViolentVoiceP2 ? uint16_t(sound.id - kViolentVoiceP2) : uint16_t(sound.id - kViolentVoiceP1);
    if (sound.id >= kViolentKoP1 || voice >= kViolentRasetsuOffset) continue;
    Check(ViolentSoundFor(sound.carrier, state) == sound.id, "a routed voice uses the slot that copied its carrier");
    const ViolentClip expected[] = {ViolentClip::kShoryuken, ViolentClip::kHadoken, ViolentClip::kTatsumaki, ViolentClip::kThrow};
    Check(sound.clip == expected[voice], "the slot holds the clip of its event");
  }
}

void TestNormalKenUnchanged() {
  ViolentSoundState state = State(false, false);
  for (const int side : {-1, 0, 1}) {
    state.round_end_ko_side = side;
    for (const int sound : {101, 102, 103, 104, 105, 315, 316, 317, 318, 319, 465}) {
      Check(ViolentSoundFor(uint16_t(sound), state) == 0, "Ken vs Ken keeps every Ken sound");
    }
  }
  state.move_voice = {ViolentMoveVoice::kShinbu, ViolentMoveVoice::kRasetsu};
  Check(ViolentSoundFor(kKenHadokenSound, state) == 0, "a stale mark does nothing on a normal Ken");
  const ViolentSoundState violent = State(true, true);
  for (const int sound : {96, 97, 98, 100, 106, 310, 314, 320, 464, 466, 484}) {
    Check(ViolentSoundFor(uint16_t(sound), violent) == 0, "no other fighter or announcer call is touched");
  }
}

void TestSideIndependence() {
  const ViolentSoundState p1 = State(true, false);
  Check(ViolentSoundFor(kKenHadokenSound, p1) == kViolentVoiceP1 + 1, "P1 Violent: P1 swaps");
  Check(ViolentSoundFor(kKenHadokenSound + kP2, p1) == 0, "P1 Violent / P2 Ken: P2 keeps Ken");
  Check(ViolentSoundFor(kKenThrowSound + kP2, p1) == 0, "P1 Violent / P2 Ken: P2 keeps Ken's throw");
  Check(ViolentSoundFor(kKenTatsumakiSound + kP2, p1) == 0, "P1 Violent / P2 Ken: P2 keeps Ken's Tatsumaki");
  const ViolentSoundState p2 = State(false, true);
  Check(ViolentSoundFor(kKenHadokenSound, p2) == 0, "P1 Ken / P2 Violent: P1 keeps Ken");
  Check(ViolentSoundFor(kKenThrowSound, p2) == 0, "P1 Ken / P2 Violent: P1 keeps Ken's throw");
  Check(ViolentSoundFor(kKenHadokenSound + kP2, p2) == kViolentVoiceP2 + 1, "P1 Ken / P2 Violent: P2 swaps");
}

void TestMirror() {
  const ViolentSoundState state = State(true, true);
  const uint16_t p1 = ViolentSoundFor(kKenHadokenSound, state);
  const uint16_t p2 = ViolentSoundFor(kKenHadokenSound + kP2, state);
  Check(p1 != 0 && p2 != 0 && p1 != p2, "Violent vs Violent: each side has its own source voice");
}

void TestContinue() {
  Check(kKenContinueSound == kKenShoryukenSound + kP2, "the Continue table names Ken's P2 Shoryuken id");
  const struct {
    const char* what;
    bool p1_violent, p2_violent;
    int loser;
    uint16_t expected;
  } kCases[] = {
      {"P1 Violent continues on the P1 voice", true, false, 0, kViolentVoiceP1},
      {"P2 Violent continues on the P2 voice", false, true, 1, kViolentVoiceP2},
      {"a P1 Ken beside a P2 Violent keeps Ken", false, true, 0, 0},
      {"a P2 Ken beside a P1 Violent keeps Ken", true, false, 1, 0},
      {"mirror, P1 loser", true, true, 0, kViolentVoiceP1},
      {"mirror, P2 loser", true, true, 1, kViolentVoiceP2},
      {"Ken vs Ken", false, false, 0, 0},
      {"an impossible loser changes nothing", true, true, 7, 0},
  };
  for (const auto& test : kCases) {
    ViolentSoundState state = State(test.p1_violent, test.p2_violent);
    state.round_end_ko_side = test.loser;
    Check(ViolentSoundFor(kKenContinueSound, state) == test.expected, test.what);
  }
  ViolentSoundState untagged = State(true, false);
  Check(ViolentSoundFor(kKenContinueSound, untagged) == 0, "an untagged P2 Shoryuken is a move of the P2 Ken");
  untagged = State(false, true);
  Check(ViolentSoundFor(kKenContinueSound, untagged) == kViolentVoiceP2, "an untagged P2 Shoryuken stays with P2");
  for (const ViolentSound& sound : kViolentSounds) {
    if (sound.id == kViolentVoiceP1 || sound.id == kViolentVoiceP2) {
      Check(sound.clip == ViolentClip::kShoryuken, "both Continue slots hold the Shoryuken clip USF2 plays there");
    }
  }
}

void TestKo() {
  const struct {
    const char* what;
    bool p1_violent, p2_violent;
    uint16_t sound;
    int round_end_side;
    uint16_t expected;
  } kCases[] = {
      {"P1 Violent, hit KO of P1", true, false, kKenKoSound, -1, kViolentKoP1},
      {"P1 Violent, round end KO of P1", true, false, kKenKoSound, 0, kViolentKoP1},
      {"P1 Violent, hit KO of the P2 Ken", true, false, kKenKoSound + kP2, -1, 0},
      {"P1 Violent, round end KO of the P2 Ken", true, false, kKenKoSound, 1, 0},
      {"P2 Violent, hit KO of P2", false, true, kKenKoSound + kP2, -1, kViolentKoP2},
      {"P2 Violent, round end KO of P2", false, true, kKenKoSound, 1, kViolentKoP2},
      {"P2 Violent, hit KO of the P1 Ken", false, true, kKenKoSound, -1, 0},
      {"P2 Violent, round end KO of the P1 Ken", false, true, kKenKoSound, 0, 0},
      {"mirror, hit KO of P1", true, true, kKenKoSound, -1, kViolentKoP1},
      {"mirror, hit KO of P2", true, true, kKenKoSound + kP2, -1, kViolentKoP2},
      {"mirror, round end KO of P1", true, true, kKenKoSound, 0, kViolentKoP1},
      {"mirror, round end KO of P2", true, true, kKenKoSound, 1, kViolentKoP2},
      {"double KO, P1 Ken scream beside a P2 Violent", false, true, kKenKoSound, -1, 0},
      {"double KO, P2 Violent scream beside a P1 Ken", false, true, kKenKoSound + kP2, -1, kViolentKoP2},
      {"double KO, P1 Violent scream beside a P2 Ken", true, false, kKenKoSound, -1, kViolentKoP1},
      {"double KO, P2 Ken scream beside a P1 Violent", true, false, kKenKoSound + kP2, -1, 0},
      {"double KO, round end names P2 while P1 is also down", false, true, kKenKoSound, 1, kViolentKoP2},
      {"double KO, round end names the P2 Ken while P1 is also down", true, false, kKenKoSound, 1, 0},
      {"an impossible loser changes nothing", true, true, kKenKoSound, 7, 0},
  };
  for (const auto& test : kCases) {
    ViolentSoundState state = State(test.p1_violent, test.p2_violent);
    state.round_end_ko_side = test.round_end_side;
    Check(ViolentSoundFor(test.sound, state) == test.expected, test.what);
  }
}

void TestMoveVoices() {
  ViolentSoundState state = State(true, true);
  state.move_voice = {ViolentMoveVoice::kRasetsu, ViolentMoveVoice::kShinbu};
  Check(ViolentSoundFor(kKenHadokenSound, state) == kViolentVoiceP1 + kViolentRasetsuOffset, "P1 Rasetsukyaku");
  Check(ViolentSoundFor(kKenHadokenSound + kP2, state) == kViolentVoiceP2 + kViolentShinbuOffset, "P2 Shinbu Messatsu");
  Check(ViolentSoundFor(kKenShoryukenSound, state) == kViolentVoiceP1, "the mark only carries on the Hadoken id");
  state = State(true, false);
  state.move_voice = {ViolentMoveVoice::kNone, ViolentMoveVoice::kShinbu};
  Check(ViolentSoundFor(kKenHadokenSound, state) == kViolentVoiceP1 + 1, "the mark of one side leaves the other alone");
  Check(ViolentSoundFor(kKenHadokenSound + kP2, state) == 0, "no mark swaps a normal Ken");

  Check(HostVoiceQueues(true, 0, false) && HostVoiceQueues(true, 1, false), "a real frame queues the host voice");
  Check(!HostVoiceQueues(true, 0, true), "a replayed frame leaves no mark for the next real voice");
  Check(!HostVoiceQueues(false, 0, false), "no clips, no mark");
  Check(!HostVoiceQueues(true, 2, false) && !HostVoiceQueues(true, -1, false), "only two sides");
}

void TestAnnouncer() {
  ViolentSoundState state = State(true, false);
  Check(ViolentSoundFor(kKenAnnouncerNameSound, state) == 0, "the name call only swaps for a Violent Ken winner");
  state.announce_name = true;
  Check(ViolentSoundFor(kKenAnnouncerNameSound, state) == kViolentAnnouncerName, "Violent Ken winner: name call swaps");
  Check(ViolentSoundFor(kRyuAnnouncerNameSound, state) == 0, "Ryu's name stays");
  Check(ViolentSoundFor(484, state) == 0 && ViolentSoundFor(175, state) == 0, "the native wins call stays");
}

void TestTable() {
  std::set<uint16_t> ids;
  for (const ViolentSound& sound : kViolentSounds) {
    Check(ids.insert(sound.id).second, "no two sounds share a slot");
    Check(sound.id >= 0xD0 && sound.id <= 0xDF, "every sound uses ids the guest leaves unused");
    Check(sound.carrier != sound.id, "a sound copies another id's flags");
    for (const EvilSound& evil : kEvilSounds) Check(sound.id != evil.id, "no slot collides with Evil Ryu's");
  }
}

void TestRealPackage() {
  std::ifstream in("game/extracted/violent_ken_audio.bin", std::ios::binary);
  if (!in) {
    std::printf("note: game/extracted/violent_ken_audio.bin not found, real-data checks skipped\n");
    return;
  }
  const std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const ViolentKenAudioPackage package = ParseViolentKenAudio(file.data(), file.size());
  Check(package.error == nullptr, "the extracted package parses");
  for (const EvilClipData& clip : package.clips) {
    Check(clip.rate == 48000, "USF2 clips are 48 kHz");
    int peak = 0;
    for (const int16_t sample : clip.samples) peak = std::max(peak, sample < 0 ? -int(sample) : int(sample));
    Check(peak > 8000, "a clip holds sound, not silence");
  }
  Check(package.Clip(ViolentClip::kKo).samples.size() > package.Clip(ViolentClip::kShinbu).samples.size(),
        "the KO scream is the long clip");
  Check(package.Clip(ViolentClip::kThrow).samples.size() == 16124, "the throw is the ngs request 24, not the kick voice of request 4");
  Check(EvilAnnouncerFrames(package.Clip(ViolentClip::kAnnouncerName)) > 38, "the name outlasts Ken's 38 frame delay");
}

}  // namespace

int main() {
  TestParser();
  TestEventMap();
  TestNormalKenUnchanged();
  TestSideIndependence();
  TestMirror();
  TestContinue();
  TestKo();
  TestMoveVoices();
  TestAnnouncer();
  TestTable();
  TestRealPackage();
  if (failures == 0) std::printf("violent ken audio tests passed\n");
  return failures == 0 ? 0 : 1;
}
