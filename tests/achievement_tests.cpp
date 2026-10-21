// Achievement and overlay tests: list and summary logic, close and input rules, the Achievements hook target and
// the wiring of the launcher page, F7 and the controller gate. Pure logic and source reads, no window or game files.
// Run: ssf2t_achievement_tests

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "achievement_list.h"
#include "achievement_ui.h"
#include "guest_memory.h"
#include "overlay_rules.h"

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

std::string ReadSource(const char* name) {
  std::ifstream in(std::filesystem::path(SSF2T_SOURCE_DIR) / "src" / name, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

// Where `a` appears before `b`, both present.
bool Before(const std::string& text, const char* a, const char* b) {
  const size_t first = text.find(a);
  const size_t second = text.find(b);
  return first != std::string::npos && second != std::string::npos && first < second;
}

struct FakeManager {
  std::vector<rex::system::AchievementInfo> infos;
  std::vector<uint32_t> unlocked;

  std::vector<rex::system::AchievementInfo> ListAchievements() const { return infos; }
  bool IsUnlocked(uint32_t id) const {
    for (uint32_t u : unlocked) {
      if (u == id) return true;
    }
    return false;
  }
};

// Twelve achievements, ids 1 to 12, stored out of order.
FakeManager Store() {
  const uint32_t gamerscores[12] = {20, 25, 10, 20, 30, 30, 10, 5, 10, 15, 15, 10};
  FakeManager manager;
  for (uint32_t i = 12; i >= 1; --i) {
    rex::system::AchievementInfo info;
    info.id = i;
    info.label = "Name " + std::to_string(i);
    info.description = "Done " + std::to_string(i);
    info.unachieved_description = "Do " + std::to_string(i);
    info.gamerscore = gamerscores[i - 1];
    manager.infos.push_back(info);
  }
  return manager;
}

void TestFitWindowExtent() {
  CHECK(FitWindowExtent(1000.0f, 420.0f, 560.0f) == 560.0f);
  CHECK(FitWindowExtent(500.0f, 420.0f, 560.0f) == 420.0f);
  CHECK(FitWindowExtent(440.0f, 420.0f, 560.0f) == 408.0f);
  CHECK(FitWindowExtent(300.0f, 420.0f, 560.0f) == 268.0f);
  CHECK(FitWindowExtent(20.0f, 420.0f, 560.0f) == 0.0f);
}

void TestCleanStoreIsLocked() {
  const std::vector<AchievementEntry> entries = ListAchievements(Store());
  CHECK(entries.size() == 12);
  for (const AchievementEntry& entry : entries) CHECK(!entry.unlocked);
  const AchievementSummary summary = Summarize(entries);
  CHECK(summary.total == 12 && summary.unlocked == 0);
  CHECK(summary.total_gamerscore == 200 && summary.earned_gamerscore == 0);
}

void TestListIsInGameOrder() {
  const std::vector<AchievementEntry> entries = ListAchievements(Store());
  for (size_t i = 0; i < entries.size(); ++i) CHECK(entries[i].info.id == i + 1);
}

void TestUnlockedCalculation() {
  FakeManager manager = Store();
  manager.unlocked = {10, 1};
  const std::vector<AchievementEntry> entries = ListAchievements(manager);
  CHECK(entries[0].unlocked && entries[9].unlocked && !entries[1].unlocked);
  const AchievementSummary summary = Summarize(entries);
  CHECK(summary.unlocked == 2 && summary.total == 12);
  CHECK(summary.earned_gamerscore == 35 && summary.total_gamerscore == 200);

  manager.unlocked = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  const AchievementSummary all = Summarize(ListAchievements(manager));
  CHECK(all.unlocked == 12 && all.earned_gamerscore == 200);
}

void TestUnknownUnlockIdsAreIgnored() {
  FakeManager manager = Store();
  manager.unlocked = {0, 99};
  const AchievementSummary summary = Summarize(ListAchievements(manager));
  CHECK(summary.unlocked == 0 && summary.earned_gamerscore == 0);
}

void TestEmptyStore() {
  const AchievementSummary summary = Summarize(ListAchievements(FakeManager{}));
  CHECK(summary.total == 0 && summary.unlocked == 0 && summary.total_gamerscore == 0);
}

void TestEntryDescription() {
  AchievementEntry entry;
  entry.info.description = "earned";
  entry.info.unachieved_description = "to earn";
  CHECK(EntryDescription(entry) == "to earn");
  entry.unlocked = true;
  CHECK(EntryDescription(entry) == "earned");
  entry.unlocked = false;
  entry.info.unachieved_description.clear();
  CHECK(EntryDescription(entry) == "earned");
}

void TestScrollDirection() {
  CHECK(ScrollDirection(true, false, 0.0f) == -1.0f);
  CHECK(ScrollDirection(false, true, 0.0f) == 1.0f);
  CHECK(ScrollDirection(true, true, 0.0f) == 0.0f);
  CHECK(ScrollDirection(true, false, -1.0f) == -1.0f);
  CHECK(ScrollDirection(false, false, 0.2f) == 0.0f);
  CHECK(ScrollDirection(false, false, -0.2f) == 0.0f);
  CHECK(ScrollDirection(false, false, 1.0f) == -1.0f);
  CHECK(ScrollDirection(false, false, -1.0f) == 1.0f);
}

// The B press that closes the overlay must not reach the game: input comes back only after it is released.
void TestCloseGateHoldsUntilRelease() {
  CloseGate gate;
  CHECK(!gate.Step(true, 0.0));
  CHECK(!gate.requested());
  gate.Request();
  CHECK(gate.requested());
  CHECK(!gate.Step(true, 1.0));
  CHECK(!gate.Step(true, 1.2));
  CHECK(!gate.Step(true, 1.9));
  CHECK(gate.Step(false, 2.0));
}

void TestCloseGateGivesUpOnAStuckButton() {
  CloseGate gate;
  gate.Request();
  CHECK(!gate.Step(true, 5.0));
  CHECK(!gate.Step(true, 5.9));
  CHECK(gate.Step(true, 6.0));
}

void TestCloseGateReleasedAtOnce() {
  CloseGate gate;
  gate.Request();
  CHECK(gate.Step(false, 3.0));
}

void TestInputReleased() {
  CHECK(InputReleased(false, 0.0, 1.0));
  CHECK(!InputReleased(true, 0.5, 1.0));
  CHECK(InputReleased(true, 1.0, 1.0));
}

void TestGameInputIsNeutralWhileModalOpen() {
  CHECK(GameInputActive(false, false, false));
  CHECK(!GameInputActive(true, false, false));
  CHECK(!GameInputActive(true, true, true));
  CHECK(GameInputActive(false, true, false));
  CHECK(!GameInputActive(false, true, true));
}

void TestToggleOverlay() {
  CHECK(ToggleOverlay(false, false) == OverlayToggle::kOpen);
  CHECK(ToggleOverlay(true, true) == OverlayToggle::kClose);
  CHECK(ToggleOverlay(true, false) == OverlayToggle::kClose);
  CHECK(ToggleOverlay(false, true) == OverlayToggle::kNone);
}

void TestHookTargetCheck() {
  uint8_t code[8] = {};
  StoreBE32(code, 0, 0x38800000);
  StoreBE32(code, 4, 0x48057BE0);
  CHECK(IsShowAchievementsWrapper(code));
  StoreBE32(code, 0, 0x38800001);
  CHECK(!IsShowAchievementsWrapper(code));
  StoreBE32(code, 0, 0x38800000);
  StoreBE32(code, 4, 0x48057BE4);
  CHECK(!IsShowAchievementsWrapper(code));
}

// The decrypted image is not in the repository; the check runs only where it was extracted.
void TestHookTargetInRealImage() {
  const std::filesystem::path image = std::filesystem::path(SSF2T_SOURCE_DIR) / "game" / "analysis" / "default.image.bin";
  std::ifstream in(image, std::ios::binary);
  if (!in) {
    std::printf("  skipped: no decrypted image\n");
    return;
  }
  constexpr uint32_t kImageBase = 0x82000000;
  const std::streamoff offset = kShowAchievementsWrapper - kImageBase;
  uint8_t words[8] = {};
  in.seekg(offset);
  in.read(reinterpret_cast<char*>(words), sizeof(words));
  CHECK(in.gcount() == 8);
  CHECK(IsShowAchievementsWrapper(words));
}

void TestLauncherPageOrder() {
  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Before(launcher, "{\"Gameplay\"", "{\"Achievements\""));
  CHECK(Before(launcher, "{\"Achievements\"", "{\"Controls\""));
  CHECK(Before(launcher, "{\"Graphics\"", "{\"Gameplay\""));
  CHECK(Has(launcher, "achievements_.reset();"));
}

void TestLauncherPageIsReadOnly() {
  const std::string launcher = ReadSource("launcher.cpp");
  const size_t begin = launcher.find("void PageAchievements");
  const size_t end = launcher.find("static void PageControls");
  CHECK(begin != std::string::npos && end != std::string::npos && begin < end);
  const std::string page = launcher.substr(begin, end - begin);
  CHECK(!Has(page, "UnlockAchievement"));
  CHECK(!Has(page, "Button"));
  CHECK(Has(page, "Summarize("));
}

void TestOverlayClosesOnBackAndEscape() {
  const std::string modal = ReadSource("modal_dialog.cpp");
  CHECK(Has(modal, "VirtualKey::kEscape"));
  CHECK(Has(modal, "ImGuiKey_GamepadFaceRight"));
  CHECK(Has(modal, "close_.Request()"));
  CHECK(Has(modal, "close_.Step(pressed_.any()"));
  const std::string overlay = ReadSource("achievements_overlay.cpp");
  CHECK(Has(overlay, "X##close_achievements"));
  CHECK(Has(overlay, "B  Back"));
}

void TestAppWiresF7AndControllerGate() {
  const std::string app = ReadSource("ssf2t_hd_remix_app.h");
  CHECK(Has(app, "CreateAchievementsOverlay() override"));
  CHECK(Has(app, "ToggleOverlay("));
  CHECK(Has(app, "SetActiveCallback"));
  CHECK(Has(app, "ModalDialog::AnyOpen()"));
  CHECK(Has(app, "SetAchievementsOpener"));
  CHECK(Has(app, "ApplyAchievementsStyle"));
  CHECK(Has(app, "delete achievements_;"));
}

void TestWindowCloseHardExits() {
  const std::string app = ReadSource("ssf2t_hd_remix_app.h");
  const size_t start = app.find("void OnClosing(");
  CHECK(start != std::string::npos);
  const std::string body = app.substr(start, app.find("\n  }\n", start) - start);
  CHECK(Has(body, "PrepareHardExit();"));
  CHECK(Has(body, "std::_Exit(0);"));
  CHECK(!Has(body, "TerminateTitle"));
  CHECK(!Has(body, "OnShutdown"));
  CHECK(!Has(body, "Shutdown("));
  CHECK(Has(app, "void OnShutdown() override"));
}

// SDK notifications stay the SDK's, and nothing here unlocks, signs in or opens the network.
void TestAchievementCodeStaysOffline() {
  const char* files[] = {"achievements_overlay.cpp", "achievements_overlay.h", "achievement_list.h",
                         "launcher_achievements.cpp", "achievement_ui_hook.cpp",  "achievement_ui.h"};
  for (const char* name : files) {
    const std::string text = ReadSource(name);
    CHECK(!text.empty());
    CHECK(!Has(text, "UnlockAchievement"));
    CHECK(!Has(text, "NetDll"));
    CHECK(!Has(text, "XamUserGetSigninState"));
    CHECK(!Has(text, "AchievementNotificationDialog"));
  }
}

// F7 belongs to ReXGlue's achievements bind; no other host key of the port may take it.
void TestF7IsNotTakenElsewhere() {
  const char* files[] = {"frame_counter.cpp", "window_host.cpp", "launcher.cpp", "modal_dialog.cpp"};
  for (const char* name : files) {
    const std::string text = ReadSource(name);
    CHECK(!Has(text, "kF7"));
    CHECK(!Has(text, "ImGuiKey_F7"));
    CHECK(!Has(text, "\"F7\""));
  }
}

}  // namespace

