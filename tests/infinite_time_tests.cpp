// Infinite Time tests: the default and launcher wiring, F10 toggling and repeats, the persisted value, the notice,
// the timer hook decision and its fingerprints. Headless: no window, no game runtime.
// Run: ssf2t_infinite_time_tests

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "game_files.h"
#include "infinite_time.h"

#include "trg/settings.h"
#include "updater/sha256.h"

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

constexpr uint32_t kImageBase = 0x82000000;

std::string ReadSource(const char* name) {
  std::ifstream in(std::filesystem::path(SSF2T_SOURCE_DIR) / "src" / name, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  std::string source = text.str();
  source.erase(std::remove(source.begin(), source.end(), '\r'), source.end());
  return source;
}

size_t CountOccurrences(const std::string& text, std::string_view needle) {
  size_t count = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size())) {
    ++count;
  }
  return count;
}

void TestDefaultIsOff() {
  CHECK(!kDefaultInfiniteTime);
  CHECK(std::string_view(kInfiniteTimeSetting) == "ssf2t_infinite_time");
  CHECK(ReadSource("settings.cpp").find("REXCVAR_DEFINE_BOOL(ssf2t_infinite_time, false, \"SSF2T\"") !=
        std::string::npos);
}

void TestLauncherOffOnWiring() {
  CHECK(std::string_view(kInfiniteTimeLabel) == "Infinite Time");
  CHECK(std::string_view(kInfiniteTimeHelp) == "Prevents the fight timer from decreasing. F10 toggles it while playing.");
  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(launcher.find("ui.Toggle(kInfiniteTimeLabel, kInfiniteTimeHelp, kInfiniteTimeSetting, kDefaultInfiniteTime, "
                      "\"Off\", \"On\");") != std::string::npos);
  const std::string settings = ReadSource("settings.cpp");
  CHECK(settings.find("      kInfiniteTimeSetting,\n") != std::string::npos);
}

void TestF10ToggleOffToOnAndBack() {
  CHECK(ToggledInfiniteTime(false));
  CHECK(!ToggledInfiniteTime(true));
  bool enabled = kDefaultInfiniteTime;
  enabled = ToggledInfiniteTime(enabled);
  CHECK(enabled);
  enabled = ToggledInfiniteTime(enabled);
  CHECK(!enabled);
}

void TestHeldKeyDoesNotRepeatToggle() {
  bool enabled = false;
  int toggles = 0;
  const auto press = [&](bool repeat) {
    if (IsFreshKeyPress(repeat)) {
      enabled = ToggledInfiniteTime(enabled);
      ++toggles;
    }
  };
  press(false);
  for (int i = 0; i < 30; ++i) {
    press(true);
  }
  CHECK(toggles == 1 && enabled);
  press(false);
  for (int i = 0; i < 30; ++i) {
    press(true);
  }
  CHECK(toggles == 2 && !enabled);
}

void TestF10WiringUsesOneSetting() {
  const std::string settings = ReadSource("settings.cpp");
  CHECK(settings.find("bool ToggleInfiniteTime(GameSettings& settings) {") != std::string::npos);
  CHECK(settings.find("ToggledInfiniteTime(REXCVAR_GET(ssf2t_infinite_time))") != std::string::npos);
  CHECK(settings.find("rex::cvar::SetFlagByName(kInfiniteTimeSetting, value);") != std::string::npos);
  CHECK(settings.find("settings.SaveNow({{kInfiniteTimeSetting, value}});") != std::string::npos);
  CHECK(settings.find("  ApplyInfiniteTime();\n  settings.SaveNow") != std::string::npos);
  const std::string app = ReadSource("ssf2t_hd_remix_app.h");
  CHECK(app.find("ShowInfiniteTimeNotice(ToggleInfiniteTime(game_settings_), kInfiniteTimeToggleNoticeSeconds);") !=
        std::string::npos);
  CHECK(app.find("QueueStartupInfiniteTimeNotice(REXCVAR_GET(ssf2t_infinite_time));") != std::string::npos);
  CHECK(app.find("QueueStartupInfiniteTimeNotice(") == app.rfind("QueueStartupInfiniteTimeNotice("));
  CHECK(app.find("DetachInfiniteTimeKey();") != std::string::npos);
  CHECK(app.find("ApplyInfiniteTime();") != std::string::npos);
}

