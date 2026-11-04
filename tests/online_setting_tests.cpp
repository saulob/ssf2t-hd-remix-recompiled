// Online settings: Online play defaults to Off, the online name persists, both reach the runtime cvars and the
// launcher, Infinite Time is locked out while Online play is on, the Ping display toggle, the developer-only
// Netplay Test controls, and PLAY waiting for a finished Connection test. Headless: no window, no game runtime.
// Run: ssf2t_online_setting_tests

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "connection_test_gate.h"
#include "corner_overlay.h"
#include "online_page.h"
#include "online_setting.h"
#include "ping_display.h"

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

bool Has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

void TestOnlineDefaultsToOff() {
  CHECK(!kDefaultOnlineEnabled);
  CHECK(!kDefaultOnlinePing);
  CHECK(std::string_view(kOnlineEnabledSetting) == "ssf2t_online_enabled");
  CHECK(std::string_view(kOnlineNameSetting) == "ssf2t_online_name");
  CHECK(std::string_view(kOnlinePingSetting) == "ssf2t_online_ping_display");
  CHECK(std::string_view(kDefaultOnlineNameValue) == "Player");
  const std::string runtime = ReadSource("online_runtime.cpp");
  CHECK(Has(runtime, "REXCVAR_DEFINE_BOOL(ssf2t_online_enabled, ssf2t::kDefaultOnlineEnabled, \"SSF2T\""));
  CHECK(Has(runtime, "REXCVAR_DEFINE_STRING(ssf2t_online_name, ssf2t::kDefaultOnlineNameValue, \"SSF2T\""));
  CHECK(Has(runtime, "REXCVAR_DEFINE_BOOL(ssf2t_online_ping_display, ssf2t::kDefaultOnlinePing, \"SSF2T\""));
}

void TestOnlineNamePersists() {
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ssf2t_online_setting_tests";
  std::filesystem::create_directories(dir);
  const std::string path = (dir / "settings.toml").string();
  std::ofstream(path) << "# player settings\n";

  CHECK(!trg::TextFileSettings(path).GetBool(kOnlineEnabledSetting, kDefaultOnlineEnabled));
  CHECK(trg::TextFileSettings(path).Get(kOnlineNameSetting, kDefaultOnlineNameValue) == "Player");

  trg::TextFileSettings writer(path);
  writer.Set(kOnlineEnabledSetting, "true");
  writer.Set(kOnlineNameSetting, "Saulo");
  writer.Set(kOnlinePingSetting, "true");
  CHECK(writer.Save());
  CHECK(trg::TextFileSettings(path).GetBool(kOnlineEnabledSetting, false));
  CHECK(trg::TextFileSettings(path).GetBool(kOnlinePingSetting, false));
  CHECK(trg::TextFileSettings(path).Get(kOnlineNameSetting, "") == "Saulo");
  const std::string saved = ReadFile(path);
  CHECK(Has(saved, "ssf2t_online_name = \"Saulo\"") || Has(saved, "ssf2t_online_name = Saulo"));
  std::filesystem::remove_all(dir);
}

void TestSettingsReachTheRuntime() {
  const std::string settings = ReadSource("settings.cpp");
  CHECK(Has(settings, "      kOnlineEnabledSetting,\n"));
  CHECK(Has(settings, "      kOnlineNameSetting,\n"));
  CHECK(Has(settings, "      kOnlinePingSetting,\n"));
}

void TestInfiniteTimeIsLockedOutWhileOnline() {
  CHECK(InfiniteTimeActive(true, false));
  CHECK(!InfiniteTimeActive(true, true));
  CHECK(!InfiniteTimeActive(false, false));
  CHECK(InfiniteTimeToggleAllowed(false));
  CHECK(!InfiniteTimeToggleAllowed(true));
  CHECK(std::string_view(kInfiniteTimeOnlineNotice) == "Infinite Time is disabled while Online play is enabled.");

  const std::string settings = ReadSource("settings.cpp");
  CHECK(Has(settings, "InfiniteTimeActive(REXCVAR_GET(ssf2t_infinite_time), REXCVAR_GET(ssf2t_online_enabled))"));
  CHECK(Has(settings, "if (!InfiniteTimeToggleAllowed(REXCVAR_GET(ssf2t_online_enabled)))"));
  const std::string app = ReadSource("ssf2t_hd_remix_app.h");
  CHECK(Has(app, "ShowInfiniteTimeMessage(kInfiniteTimeOnlineNotice"));
  CHECK(Has(app, "InfiniteTimeActive(REXCVAR_GET(ssf2t_infinite_time), REXCVAR_GET(ssf2t_online_enabled))"));

  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "ui.Info(kInfiniteTimeLabel, kInfiniteTimeOnlineNotice)"));
  const size_t gate = launcher.find("GetBool(kOnlineEnabledSetting, kDefaultOnlineEnabled)) {\n      ui.Info(kInfiniteTimeLabel");
  const size_t toggle = launcher.find("ui.Toggle(kInfiniteTimeLabel");
  CHECK(gate != std::string::npos && toggle != std::string::npos && gate < toggle);
  const std::string page = ReadSource("online_page.cpp");
  CHECK(Has(page, "settings.Set(kInfiniteTimeSetting, \"false\")"));
}

