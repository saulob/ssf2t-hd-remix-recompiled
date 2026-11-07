#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

int failures = 0;

void Check(bool condition, const char* what) {
  if (condition) return;
  std::printf("FAIL %s\n", what);
  ++failures;
}

std::string ReadText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const std::string& needle) { return text.find(needle) != std::string::npos; }

std::vector<fs::path> Sources(const fs::path& root) {
  std::vector<fs::path> files;
  for (const auto& entry : fs::recursive_directory_iterator(root / "src")) {
    const std::string ext = entry.path().extension().string();
    if (entry.is_regular_file() && (ext == ".cpp" || ext == ".h")) files.push_back(entry.path());
  }
  return files;
}

void TestRemovedModulesAreGone(const fs::path& root) {
  for (const char* file : {"src/character/evil_ryu_debug.h", "src/character/evil_ryu_debug.cpp",
                           "src/character/evil_ryu_debug_keys.h", "src/character/evil_ryu_debug_keys.cpp",
                           "src/character/ultra_arcade_debug.h", "src/character/ultra_arcade_debug.cpp",
                           "src/character/ultra_arcade_debug_hook.cpp", "src/character/ultra_arcade_debug_keys.h",
                           "src/character/ultra_arcade_debug_keys.cpp"}) {
    Check(!fs::exists(root / file), "the debug hotkey module is deleted");
  }
  const std::string cmake = ReadText(root / "CMakeLists.txt");
  Check(!Has(cmake, "evil_ryu_debug") && !Has(cmake, "ultra_arcade_debug") && !Has(cmake, "arcade_debug_tests"),
        "no build target names a removed debug module");
}

void TestNoDebugKeyDispatch(const fs::path& root) {
  const std::regex listener(R"(VirtualKey::kF(1|4|5|6|7|8)\b)");
  const std::vector<std::string> names = {"debug-hotkey",    "debug-key",   "DebugBridge",   "DebugMove",
                                          "DebugKey",        "ArcadeBridge", "FinalBossRequest",
                                          "AttachEvilRyuDebugKeys", "AttachArcadeDebugKeys"};
  for (const fs::path& path : Sources(root)) {
    const std::string text = ReadText(path);
    Check(!std::regex_search(text, listener), "F1 and F4-F8 have no key listener");
    for (const std::string& name : names) Check(!Has(text, name), "no debug hotkey command or log name remains");
  }
}

void TestBisonProductionUntouched(const fs::path& root) {
  const std::string hooks = ReadText(root / "src/character/ultra_hook_table.h");
  Check(!Has(hooks, "kArcade"), "the Arcade final-boss hooks and flags are gone from the hook table");
  for (const fs::path& path : Sources(root)) {
    const std::string text = ReadText(path);
    for (const char* address : {"sub_8223DD58", "sub_823804A0", "sub_82380C98", "sub_8223A5A0", "sub_8223DE80",
                                "sub_82380570", "sub_82380D68", "sub_8223A6C8"}) {
      Check(!Has(text, address), "no hook wraps the guest's Arcade ladder or ending");
    }
  }
}

void TestRealInputsStillWired(const fs::path& root) {
  const std::string hook = ReadText(root / "src/character/ultra_moves_hook.cpp");
  Check(Has(hook, "move.teleport.Update(") && Has(hook, "StartTeleport<H>(ctx, base, fighter, side, variant)"),
        "the teleport recogniser still starts the teleport");
  Check(Has(hook, "move.raging_demon.Update(") && Has(hook, "StartRagingDemon<H>(ctx, base, fighter, side)"),
        "the Shun Goku Satsu recogniser still starts the move");
  Check(Has(hook, "SenpuInput(") && Has(hook, "StartSenpuKyaku<H>(ctx, base, fighter, side)"),
        "the Senpu Kyaku input still starts the move");
  Check(Has(hook, "DetectMoves<H>(ctx, base, fighter, side)"), "Evil Ryu detection still runs every frame");
  Check(Has(hook, "DetectViolentMoves<H>(ctx, base, fighter, violent)"), "Violent Ken detection still runs every frame");
  Check(Has(hook, "move.rasetsu.Update(") && Has(hook, "StartRasetsu<H>(ctx, base, fighter, side, strength)"),
        "Violent Ken's Rasetsukyaku input still starts the move");
  Check(Has(hook, "move.shinbu.Update(") && Has(hook, "StartShinbu<H>(ctx, base, fighter, side)"),
        "Violent Ken's Shinbu Messatsu input still starts the move");
  Check(Has(hook, "HostMoveKind::kInazuma, kInazumaAction"), "Violent Ken's Inazuma Kick input still starts the move");
  Check(Has(hook, "REX_HOOK_RAW(base013_sub_82265228)") && Has(hook, "REX_HOOK_RAW(base013_sub_8228E440)"),
        "the Evil Ryu and Violent Ken special trackers stay hooked");
}

}  // namespace

int main() {
  const fs::path root = SSF2T_SOURCE_DIR;
  TestRemovedModulesAreGone(root);
  TestNoDebugKeyDispatch(root);
  TestBisonProductionUntouched(root);
  TestRealInputsStillWired(root);
  if (failures == 0) std::printf("debug hotkeys removed tests passed\n");
  return failures == 0 ? 0 : 1;
}