void TestPersistenceRoundTrip() {
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ssf2t_infinite_time_tests";
  std::filesystem::create_directories(dir);
  const std::string path = (dir / "settings.toml").string();
  {
    std::ofstream(path) << "# player settings\n";
  }
  trg::TextFileSettings on_write(path);
  on_write.Set(kInfiniteTimeSetting, "true");
  CHECK(on_write.Save());
  CHECK(trg::TextFileSettings(path).GetBool(kInfiniteTimeSetting, false));

  trg::TextFileSettings off_write(path);
  off_write.Set(kInfiniteTimeSetting, "false");
  CHECK(off_write.Save());
  CHECK(!trg::TextFileSettings(path).GetBool(kInfiniteTimeSetting, true));
  std::filesystem::remove_all(dir);
}

void TestNoticeText() {
  CHECK(std::string_view(InfiniteTimeNoticeText(true)) == "Infinite Time: On");
  CHECK(std::string_view(InfiniteTimeNoticeText(false)) == "Infinite Time: Off");
}

void TestNoticeAppearsAndExpires() {
  CHECK(kInfiniteTimeToggleNoticeSeconds == 2.0);
  InfiniteTimeNotice notice;
  CHECK(!notice.Visible(0.0));
  notice.Show(true, kInfiniteTimeToggleNoticeSeconds, 10.0);
  CHECK(std::string_view(notice.text()) == "Infinite Time: On");
  CHECK(!notice.Visible(9.5));
  CHECK(notice.Visible(10.0));
  CHECK(notice.Visible(11.9));
  CHECK(!notice.Visible(12.0));
  CHECK(!notice.Visible(30.0));
  notice.Show(false, kInfiniteTimeToggleNoticeSeconds, 20.0);
  CHECK(std::string_view(notice.text()) == "Infinite Time: Off");
  CHECK(notice.Visible(21.0));
  CHECK(!notice.Visible(22.5));
}

void TestStartupNoticeOffShowsNothing() {
  CHECK(kInfiniteTimeStartupNoticeSeconds == 5.0);
  InfiniteTimeNotice notice;
  notice.QueueStartup(false, kInfiniteTimeStartupNoticeSeconds);
  CHECK(!notice.Visible(1000.0));
  CHECK(!notice.Visible(1001.0));
}

void TestStartupNoticeOnShowsFiveSeconds() {
  InfiniteTimeNotice notice;
  notice.QueueStartup(true, kInfiniteTimeStartupNoticeSeconds);
  CHECK(std::string_view(notice.text()) == "Infinite Time: On");
  CHECK(notice.Visible(1000.0));
  CHECK(notice.Visible(1004.9));
  CHECK(!notice.Visible(1005.0));
}

void TestStartupNoticeOnlyOncePerLaunch() {
  InfiniteTimeNotice notice;
  notice.QueueStartup(true, kInfiniteTimeStartupNoticeSeconds);
  CHECK(notice.Visible(0.0));
  CHECK(!notice.Visible(6.0));
  notice.QueueStartup(true, kInfiniteTimeStartupNoticeSeconds);
  CHECK(!notice.Visible(7.0));
  CHECK(!notice.Visible(100.0));
}

void TestRoundsDoNotRestartStartupNotice() {
  InfiniteTimeNotice notice;
  notice.QueueStartup(true, kInfiniteTimeStartupNoticeSeconds);
  CHECK(notice.Visible(0.0));
  CHECK(!notice.Visible(5.0));
  for (double round = 1.0; round <= 3.0; round += 1.0) {
    notice.QueueStartup(true, kInfiniteTimeStartupNoticeSeconds);
    CHECK(!notice.Visible(5.0 + 10.0 * round));
  }
}