void TestOnlyInfiniteTimeIsLockedAndVisualSettingsStayAvailable() {
  const std::string launcher = ReadSource("launcher.cpp");
  size_t uses = 0;
  for (size_t at = launcher.find("kOnlineEnabledSetting"); at != std::string::npos;
       at = launcher.find("kOnlineEnabledSetting", at + 1)) {
    ++uses;
  }
  CHECK(uses == 1);
  const std::string settings = ReadSource("settings.cpp");
  size_t gates = 0;
  for (size_t at = settings.find("ssf2t_online_enabled"); at != std::string::npos;
       at = settings.find("ssf2t_online_enabled", at + 1)) {
    ++gates;
  }
  CHECK(gates == 2);
}

void TestPingDisplayRules() {
  CHECK(PingText(32) == "Ping: 32 ms");
  CHECK(PingText(std::nullopt) == "Ping: --");
  CHECK(!PingVisible(false, true));
  CHECK(!PingVisible(true, false));
  CHECK(PingVisible(true, true));
  // Without a connected peer the readout stays up and says "--".
  CHECK(PingText(MeasuredRtt(online::NetplayLink::kNone, 40)) == "Ping: --");
  CHECK(PingText(MeasuredRtt(online::NetplayLink::kConnecting, 40)) == "Ping: --");
  CHECK(PingText(MeasuredRtt(online::NetplayLink::kConnected, std::nullopt)) == "Ping: --");
  CHECK(PingText(MeasuredRtt(online::NetplayLink::kConnected, 42)) == "Ping: 42 ms");
  CHECK(PingText(MeasuredRtt(online::NetplayLink::kFighting, 42)) == "Ping: 42 ms");
  CHECK(std::string_view(kPingDisplayKey) == "F3");
  const std::string ping = ReadSource("ping_display.cpp");
  CHECK(Has(ping, "kPingDisplayKey") && !Has(ping, "\"F9\""));
  CHECK(Has(ping, "Online().netplay_info()"));
  CHECK(Has(ping, "DrawCornerText(PingText(MeasuredRtt(link, rtt)), PingSlot(counter));"));
  CHECK(Has(ReadSource("frame_counter.cpp"), "DrawCornerText(FrameCounterText(stats), FrameCounterSlot());"));
  CHECK(!Has(ping, "AddRect"));
}

void TestCornerReadoutsStackWithoutOverlap() {
  CHECK(FrameCounterSlot() == 0 && PingSlot(true) == 1 && PingSlot(false) == 0);
  for (const float scale : {1.0f, 1.5f, 2.25f}) {
    const float font = 18.0f * scale;
    const float width = 1920.0f * scale;
    const CornerPanel fps = CornerPanelAt(width, font, 9.0f * font, font, FrameCounterSlot());
    const CornerPanel ping = CornerPanelAt(width, font, 6.0f * font, font, PingSlot(true));
    const CornerPanel alone = CornerPanelAt(width, font, 6.0f * font, font, PingSlot(false));
    CHECK(fps.x1 == ping.x1 && fps.x1 == width - font);
    CHECK(ping.y0 > fps.y1 && ping.y0 - fps.y1 == font * 0.25f);
    CHECK(ping.y1 - ping.y0 == fps.y1 - fps.y0);
    CHECK(alone.y0 == fps.y0 && alone.x1 == fps.x1);
    CHECK(fps.x0 > 0 && fps.y0 == font);
    CHECK(std::abs(ping.text_x - ping.x0 - font * 0.6f) < 0.01f && std::abs(ping.text_y - ping.y0 - font * 0.3f) < 0.01f);
  }
}

void TestNoDebugOverlayText() {
  CHECK(Has(ReadSource("launcher.cpp"), "\"Shows the game's frame rate in the corner. F2 switches it while playing.\""));
  for (const char* source : {"launcher.cpp", "online_page.cpp", "online_setting.h", "ping_display.h"}) {
    const std::string text = ReadSource(source);
    CHECK(!Has(text, "F9") && !Has(text, "F4 debug") && !Has(text, "F4 settings") && !Has(text, "debug overlay"));
    CHECK(!Has(text, "settings overlay"));
  }
  for (const char* doc : {"README.md", "docs/online.md", "docs/save-and-achievements.md"}) {
    const std::string text = ReadFile(std::filesystem::path(SSF2T_SOURCE_DIR) / doc);
    CHECK(!Has(text, "**F9**") && !Has(text, "F9 ") && !Has(text, "debug overlay") && !Has(text, "F4 settings") &&
          !Has(text, "F3 debug"));
  }
  CHECK(Has(ReadFile(std::filesystem::path(SSF2T_SOURCE_DIR) / "docs/online.md"), "**F3** shows or hides it"));
}

void TestOnlinePageSections() {
  CHECK(OnlinePageSections(false, 3) == std::vector<OnlineSection>{OnlineSection::kOnlinePlay});
  const std::vector<OnlineSection> on = OnlinePageSections(true, 0);
  CHECK((on == std::vector<OnlineSection>{OnlineSection::kOnlinePlay, OnlineSection::kOnlineName,
                                          OnlineSection::kFriendCode, OnlineSection::kConnectionTest,
                                          OnlineSection::kPingDisplay, OnlineSection::kSecretCharacters,
                                          OnlineSection::kFriends}));
  const std::vector<OnlineSection> with_requests = OnlinePageSections(true, 2);
  CHECK(with_requests.size() == 8 && with_requests[1] == OnlineSection::kFriendRequests);
}

