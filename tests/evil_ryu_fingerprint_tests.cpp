#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "character/evil_ryu_audio.h"
#include "character/violent_ken_audio.h"

namespace {

using namespace ssf2t::character;

int failures = 0;

void Check(bool condition, const char* what) {
  if (condition) return;
  std::printf("FAIL %s\n", what);
  ++failures;
}

enum Image { kBase013, kTitleUpdate023 };

constexpr uint32_t kAkumaLive = 32;

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
    {"QuoteDialog", kBase013, 0x823CE430, {0x7D8802A6, 0x9181FFF8, 0x9421FF50, 0x906100C4, 0x4BDA5801, 0x4806175D, 0x90610058, 0x81610058}},
    {"QuoteDialog", kTitleUpdate023, 0x823CEA10, {0x7D8802A6, 0x9181FFF8, 0x9421FF50, 0x906100C4, 0x4BDA5241, 0x480618BD, 0x90610058, 0x81610058}},
    {"QuoteHashName", kBase013, 0x824308A0, {0x4BFC16E8, 0x00000000, 0x7D8802A6, 0x9181FFF8, 0xFBE1FFF0, 0x9421FFA0, 0x3D608480, 0x3D408480}},
    {"QuoteHashName", kTitleUpdate023, 0x82430FE0, {0x4BFC1718, 0x00000000, 0x7D8802A6, 0x9181FFF8, 0xFBE1FFF0, 0x9421FFA0, 0x3D608480, 0x3D408480}},
    {"QuoteFindText", kBase013, 0x82439990, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D608481, 0x7C641B78, 0x806B7E6C, 0x2B030000, 0x409A000C}},
    {"QuoteFindText", kTitleUpdate023, 0x8243A100, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D608482, 0x7C641B78, 0x806B838C, 0x2B030000, 0x409A000C}},
    {"QuoteTextPointer", kBase013, 0x823C2158, {0x546B0001, 0x4182004C, 0x3D608481, 0x816B7E68, 0x2B0B0000, 0x419A003C, 0x3D408482, 0x5469007E}},
    {"QuoteTextPointer", kTitleUpdate023, 0x823C2700, {0x546B0001, 0x4182004C, 0x3D608482, 0x816B8388, 0x2B0B0000, 0x419A003C, 0x3D408482, 0x5469007E}},
    {"QueueSound", kBase013, 0x823782B0, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0xB0610076, 0xB081007E, 0x90A10084, 0x90C1008C, 0x480A497D}},
    {"QueueSound", kTitleUpdate023, 0x82378380, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0xB0610076, 0xB081007E, 0x90A10084, 0x90C1008C, 0x480A4F85}},
    {"BeatenPortrait", kBase013, 0x82179A58, {0x7D8802A6, 0x9181FFF8, 0xFBE1FFF0, 0x9421FF50, 0x986100C7, 0x3D6083BE, 0x816B5BD4, 0x2F0B0000}},
    {"BeatenPortrait", kTitleUpdate023, 0x82179A78, {0x7D8802A6, 0x9181FFF8, 0xFBE1FFF0, 0x9421FF50, 0x986100C7, 0x3D6083BE, 0x816B5C54, 0x2F0B0000}},
    {"PlayCinematic", kBase013, 0x823CBE80, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x39600000, 0x91610050, 0x3D6083BE, 0x816B5904, 0x91610054}},
    {"PlayCinematic", kTitleUpdate023, 0x823CC460, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x39600000, 0x91610050, 0x3D6083BE, 0x816B5984, 0x91610054}},
    {"EndingLoadScreen", kBase013, 0x823CBCE0, {0x7D8802A6, 0x9181FFF8, 0xFBE1FFF0, 0x9421FFA0, 0x90610074, 0x9081007C, 0x90A10084, 0x38800001}},
    {"EndingLoadScreen", kTitleUpdate023, 0x823CC2C0, {0x7D8802A6, 0x9181FFF8, 0xFBE1FFF0, 0x9421FFA0, 0x90610074, 0x9081007C, 0x90A10084, 0x38800001}},
    {"EndingFindElement", kBase013, 0x8243E780, {0x7D8802A6, 0x9181FFF8, 0xFBE1FFF0, 0x9421FFA0, 0x3D608480, 0x7C7F1B78, 0x806BAF4C, 0x4BFF1405}},
    {"EndingFindElement", kTitleUpdate023, 0x8243EF50, {0x7D8802A6, 0x9181FFF8, 0xFBE1FFF0, 0x9421FFA0, 0x3D608480, 0x7C7F1B78, 0x806BB46C, 0x4BFF1375}},
    {"EndingSetAlpha", kBase013, 0x8243F158, {0x7D8802A6, 0x9181FFF8, 0x9421FF90, 0x356300A0, 0x40820014, 0x3D608202, 0x386B38F4, 0x4BFF0DFD}},
    {"EndingSetAlpha", kTitleUpdate023, 0x8243F928, {0x7D8802A6, 0x9181FFF8, 0x9421FF90, 0x356300A0, 0x40820014, 0x3D608202, 0x386B39D4, 0x4BFF0D6D}},
    {"EndingSendEvent", kBase013, 0x82441828, {0x7D8802A6, 0x495BE37D, 0x9421FF80, 0x3D608480, 0x7C7C1B78, 0x806BAF4C, 0x2B030000, 0x419A0070}},
    {"EndingSendEvent", kTitleUpdate023, 0x82441FF8, {0x7D8802A6, 0x495BE30D, 0x9421FF80, 0x3D608480, 0x7C7C1B78, 0x806BB46C, 0x2B030000, 0x419A0070}},
    {"EndingPrepareMusic", kBase013, 0x82376740, {0x7D8802A6, 0x9181FFF8, 0x9421FEA0, 0x90610174, 0x3D6083A3, 0x816B51D0, 0x2F0BFFFF, 0x419A003C}},
    {"EndingPrepareMusic", kTitleUpdate023, 0x82376810, {0x7D8802A6, 0x9181FFF8, 0x9421FEA0, 0x90610174, 0x3D6083A3, 0x816B51D0, 0x2F0BFFFF, 0x419A003C}},
    {"EndingPlayMusic", kBase013, 0x82376908, {0x7D8802A6, 0x9181FFF8, 0x9421FF90, 0xB0610086, 0xA1610086, 0x91610050, 0x81610050, 0x556B0631}},
    {"EndingPlayMusic", kTitleUpdate023, 0x823769D8, {0x7D8802A6, 0x9181FFF8, 0x9421FF90, 0xB0610086, 0xA1610086, 0x91610050, 0x81610050, 0x556B0631}},
    {"EndingFinish", kBase013, 0x823C66D0, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x90610074, 0x81610074, 0x3D4083C2, 0x916A7564, 0x81610074}},
    {"EndingFinish", kTitleUpdate023, 0x823C6CF8, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x90610074, 0x81610074, 0x3D4083C2, 0x916A78DC, 0x81610074}},
    {"HalfCircleTracker", kBase013, 0x82264F78, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x39400000, 0x914B52CC, 0x3D6083CD, 0x816B52D8}},
    {"HalfCircleTracker", kTitleUpdate023, 0x822653E8, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x39400000, 0x914B57E4, 0x3D6083CD, 0x816B57F0}},
    {"ShakunetsuStarter", kBase013, 0x8225DED8, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x816B52D8, 0x3940000C, 0x994B0003, 0x3D6083CD}},
    {"ShakunetsuStarter", kTitleUpdate023, 0x8225DF10, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x816B57F0, 0x3940000C, 0x994B0003, 0x3D6083CD}},
    {"KenShoryukenStarter", kBase013, 0x82282868, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x816B52D8, 0x3940000C, 0x994B0003, 0x3D6083CD}},
    {"KenShoryukenStarter", kTitleUpdate023, 0x82282F60, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x816B57F0, 0x3940000C, 0x994B0003, 0x3D6083CD}},
    {"KenTatsumakiStarter", kBase013, 0x822826A0, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x816B52D8, 0x3940000C, 0x994B0003, 0x3D6083CD}},
    {"KenTatsumakiStarter", kTitleUpdate023, 0x82282D98, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x816B57F0, 0x3940000C, 0x994B0003, 0x3D6083CD}},
    {"HeavyPunch", kBase013, 0x8225E1F0, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x816B52D8, 0x39400000, 0x994B0087, 0x3D6083CD}},
    {"HeavyPunch", kTitleUpdate023, 0x8225E228, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x816B57F0, 0x39400000, 0x994B0087, 0x3D6083CD}},
    {"RyuHadokenVoice", kBase013, 0x821E5B5C, {0x48192A3D, 0x3D6083CD, 0x39400061, 0x914B52C8, 0x3D6083CD, 0x39400000, 0x914B52A0, 0x3D6083CD}},
    {"RyuHadokenVoice", kTitleUpdate023, 0x821E5EBC, {0x481927AD, 0x3D6083CD, 0x39400061, 0x914B57E0, 0x3D6083CD, 0x39400000, 0x914B57B8, 0x3D6083CD}},
    {"RyuHadokenVoiceSecond", kBase013, 0x821E5F44, {0x48192655, 0x3D6083CD, 0x39400061, 0x914B52C8, 0x3D6083CD, 0x39400000, 0x914B52A0, 0x3D6083CD}},
    {"RyuHadokenVoiceSecond", kTitleUpdate023, 0x821E61AC, {0x481924BD, 0x3D6083CD, 0x39400061, 0x914B57E0, 0x3D6083CD, 0x39400000, 0x914B57B8, 0x3D6083CD}},
    {"RyuShoryukenVoice", kBase013, 0x821E6168, {0x48192431, 0x3D6083CD, 0x39400060, 0x914B52C8, 0x3D6083CD, 0x39400000, 0x914B52A0, 0x3D6083CD}},
    {"RyuShoryukenVoice", kTitleUpdate023, 0x821E63D0, {0x48192299, 0x3D6083CD, 0x39400060, 0x914B57E0, 0x3D6083CD, 0x39400000, 0x914B57B8, 0x3D6083CD}},
    {"RyuTatsumakiVoice", kBase013, 0x821E6678, {0x3D6083CD, 0x396B52C8, 0x39400062, 0xB14B0002, 0x3D6083CD, 0x39400000, 0x914B52A0, 0x3D6083CD}},
    {"RyuTatsumakiVoice", kTitleUpdate023, 0x821E6B90, {0x3D6083CD, 0x396B57E0, 0x39400062, 0xB14B0002, 0x3D6083CD, 0x39400000, 0x914B57B8, 0x3D6083CD}},
    {"KenThrowVoice", kBase013, 0x821F5620, {0x994B0004, 0x3D6083CD, 0x3D40013E, 0x614A0068, 0x914B52C8, 0x3D6083CD, 0x396B52C0, 0x39400080}},
    {"KenThrowVoice", kTitleUpdate023, 0x821F5910, {0x994B0004, 0x3D6083CD, 0x3D40013E, 0x614A0068, 0x914B57E0, 0x3D6083CD, 0x396B57D8, 0x39400080}},
    {"KenThrowVoiceLanding", kBase013, 0x821F5718, {0x996A0004, 0x3D6083CD, 0x3D40013E, 0x614A0068, 0x914B52C8, 0x3D6083CD, 0x396B52C0, 0x39400080}},
    {"KenThrowVoiceLanding", kTitleUpdate023, 0x821F5A08, {0x996A0004, 0x3D6083CD, 0x3D40013E, 0x614A0068, 0x914B57E0, 0x3D6083CD, 0x396B57D8, 0x39400080}},
    {"KenHadokenVoice", kBase013, 0x821F6484, {0x3D6083CD, 0x396B52C8, 0x39400066, 0xB14B0002, 0x3D6083CD, 0x39400000, 0x914B52A0, 0x3D6083CD}},
    {"KenHadokenVoice", kTitleUpdate023, 0x821F672C, {0x3D6083CD, 0x396B57E0, 0x39400066, 0xB14B0002, 0x3D6083CD, 0x39400000, 0x914B57B8, 0x3D6083CD}},
    {"KenShoryukenVoice", kBase013, 0x821F65BC, {0x3D6083CD, 0x396B52C8, 0x39400065, 0xB14B0002, 0x3D6083CD, 0x39400000, 0x914B52A0, 0x3D6083CD}},
    {"KenShoryukenVoice", kTitleUpdate023, 0x821F6864, {0x3D6083CD, 0x396B57E0, 0x39400065, 0xB14B0002, 0x3D6083CD, 0x39400000, 0x914B57B8, 0x3D6083CD}},
    {"KoSideSwap", kBase013, 0x821B02E0, {0x3D6083CD, 0x816B52D8, 0x896B0381, 0x7D6B0774, 0x3D4083CD, 0x916A52A8, 0x3D6083CD, 0x816B52A8}},
    {"KoSideSwap", kTitleUpdate023, 0x821B0398, {0x3D6083CD, 0x816B57F0, 0x896B0381, 0x7D6B0774, 0x3D4083CD, 0x916A57C0, 0x3D6083CD, 0x816B57C0}},
    {"HitKoTableLoad", kBase013, 0x821B02A0, {0x3D6083A3, 0x396B9060, 0x3D4083CD, 0x916A52E0, 0x3D6083CD, 0x396B52CC, 0xA16B0002, 0x7D6B0734}},
    {"HitKoTableLoad", kTitleUpdate023, 0x821B0358, {0x3D6083A3, 0x396B9060, 0x3D4083CD, 0x916A57F8, 0x3D6083CD, 0x396B57E4, 0xA16B0002, 0x7D6B0734}},
    {"HitKoTableLoadSecond", kBase013, 0x821B0890, {0x3D6083A3, 0x396B9060, 0x3D4083CD, 0x916A52F0, 0x3D6083CD, 0x396B52CC, 0xA16B0002, 0x7D6B0734}},
    {"HitKoTableLoadSecond", kTitleUpdate023, 0x821B0948, {0x3D6083A3, 0x396B9060, 0x3D4083CD, 0x916A5808, 0x3D6083CD, 0x396B57E4, 0xA16B0002, 0x7D6B0734}},
    {"RoundEndScream", kBase013, 0x8223CDD0, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083E1, 0x816B9CC0, 0x396B0700, 0x3D4083CD, 0x916A52E8}},
    {"RoundEndScream", kTitleUpdate023, 0x8223CEF8, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083E1, 0x816BA1D8, 0x396B0700, 0x3D4083CD, 0x916A5800}},
    {"RoundEndLoser", kBase013, 0x8223CDFC, {0x3D6083CD, 0x816B52DC, 0x896B054C, 0x3D4083CD, 0x394A52C4, 0x996A0003, 0x3D6083CD, 0x396B52C4}},
    {"RoundEndLoser", kTitleUpdate023, 0x8223CF24, {0x3D6083CD, 0x816B57F4, 0x896B054C, 0x3D4083CD, 0x394A57DC, 0x996A0003, 0x3D6083CD, 0x396B57DC}},
    {"RoundEndKoTableLoad", kBase013, 0x8223CFCC, {0x3D4083A3, 0x394ACF54, 0x7D6B522E, 0x3D4083CD, 0x394A52C8, 0xB16A0002, 0x3D6083CD, 0x39400000}},
    {"RoundEndKoTableLoad", kTitleUpdate023, 0x8223D0F4, {0x3D4083A3, 0x394ACF54, 0x7D6B522E, 0x3D4083CD, 0x394A57E0, 0xB16A0002, 0x3D6083CD, 0x39400000}},
    {"SoundConsumer", kBase013, 0x823773B8, {0x7D8802A6, 0x9181FFF8, 0x9421FF30, 0x3D6083CD, 0xA16B204C, 0x3D4083CD, 0xA14A4050, 0x7F0B5000}},
    {"SoundConsumer", kTitleUpdate023, 0x82377488, {0x7D8802A6, 0x9181FFF8, 0x9421FF30, 0x3D6083CD, 0xA16B2564, 0x3D4083CD, 0xA14A4568, 0x7F0B5000}},
    {"SoundQueueRing", kBase013, 0x823782DC, {0x3D6083CD, 0xA16B4050, 0x556B2036, 0x3D4083CD, 0x394A4058, 0xA1210076, 0x7D2A5B2E, 0x3D6083CD}},
    {"SoundQueueRing", kTitleUpdate023, 0x823783AC, {0x3D6083CD, 0xA16B4568, 0x556B2036, 0x3D4083CD, 0x394A4570, 0xA1210076, 0x7D2A5B2E, 0x3D6083CD}},
    {"HitKoTable", kBase013, 0x83A29060, {0x013A0064, 0x0150007A, 0x0153007D, 0x014A0074, 0x013F0069, 0x01460070, 0x015C0086, 0x013A0064}},
    {"RoundEndKoTable", kBase013, 0x83A2CF54, {0x0064007A, 0x007D0074, 0x00690070, 0x00860064, 0x00990096, 0x0088008F, 0x00AA009D, 0x00A301C5}},
    {"HitKoTable", kTitleUpdate023, 0x83A29060, {0x013A0064, 0x0150007A, 0x0153007D, 0x014A0074, 0x013F0069, 0x01460070, 0x015C0086, 0x013A0064}},
    {"RoundEndKoTable", kTitleUpdate023, 0x83A2CF54, {0x0064007A, 0x007D0074, 0x00690070, 0x00860064, 0x00990096, 0x0088008F, 0x00AA009D, 0x00A301C5}},
    {"KenTracker", kBase013, 0x8228E440, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x816B52D8, 0x39400000, 0x994B016D, 0x4BFFFE5D}},
    {"KenTracker", kTitleUpdate023, 0x8228ED10, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x816B57F0, 0x39400000, 0x994B016D, 0x4BFFFE5D}},
    {"KenSpecialClass", kBase013, 0x8228E488, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x39400000, 0x914B52A0, 0x3D6083CD, 0x816B52D8}},
    {"KenSpecialClass", kTitleUpdate023, 0x8228ED58, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x39400000, 0x914B57B8, 0x3D6083CD, 0x816B57F0}},
    {"KenKick236", kBase013, 0x8228D890, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x39400000, 0x914B52CC, 0x3D6083CD, 0x816B52D8}},
    {"KenKick236", kTitleUpdate023, 0x8228E160, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x39400000, 0x914B57E4, 0x3D6083CD, 0x816B57F0}},
    {"KenKick2nd", kBase013, 0x8228D8F8, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x39400000, 0x914B52CC, 0x3D6083CD, 0x816B52D8}},
    {"KenKick2nd", kTitleUpdate023, 0x8228E1C8, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x39400000, 0x914B57E4, 0x3D6083CD, 0x816B57F0}},
    {"KenKick41236", kBase013, 0x8228D960, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x39400000, 0x914B52CC, 0x3D6083CD, 0x816B52D8}},
    {"KenKick41236", kTitleUpdate023, 0x8228E230, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x39400000, 0x914B57E4, 0x3D6083CD, 0x816B57F0}},
    {"CpuMoveSelect", kBase013, 0x82195568, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x816B52D8, 0x39400000, 0x994B02B2, 0x3D6083CD}},
    {"CpuMoveSelect", kTitleUpdate023, 0x821953A0, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x816B57F0, 0x39400000, 0x994B02B2, 0x3D6083CD}},
    {"SuperFreeze", kBase013, 0x82377AC0, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x38800005, 0x3D6083CD, 0x816B52D8, 0x896B0381, 0x7D630774}},
    {"SuperFreeze", kTitleUpdate023, 0x82377B90, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x38800005, 0x3D6083CD, 0x816B57F0, 0x896B0381, 0x7D630774}},
    {"JumpStart", kBase013, 0x821B41F8, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x816B52D8, 0x39400004, 0x994B0003, 0x3D6083CD}},
    {"JumpStart", kTitleUpdate023, 0x821B4318, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x816B57F0, 0x39400004, 0x994B0003, 0x3D6083CD}},
    {"CpuJumpStart", kBase013, 0x8218BB30, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x816B52D8, 0x39400004, 0x994B0003, 0x3D6083CD}},
    {"CpuJumpStart", kTitleUpdate023, 0x8218B998, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x816B57F0, 0x39400004, 0x994B0003, 0x3D6083CD}},
    {"SetClassic", kBase013, 0x823A89D8, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x816B52E0, 0x39400001, 0x994B03B6, 0x3D6083CD}},
    {"SetClassic", kTitleUpdate023, 0x823A89C8, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x816B57F8, 0x39400001, 0x994B03B6, 0x3D6083CD}},
    {"SelectPrompt", kBase013, 0x82246820, {0x7D8802A6, 0x9181FFF8, 0x9421FF90, 0x90610084, 0x3D6083C1, 0x39400000, 0x994B33A6, 0x3D6083C1}},
    {"SelectPrompt", kTitleUpdate023, 0x82246868, {0x7D8802A6, 0x9181FFF8, 0x9421FF90, 0x90610084, 0x3D6083C1, 0x39400000, 0x994B3426, 0x3D6083C1}},
    {"SelectOptions", kBase013, 0x82244BF0, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083C3, 0x816BA6E4, 0x556B05B1, 0x41820014, 0x3D6083C3}},
    {"SelectOptions", kTitleUpdate023, 0x82244C58, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083C3, 0x816BAA5C, 0x556B05B1, 0x41820014, 0x3D6083C3}},
    {"ContinueScreen", kBase013, 0x8223AAB8, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083A2, 0x39400007, 0x914B130C, 0x4817CF99, 0x4813DAC5}},
    {"ContinueScreen", kTitleUpdate023, 0x8223ABE0, {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083A2, 0x39400007, 0x914B130C, 0x4817CEA1, 0x4813DA6D}},
};