void TestF10OverridesStartupNotice() {
  InfiniteTimeNotice notice;
  notice.QueueStartup(true, kInfiniteTimeStartupNoticeSeconds);
  CHECK(notice.Visible(0.0));
  notice.Show(false, kInfiniteTimeToggleNoticeSeconds, 1.0);
  CHECK(std::string_view(notice.text()) == "Infinite Time: Off");
  CHECK(notice.Visible(1.0));
  CHECK(notice.Visible(2.9));
  CHECK(!notice.Visible(3.0));
}

void TestF10BeforeStartupNoticeIsDrawn() {
  InfiniteTimeNotice notice;
  notice.QueueStartup(true, kInfiniteTimeStartupNoticeSeconds);
  notice.Show(false, kInfiniteTimeToggleNoticeSeconds, 10.0);
  CHECK(std::string_view(notice.text()) == "Infinite Time: Off");
  CHECK(notice.Visible(11.9));
  CHECK(!notice.Visible(12.0));
}

void TestStartupWaitsForFpsWhenShown() {
  InfiniteTimeNotice notice;
  notice.QueueStartup(true, kInfiniteTimeStartupNoticeSeconds);
  const bool ready_before_fps = StartupNoticeReady(true, false);
  CHECK(!ready_before_fps);
  CHECK(!notice.Visible(100.0, ready_before_fps));
  CHECK(!notice.Visible(103.0, ready_before_fps));
  const bool ready_with_fps = StartupNoticeReady(true, true);
  CHECK(ready_with_fps);
  CHECK(notice.Visible(110.0, ready_with_fps));
  CHECK(notice.Visible(114.9, true));
  CHECK(!notice.Visible(115.0, true));
}

void TestStartupDoesNotWaitWhenFpsHidden() {
  InfiniteTimeNotice notice;
  notice.QueueStartup(true, kInfiniteTimeStartupNoticeSeconds);
  CHECK(StartupNoticeReady(false, false));
  CHECK(notice.Visible(100.0, StartupNoticeReady(false, false)));
  CHECK(notice.Visible(104.9, true));
  CHECK(!notice.Visible(105.0, true));
}

void TestStartupOffWithFpsShowsNothing() {
  InfiniteTimeNotice notice;
  notice.QueueStartup(false, kInfiniteTimeStartupNoticeSeconds);
  CHECK(!notice.Visible(110.0, StartupNoticeReady(true, true)));
  CHECK(!notice.Visible(111.0, true));
}

void TestF10CancelsPendingStartup() {
  InfiniteTimeNotice notice;
  notice.QueueStartup(true, kInfiniteTimeStartupNoticeSeconds);
  CHECK(!notice.Visible(100.0, false));
  notice.Show(false, kInfiniteTimeToggleNoticeSeconds, 101.0);
  CHECK(std::string_view(notice.text()) == "Infinite Time: Off");
  CHECK(notice.Visible(101.0, false));
  CHECK(notice.Visible(102.9, false));
  CHECK(!notice.Visible(103.0, false));
  CHECK(!notice.Visible(110.0, true));
  CHECK(std::string_view(notice.text()) == "Infinite Time: Off");
}

void TestStartupReadyRule() {
  CHECK(StartupNoticeReady(false, false));
  CHECK(StartupNoticeReady(false, true));
  CHECK(!StartupNoticeReady(true, false));
  CHECK(StartupNoticeReady(true, true));
}

void TestStartupUsesFrameCounterVisibility() {
  const std::string ui = ReadSource("infinite_time_ui.cpp");
  CHECK(ui.find("FrameCounterVisible(frame_counter_shown, GetGuestFrameStats())") != std::string::npos);
  CHECK(ui.find("REXCVAR_GET(ssf2t_show_frame_counter)") != std::string::npos);
}

