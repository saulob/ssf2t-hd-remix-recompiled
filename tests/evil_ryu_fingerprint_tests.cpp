#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
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

enum Image { kBase013, kTitleUpdate023 };

struct Fingerprint {
  const char* name;
  Image image;
  uint32_t address;
  std::array<uint32_t, 8> words;
};

constexpr uint32_t kImageBase = 0x82000000;

// Prologue words of every hook target, read from each build on its own: a wrong address in either build fails here.
constexpr Fingerprint kFingerprints[] = {
    {"SoundLoadAll", kBase013, 0x823D5D18, {0x7D8802A6, 0x9181FFF8, 0x9421F790, 0x39600000, 0x91610050, 0x48000010, 0x81610050, 0x396B0001}},
    {"SoundLoadAll", kTitleUpdate023, 0x823D63D8, {0x7D8802A6, 0x9181FFF8, 0x9421F790, 0x39600000, 0x91610050, 0x48000010, 0x81610050, 0x396B0001}},
    {"SoundRequest", kBase013, 0x82375C60, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0xB0610076, 0xB081007E, 0xB0A10086, 0xB0C1008E, 0x3D600A00}},
    {"SoundRequest", kTitleUpdate023, 0x82375D30, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0xB0610076, 0xB081007E, 0xB0A10086, 0xB0C1008E, 0x3D600A00}},
    {"WinnerNameState", kBase013, 0x82342148, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x816B52D8, 0x896B0080, 0x7D6B0774, 0x396B0002}},
    {"WinnerNameState", kTitleUpdate023, 0x82342048, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x816B57F0, 0x896B0080, 0x7D6B0774, 0x396B0002}},
    {"WinText", kBase013, 0x823CDBF8, {0x7D8802A6, 0x9181FFF8, 0x9421FF80, 0x90610094, 0x9881009F, 0x4BDA6035, 0x48061F91, 0x90610054}},
    {"WinText", kTitleUpdate023, 0x823CE1D8, {0x7D8802A6, 0x9181FFF8, 0x9421FF80, 0x90610094, 0x9881009F, 0x4BDA5A75, 0x480620F1, 0x90610054}},
    {"RecordMatch", kBase013, 0x823C33C8, {0x7D8802A6, 0x9181FFF8, 0x9421FF60, 0x3D6083C2, 0x896B2221, 0x2C0B0000, 0x41820008, 0x48000724}},
    {"RecordMatch", kTitleUpdate023, 0x823C3970, {0x7D8802A6, 0x9181FFF8, 0x9421FF60, 0x3D6083C2, 0x896B22A1, 0x2C0B0000, 0x41820008, 0x48000724}},
    {"PlayerResults", kBase013, 0x823C8468, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x90610074, 0x3960FFFF, 0x91610050, 0x3D6083C2, 0x396B4D90}},
    {"PlayerResults", kTitleUpdate023, 0x823C8A60, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x90610074, 0x3960FFFF, 0x91610050, 0x3D6083C2, 0x396B5108}},
    {"PlayerResultsCharacter", kBase013, 0x823C85E0, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x90610074, 0x3960FFFF, 0x91610050, 0x3D6083C2, 0x396B4D90}},
    {"PlayerResultsCharacter", kTitleUpdate023, 0x823C8BD8, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x90610074, 0x3960FFFF, 0x91610050, 0x3D6083C2, 0x396B5108}},
    {"ResetPlayerResults", kBase013, 0x823C8540, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x90610074, 0x3D6083C2, 0x396B4D90, 0x816B001C, 0x2F0B0000}},
    {"ResetPlayerResults", kTitleUpdate023, 0x823C8B38, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x90610074, 0x3D6083C2, 0x396B5108, 0x816B001C, 0x2F0B0000}},
    {"QueueSound", kBase013, 0x823782B0, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0xB0610076, 0xB081007E, 0x90A10084, 0x90C1008C, 0x480A497D}},
    {"QueueSound", kTitleUpdate023, 0x82378380, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0xB0610076, 0xB081007E, 0x90A10084, 0x90C1008C, 0x480A4F85}},
};

struct BuildData {
  const char* name;
  const char* file;
  uint32_t sound_flags;
};

constexpr BuildData kBuilds[] = {
    {"Base013", "game/analysis/default-0.0.1.3.image.bin", 0x83A37DB0},
    {"TU023", "game/analysis/default.image.bin", 0x83A37E28},
};

uint32_t Word(const std::vector<uint8_t>& image, uint32_t address) {
  const size_t at = address - kImageBase;
  return uint32_t(image[at]) << 24 | image[at + 1] << 16 | image[at + 2] << 8 | image[at + 3];
}

void TestImage(Image which) {
  std::ifstream in(kBuilds[which].file, std::ios::binary);
  if (!in) {
    std::printf("note: %s not found, fingerprint checks skipped\n", kBuilds[which].file);
    return;
  }
  const std::vector<uint8_t> image((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  for (const Fingerprint& print : kFingerprints) {
    if (print.image != which) continue;
    for (size_t i = 0; i < print.words.size(); ++i) {
      Check(Word(image, print.address + uint32_t(i) * 4) == print.words[i], print.name);
    }
  }
  const uint32_t flags = kBuilds[which].sound_flags;
  for (const EvilSound& sound : kEvilSounds) {
    Check(Word(image, flags + sound.id * 4u) == 0xFFFFFFFF, "every Evil Ryu sound id is unused in this build");
  }
  Check(Word(image, flags + kRyuHadokenSound * 4u) == 0, "Hadoken carries Ryu's character flag");
  Check(Word(image, flags + (kRyuHadokenSound + kPlayerTwoVoiceOffset) * 4u) == 0x800, "P2 Hadoken is the 0x800 copy");
  Check(Word(image, flags + kRyuKoSound * 4u) == 0x80, "Ryu's KO is the shared 0x80 sound");
  Check(Word(image, flags + kRyuAnnouncerNameSound * 4u) == 0x20, "the name call is an announcer sound");
  Check(uint16_t(Word(image, 0x83A33B7C) >> 16) == kRyuAnnouncerNameSound, "Ryu's name call in the announcer table");
  Check(uint16_t(Word(image, 0x83A2CF54) >> 16) == kRyuKoSound, "Ryu's KO in the KO table");
}

}  // namespace

int main() {
  TestImage(kBase013);
  TestImage(kTitleUpdate023);
  if (failures == 0) std::printf("evil ryu fingerprint tests passed\n");
  return failures == 0 ? 0 : 1;
}