struct BuildData {
  const char* name;
  const char* file;
  uint32_t sound_flags;
  uint32_t quote_dialog;
  std::array<uint32_t, 2> quote_winner_call;
  uint32_t taunt_table_load;
  uint32_t taunt_table;
  uint32_t continue_load;
  uint32_t continue_table;
  uint32_t losing_load;
  uint32_t losing_table;
  uint32_t ending_ken_case;
  uint32_t ending_setter;
  uint32_t ending_word;
  uint32_t hd_style;
  std::array<uint32_t, 2> hd_style_writes;
  uint32_t classic_setter;
  std::array<uint32_t, 2> classic_calls;
  std::array<uint32_t, 10> classic_stores;
  std::array<uint32_t, 3> classic_option_reads;
  uint32_t classic_code_gate;
  uint32_t classic_code_style_read;
  uint32_t observe_lock_gate_call;
  uint32_t select_prompt;
  uint32_t prompt_dispatch;
  uint32_t observe_lock_prompt_call;
  uint32_t continue_voice;
  uint32_t continue_voice_load;
  uint32_t continue_voice_call;
};

constexpr BuildData kBuilds[] = {
    {"Base013", "game/analysis/default-0.0.1.3.image.bin", 0x83A37DB0, 0x823CE430, {0x8223891C, 0x82238C24}, 0x823CE3FC, 0x83C27380,
     0x82179C68, 0x83BE2F40, 0x82179D48, 0x83BE2F88, 0x823CC0A8, 0x8223A5F4, 0x83BE5904,
     0x83A3876D, {0x823C87DC, 0x823C87F4}, 0x823A89D8, {0x82246B88, 0x823A9050},
     {0x8223E970, 0x8223EFBC, 0x82240C48, 0x82242B60, 0x823A89F0, 0x823C7044, 0x823C717C, 0x824B4528, 0x824EC3F8, 0x8270E00C},
     {0x82244D78, 0x82244E64, 0x82244F00}, 0x82240E98, 0x82240ED8, 0x82246F94, 0x82246820, 0x82246CD0, 0x82246DC8,
     0x8223AAB8, 0x8223AB40, 0x8223D090},
    {"TU023", "game/analysis/default.image.bin", 0x83A37E28, 0x823CEA10, {0x82238A44, 0x82238D4C}, 0x823CE9DC, 0x83C276F8,
     0x82179C88, 0x83BE2FC0, 0x82179D68, 0x83BE3008, 0x823CC688, 0x8223A71C, 0x83BE5984,
     0x83A387E5, {0x823C8DD4, 0x823C8DEC}, 0x823A89C8, {0x82246BD0, 0x823A9040},
     {0x8223EAA0, 0x8223F0EC, 0x82240D78, 0x82242BC8, 0x823A89E0, 0x823C763C, 0x823C7774, 0x824B4C80, 0x824ECB50, 0x8270E764},
     {0x82244DE0, 0x82244ECC, 0x82244F68}, 0x82240FC8, 0x82241008, 0x82246FDC, 0x82246868, 0x82246D18, 0x82246E10,
     0x8223ABE0, 0x8223AC68, 0x8223D1B8},
};