void TestNoticeDoesNotTakeInput() {
  const std::string ui = ReadSource("infinite_time_ui.cpp");
  CHECK(ui.find("set_handled") == std::string::npos);
  CHECK(ui.find("ModalDialog") == std::string::npos);
  CHECK(ui.find("AddInputListener(g_key.get(), kInputZOrder)") != std::string::npos);
}

void TestF10IsTheOnlyKeyAdded() {
  const std::filesystem::path src = std::filesystem::path(SSF2T_SOURCE_DIR) / "src";
  int files_with_f10 = 0;
  for (const auto& entry : std::filesystem::directory_iterator(src)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    std::string name = entry.path().filename().string();
    CHECK(name.find("cheat") == std::string::npos);
    std::ifstream in(entry.path(), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.find("kF10") != std::string::npos) {
      ++files_with_f10;
    }
  }
  CHECK(files_with_f10 == 1);
  CHECK(ReadSource("infinite_time_ui.cpp").find("VirtualKey::kF10") != std::string::npos);
}

void TestNoCheatsAdded() {
  CHECK(ReadSource("infinite_time.cpp").find("cheat") == std::string::npos);
  CHECK(ReadSource("infinite_time_ui.cpp").find("cheat") == std::string::npos);
  CHECK(ReadSource("settings.cpp").find("cheat") == std::string::npos);
  CHECK(ReadSource("launcher.cpp").find("cheat") == std::string::npos);
}

void TestTimerHookIsOnlyTheTick() {
  const std::string hook = ReadSource("infinite_time.cpp");
  CHECK(CountOccurrences(hook, "REX_HOOK_RAW(") == 1);
  CHECK(hook.find("REX_HOOK_RAW(sub_8224EDA0)") != std::string::npos);
  CHECK(hook.find("ssf2t::SkipTimerTick(enabled, supported, TimerCodeMatches(base))") != std::string::npos);
  CHECK(hook.find("__imp__sub_8224EDA0(ctx, base);") != std::string::npos);
  CHECK(hook.find("REX_STORE") == std::string::npos);
  CHECK(hook.find("StoreBE") == std::string::npos);
}

void TestOffRunsStockTimer() {
  CHECK(!SkipTimerTick(false, true, true));
  CHECK(!SkipTimerTick(false, false, true));
  CHECK(!SkipTimerTick(false, true, false));
}

void TestOnSuppressesOnlyWithAllGates() {
  CHECK(SkipTimerTick(true, true, true));
  CHECK(!SkipTimerTick(true, false, true));
  CHECK(!SkipTimerTick(true, true, false));
}

void TestTimerFingerprintMatchesAndFallsBack() {
  static uint8_t memory[0x2000000] = {};
  uint8_t* base = memory - kImageBase;
  uint32_t offset = 0;
  for (const uint32_t word : kTimerTickWords) {
    StoreBE32(base, kTimerTick + offset, word);
    offset += 4;
  }
  CHECK(TimerTickMatches(base));
  CHECK(SkipTimerTick(true, true, TimerTickMatches(base)));

  for (size_t i = 0; i < std::size(kTimerTickWords); ++i) {
    const uint32_t address = kTimerTick + uint32_t(i * 4);
    StoreBE32(base, address, kTimerTickWords[i] ^ 0x00010000);
    CHECK(!TimerTickMatches(base));
    CHECK(!SkipTimerTick(true, true, TimerTickMatches(base)));
    StoreBE32(base, address, kTimerTickWords[i]);
  }
  CHECK(TimerTickMatches(base));
}

void TestTimerHookFallsBackOnUnknownBuild() {
  const std::string hook = ReadSource("infinite_time.cpp");
  CHECK(hook.find("static const bool matches = ssf2t::TimerTickMatches(base);") != std::string::npos);
  CHECK(hook.find("g_xex_supported.load") != std::string::npos);
}

