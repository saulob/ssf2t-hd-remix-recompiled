#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include "character/evil_ryu_palette.h"

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
  if (condition) return;
  std::printf("FAIL %s\n", what);
  ++failures;
}

std::vector<uint8_t> Build(uint32_t count, int colours) {
  std::vector<uint8_t> file;
  const char magic[] = "SSF2ERP1";
  file.insert(file.end(), magic, magic + 8);
  for (int i = 0; i < 4; ++i) file.push_back(uint8_t(count >> (8 * i)));
  for (int colour = 0; colour < colours; ++colour) {
    for (int set = 0; set < 2; ++set) {
      for (int word = 0; word < 16; ++word) {
        const uint16_t value = uint16_t((set ? 0x100 : 0) + colour * 0x10 + word);
        file.push_back(uint8_t(value));
        file.push_back(uint8_t(value >> 8));
      }
    }
  }
  return file;
}

}  // namespace

int main() {
  using namespace ssf2t::character;

  const std::vector<uint8_t> good = Build(10, 10);
  EvilRyuPalette palette;
  Check(ParseEvilRyuPalette(good.data(), good.size(), &palette), "parses a valid file");
  Check(palette.ryu[3][5] == 0x35 && palette.evil[3][5] == 0x135, "ryu and evil words are separate");

  std::array<uint16_t, 16> words{};
  for (int word = 0; word < 16; ++word) words[word] = palette.ryu[7][word];
  Check(MatchRyuColour(palette, words.data()) == 7, "matches the Ryu colour");
  words[0] = 0xFFFF;
  words[15] = 0xFFFF;
  Check(MatchRyuColour(palette, words.data()) == 7, "pens 0 and 15 do not take part in the match");
  words[4] ^= 1;
  Check(MatchRyuColour(palette, words.data()) == -1, "a changed body pen does not match");

  const std::vector<uint8_t> bad_count = Build(9, 10);
  Check(!ParseEvilRyuPalette(bad_count.data(), bad_count.size(), &palette), "rejects a wrong count");
  const std::vector<uint8_t> short_file = Build(10, 9);
  Check(!ParseEvilRyuPalette(short_file.data(), short_file.size(), &palette), "rejects a short file");
  std::vector<uint8_t> bad_magic = good;
  bad_magic[0] = 'X';
  Check(!ParseEvilRyuPalette(bad_magic.data(), bad_magic.size(), &palette), "rejects bad magic");

  {
    std::vector<uint8_t> hd;
    const char magic[] = "SSF2ERH1";
    hd.insert(hd.end(), magic, magic + 8);
    hd.insert(hd.end(), {11, 0, 0, 0});
    for (int colour = 0; colour < 11; ++colour) hd.insert(hd.end(), 1024, uint8_t(colour + 1));
    EvilRyuHdPalette hd_palette;
    Check(ParseEvilRyuHdPalette(hd.data(), hd.size(), &hd_palette), "parses the HD palette file");
    Check(hd_palette.colours[0][0] == 1 && hd_palette.colours[9][1023] == 10, "HD palette blocks are per colour");
    Check(hd_palette.colours[kHdPaletteMirrorEntry][0] == 11, "the mirror palette follows the colours");
    hd[8] = 10;
    Check(!ParseEvilRyuHdPalette(hd.data(), hd.size(), &hd_palette), "HD palette rejects a wrong count");
    hd[8] = 11;
    Check(!ParseEvilRyuHdPalette(hd.data(), hd.size() - 1, &hd_palette), "HD palette rejects a short file");
  }

  Check(HdPaletteEntry(4, 0, false) == 4 && HdPaletteEntry(4, 1, false) == 4, "a lone Evil Ryu uses his colour entry");
  Check(HdPaletteEntry(4, 0, true) == 4, "the first player keeps his entry in a mirror match");
  Check(HdPaletteEntry(4, 1, true) == kHdPaletteMirrorEntry, "the second player differs in a mirror match");

  Check(LoadedEvilRyuPalette() == nullptr, "no palette without a directory file");

  if (failures == 0) std::printf("evil ryu palette tests passed\n");
  return failures == 0 ? 0 : 1;
}