uint32_t Word(const std::vector<uint8_t>& image, uint32_t address) {
  const size_t at = address - kImageBase;
  return uint32_t(image[at]) << 24 | image[at + 1] << 16 | image[at + 2] << 8 | image[at + 3];
}

bool Calls(const std::vector<uint8_t>& image, uint32_t site, uint32_t target) {
  const uint32_t branch = Word(image, site);
  const int32_t reach = int32_t((branch & 0x03FFFFFC) << 6) >> 6;
  return (branch & 0xFC000003) == 0x48000001 && site + uint32_t(reach) == target;
}

// A whole-image answer, so a route these lists do not name fails here instead of staying unknown.
void TestClassicRoutes(const std::vector<uint8_t>& image, const BuildData& build) {
  constexpr uint32_t kTextEnd = 0x82A00000;
  constexpr uint32_t kStoreByte = 38;
  constexpr uint32_t kClassicFlag = 0x3B6;
  constexpr uint32_t kLiR10One = 0x39400001;
  std::vector<uint32_t> stores;
  std::vector<uint32_t> calls;
  std::vector<uint32_t> style_writes;
  const uint32_t style_low = uint16_t(build.hd_style);
  const uint32_t style_high = 0x3D600000u | uint16_t((build.hd_style + 0x8000) >> 16);
  for (uint32_t at = kImageBase + 8; at < kTextEnd; at += 4) {
    const uint32_t word = Word(image, at);
    if ((word >> 26) == kStoreByte && (word & 0xFFFF) == kClassicFlag) stores.push_back(at);
    if ((word >> 26) == kStoreByte && (word & 0xFFFF) == style_low && Word(image, at - 8) == style_high) {
      style_writes.push_back(at);
    }
    if (Calls(image, at, build.classic_setter)) calls.push_back(at);
  }
  Check(std::equal(stores.begin(), stores.end(), build.classic_stores.begin(), build.classic_stores.end()),
        "these are all the stores to the classic flag");
  for (const uint32_t store : stores) {
    Check((Word(image, store - 4) == kLiR10One) == (store == build.classic_setter + 0x18),
          "only the classic setter stores a literal one");
  }
  Check(std::equal(calls.begin(), calls.end(), build.classic_calls.begin(), build.classic_calls.end()),
        "the classic setter has two callers");
  Check(std::equal(style_writes.begin(), style_writes.end(), build.hd_style_writes.begin(), build.hd_style_writes.end()),
        "one function writes the style byte");

  constexpr std::array<uint32_t, 9> kOptionTail = {0x2C0B0000, 0x41820014, 0x81610050, 0x39400000, 0x994B0002,
                                                   0x48000010, 0x81610050, 0x39400001, 0x994B0002};
  const uint32_t style_read = 0x896B0000u | style_low;
  for (const uint32_t read : build.classic_option_reads) {
    Check(Word(image, read) == style_high && Word(image, read + 4) == style_read,
          "the versus modes read the style for the NORMAL/SUPER prompt");
    for (size_t i = 0; i < kOptionTail.size(); ++i) {
      Check(Word(image, read + 8 + uint32_t(i) * 4) == kOptionTail[i], "the prompt option is the inverse of the style");
    }
  }
  Check(Word(image, build.classic_code_style_read) == style_read, "the arcade classic code reads the style");
  Check(Word(image, build.classic_code_style_read + 8) == 0x41820008, "the arcade classic code runs only without it");
  Check(Word(image, build.classic_code_style_read - 0x14) == 0x896B0002, "and only without the prompt");
  Check(Calls(image, build.observe_lock_gate_call, build.classic_code_gate), "the lock hook runs the arcade classic code");
  Check(Calls(image, build.observe_lock_prompt_call, build.prompt_dispatch), "the lock hook runs the prompts");
  constexpr uint32_t kPromptStates = 0x83A2D53C;
  Check(Word(image, kPromptStates) == build.select_prompt && Word(image, kPromptStates + 8) == build.select_prompt,
        "prompt states 0 and 2 are the function that calls the setter");
  Check(build.classic_calls[0] > build.select_prompt && build.classic_calls[0] < build.select_prompt + 0x450,
        "the setter call lies in the prompt");

  constexpr uint32_t kContinueVoices = 0x83A2CF20;
  Check(Word(image, build.continue_voice_load) == (0x3D400000u | uint16_t((kContinueVoices + 0x8000) >> 16)) &&
            Word(image, build.continue_voice_load + 4) == (0x394A0000u | uint16_t(kContinueVoices)),
        "the Continue screen reads its voice table");
  Check(uint16_t(Word(image, kContinueVoices + 8) >> 16) == kKenContinueSound, "Ken's Continue voice");
  Check(Calls(image, build.continue_voice_call, build.continue_voice), "the round end scream state starts Continue");
}