void TestSupportedXexConstants() {
  CHECK(kXexSize == 3932160);
  CHECK(kXexCrc32 == 0xB734F075);
  CHECK(std::string_view(kXexSha256) == "6e1c2f9ed7d1102a311a5f5626563527eda5f85b2fbe8db8405b866ded3f31ee");
  CHECK(kXexpSize == 2166784);
  CHECK(kXexpCrc32 == 0x38DE2310);
  CHECK(std::string_view(kXexpSha256) == "a2e8e66b482c5d3a0d3a5d3f67a3cecb7a12ce1f6189196694fd3e765910606c");
  CHECK(ReadSource("infinite_time.cpp").find("CheckGameFiles(game_dir).state != GameFiles::kReady") !=
        std::string::npos);
  CHECK(ReadSource("infinite_time.cpp").find("== kXexSha256") != std::string::npos);
  CHECK(ReadSource("infinite_time.cpp").find("== kXexpSha256") != std::string::npos);
}

// The decrypted image is not in the repository; the check runs only where it was extracted.
void TestTimerTickInRealImage() {
  const std::filesystem::path image = std::filesystem::path(SSF2T_SOURCE_DIR) / "game" / "analysis" / "default.image.bin";
  std::ifstream in(image, std::ios::binary);
  if (!in) {
    std::printf("  skipped: no decrypted image\n");
    return;
  }
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  CHECK(bytes.size() > 0x1400000);
  CHECK(TimerTickMatches(bytes.data() - kImageBase));
}

// The extracted default.xex is not in the repository; the check runs only where it is present.
void TestSupportedXexOnDisk() {
  const std::filesystem::path game_dir = std::filesystem::path(SSF2T_SOURCE_DIR) / "game" / "extracted";
  std::error_code ec;
  if (!std::filesystem::is_regular_file(game_dir / "default.xex", ec)) {
    std::printf("  skipped: no extracted default.xex\n");
    return;
  }
  std::string error;
  CHECK(trg::update::Sha256FileHex(game_dir / "default.xex", error) == kXexSha256);
  CHECK(error.empty());
  if (std::filesystem::is_regular_file(game_dir / kTitleUpdateFile, ec)) {
    CHECK(trg::update::Sha256FileHex(game_dir / kTitleUpdateFile, error) == kXexpSha256);
    CHECK(error.empty());
  }
}

}  // namespace

int main() {
  TestDefaultIsOff();
  TestLauncherOffOnWiring();
  TestF10ToggleOffToOnAndBack();
  TestHeldKeyDoesNotRepeatToggle();
  TestF10WiringUsesOneSetting();
  TestPersistenceRoundTrip();
  TestNoticeText();
  TestNoticeAppearsAndExpires();
  TestStartupNoticeOffShowsNothing();
  TestStartupNoticeOnShowsFiveSeconds();
  TestStartupNoticeOnlyOncePerLaunch();
  TestRoundsDoNotRestartStartupNotice();
  TestF10OverridesStartupNotice();
  TestF10BeforeStartupNoticeIsDrawn();
  TestStartupWaitsForFpsWhenShown();
  TestStartupDoesNotWaitWhenFpsHidden();
  TestStartupOffWithFpsShowsNothing();
  TestF10CancelsPendingStartup();
  TestStartupReadyRule();
  TestStartupUsesFrameCounterVisibility();
  TestNoticeDoesNotTakeInput();
  TestF10IsTheOnlyKeyAdded();
  TestNoCheatsAdded();
  TestTimerHookIsOnlyTheTick();
  TestOffRunsStockTimer();
  TestOnSuppressesOnlyWithAllGates();
  TestTimerFingerprintMatchesAndFallsBack();
  TestTimerHookFallsBackOnUnknownBuild();
  TestSupportedXexConstants();
  TestTimerTickInRealImage();
  TestSupportedXexOnDisk();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
