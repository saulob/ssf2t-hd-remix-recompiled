// Guest binding tests: the analysed addresses and fingerprints of both images against the repository's analysis
// images (skipped when absent) and the generated trees, plus the determinism helpers. Headless.
// Run: ssf2t_netplay_guest_tests

#include <filesystem>
#include <iterator>
#include <fstream>
#include <string>

#include "netplay/determinism.h"
#include "netplay/guest_netplay.h"
#include "netplay_test_support.h"

using namespace ssf2t::netplay;
using namespace netplay_test;

namespace {

namespace fs = std::filesystem;

constexpr uint32_t kImageBase = 0x82000000;

fs::path Root() { return fs::path(SSF2T_SOURCE_DIR); }

bool ReadWords(const fs::path& image, uint32_t address, std::span<const uint32_t> expected, bool& readable) {
  std::ifstream in(image, std::ios::binary);
  readable = in.good();
  if (!readable) return false;
  in.seekg(address - kImageBase);
  for (const uint32_t word : expected) {
    uint8_t b[4];
    in.read(reinterpret_cast<char*>(b), 4);
    if (!in) return false;
    if ((uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | b[3]) != word) return false;
  }
  return true;
}

template <typename G>
void CheckFingerprintsAgainstImage(const fs::path& image) {
  bool readable = false;
  ReadWords(image, G::kCallback, G::kCallbackWords, readable);
  if (!readable) {
    std::printf("  SKIP %s image: %s not found\n", G::kName, image.string().c_str());
    return;
  }
  bool r;
  CHECK(ReadWords(image, G::kCallback, G::kCallbackWords, r));
  CHECK(ReadWords(image, G::kSimStep, G::kSimStepWords, r));
  CHECK(ReadWords(image, G::kEffectsGate, G::kEffectsGateWords, r));
  CHECK(ReadWords(image, G::kFightLoop, G::kFightLoopWords, r));
  CHECK(ReadWords(image, G::kInputDecode, G::kInputDecodeWords, r));
  CHECK(ReadWords(image, G::kInputHistoryStep, G::kInputHistoryStepWords, r));
  CHECK(ReadWords(image, G::kRandStep, G::kRandStepWords, r));
  CHECK(ReadWords(image, G::kSideSetup, G::kSideSetupWords, r));
}

template <typename G>
void CheckLayout() {
  CHECK(G::kGlobalsSnapshot + kGlobalsSaveSize == G::kSnapshotPointer);
  CHECK(G::kCallback % 4 == 0 && G::kSimStep % 4 == 0 && G::kEffectsGate % 4 == 0 && G::kFightLoop % 4 == 0);
  CHECK(G::kArenaPointer % 4 == 0 && G::kSnapshotPointer % 4 == 0 && G::kFrameCounter % 4 == 0);
  CHECK(G::kInputTable % 4 == 0);
  CHECK(G::kNetActive >= 0x84000000);
  CHECK(G::kInputDecode % 4 == 0 && G::kInputHistoryStep % 4 == 0 && G::kInputRing % 4 == 0);
  CHECK(G::kInputRing >= kDiagWindowLow && G::kInputRing + kInputRingBytes <= kDiagWindowHigh);
  CHECK(G::kInputTable >= kDiagWindowLow && G::kInputTable + kInputTableStateBytes <= kDiagWindowHigh);
  CHECK(G::kGlobalsSnapshot >= kDiagWindowLow && G::kGlobalsSnapshot + kGlobalsSaveSize <= kDiagWindowHigh);
  CHECK(G::kFrameCounter >= kDiagWindowLow && G::kFrameCounter < kDiagWindowHigh);
  CHECK(kPlayersInTable * kInputEntryStride == 4 * 156);
  CHECK(G::kRandStep % 4 == 0 && G::kSideSetup % 4 == 0 && G::kRngState % 4 == 0 && G::kSideOrder % 4 == 0);
  CHECK(G::kRngState >= kDiagWindowLow && G::kRngState + kRngStateBytes <= kDiagWindowHigh);
  CHECK(G::kSideOrder >= kDiagWindowLow && G::kSideOrder + kSideOrderBytes <= kDiagWindowHigh);
  CHECK(G::kPaceCounter >= kDiagWindowLow && G::kPaceCounter < kDiagWindowHigh);
  const uint32_t restored[][2] = {{G::kInputTable, kInputTableStateBytes}, {G::kInputRing, kInputRingBytes},
                                  {G::kRngState, kRngStateBytes},          {G::kSideOrder, kSideOrderBytes},
                                  {G::kGlobalsSnapshot, kGlobalsSaveSize}};
  for (const auto& r : restored) {
    CHECK(G::kPaceCounter + 4 <= r[0] || G::kPaceCounter >= r[0] + r[1]);
    CHECK(G::kDrawListHigh <= r[0] || G::kDrawListLow >= r[0] + r[1]);
  }
  for (size_t i = 0; i < 5; ++i) {
    for (size_t j = i + 1; j < 5; ++j) {
      CHECK(restored[i][0] + restored[i][1] <= restored[j][0] || restored[j][0] + restored[j][1] <= restored[i][0]);
    }
  }
}

bool GeneratedDefines(const char* tree, const std::string& symbol) {
  const fs::path dir = Root() / "generated" / tree;
  std::error_code ec;
  if (!fs::exists(dir, ec)) return true;
  const std::string needle = "DEFINE_REX_FUNC(" + symbol + ")";
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (entry.path().extension() != ".cpp") continue;
    std::ifstream in(entry.path());
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.find(needle) != std::string::npos) return true;
  }
  return false;
}