struct ClassicAkumaBuild {
  uint32_t commit;
  uint32_t commit_end;
  uint32_t tick;
  uint32_t tick_end;
  uint32_t prompt;
  uint32_t prompt_end;
  uint32_t confirm;
  uint32_t unlock;
  uint32_t asset_request;
  uint32_t rule_flag;
  uint32_t portrait_load;
  uint32_t navigate;
  uint32_t navigate_end;
  uint32_t cursor;
  uint32_t style;
  uint32_t live;
  uint32_t akuma_table;
  uint32_t selected;
  uint32_t confirm_result;
  uint32_t online_gate;
  uint32_t old_version_prompt;
  uint32_t prompt_gate_frame;
  uint32_t mode;
  uint32_t mode_setter;
  uint32_t mode_setter_name;
  uint32_t old_version_setter;
  uint32_t training_join;
};

constexpr ClassicAkumaBuild kClassicAkuma[] = {
    {0x82240870, 0x82240E98, 0x82246D38, 0x82247620, 0x82246820, 0x82246CD0, 0x82240F78, 0x823C9668, 0x823B5EA0,
     0x821C1BE0, 0x821837B0, 0x82245748, 0x82245988, 0x8232DF78, 0x83A3876D, 0x83CA6C9C, 0x83CA6C88, 0x83CD52CE,
     0x83CD52A8, 0x83C24DB0, 0x83CA6C76, 0x82247E90, 0x83CA294F, 0x823C7298, 0x8200ADF4, 0x823A89D8, 0x822427E0},
    {0x822409A0, 0x82240FC8, 0x82246D80, 0x82247668, 0x82246868, 0x82246D18, 0x822410A8, 0x823C9C60, 0x823B5ED0,
     0x821C1D00, 0x821837D0, 0x822457B0, 0x822459F0, 0x8232DE58, 0x83A387E5, 0x83CA71B4, 0x83CA71A0, 0x83CD57E6,
     0x83CD57C0, 0x83C25128, 0x83CA718E, 0x82247ED8, 0x83CA2E67, 0x823C7890, 0x8200ADFC, 0x823A89C8,
     0x82242848},
};

