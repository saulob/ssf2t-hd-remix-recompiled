// The non-UI pieces added from King Kong Recompiled: shader pack merging, the
// frame-time summary and the Xbox 360 disc reader's rejection of non-discs.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "trg/game_helpers.h"
#include "trg/platform.h"
#include "trg/shader_pack.h"
#include "trg/xbox360.h"

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
  std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}

void Put32(std::string& s, uint32_t v) { s.append(reinterpret_cast<const char*>(&v), 4); }
void Put64(std::string& s, uint64_t v) { s.append(reinterpret_cast<const char*>(&v), 8); }

// A ReXGlue shader storage file: 'XESH', version, then {hash, dword count, ucode} records.
std::string ShaderFile(const std::vector<uint64_t>& hashes) {
  std::string s;
  Put32(s, 0x48534558);
  Put32(s, 0x19122020);
  for (uint64_t h : hashes) {
    Put64(s, h);
    Put32(s, 2);  // two ucode dwords
    Put32(s, uint32_t(h));
    Put32(s, uint32_t(h >> 32));
  }
  return s;
}

void Write(const char* path, const std::string& data) {
  FILE* f = trg::OpenFile(path, "wb");
  fwrite(data.data(), 1, data.size(), f);
  fclose(f);
}

std::string Read(const char* path) {
  std::string s;
  if (FILE* f = trg::OpenFile(path, "rb")) {
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    fclose(f);
  }
  return s;
}

}  // namespace

int main(int argc, char** argv) {
  // trg_helpers_test <image.iso>: also print a real disc's title ID.
  if (argc > 1) std::printf("title ID of %s: %08X\n", argv[1], trg::ReadXbox360TitleId(argv[1]));
  trg::RemoveFile("pack_test_into.xsh");
  Write("pack_test_pack.xsh", ShaderFile({1, 2, 3}));
  Check(trg::MergeShaderStorageFile("pack_test_pack.xsh", "pack_test_into.xsh") == 3, "merge into a new file copies it");
  Write("pack_test_into.xsh", ShaderFile({3, 4}));
  Check(trg::MergeShaderStorageFile("pack_test_pack.xsh", "pack_test_into.xsh") == 2, "merge adds only missing records");
  Check(Read("pack_test_into.xsh") == ShaderFile({3, 4, 1, 2}), "merged file keeps the old records first");
  Check(trg::MergeShaderStorageFile("pack_test_pack.xsh", "pack_test_into.xsh") == 0, "merging again adds nothing");
  std::string other = ShaderFile({9});
  other[4] ^= 1;  // another runtime version
  Write("pack_test_other.xsh", other);
  Check(trg::MergeShaderStorageFile("pack_test_other.xsh", "pack_test_into.xsh") == -1,
        "a different version is refused");
  Check(Read("pack_test_into.xsh") == ShaderFile({3, 4, 1, 2}), "a refused merge leaves the file alone");

  trg::FrameTimeStats stats(1.0);
  std::optional<std::string> line;
  for (int i = 0; i < 59 && !line; ++i) line = stats.Add(1000.0 / 60.0);
  Check(!line, "no summary before the window ends");
  line = stats.Add(120.0);
  Check(line && line->find("1 over 100 ms") != std::string::npos, "the summary counts long frames");

  Write("pack_test_not_a_disc.iso", std::string(0x20000, 'x'));
  Check(trg::ReadXbox360TitleId("pack_test_not_a_disc.iso") == 0, "a file that is not a disc has no title ID");

  for (const char* f : {"pack_test_pack.xsh", "pack_test_into.xsh", "pack_test_other.xsh", "pack_test_not_a_disc.iso"})
    trg::RemoveFile(f);
  std::puts(failures ? "FAILED" : "all passed");
  return failures ? 1 : 0;
}