char Hex(uint32_t v) { return "0123456789ABCDEF"[v & 15]; }

std::string Name(const char* prefix, uint32_t address) {
  std::string s = std::string(prefix) + "sub_";
  for (int shift = 28; shift >= 0; shift -= 4) s += Hex(address >> shift);
  return s;
}

void TestTraitsAndImages() {
  std::printf("  traits\n");
  CheckLayout<Base013Netplay>();
  CheckLayout<TitleUpdate023Netplay>();
  CHECK(Base013Netplay::kGuest != TitleUpdate023Netplay::kGuest);
  CheckFingerprintsAgainstImage<Base013Netplay>(Root() / "game" / "analysis" / "default-0.0.1.3.image.bin");
  CheckFingerprintsAgainstImage<TitleUpdate023Netplay>(Root() / "game" / "analysis" / "default.image.bin");
}

void TestGeneratedTreesDefineTheHookedFunctions() {
  std::printf("  generated trees\n");
  CHECK(GeneratedDefines("base", Name("base013_", Base013Netplay::kCallback)));
  CHECK(GeneratedDefines("base", Name("base013_", Base013Netplay::kSimStep)));
  CHECK(GeneratedDefines("base", Name("base013_", Base013Netplay::kEffectsGate)));
  CHECK(GeneratedDefines("tu", Name("tu023_", TitleUpdate023Netplay::kCallback)));
  CHECK(GeneratedDefines("tu", Name("tu023_", TitleUpdate023Netplay::kSimStep)));
  CHECK(GeneratedDefines("tu", Name("tu023_", TitleUpdate023Netplay::kEffectsGate)));
}

void TestWordsMatchOnAFakeGuest() {
  std::printf("  fingerprint matching\n");
  std::vector<uint8_t> memory(0x1000, 0);
  const auto store = [&](uint32_t offset, uint32_t word) {
    memory[offset] = uint8_t(word >> 24);
    memory[offset + 1] = uint8_t(word >> 16);
    memory[offset + 2] = uint8_t(word >> 8);
    memory[offset + 3] = uint8_t(word);
  };
  const uint32_t words[] = {0x7D8802A6, 0x9181FFF8, 0x9421FFA0};
  for (uint32_t i = 0; i < 3; ++i) store(0x100 + i * 4, words[i]);
  CHECK(WordsMatch(memory.data(), 0x100, words));
  store(0x108, 0x9421FFA1);
  CHECK(!WordsMatch(memory.data(), 0x100, words));
}

void TestBlobComparison() {
  std::printf("  blob comparison\n");
  std::vector<uint8_t> a(kGuestStateSize + 4, 7), b = a;
  CHECK(CompareBlobs(a, b).equal);
  b[kArenaSaveSize + 0x40] = 8;
  b[kArenaSaveSize + 0x41] = 9;
  b[3] = 1;
  const BlobDiff d = CompareBlobs(a, b);
  CHECK(!d.equal && d.first_offset == 3 && d.differing_bytes == 3 && d.hash_a != d.hash_b);
  const BlobDiff prefix = CompareBlobs(a, b, 3);
  CHECK(prefix.equal);
  std::vector<uint8_t> shorter(a.begin(), a.end() - 4);
  const BlobDiff s = CompareBlobs(a, shorter);
  CHECK(!s.equal && s.first_offset == shorter.size() && s.differing_bytes == 4);
  CHECK(CompareBlobs(a, shorter, shorter.size()).equal);
  CHECK(Fnv1a64(reinterpret_cast<const uint8_t*>("a"), 1) == 0xAF63DC4C8601EC8Cull);
  CHECK(Fnv1a64(nullptr, 0) == 14695981039346656037ull);
}