int CallsIn(const std::vector<uint8_t>& image, uint32_t start, uint32_t end, uint32_t target) {
  int calls = 0;
  for (uint32_t at = start; at < end; at += 4) calls += Calls(image, at, target) ? 1 : 0;
  return calls;
}

bool HasWord(const std::vector<uint8_t>& image, uint32_t start, uint32_t end, uint32_t word) {
  for (uint32_t at = start; at < end; at += 4) {
    if (Word(image, at) == word) return true;
  }
  return false;
}

// lis rX, then a load, store or addi of the low half on any register.
bool LoadsAddress(const std::vector<uint8_t>& image, uint32_t start, uint32_t end, uint32_t address) {
  const uint32_t high = uint16_t((address + 0x8000) >> 16);
  const uint32_t low = uint16_t(address);
  for (uint32_t at = start; at < end; at += 4) {
    const uint32_t lis = Word(image, at);
    if ((lis >> 26) != 15 || (lis & 0xFFFF) != high) continue;
    if ((Word(image, at + 4) & 0xFFFF) == low || (Word(image, at + 8) & 0xFFFF) == low) return true;
  }
  return false;
}

bool StoresByteAt(const std::vector<uint8_t>& image, uint32_t start, uint32_t end, uint32_t offset) {
  for (uint32_t at = start; at < end; at += 4) {
    const uint32_t word = Word(image, at);
    if ((word >> 26) == 38 && (word & 0xFFFF) == offset) return true;
  }
  return false;
}

