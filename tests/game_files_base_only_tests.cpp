// Game file checks for a program that holds only the 0.0.1.3 image (SSF2T_TITLE_UPDATE_IMAGE=0): the base game alone is
// ready, and a valid Title Update file stays a valid file that this program simply cannot run.
// Run: ssf2t_game_files_base_only_tests [default.xex]

#include <cstdio>
#include <filesystem>
#include <fstream>

#include "game_files.h"

#if SSF2T_TITLE_UPDATE_IMAGE
#error build this test with SSF2T_TITLE_UPDATE_IMAGE=0
#endif

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

void TestBaseAloneIsReady(const fs::path& xex, const fs::path& xexp) {
  CHECK(!kTitleUpdateImageBuilt);
  const fs::path dir = fs::temp_directory_path() / "ssf2t_game_files_base_only_tests";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  fs::copy_file(xex, dir / "default.xex");
  std::ofstream(dir / kAssetArchive, std::ios::binary).close();
  fs::resize_file(dir / kAssetArchive, kAssetArchiveSize);

  GameCheck check = CheckGameFiles(dir);
  CHECK(check.state == GameFiles::kReady);
  CHECK(SelectGuest(check) == GuestVersion::kBase013);
  CHECK(DescribePlayCard(check).title == "Ready to play");
  CHECK(!DescribeGameButtons(check).update_available);

  if (!xexp.empty()) {
    fs::copy_file(xexp, dir / kTitleUpdateFile);
    check = CheckGameFiles(dir);
    CHECK(check.state == GameFiles::kReady && check.guest == GuestVersion::kTitleUpdate023);
    CHECK(check.xexp_crc32 == 0x38DE2310);
    CHECK(!SelectGuest(check));
    CHECK(DescribePlayCard(check).title != "Different Title Update");
    CHECK(DescribeGameButtons(check).update_label == "Remove Title Update");

    const RemoveResult removed = RemoveTitleUpdate(dir);
    CHECK(removed.removed);
    CHECK(SelectGuest(removed.files) == GuestVersion::kBase013);
  }
  fs::remove_all(dir, ec);
}

}  // namespace

int main(int argc, char** argv) {
  fs::path xex = fs::path(SSF2T_SOURCE_DIR) / "game" / "extracted" / "default.xex";
  if (argc > 1) xex = argv[1];
  std::error_code ec;
  if (!fs::exists(xex, ec)) {
    std::printf("skipped: no default.xex at %s\n", xex.string().c_str());
    return 0;
  }
  const fs::path xexp = xex.parent_path() / kTitleUpdateFile;
  TestBaseAloneIsReady(xex, fs::exists(xexp, ec) ? xexp : fs::path());
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
