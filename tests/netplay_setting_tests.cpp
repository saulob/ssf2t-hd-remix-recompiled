// Netplay Determinism Test setting: default, developer-only launcher wiring, persisted value, and the single runtime
// setting both guests read. Headless: no window, no game runtime.
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
        "Developer: runs rollback save/load/resimulation checks during Local Versus matches.");
  // Not a player setting: only the developer controls show it.
  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(launcher.find("ui.Toggle(kNetplayDeterminismLabel, kNetplayDeterminismHelp, kNetplayDeterminismSetting,\n"
                      "                kDefaultNetplayDeterminism, \"Off\", \"On\");") != std::string::npos);
  const size_t gameplay = launcher.find("static void PageGameplay(");
  const size_t developer = launcher.find("if (DeveloperControlsEnabled()) {", gameplay);
  const size_t toggle = launcher.find("kNetplayDeterminismLabel");
  const size_t developer_end = launcher.find("\n    }\n", launcher.find("kNetplayPeerAddressLabel"));
  const size_t achievements = launcher.find("void PageAchievements(");
  CHECK(gameplay != std::string::npos && gameplay < developer && developer < toggle && toggle < developer_end &&
        developer_end < achievements);
  CHECK(Count(launcher, "kNetplayDeterminismLabel") == 1);
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

void TestNetplayRoleDefaultsToOff() {
  CHECK(std::string_view(kDefaultNetplayRole) == "off");
  CHECK(std::string_view(kNetplayRoleSetting) == "ssf2t_netplay_role");
  CHECK(std::string_view(kDefaultNetplayPeerAddress) == "127.0.0.1");
  CHECK(std::string_view(kNetplayPeerAddressSetting) == "ssf2t_netplay_peer_address");
  const std::string hook = ReadSource("netplay_hook.cpp");
  CHECK(hook.find("REXCVAR_DEFINE_STRING(ssf2t_netplay_role, ssf2t::kDefaultNetplayRole, \"SSF2T\"") !=
        std::string::npos);
  CHECK(hook.find("REXCVAR_DEFINE_STRING(ssf2t_netplay_peer_address, ssf2t::kDefaultNetplayPeerAddress, \"SSF2T\"") !=
        std::string::npos);
}

void TestNetplayRoleLauncherWiring() {
  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(launcher.find("ui.Choice(kNetplayRoleLabel, kNetplayRoleHelp, kNetplayRoleSetting, kDefaultNetplayRole,") !=
        std::string::npos);
  CHECK(launcher.find("ui.InputText(kNetplayPeerAddressLabel, kNetplayPeerAddressHelp, kNetplayPeerAddressSetting,") !=
        std::string::npos);
  // Peer Address is only shown for Join.
  const size_t role_row = launcher.find("kNetplayRoleLabel");
  const size_t peer_row = launcher.find("kNetplayPeerAddressLabel");
  const size_t join_check = launcher.find("kNetplayRoleJoin", role_row);
  CHECK(role_row != std::string::npos && join_check != std::string::npos && join_check < peer_row);
}

void TestNetplaySettingsReachTheRuntimeCvar() {
  const std::string settings = ReadSource("settings.cpp");
  CHECK(settings.find("      kNetplayRoleSetting,\n") != std::string::npos);
  CHECK(settings.find("      kNetplayPeerAddressSetting,\n") != std::string::npos);
}

void TestOneSettingServesBothGuests() {
  CHECK(CountInSources("REXCVAR_DEFINE_BOOL(ssf2t_netplay_determinism_test") == 1);
  CHECK(CountInSources("\"ssf2t_netplay_determinism_test\"") == 1);
  CHECK(CountInSources("REXCVAR_GET(ssf2t_netplay_determinism_test)") == 1);
  const std::string hook = ReadSource("netplay_hook.cpp");
  CHECK(Count(hook, "FightStep<np::Base013Netplay>(") == 1);
  CHECK(Count(hook, "FightStep<np::TitleUpdate023Netplay>(") == 1);
  CHECK(hook.find("return developer && REXCVAR_GET(ssf2t_netplay_determinism_test);") != std::string::npos);
  CHECK(hook.find("static const bool developer = ssf2t::DeveloperControlsEnabled();") != std::string::npos);
  const size_t gate = hook.find("if (DeterminismTestOn() && CodeMatches<G>(base)");
  CHECK(gate != std::string::npos && gate < hook.find("RunDeterminismSample<G>(ctx, base, calls, frame);"));
}

// A role saved in an earlier developer session must not start a hidden Netplay Test.
void TestNetplayRoleNeedsDeveloperControls() {
  if (!DeveloperControlsEnabled()) {
    CHECK(!NetplayTestRole(kNetplayRoleHost) && !NetplayTestRole(kNetplayRoleJoin));
  }
  CHECK(!NetplayTestRole(kNetplayRoleOff) && !NetplayTestRole("") && !NetplayTestRole("other"));
  CHECK(ReadSource("netplay_hook.cpp").find("if (!ssf2t::NetplayTestRole(role)) return false;") != std::string::npos);
  for (const char* source : {"character/akuma_bug_fixes.cpp", "character/ultra_fighters.cpp", "character/versus_cpu_hook.cpp"}) {
    const std::string text = ReadSource(source);
    CHECK(text.find("NetplayTestRole(rex::cvar::GetFlagByName(kNetplayRoleSetting))") != std::string::npos);
    CHECK(text.find("!= kNetplayRoleOff") == std::string::npos);
  }
}

}  // namespace

int main() {
  TestDefaultIsOff();
  TestLauncherWording();
  TestPersistedValueRoundTrip();
  TestSettingReachesTheRuntimeCvar();
  TestOneSettingServesBothGuests();
  TestNetplayRoleDefaultsToOff();
  TestNetplayRoleLauncherWiring();
  TestNetplaySettingsReachTheRuntimeCvar();
  TestNetplayRoleNeedsDeveloperControls();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