void TestLauncherHasAnOnlinePageWithoutTechnicalControls() {
  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "{\"Online\", "));
  CHECK(Has(launcher, "online_page_.Draw(ui)"));
  CHECK(Has(launcher, "online_page_.Tick(settings_)"));
  CHECK(Has(launcher, "online_page_.NeedsAttention()"));
  const std::string page = ReadSource("online_page.cpp");
  for (const char* shown : {"Add a friend", "Send request", "Accept", "Decline", "Run test", "Your friend code", "Copy",
                            "Remove"}) {
    CHECK(Has(page, shown));
  }
  for (const char* hidden : {"7001", "7002", "wss://", "workers.dev", "token", "Lobby", "lobby", "Host", "Join", "IP address",
                             "Challenge", "challenge", "Disconnect", "wants to play", "Waiting for"}) {
    CHECK(!Has(page, hidden));
  }
  const std::string view = ReadSource("online/friend_view.cpp");
  CHECK(!Has(view, "hallenge") && !Has(view, "Press Play"));
}

void TestFriendCodeCopyAndPasteUseTheNativeClipboard() {
  const std::string page = ReadSource("online_page.cpp");
  const std::string header = ReadSource("online_page.h");
  const std::string field = ReadSource("friend_code_field.cpp");
  const std::string win32 = ReadSource("clipboard_win32.cpp");
  CHECK(Has(header, "char add_code_[kFriendCodeFieldSize] = {};"));
  CHECK(Has(header, "return ReadClipboardText(owner_window_, log);"));
  CHECK(Has(page, "add_field_.Draw(add_code_, sizeof(add_code_)"));
  CHECK(Has(page, "return WriteClipboardText(owner_window_, text, log);"));
  CHECK(Has(page, "CopyFriendCode(state.friend_code, write"));
  CHECK(Has(ReadSource("launcher.cpp"), "online_page_.SetOwnerWindow(WindowHandle());"));
  CHECK(!Has(page, "ImGui::SetClipboardText") && !Has(page, "SDL_"));
  CHECK(!Has(field, "Clipboard(") && !Has(field, "SDL_") && !Has(field, "windows.h"));
  CHECK(Has(field, "ImGui::InputTextWithHint(\"##add_code\", \"XXXX-XXXX\""));
  CHECK(Has(field, "ImGui::IsKeyDown(ImGuiKey_LeftCtrl)") && Has(field, "ImGui::IsKeyPressed(ImGuiKey_V, false)"));
  for (const char* call : {"::OpenClipboard(owner)", "::CloseClipboard()", "::EmptyClipboard()",
                           "::IsClipboardFormatAvailable(format)", "::GetClipboardData(format)",
                           "::SetClipboardData(format, memory)", "::GlobalAlloc(flags, bytes)", "::GlobalLock(memory)",
                           "::GlobalUnlock(memory)", "::GetLastError()", "CF_UNICODETEXT", "MB_ERR_INVALID_CHARS",
                           "WC_ERR_INVALID_CHARS"}) {
    CHECK(Has(win32, call));
  }
  CHECK(!Has(win32, "SDL_"));
  const std::string cmake = ReadFile(std::filesystem::path(SSF2T_SOURCE_DIR) / "CMakeLists.txt");
  CHECK(Has(cmake, "list(APPEND SSF2T_SOURCES src/clipboard_win32.cpp)"));
  for (const char* gone : {"imgui_clipboard.cpp", "sdl_clipboard.cpp"}) {
    CHECK(!std::filesystem::exists(std::filesystem::path(SSF2T_SOURCE_DIR) / "src" / gone));
    CHECK(!Has(cmake, gone));
  }
  CHECK(Has(page, "online::NormalizeFriendCode(add_code_)"));
  CHECK(Has(page, "ImGui::BeginDisabled(code.empty());"));
  CHECK(Has(page, "Online().AddFriend(code) == online::FriendRequestResult::kSent"));
  const size_t field_at = page.find("add_field_.Draw(");
  const size_t normalized = page.find("online::NormalizeFriendCode(add_code_)");
  const size_t button = page.find("ImGui::Button(\"Send request\")");
  CHECK(field_at != std::string::npos && field_at < normalized && normalized < button);
}

void TestConnectionTestPageShowsTheReport() {
  const std::string page = ReadSource("online_page.cpp");
  CHECK(Has(page, "ui.Heading(state.test->verdict.c_str());"));
  CHECK(Has(page, "online::ConnectionReport::Line& line : state.test->lines"));
  CHECK(Has(page, "Checks that you can reach the online service and that other players can reach you."));
  const std::string test = ReadSource("online/connection_test.cpp");
  for (const char* label : {"\"Online service\"", "\"Direct connections\"", "\"Router configuration\"", "Ready for online play",
                            "Limited: direct connections may be blocked by your network"}) {
    CHECK(Has(test, label));
  }
  CHECK(!Has(test, "localhost") && !Has(test, "127.0.0.1"));
  // STUN, UPnP and router details go to the log, not the page.
  CHECK(Has(ReadSource("online/online_service.cpp"),
            "for (const std::string& detail : finished->details) Log(\"online connection test \" + detail);"));
  for (const char* logged : {"\"STUN \" + host", "upnp + status.detail", "double NAT or "}) CHECK(Has(test, logged));
  CHECK(Has(ReadSource("online/stun.h"), "stun.cloudflare.com:3478,stun.l.google.com:19302"));
  CHECK(Has(ReadSource("online/online_service.h"), "wss://ssf2t-lobby.saulobenigno.workers.dev/"));
}