void TestClassicAkuma(const std::vector<uint8_t>& image, const ClassicAkumaBuild& build) {
  constexpr uint32_t kCmpwiThreePunches = 0x2F0B0070;
  constexpr uint32_t kCmplwiCarrierKen = 0x2B0B0004;
  constexpr uint32_t kCmplwiAkumaLive = 0x2B0B0020;
  constexpr uint32_t kCmpwiHoverAkuma = 0x2F0B0010;
  Check(CallsIn(image, build.tick, build.tick_end, build.commit) == 1, "the window call is the tick's one commit call");
  Check(CallsIn(image, build.tick, build.tick_end, build.confirm) == 1, "the tick confirms through the hooked function");
  Check(CallsIn(image, build.prompt, build.prompt_end, build.commit) == 1, "the guest's own prompt calls the commit");
  Check(CallsIn(image, build.commit, build.commit_end, build.unlock) == 1, "the commit asks the unlock the port answers");
  Check(CallsIn(image, build.commit, build.commit_end, build.asset_request) == 1, "the commit requests the fighter");
  Check(CallsIn(image, build.commit, build.commit_end, build.rule_flag) == 1, "the commit reads the mirror rule");
  Check(CallsIn(image, build.commit, build.commit_end, build.portrait_load) == 1, "the commit loads the portrait");
  Check(HasWord(image, build.commit, build.commit_end, kCmpwiThreePunches), "the commit wants the three punches");
  Check(HasWord(image, build.commit, build.commit_end, kCmplwiCarrierKen), "the commit wants a carrier slot");
  for (const uint32_t offset : {945u, 961u, 925u, 950u, 162u}) {
    Check(StoresByteAt(image, build.commit, build.commit_end, offset), "the commit writes its fighter fields");
  }
  Check(LoadsAddress(image, build.commit, build.commit_end, build.akuma_table), "the commit picks the Akuma table");
  Check(LoadsAddress(image, build.commit, build.commit_end, build.style), "the commit reads the style");
  Check(LoadsAddress(image, build.commit, build.commit_end, build.live), "the commit reads live");
  Check(LoadsAddress(image, build.confirm, build.confirm + 0x900, build.confirm_result), "confirm answers in this word");

  const auto table_word = [&](uint32_t table, uint32_t id) { return Word(image, table + id * 4); };
  constexpr uint32_t kClassicMoves = 0x83A2D188;
  constexpr uint32_t kRemixMoves = 0x83A2D320;
  Check(table_word(kClassicMoves, 3) == 0x0707020D, "Classic Guile has no way up to Akuma");
  Check(table_word(kRemixMoves, 3) == 0x2007020D, "Remix Guile goes up to Akuma");
  Check(table_word(kClassicMoves, kAkumaLive) == 0, "every Classic move from id 32 lands on Ryu");
  for (const uint32_t id : {0u, 1u, 2u, 4u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u}) {
    Check(table_word(kClassicMoves, id) == table_word(kRemixMoves, id), "the two arcade grids are the same grid");
  }
  Check(HasWord(image, build.cursor, build.cursor + 0x100, kCmplwiAkumaLive) &&
            HasWord(image, build.cursor, build.cursor + 0x100, kCmpwiHoverAkuma),
        "the Classic cursor step keeps id 32 only while the hover says Akuma");
  Check(LoadsAddress(image, build.navigate, build.navigate_end, build.selected - 2), "the move result reaches the slot");

  constexpr uint32_t kClassicOtherMoves = 0x83A2D210;
  constexpr uint32_t kRemixVersusMoves = 0x83A2D298;
  Check(table_word(kClassicOtherMoves, kAkumaLive) == 0, "Classic outside Arcade also sends id 32 to Ryu");
  Check(table_word(kRemixVersusMoves, kAkumaLive) == 0x20012020, "Remix Local Versus keeps id 32 by itself");
  for (uint32_t id = 0; id < 18; ++id) {
    if (id == 1) continue;
    Check(table_word(kClassicOtherMoves, id) == table_word(kRemixVersusMoves, id), "the two 3x6 grids are the same grid");
  }
  Check(table_word(kClassicOtherMoves, 1) == 0x0E050002 && table_word(kRemixVersusMoves, 1) == 0x20050002,
        "they differ only in Honda's way up to Akuma");
  constexpr uint32_t kCmplwiOnlineMode = 0x2B0B0003;
  constexpr uint32_t kLoadSessionWord = 0x816B0020;
  Check(LoadsAddress(image, build.commit, build.commit + 0x40, build.online_gate - 32) &&
            HasWord(image, build.commit, build.commit + 0x40, kLoadSessionWord) &&
            HasWord(image, build.commit, build.commit + 0x40, kCmplwiOnlineMode),
        "online Classic refuses the commit behind this session word");
  Check(LoadsAddress(image, build.prompt, build.prompt_end, build.old_version_prompt - 2),
        "the locked side's prompts read their options");
  Check(CallsIn(image, build.prompt, build.prompt_end, build.old_version_setter) == 1,
        "the old-version choice is the prompt's one call to the setter");
  constexpr uint32_t kLiZero = 0x39400000;
  constexpr uint32_t kStoreGoukiOption = 0x994B0001;
  bool gouki_cleared = false;
  for (uint32_t at = build.prompt_gate_frame; at < build.prompt_gate_frame + 0x300; at += 4) {
    gouki_cleared = gouki_cleared || (Word(image, at) == kLiZero && Word(image, at + 4) == kStoreGoukiOption);
  }
  Check(gouki_cleared && LoadsAddress(image, build.prompt_gate_frame, build.prompt_gate_frame + 0x300,
                                      build.old_version_prompt - 2),
        "the frame step clears the GOUKI? option, so only NORMAL / SUPER can show");
  constexpr uint32_t kCmplwiTrainingMode = 0x2B0B0002;
  Check(HasWord(image, build.training_join, build.training_join + 0x600, kCmplwiTrainingMode) &&
            LoadsAddress(image, build.training_join, build.training_join + 0x600, build.mode),
        "the dummy's turn starts from Training's own branch");
  Check(HasWord(image, build.confirm, build.confirm + 0x90, kCmplwiTrainingMode), "confirm takes Start its own way in Training");
  constexpr uint32_t kCmpwiModeLimit = 0x2F0B0004;
  Check(HasWord(image, build.mode_setter, build.mode_setter + 0x20, kCmpwiModeLimit) &&
            LoadsAddress(image, build.mode_setter, build.mode_setter + 0x20, build.mode),
        "SetGameMode refuses anything from 4 up");
  const std::string name(image.begin() + (build.mode_setter_name - kImageBase),
                         image.begin() + (build.mode_setter_name - kImageBase) + 12);
  Check(name == std::string("SetGameMode\0", 12), "the setter's script name");
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
  constexpr uint32_t kPunchHandlers = 0x83A2E500;
  constexpr uint32_t kHeavyPunchHandler[] = {0x8225E1F0, 0x8225E228};
  Check(Word(image, kPunchHandlers + 8) == kHeavyPunchHandler[which], "the heavy punch hook is the guest's third punch handler");
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

  const BuildData& build = kBuilds[which];
  constexpr std::array<uint16_t, 2> kHoverOffsets = {0x1374, 0x1378};
  for (size_t side = 0; side < 2; ++side) {
    const uint32_t call = build.quote_winner_call[side];
    Check(Word(image, call - 4) == (0x806B0000u | kHoverOffsets[side]), "the quote takes that side's character word");
    const uint32_t branch = Word(image, call);
    const uint32_t target = call + (branch & 0x03FFFFFC);
    Check((branch & 0xFC000003) == 0x48000001 && (target & 0xFFFFFFFF) == build.quote_dialog,
          "the quote's winner call reaches the dialog");
  }
  const uint32_t table = build.taunt_table;
  Check(Word(image, build.taunt_table_load) == (0x3D400000u | uint16_t((table + 0x8000) >> 16)) &&
            Word(image, build.taunt_table_load + 4) == (0x394A0000u | uint16_t(table)),
        "the taunt table is the one the init fills");
  Check(Word(image, 0x83A2D5E8) == 0x82006560, "Ken's banner id is the WINS_KEN literal");
  const size_t at = 0x82006560 - kImageBase;
  Check(std::string(image.begin() + at, image.begin() + at + 8) == "WINS_KEN", "the literal reads WINS_KEN");

  const auto loads = [&](uint32_t load, uint32_t address) {
    return Word(image, load) == (0x3D400000u | uint16_t((address + 0x8000) >> 16)) &&
           Word(image, load + 4) == (0x394A0000u | uint16_t(address));
  };
  Check(loads(build.continue_load, build.continue_table), "the Continue portrait table is the one that draw reads");
  Check(loads(build.losing_load, build.losing_table), "the Losing portrait table is the one that draw reads");
  Check(build.losing_table - build.continue_table == 72, "the Continue table ends at the swap word before Losing");
  Check(Word(image, build.ending_ken_case + 0x60) == 0x38600021, "case 4 of the ending switch asks for Ken's music");
  Check(uint16_t(Word(image, build.ending_ken_case + 4)) == uint16_t(0x9038), "that name is the literal the hook uses");
  Check(Word(image, build.ending_setter) == (0x3D400000u | uint16_t((build.ending_word + 0x8000) >> 16)) &&
            uint16_t(Word(image, build.ending_setter + 4)) == uint16_t(build.ending_word),
        "the ending word is the one the guest fills with the winner's character");
  const auto text = [&](uint32_t address, size_t size) {
    return std::string(image.begin() + (address - kImageBase), image.begin() + (address - kImageBase) + size);
  };
  Check(text(0x82009030, 8) == std::string("end_ken\0", 8), "the screen set literal");
  Check(text(0x82009038, 7) == std::string("EndKen\0", 7), "the screen literal");
  TestClassicRoutes(image, build);
  TestClassicAkuma(image, kClassicAkuma[which]);
}

}  // namespace

int main() {
  TestImage(kBase013);
  TestImage(kTitleUpdate023);
  if (failures == 0) std::printf("evil ryu fingerprint tests passed\n");
  return failures == 0 ? 0 : 1;
}
