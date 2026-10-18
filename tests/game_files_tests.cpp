// Game file check tests: default.xex casing is normalized without weakening the version check.
// Run: ssf2t_game_files_tests

#include <cstdio>
#include <filesystem>
#include <fstream>

#include "game_files.h"

using namespace ssf2t;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

fs::path MakeDir(const char* name) {
  const fs::path dir = fs::temp_directory_path() / "ssf2t_game_files_tests" / name;
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  return dir;
}

bool HasEntry(const fs::path& dir, const char* name) {
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (entry.path().filename().string() == name) return true;
  }
  return false;
}

void TestUnsupportedXexStillRejected() {
  for (const char* name : {"default.xex", "Default.xex", "DEFAULT.XEX"}) {
    const fs::path dir = MakeDir("unsupported");
    std::ofstream(dir / name, std::ios::binary) << "not a supported xex";
    CHECK(CheckGameFiles(dir).state == GameFiles::kUnsupported);
    CHECK(HasEntry(dir, "default.xex"));
    fs::remove_all(dir);
  }
}

void TestMixedCaseSupportedXexIsNormalized(const fs::path& real_xex) {
  for (const char* name : {"default.xex", "Default.xex", "DEFAULT.XEX"}) {
    const fs::path dir = MakeDir("normalize");
    fs::copy_file(real_xex, dir / name);
    const GameCheck check = CheckGameFiles(dir);
    CHECK(check.xex_crc32 == kXexCrc32);
    CHECK(check.state != GameFiles::kUnsupported);
    CHECK(HasEntry(dir, "default.xex"));
    CHECK(!HasEntry(dir, "default.xex.rename"));
    fs::remove_all(dir);
  }
}

}  // namespace

int main(int argc, char** argv) {
  fs::path xex = fs::path(SSF2T_SOURCE_DIR) / "game" / "extracted" / "default.xex";
  if (argc > 1) xex = argv[1];
  TestUnsupportedXexStillRejected();
  std::error_code ec;
  if (fs::exists(xex, ec)) {
    TestMixedCaseSupportedXexIsNormalized(xex);
  } else {
    std::printf("skipped normalization: no default.xex at %s\n", xex.string().c_str());
  }
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