void TestLocalAndOnlineNamesAreSeparate() {
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ssf2t_online_name_tests";
  std::filesystem::create_directories(dir);
  const std::string path = (dir / "settings.toml").string();
  std::ofstream(path) << "# player settings\n";
  trg::TextFileSettings file(path);
  file.Set(kOnlineNameSetting, "Saulo");
  file.Set("ssf2t_player_2_name", "teste2");
  file.Set("ssf2t_player_1_name", "saulo1");
  CHECK(file.Get(kOnlineNameSetting, kDefaultOnlineNameValue) == "Saulo");
  file.Set(kOnlineNameSetting, "Saulo-2");
  CHECK(file.Get("ssf2t_player_1_name", "Player 1") == "saulo1");
  CHECK(file.Get("ssf2t_player_2_name", "Player 2") == "teste2");
  CHECK(file.Save());
  trg::TextFileSettings reread(path);
  CHECK(reread.Get(kOnlineNameSetting, "") == "Saulo-2" && reread.Get("ssf2t_player_1_name", "") == "saulo1" &&
        reread.Get("ssf2t_player_2_name", "") == "teste2");
  std::filesystem::remove_all(dir);

  // The online identity reads only the Online name; the local names only feed the SDK's local players.
  const std::string page = ReadSource("online_page.cpp");
  CHECK(Has(page, "edited.name = settings.Get(kOnlineNameSetting, kDefaultOnlineNameValue);"));
  for (const char* source : {"online_page.cpp", "online_runtime.cpp", "online/online_service.cpp",
                             "player_match_hook.cpp", "player_match_net_hook.cpp"}) {
    const std::string text = ReadSource(source);
    CHECK(!Has(text, "ssf2t_player_1_name") && !Has(text, "ssf2t_player_2_name") && !Has(text, "kPlayerNames"));
    CHECK(!Has(text, "local_player_1_name") && !Has(text, "local_player_2_name"));
  }
  CHECK(Has(ReadSource("player_match_hook.cpp"), "live.name = profile.name;"));
  CHECK(Has(ReadSource("player_match_net_hook.cpp"), "? ssf2t::Online().Profile()"));
  const std::string settings = ReadSource("settings.cpp");
  const size_t apply = settings.find("void ApplyPlayerNames() {");
  const std::string apply_names = settings.substr(apply, settings.find("\n}\n", apply) - apply);
  CHECK(!Has(apply_names, "online") && !Has(apply_names, "Online"));
}

void TestOnlineNameFieldMatchesItsHelp() {
  CHECK(std::string_view(kOnlineNameLabel) == "Online name");
  CHECK(std::string_view(kOnlineNameHelp) == "What other players see: up to 16 letters, digits, - or _.");
  CHECK(online::kMaxOnlineNameChars == 16);
  for (int c = 0; c < 256; ++c) {
    const bool listed = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    CHECK(online::OnlineNameChar(char(c)) == listed);
  }
  const std::string page = ReadSource("online_page.cpp");
  CHECK(Has(page, "case OnlineSection::kOnlineName: OnlineNameRow(ui); break;"));
  CHECK(Has(page, "char text[online::kMaxOnlineNameChars + 1] = {};"));
  CHECK(Has(page, "ImGuiInputTextFlags_CallbackCharFilter, OnlineNameFilter"));
  CHECK(Has(page, "online::OnlineNameChar(char(data->EventChar))"));
  CHECK(Has(page, "Tr(\"Other players see \") + shown"));
  CHECK(Has(ReadSource("online/identity.cpp"), "if (OnlineNameChar(c)) out += c;"));
}

void TestOnlinePageWording() {
  CHECK(std::string_view(kOnlineEnabledLabel) == "Online play");
  CHECK(std::string_view(kOnlineEnabledHelp) ==
        "Lets the game's own Online Match menu create and join matches. Players meet through the online service; the "
        "fight itself runs directly between your PCs.");
  CHECK(std::string_view(kOnlinePingLabel) == "Ping display");
  CHECK(std::string_view(kOnlinePingHelp) ==
        "Shows the round trip to the other player in game. F3 switches it while playing.");
  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "\"Add friends and see who is online. Play them from Multiplayer > Online Match.\""));
  const std::string page = ReadSource("online_page.cpp");
  for (const char* shown : {"\"Share this code so another player can add you.\"", "\"Friend code copied.\"",
                            "\"Testing...\" : \"Run test\"", "ui.Heading(\"Friends\");",
                            "\"Their friend code, for example ABCD-EFGH.\"", "\"XXXX-XXXX\"",
                            "\"New friend request\"", "\"     Accept\"", "\"Decline\"",
                            "\"Your request is waiting for an answer.\"", "\"Cancel request\"", "\"Remove\"",
                            "\"No friends yet. Add someone with their friend code.\""}) {
    CHECK(Has(page + ReadSource("friend_code_field.cpp"), shown));
  }
  CHECK(!Has(page, "Join Friends Room") && !Has(page, "room"));
  for (const char* source : {"launcher.cpp", "online_page.cpp", "online_setting.h", "player_name_setting.h",
                             "online/friend_view.cpp", "online/connection_test.cpp"}) {
    const std::string text = ReadSource(source);
    CHECK(!Has(text, "Player Match"));
    CHECK(!Has(text, "Xbox LIVE") && !Has(text, "Challenge"));
  }
  CHECK(Has(ReadSource("online/protocol.cpp"), "case FriendState::kPlayerMatch: line = Tr(\"Online Match\"); break;"));
}