void TestOffsetNamesAndLogLines() {
  std::printf("  log lines\n");
  CHECK(DescribeOffset(0) == "arena+0x0");
  CHECK(DescribeOffset(kArenaSaveSize - 1) == "arena+0xFFFF");
  CHECK(DescribeOffset(kArenaSaveSize) == "globals+0x0");
  CHECK(DescribeOffset(kGuestStateSize - 1) == "globals+0x299FF");
  CHECK(DescribeOffset(kGuestStateSize) == "input_table+0x0");
  CHECK(DescribeOffset(kGuestStateSize + 8) == "input_table+0x8");
  CHECK(DescribeOffset(kGuestStateSize + kInputTableStateBytes - 1) == "input_table+0x26F");
  CHECK(DescribeOffset(kGuestStateSize + kInputTableStateBytes) == "input_ring+0x0");
  CHECK(DescribeOffset(kGuestStateSize + kInputTableStateBytes + kInputRingBytes - 1) == "input_ring+0x3F");
  CHECK(DescribeOffset(kGuestStateSize + kInputTableStateBytes + kInputRingBytes) == "rng+0x0");
  CHECK(DescribeOffset(kSimStateSize - kSideOrderBytes - 1) == "rng+0x83");
  CHECK(DescribeOffset(kSimStateSize - kSideOrderBytes) == "side_order+0x0");
  CHECK(DescribeOffset(kSimStateSize - 1) == "side_order+0xF");
  CHECK(DescribeOffset(kSimStateSize + 2) == "host+0x2");
  CHECK(kInputTableStateBytes == 624 && kInputRingBytes == 64);
  CHECK(kSimBlobSize == kSimStateSize + 4 && kSimStateSize == kGuestStateSize + 624 + 64 + 0x84 + 16);
  BlobDiff ok;
  CHECK(FormatCheck(123, "resim", ok) == "DEBUG_SAULO netplay frame=123 resim=match");
  BlobDiff bad;
  bad.equal = false;
  bad.first_offset = kArenaSaveSize + 0x10;
  bad.differing_bytes = 2;
  bad.hash_a = 0x1234;
  bad.hash_b = 0xABCDEF;
  CHECK(FormatCheck(7, "roundtrip", bad) ==
        "DEBUG_SAULO netplay frame=7 roundtrip=MISMATCH first=globals+0x10 bytes=2 hashA=0000000000001234 "
        "hashB=0000000000abcdef");
}

void TestRuns() {
  std::printf("  diff runs\n");
  std::vector<uint8_t> a(200, 0), b = a;
  CHECK(FindRuns(a, b, 16, 8).total == 0);
  b[4] = 1;
  b[10] = 1;
  b[100] = 1;
  b[199] = 1;
  const DiffRuns d = FindRuns(a, b, 16, 8);
  CHECK(d.total == 3 && d.runs.size() == 3);
  CHECK(d.runs[0].offset == 4 && d.runs[0].length == 7);
  CHECK(d.runs[1].offset == 100 && d.runs[1].length == 1);
  CHECK(d.runs[2].offset == 199 && d.runs[2].length == 1);
  const DiffRuns capped = FindRuns(a, b, 16, 2);
  CHECK(capped.total == 3 && capped.runs.size() == 2);
  CHECK(FormatRuns(9, "leak", 0x83C19FC0, d) ==
        "DEBUG_SAULO netplay frame=9 leak runs=3 83C19FC4+7 83C1A024+1 83C1A087+1");
  CHECK(FormatRuns(9, "post", 0x1000, capped) == "DEBUG_SAULO netplay frame=9 post runs=3 00001004+7 00001064+1 ...");
  CHECK(FormatRuns(9, "post", 0x1000, FindRuns(a, a, 16, 8)) == "DEBUG_SAULO netplay frame=9 post runs=0");
}

void TestSampleSchedule() {
  std::printf("  sample schedule\n");
  SampleSchedule s(120, 300, 3);
  CHECK(!s.ShouldSample(0));
  CHECK(!s.ShouldSample(119));
  CHECK(s.ShouldSample(120));
  s.Taken(120);
  CHECK(!s.ShouldSample(121));
  CHECK(!s.ShouldSample(419));
  CHECK(s.ShouldSample(420));
  s.Taken(420);
  s.Taken(900);
  CHECK(s.taken() == 3);
  CHECK(!s.ShouldSample(5000));
}

}  // namespace

int main() {
  std::printf("netplay guest tests\n");
  TestTraitsAndImages();
  TestGeneratedTreesDefineTheHookedFunctions();
  TestWordsMatchOnAFakeGuest();
  TestBlobComparison();
  TestOffsetNamesAndLogLines();
  TestRuns();
  TestSampleSchedule();
  std::printf("%d checks, %d failures\n", netplay_test::g_checks, netplay_test::g_failures);
  return netplay_test::g_failures == 0 ? 0 : 1;
}