int main() {
  struct Test {
    const char* name;
    void (*run)();
  };
  const Test tests[] = {
      {"window extent", TestFitWindowExtent},
      {"clean store is locked", TestCleanStoreIsLocked},
      {"list is in game order", TestListIsInGameOrder},
      {"unlocked calculation", TestUnlockedCalculation},
      {"unknown unlock ids", TestUnknownUnlockIdsAreIgnored},
      {"empty store", TestEmptyStore},
      {"entry description", TestEntryDescription},
      {"scroll direction", TestScrollDirection},
      {"close gate holds until release", TestCloseGateHoldsUntilRelease},
      {"close gate gives up", TestCloseGateGivesUpOnAStuckButton},
      {"close gate released at once", TestCloseGateReleasedAtOnce},
      {"input released", TestInputReleased},
      {"game input neutral while modal open", TestGameInputIsNeutralWhileModalOpen},
      {"F7 toggle", TestToggleOverlay},
      {"hook target check", TestHookTargetCheck},
      {"hook target in real image", TestHookTargetInRealImage},
      {"launcher page order", TestLauncherPageOrder},
      {"launcher page is read-only", TestLauncherPageIsReadOnly},
      {"overlay closes on B and Escape", TestOverlayClosesOnBackAndEscape},
      {"app wires F7 and controller gate", TestAppWiresF7AndControllerGate},
      {"window close hard-exits", TestWindowCloseHardExits},
      {"achievement code stays offline", TestAchievementCodeStaysOffline},
      {"F7 not taken elsewhere", TestF7IsNotTakenElsewhere},
  };
  for (const Test& test : tests) {
    std::printf("%s\n", test.name);
    test.run();
  }
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