void TestNetplayTestControlsAreDeveloperOnly() {
  const std::string launcher = ReadSource("launcher.cpp");
  const size_t gate = launcher.find("if (DeveloperControlsEnabled()) {");
  const size_t role = launcher.find("ui.Choice(kNetplayRoleLabel");
  const size_t peer = launcher.find("ui.InputText(kNetplayPeerAddressLabel");
  const size_t names = launcher.find("ui.Section(kPlayerNamesTitle");
  CHECK(gate != std::string::npos && gate < role && role < peer && peer < names);
  CHECK(std::string_view(kDeveloperControlsEnv) == "SSF2T_DEVELOPER");
  CHECK(std::string_view(kOnlineServiceEnv) == "SSF2T_LOBBY");
}

void TestPlayerMatchAssignmentDrivesTheSessionNotTheDeveloperSetting() {
  const std::string hook = ReadSource("netplay_hook.cpp");
  CHECK(Has(hook, "(!online_driven_ || base[G::kGameMode] == kOnlineGameMode)"));
  CHECK(Has(hook, "player_match rollback takeover guest={}"));
  const size_t online = hook.find("ssf2t::Online().Assignment()");
  const size_t legacy = hook.find("REXCVAR_GET(ssf2t_netplay_role)", online);
  CHECK(online != std::string::npos && legacy != std::string::npos && online < legacy);
  CHECK(Has(hook, "PumpOnlineService();"));
  CHECK(Has(hook, "Online().SetNetplayLink("));
  CHECK(Has(hook, "session_->rtt_ms()"));
}

void TestGameRuntimeReconnectsWithoutTheLauncher() {
  const std::string app = ReadSource("ssf2t_hd_remix_app.h");
  const size_t skipped = app.find("if (!show_launcher_) {");
  const size_t configured = app.find("ConfigureOnline(defaults.user_data_root, guest_);", skipped);
  const size_t applied = app.find("ApplyMultisampling();", skipped);
  CHECK(skipped != std::string::npos && configured != std::string::npos && configured < applied);
  const std::string runtime = ReadSource("online_runtime.cpp");
  CHECK(Has(runtime, "online runtime reconnect begin"));
  CHECK(Has(runtime, "kLockAttempts"));
  CHECK(Has(ReadSource("window_host.cpp"), "ssf2t::PumpNetplayNetwork(base);"));
}

void TestPlayerMatchHooksCoverBothGuests() {
  const std::string hook = ReadSource("player_match_hook.cpp");
  for (const char* name : {"base013_sub_823C6DF8", "tu023_sub_823C73F0", "base013_sub_839C34D0", "tu023_sub_839C3C30",
                           "base013_sub_839C2B00", "tu023_sub_839C3260", "base013_sub_839C23E0", "tu023_sub_839C2B40",
                           "base013_sub_823CCA10", "tu023_sub_823CCFF0"}) {
    CHECK(Has(hook, (std::string("REX_HOOK_RAW(") + name + ")").c_str()));
  }
  const size_t tu = hook.find("#if SSF2T_TITLE_UPDATE_IMAGE");
  CHECK(tu != std::string::npos && hook.find("tu023_sub_", 0) > tu);
  CHECK(!Has(hook, "token"));
  const std::string cmake = ReadFile(std::filesystem::path(SSF2T_SOURCE_DIR) / "CMakeLists.txt");
  CHECK(Has(cmake, "src/player_match_hook.cpp"));
}

void TestOnlineOffIsTheOnlyGateForTheNetwork() {
  const std::string runtime = ReadSource("online_runtime.cpp");
  CHECK(Has(runtime, "settings.enabled = REXCVAR_GET(ssf2t_online_enabled);"));
  const std::string service = ReadSource("online/online_service.cpp");
  CHECK(Has(service, "if (!settings_.enabled) return;"));
}

// A launcher session's settings: the store the Online page edits, and what has reached the player file at once.
struct GateRig {
  trg::MemorySettings settings;
  std::map<std::string, std::string> file;
  SaveSettingNow save_now = [this](std::string_view key, const std::string& value) {
    file[std::string(key)] = value;
    return true;
  };

  GateRig() {
    settings.true_value = "true";
    settings.false_value = "false";
  }
  void Online(bool on) { settings.SetBool(kOnlineEnabledSetting, on); }
  bool PlayBlocked(bool testing = false) const { return ConnectionTestNeeded(settings, testing); }
};

void TestPlayWaitsForAConnectionTestOnlyWithOnlinePlayOn() {
  GateRig rig;
  CHECK(!rig.PlayBlocked());  // Online off, never tested
  CHECK(!rig.PlayBlocked(true));
  rig.Online(true);
  CHECK(rig.PlayBlocked());  // never tested
  CHECK(std::string_view(kConnectionTestSetting) == "ssf2t_online_connection_test");
  CHECK(OnlineUdpConfiguration() == "udp 7001,7002");
}

