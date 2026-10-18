// Achievement metadata and persistence from the game's own XDBF, read the way the launcher reads it: a runtime
// without graphics, audio or input. It needs game\extracted and skips without it. All writes go to a temporary
// user data folder.
// Run: ssf2t_achievement_metadata_tests [game folder]

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "launcher_achievements.h"

using namespace ssf2t;

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

constexpr uint32_t kEnglish = 1;
constexpr uint32_t kGerman = 3;

std::filesystem::path MakeTempUserData(const char* name) {
  const std::filesystem::path root = std::filesystem::temp_directory_path() / "ssf2t_achievement_tests" / name;
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  return root;
}

bool HasAnyFile(const std::filesystem::path& folder) {
  std::error_code ec;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(folder, ec)) {
    if (entry.is_regular_file(ec)) return true;
  }
  return false;
}

void TestCleanUserDataStartsLocked(const std::filesystem::path& game) {
  const std::filesystem::path user = MakeTempUserData("clean");
  LauncherAchievements list(game, user, kEnglish, nullptr);
  CHECK(list.ok());
  const std::vector<AchievementEntry>& entries = list.entries();
  CHECK(entries.size() == 12);
  for (size_t i = 0; i < entries.size(); ++i) {
    CHECK(entries[i].info.id == i + 1);
    CHECK(entries[i].info.image_id == i + 1);
    CHECK(!entries[i].info.label.empty());
    CHECK(!entries[i].info.description.empty());
    CHECK(!entries[i].info.unachieved_description.empty());
    CHECK(!entries[i].unlocked);
  }
  const AchievementSummary summary = Summarize(entries);
  CHECK(summary.total == 12 && summary.unlocked == 0);
  CHECK(summary.total_gamerscore == 200 && summary.earned_gamerscore == 0);
  CHECK(!HasAnyFile(user));
}

// The file ReXGlue writes when the game unlocks achievement 10 (0x0A).
void TestPersistedUnlockIsRead(const std::filesystem::path& game) {
  const std::filesystem::path user = MakeTempUserData("unlocked");
  std::filesystem::create_directories(user / "achievements");
  {
    std::ofstream out(user / "achievements" / "584108A4.toml");
    out << "# Achievement unlock state - managed by ReXGlue runtime\n\n[unlocked.10]\nfiletime = 134368092736222312\n";
  }
  LauncherAchievements list(game, user, kEnglish, nullptr);
  CHECK(list.ok());
  const AchievementSummary summary = Summarize(list.entries());
  CHECK(summary.unlocked == 1 && summary.total == 12);
  CHECK(summary.earned_gamerscore == 15 && summary.total_gamerscore == 200);
  for (const AchievementEntry& entry : list.entries()) CHECK(entry.unlocked == (entry.info.id == 10));
}

// One runtime at a time: each reads the strings for the language it is given.
void TestLanguageSelectsStrings(const std::filesystem::path& game) {
  const std::filesystem::path user = MakeTempUserData("language");
  for (const uint32_t language : {kEnglish, kGerman}) {
    LauncherAchievements list(game, user, language, nullptr);
    CHECK(list.ok() && list.language() == language);
    CHECK(list.entries().size() == 12);
    CHECK(!list.entries().empty() && !list.entries()[0].info.label.empty());
  }
}

void TestMissingGameFilesFailCleanly() {
  const std::filesystem::path user = MakeTempUserData("missing");
  LauncherAchievements list(user / "no_game", user, kEnglish, nullptr);
  CHECK(!list.ok());
  CHECK(list.entries().empty());
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path game = std::filesystem::path(SSF2T_SOURCE_DIR) / "game" / "extracted";
  if (argc > 1) game = argv[1];
  std::error_code ec;
  if (!std::filesystem::exists(game / "default.xex", ec)) {
    std::printf("skipped: no default.xex in %s\n", game.string().c_str());
    return 0;
  }
  TestCleanUserDataStartsLocked(game);
  TestPersistedUnlockIsRead(game);
  TestLanguageSelectsStrings(game);
  TestMissingGameFilesFailCleanly();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
