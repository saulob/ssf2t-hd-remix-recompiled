// Netplay Determinism Test setting: default, launcher wiring, persisted value, and the single runtime setting both
// guests read. Headless: no window, no game runtime.
// Run: ssf2t_netplay_setting_tests

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include "netplay_setting.h"

#include "trg/settings.h"

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

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  std::string source = text.str();
  source.erase(std::remove(source.begin(), source.end(), '\r'), source.end());
  return source;
}

std::string ReadSource(const char* name) { return ReadFile(std::filesystem::path(SSF2T_SOURCE_DIR) / "src" / name); }

size_t Count(const std::string& text, std::string_view needle) {
  size_t count = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size())) ++count;
  return count;
}

size_t CountInSources(std::string_view needle) {
  size_t count = 0;
  const std::filesystem::path src = std::filesystem::path(SSF2T_SOURCE_DIR) / "src";
  for (const auto& entry : std::filesystem::recursive_directory_iterator(src)) {
    const std::string ext = entry.path().extension().string();
    if (entry.is_regular_file() && (ext == ".cpp" || ext == ".h")) count += Count(ReadFile(entry.path()), needle);
  }
  return count;
}

void TestDefaultIsOff() {
  CHECK(!kDefaultNetplayDeterminism);
  CHECK(std::string_view(kNetplayDeterminismSetting) == "ssf2t_netplay_determinism_test");
  CHECK(ReadSource("netplay_hook.cpp").find("REXCVAR_DEFINE_BOOL(ssf2t_netplay_determinism_test, false, \"SSF2T\"") !=
        std::string::npos);
}

void TestLauncherWording() {
  CHECK(std::string_view(kNetplayDeterminismLabel) == "Netplay Determinism Test");
  CHECK(std::string_view(kNetplayDeterminismHelp) ==
        "Runs rollback save/load/resimulation checks during Local Versus matches.");
  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(launcher.find("ui.Toggle(kNetplayDeterminismLabel, kNetplayDeterminismHelp, kNetplayDeterminismSetting, "
                      "kDefaultNetplayDeterminism,\n              \"Off\", \"On\");") != std::string::npos);
  const size_t gameplay = launcher.find("static void PageGameplay(");
  const size_t toggle = launcher.find("kNetplayDeterminismLabel");
  const size_t achievements = launcher.find("void PageAchievements(");
  CHECK(gameplay != std::string::npos && gameplay < toggle && toggle < achievements);
}

void TestPersistedValueRoundTrip() {
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ssf2t_netplay_setting_tests";
  std::filesystem::create_directories(dir);
  const std::string path = (dir / "settings.toml").string();
  std::ofstream(path) << "# player settings\n";

  CHECK(!trg::TextFileSettings(path).GetBool(kNetplayDeterminismSetting, kDefaultNetplayDeterminism));

  trg::TextFileSettings on_write(path);
  on_write.Set(kNetplayDeterminismSetting, "true");
  CHECK(on_write.Save());
  CHECK(trg::TextFileSettings(path).GetBool(kNetplayDeterminismSetting, false));
  CHECK(ReadFile(path).find("ssf2t_netplay_determinism_test = true") != std::string::npos);

  trg::TextFileSettings off_write(path);
  off_write.Set(kNetplayDeterminismSetting, "false");
  CHECK(off_write.Save());
  CHECK(!trg::TextFileSettings(path).GetBool(kNetplayDeterminismSetting, true));
  const std::string saved = ReadFile(path);
  CHECK(saved.find("ssf2t_netplay_determinism_test = false") != std::string::npos);
  CHECK(Count(saved, "ssf2t_netplay_determinism_test") == 1);
  std::filesystem::remove_all(dir);
}

void TestSettingReachesTheRuntimeCvar() {
  const std::string settings = ReadSource("settings.cpp");
  CHECK(settings.find("      kNetplayDeterminismSetting,\n") != std::string::npos);
  CHECK(Count(settings, "kNetplayDeterminismSetting") == 1);
  CHECK(settings.find("file.Set(key, TomlValue(key, value));") != std::string::npos);
  CHECK(settings.find("rex::cvar::SetFlagByName(key, value);") != std::string::npos);
}

void TestOneSettingServesBothGuests() {
  CHECK(CountInSources("REXCVAR_DEFINE_BOOL(ssf2t_netplay_determinism_test") == 1);
  CHECK(CountInSources("\"ssf2t_netplay_determinism_test\"") == 1);
  CHECK(CountInSources("REXCVAR_GET(ssf2t_netplay_determinism_test)") == 1);
  const std::string hook = ReadSource("netplay_hook.cpp");
  CHECK(Count(hook, "FightStep<np::Base013Netplay>(") == 1);
  CHECK(Count(hook, "FightStep<np::TitleUpdate023Netplay>(") == 1);
  const size_t gate = hook.find("if (REXCVAR_GET(ssf2t_netplay_determinism_test) && CodeMatches<G>(base)");
  CHECK(gate != std::string::npos && gate < hook.find("RunDeterminismSample<G>(ctx, base, calls, frame);"));
}

}  // namespace

int main() {
  TestDefaultIsOff();
  TestLauncherWording();
  TestPersistedValueRoundTrip();
  TestSettingReachesTheRuntimeCvar();
  TestOneSettingServesBothGuests();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