// The gate never looks at the report: the page records every finished test (Ready, Limited or Not ready) alike.
void TestAFinishedTestUnlocksPlay() {
  GateRig rig;
  rig.Online(true);
  CHECK(rig.PlayBlocked(true));  // running
  CHECK(RecordConnectionTest(rig.settings, rig.save_now));
  CHECK(!rig.PlayBlocked());
  CHECK(rig.file[kConnectionTestSetting] == OnlineUdpConfiguration());
  CHECK(rig.PlayBlocked(true));  // a new run blocks PLAY again until it finishes
  CHECK(!Has(ReadSource("connection_test_gate.h"), "verdict") && !Has(ReadSource("connection_test_gate.h"), "ready"));
}

void TestFinishedTestSurvivesARestart() {
  GateRig first;
  first.Online(true);
  RecordConnectionTest(first.settings, first.save_now);
  // The next launcher reads the player file: the test was written to it at once, without Save.
  GateRig next;
  next.Online(true);
  next.settings.Set(kConnectionTestSetting, first.file[kConnectionTestSetting]);
  CHECK(!next.PlayBlocked());
  ForgetConnectionTestWhenOff(next.settings);
  CHECK(!next.PlayBlocked());
}

void TestOnlineOffForgetsTheTest() {
  GateRig rig;
  rig.Online(true);
  RecordConnectionTest(rig.settings, rig.save_now);
  rig.Online(false);
  ForgetConnectionTestWhenOff(rig.settings);
  CHECK(rig.settings.Get(kConnectionTestSetting).empty());
  CHECK(!rig.PlayBlocked());
  rig.Online(true);
  CHECK(rig.PlayBlocked());
  // A test that finishes while Online play is off proves nothing for the next time it is on.
  rig.Online(false);
  CHECK(!RecordConnectionTest(rig.settings, rig.save_now));
  rig.Online(true);
  CHECK(rig.PlayBlocked());
}

void TestOnlyAnotherUdpConfigurationInvalidatesTheTest() {
  GateRig rig;
  rig.Online(true);
  RecordConnectionTest(rig.settings, rig.save_now);
  CHECK(ConnectionTestNeeded(rig.settings, false, "udp 7101,7102"));
  for (const auto& [key, value] : std::vector<std::pair<const char*, const char*>>{
           {kOnlineNameSetting, "Renamed"}, {kOnlinePingSetting, "true"}, {"ssf2t_show_fps", "true"},
           {"resolution_scale", "2"}, {"fullscreen", "true"}, {"mnk_mode", "true"},
           {"ssf2t_show_render_resolution", "true"}}) {
    rig.settings.Set(key, value);
    ForgetConnectionTestWhenOff(rig.settings);
    CHECK(!rig.PlayBlocked());
  }
  // Friends live on the online service and the game image (Base013/TU023) is not part of it either.
  const std::string gate = ReadSource("connection_test_gate.h");
  CHECK(!Has(gate, "guest") && !Has(gate, "Guest") && !Has(gate, "friend"));
}

ConnectionTestView ViewOf(const GateRig& rig, bool testing, bool windows = true) {
  const bool on = rig.settings.GetBool(kOnlineEnabledSetting, kDefaultOnlineEnabled);
  return DescribeConnectionTestView(on, ConnectionTestNeeded(rig.settings, testing), testing, windows);
}

void TestRequiredBlockIsHighlightedWithAPrimaryRunTest() {
  GateRig rig;
  rig.Online(true);
  const ConnectionTestView view = ViewOf(rig, false);
  CHECK(view.highlighted && view.primary_button && view.required_text);
  CHECK(std::string_view(kConnectionTestRequiredLabel) == "Connection test required");
  CHECK(rig.PlayBlocked());
}

void TestRunningTestKeepsPlayBlockedAndTheBorder() {
  GateRig rig;
  rig.Online(true);
  const ConnectionTestView view = ViewOf(rig, true);
  CHECK(rig.PlayBlocked(true) && view.highlighted);
  CHECK(!view.primary_button && !view.required_text);
  CHECK(view.windows_note && !ViewOf(rig, true, false).windows_note);
  CHECK(!ViewOf(rig, false).windows_note);
}

// Ready, Limited and Failed are all finished tests: the page records them alike, so the required look ends and Play opens.
void TestEveryFinishedTestEndsTheRequiredLook() {
  GateRig rig;
  rig.Online(true);
  RecordConnectionTest(rig.settings, rig.save_now);
  const ConnectionTestView view = ViewOf(rig, false);
  CHECK(!view.highlighted && !view.primary_button && !view.required_text);
  CHECK(!rig.PlayBlocked());
  const std::string gate = ReadSource("connection_test_gate.h");
  CHECK(!Has(gate, "verdict") && !Has(gate, "ready") && !Has(gate, "ConnectionReport"));
  CHECK(Has(ReadSource("online_page.cpp"), "if (!state.test || state.testing) return;"));  // the report stays visible
}

void TestOnlineOffHasNoRequiredLook() {
  GateRig rig;
  const ConnectionTestView view = ViewOf(rig, false);
  CHECK(!view.highlighted && !view.primary_button && !view.required_text && !view.windows_note);
  CHECK(!ViewOf(rig, true).highlighted);
  const ConnectionTestView off = DescribeConnectionTestView(false, true, true, true);
  CHECK(!off.highlighted && !off.windows_note);
}

void TestReloadKeepsTheCompletionAndOnlineOffRestoresTheRequirement() {
  GateRig first;
  first.Online(true);
  RecordConnectionTest(first.settings, first.save_now);
  GateRig reloaded;
  reloaded.Online(true);
  reloaded.settings.Set(kConnectionTestSetting, first.file[kConnectionTestSetting]);
  CHECK(!ViewOf(reloaded, false).highlighted && !reloaded.PlayBlocked());
  reloaded.Online(false);
  ForgetConnectionTestWhenOff(reloaded.settings);
  reloaded.Online(true);
  CHECK(ViewOf(reloaded, false).highlighted && reloaded.PlayBlocked());
  for (const auto& [key, value] : std::vector<std::pair<const char*, const char*>>{
           {kOnlineNameSetting, "Renamed"}, {kOnlinePingSetting, "true"}, {"ssf2t_show_fps", "true"}}) {
    GateRig rig;
    rig.Online(true);
    RecordConnectionTest(rig.settings, rig.save_now);
    rig.settings.Set(key, value);
    CHECK(!ViewOf(rig, false).highlighted);
  }
}

void TestPlayPressIsTheSecondAskInAFrame() {
  PlayAttemptDetector play;
  CHECK(!play.PressedThisCall(10));  // the footer drawing the Play button
  CHECK(play.PressedThisCall(10));   // the press, in the same frame
  CHECK(!play.PressedThisCall(11));
  CHECK(!play.PressedThisCall(12));
  CHECK(!play.PressedThisCall(13));
  CHECK(play.PressedThisCall(13));
}

void TestAttentionScrollsFocusesAndPulsesBriefly() {
  ConnectionTestAttention attention;
  const auto idle = attention.Frame(100.0, true);
  CHECK(!idle.scroll_into_view && !idle.focus_run_test && idle.strength == 0.0f && !attention.active());

  attention.Request();
  CHECK(attention.active());
  const auto first = attention.Frame(100.0, true);
  CHECK(first.scroll_into_view && first.focus_run_test && first.strength > 0.99f);

  // Focus is asked for once, scrolling only while the page may still be settling; ordinary frames restart nothing.
  for (double t = 100.016; t < 100.35; t += 0.016) {
    const auto step = attention.Frame(t, true);
    CHECK(!step.focus_run_test && step.scroll_into_view);
  }
  const auto later = attention.Frame(100.9, true);
  CHECK(!later.scroll_into_view && !later.focus_run_test && later.strength > 0.0f && attention.active());

  // It fades out within the 2-3 s window and stays out.
  float peak = 0.0f;
  for (double t = 101.0; t < 102.5; t += 0.016) peak = std::max(peak, attention.Frame(t, true).strength);
  CHECK(peak < 0.7f);
  CHECK(attention.Frame(102.6, true).strength == 0.0f && !attention.active());
  CHECK(attention.Frame(110.0, true).strength == 0.0f);

  // A second refused Play restarts it.
  attention.Request();
  const auto again = attention.Frame(120.0, true);
  CHECK(again.focus_run_test && again.scroll_into_view && again.strength > 0.99f);
  for (double t = 120.0; t < 122.5; t += 0.05) {
    const float strength = attention.Frame(t, true).strength;
    CHECK(strength >= 0.0f && strength <= 1.0f);
  }
}

void TestAttentionNeverOutlivesTheRequirement() {
  ConnectionTestAttention attention;
  attention.Request();
  CHECK(attention.Frame(5.0, false).strength == 0.0f && !attention.active());  // Online off, or already finished
  CHECK(!attention.Frame(5.1, true).focus_run_test);
  attention.Request();
  attention.Frame(6.0, true);
  CHECK(attention.Frame(6.5, false).strength == 0.0f && !attention.active());  // the test finished mid-pulse
  CHECK(attention.Frame(6.6, true).strength == 0.0f);
}

void TestLauncherWiresPlayRefusalToTheOnlinePage() {
  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "const bool pressed = play_attempt_.PressedThisCall(ImGui::GetFrameCount());"));
  CHECK(Has(launcher, "if (pressed) online_page_.RequestConnectionTestAttention();\n        return trg::PlayCheck{false, "
                      "kConnectionTestRequired, online_page};"));
  CHECK(Has(launcher, "ImGui::TextWrapped(\"%s\", Tr(kConnectionTestRequired));"));  // the Play page keeps its line
  const std::string page = ReadSource("online_page.cpp");
  CHECK(Has(page, "ui.ButtonRow(\"Connection test\", kTestHelp, state.testing ? \"Testing...\" : \"Run test\", "
                  "view.primary_button)"));
  CHECK(Has(page, "if (focus_run_test) ImGui::SetKeyboardFocusHere();"));
  CHECK(Has(page, "ImGuiChildFlags_NavFlattened"));
  CHECK(Has(page, "DrawConnectionTestBody(ui, state, view, step.focus_run_test);\n  ui.EndRows();\n  ImGui::EndChild();"));
  CHECK(Has(page, "ImGui::SetScrollY("));
  CHECK(Has(page, "t.accent.x") && !Has(page, "IM_COL32"));  // the theme's accent, no hardcoded colour
}

void TestWindowsFirewallNoteIsWindowsOnly() {
  const char* note = ConnectionTestRunningNote(true);
  CHECK(note && Has(note, "If Windows asks whether to allow network access, choose Allow access"));
  CHECK(ConnectionTestRunningNote(false) == nullptr);
#if defined(_WIN32)
  CHECK(kOnWindows);
#else
  CHECK(!kOnWindows);
#endif
  const std::string page = ReadSource("online_page.cpp");
  CHECK(Has(page, "if (view.windows_note) {\n    if (const char* note = ConnectionTestRunningNote(true)) ui.Help(note);"));
  CHECK(Has(page, "online_on, ConnectionTestNeeded(ui.settings(), state.testing), state.testing, kOnWindows);"));
  CHECK(!Has(ReadSource("launcher.cpp"), "Allow access"));
}

void TestLauncherGatesPlayOnTheConnectionTest() {
  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "if (online_page_.PlayNeedsTest(settings_)) {\n        if (pressed)"));
  CHECK(Has(launcher, "if (playable_ && online_page_.PlayNeedsTest(settings_)) {"));
  CHECK(Has(launcher, "return settings_.SaveNow({{std::string(key), value}});"));
  CHECK(!Has(launcher, "TestConnection"));  // PLAY never starts a test itself
  const std::string page = ReadSource("online_page.cpp");
  CHECK(Has(page, "ForgetConnectionTestWhenOff(settings);"));
  CHECK(Has(page, "const int finished = Online().snapshot().tests_finished;"));
  CHECK(Has(page, "if (RecordConnectionTest(settings, save_now_)) {"));
  CHECK(Has(page, "return ConnectionTestNeeded(settings, Online().snapshot().testing);"));
  const std::string settings = ReadSource("settings.cpp");
  CHECK(Has(settings, "      kConnectionTestSetting,\n"));
  CHECK(Has(settings, "file.Set(key, TomlValue(key, value));\n  }\n  if (!file.Save()) {\n    REXLOG_ERROR(\"Cannot write "
                      "player settings to {}\", Utf8(player_file_.filename()));\n    return false;\n  }\n  return true;"));
  CHECK(Has(ReadSource("online_runtime.cpp"), "REXCVAR_DEFINE_STRING(ssf2t_online_connection_test, \"\", \"SSF2T\","));
}

// The gate is launcher-only: the game's own socket, bound after PLAY, keeps its lifecycle.
void TestGameplaySocketLifecycleIsUntouched() {
  const std::string runtime = ReadSource("online_runtime.cpp");
  CHECK(Has(runtime, "if (settings.enabled) EnsureOnlineGameSocket();"));
  CHECK(Has(runtime, "g_game_socket.Open({kNetplayHostPort, kNetplayJoinPort})"));
  CHECK(!Has(runtime, "kConnectionTestSetting") && !Has(runtime, "ConnectionTestNeeded"));
  const std::string test = ReadSource("online/connection_test.cpp");
  CHECK(!Has(test, "OnlineGameSocket") && Has(test, "if (handle != net::kNoSocket) net::CloseSocket(handle);"));
}

void TestDefaultServiceAddressAndVersionSource() {
  const std::string service = ReadSource("online/online_service.h");
  CHECK(Has(service, "wss://ssf2t-lobby.saulobenigno.workers.dev/"));
  CHECK(!Has(service, "dcp-lobby"));
  const std::string runtime = ReadSource("online_runtime.cpp");
  CHECK(Has(runtime, "config.version = SSF2T_PORT_VERSION;"));
  CHECK(!Has(runtime, "0.2.2"));
}

}  // namespace

int main() {
  TestCornerReadoutsStackWithoutOverlap();
  TestNoDebugOverlayText();
  TestLocalAndOnlineNamesAreSeparate();
  TestOnlineNameFieldMatchesItsHelp();
  TestOnlinePageWording();
  TestOnlineDefaultsToOff();
  TestOnlineNamePersists();
  TestSettingsReachTheRuntime();
  TestInfiniteTimeIsLockedOutWhileOnline();
  TestOnlyInfiniteTimeIsLockedAndVisualSettingsStayAvailable();
  TestPingDisplayRules();
  TestOnlinePageSections();
  TestLauncherHasAnOnlinePageWithoutTechnicalControls();
  TestFriendCodeCopyAndPasteUseTheNativeClipboard();
  TestConnectionTestPageShowsTheReport();
  TestNetplayTestControlsAreDeveloperOnly();
  TestPlayerMatchAssignmentDrivesTheSessionNotTheDeveloperSetting();
  TestGameRuntimeReconnectsWithoutTheLauncher();
  TestPlayerMatchHooksCoverBothGuests();
  TestOnlineOffIsTheOnlyGateForTheNetwork();
  TestDefaultServiceAddressAndVersionSource();
  TestPlayWaitsForAConnectionTestOnlyWithOnlinePlayOn();
  TestAFinishedTestUnlocksPlay();
  TestFinishedTestSurvivesARestart();
  TestOnlineOffForgetsTheTest();
  TestOnlyAnotherUdpConfigurationInvalidatesTheTest();
  TestRequiredBlockIsHighlightedWithAPrimaryRunTest();
  TestRunningTestKeepsPlayBlockedAndTheBorder();
  TestEveryFinishedTestEndsTheRequiredLook();
  TestOnlineOffHasNoRequiredLook();
  TestReloadKeepsTheCompletionAndOnlineOffRestoresTheRequirement();
  TestPlayPressIsTheSecondAskInAFrame();
  TestAttentionScrollsFocusesAndPulsesBriefly();
  TestAttentionNeverOutlivesTheRequirement();
  TestLauncherWiresPlayRefusalToTheOnlinePage();
  TestWindowsFirewallNoteIsWindowsOnly();
  TestLauncherGatesPlayOnTheConnectionTest();
  TestGameplaySocketLifecycleIsUntouched();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
