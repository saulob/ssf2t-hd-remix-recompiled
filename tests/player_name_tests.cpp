// Player display names: the validator, the launcher table and the SDK patch that applies them. A model of
// the patched name lookup checks the logical-player mapping and that renaming leaves identity alone.
// Run: ssf2t_player_name_tests

#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include "local_player_name.h"
#include "player_name_setting.h"

using namespace ssf2t;
namespace names = local_player_name;

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

std::string ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, std::string_view needle) {
  return text.find(needle) != std::string::npos;
}

struct Identity {
  uint64_t xuid = 0xB13EBABEBABEBABE;
  std::string storage_key = "User";
  bool operator==(const Identity&) const = default;
};

struct Session {
  Identity identity;
  std::array<std::string, 4> sdk_names;
  uint32_t pad_user_offset = 0;

  void Configure(const std::array<std::string, kPlayerNames.size()>& raw) {
    for (size_t i = 0; i < raw.size(); ++i) {
      sdk_names[i] = names::SdkValue(raw[i], kPlayerNames[i].default_name);
    }
  }

  uint32_t Player(uint32_t user) const { return user + 1 - pad_user_offset; }

  std::string DisplayName(uint32_t user) const {
    const uint32_t player = Player(user);
    const std::string stock = user ? "Player " + std::to_string(player) : identity.storage_key;
    if (player < 1 || player > 4 || sdk_names[player - 1].empty()) {
      return stock;
    }
    return sdk_names[player - 1];
  }
};

void TestDefaults() {
  std::printf("defaults are Player 1 and Player 2 and keep stock behaviour\n");
  Session pad_first;
  pad_first.Configure({"Player 1", "Player 2"});
  CHECK(pad_first.DisplayName(0) == "User");
  for (uint32_t user = 1; user < 4; ++user) {
    CHECK(pad_first.DisplayName(user) == "Player " + std::to_string(user + 1));
  }
  CHECK(kPlayerNames.size() == 2);
  for (size_t i = 0; i < kPlayerNames.size(); ++i) {
    CHECK(std::string(kPlayerNames[i].default_name) == "Player " + std::to_string(i + 1));
    CHECK(std::string(kPlayerNames[i].key) == "ssf2t_player_" + std::to_string(i + 1) + "_name");
    CHECK(std::string(kPlayerNames[i].sdk_key) == "local_player_" + std::to_string(i + 1) + "_name");
  }
}

void TestLogicalMapping() {
  std::printf("names follow logical players, not raw users\n");
  Session keyboard_owns_user_0;
  keyboard_owns_user_0.pad_user_offset = 1;
  keyboard_owns_user_0.Configure({"Saulo", "Guest"});
  CHECK(keyboard_owns_user_0.DisplayName(0) == "User");
  CHECK(keyboard_owns_user_0.DisplayName(1) == "Saulo");
  CHECK(keyboard_owns_user_0.DisplayName(2) == "Guest");
  CHECK(keyboard_owns_user_0.DisplayName(3) == "Player 3");

  Session pad_first;
  pad_first.Configure({"Saulo", "Guest"});
  CHECK(pad_first.DisplayName(0) == "Saulo");
  CHECK(pad_first.DisplayName(1) == "Guest");
  CHECK(pad_first.DisplayName(2) == "Player 3");
  CHECK(pad_first.DisplayName(3) == "Player 4");
}

void TestRenameKeepsIdentity() {
  std::printf("renaming changes only the displayed text\n");
  Session session;
  session.pad_user_offset = 1;
  const Identity before = session.identity;
  session.Configure({"Player 1", "Player 2"});
  CHECK(session.DisplayName(1) == "Player 1");
  session.Configure({"Saulo", "Guest"});
  CHECK(session.DisplayName(1) == "Saulo");
  CHECK(session.DisplayName(2) == "Guest");
  CHECK(session.identity == before);
  CHECK(session.identity.storage_key == "User");
  session.Configure({"Player 1", "Player 2"});
  CHECK(session.DisplayName(1) == "Player 1");
  CHECK(session.identity == before);
}

void TestValidation() {
  std::printf("names are trimmed, bounded and checked as UTF-8\n");
  CHECK(names::Check("  Saulo  ").name == "Saulo");
  CHECK(names::Check("").result == names::Result::Empty);
  CHECK(names::Check("    ").result == names::Result::Empty);
  CHECK(names::Check(std::string(15, 'a')).result == names::Result::Ok);
  CHECK(names::Check(std::string(16, 'a')).result == names::Result::TooManyCharacters);
  CHECK(names::Check(" " + std::string(15, 'a') + " ").result == names::Result::Ok);
  CHECK(std::string(names::Message(names::Result::TooManyCharacters)) == "Maximum 15 characters.");
  CHECK(std::string(names::Message(names::Result::TooManyBytes)) == "Name is too long for the game (15-byte limit).");
  CHECK(std::string(kPlayerNameHint) == "Up to 15 characters.");
  CHECK(names::Check(std::string(8, 'a') + "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9").result == names::Result::TooManyBytes);
  CHECK(names::Check(std::string(8, 'a') + "\xC3\xA9\xC3\xA9\xC3\xA9").result == names::Result::Ok);
  CHECK(names::Check(std::string(16, 'a') + "\xFF").result == names::Result::BadCharacter);
  CHECK(names::Check(std::string(16, 'a') + "\t").result == names::Result::BadCharacter);
  CHECK(names::Check("a\tb").result == names::Result::BadCharacter);
  CHECK(names::Check("a\nb").result == names::Result::BadCharacter);
  CHECK(names::Check(std::string("a\0b", 3)).result == names::Result::BadCharacter);
  CHECK(names::Check("a\x7f").result == names::Result::BadCharacter);
  CHECK(names::Check("Jos\xC3\xA9").name == "Jos\xC3\xA9");
  CHECK(names::Check("Jos\xC3").result == names::Result::BadCharacter);
  CHECK(names::Check("\xC0\xAF").result == names::Result::BadCharacter);
  CHECK(names::Check(std::string(14, 'a') + "\xC3\xA9").result == names::Result::TooManyBytes);
  CHECK(names::Check(std::string(13, 'a') + "\xC3\xA9").result == names::Result::Ok);
}

void TestFallback() {
  std::printf("an invalid or default name selects the stock name\n");
  CHECK(names::SdkValue("Saulo", "Player 1") == "Saulo");
  CHECK(names::SdkValue("Player 1", "Player 1").empty());
  CHECK(names::SdkValue("", "Player 1").empty());
  CHECK(names::SdkValue(std::string(40, 'x'), "Player 1").empty());
  CHECK(names::SdkValue("bad\x01", "Player 1").empty());
  Session session;
  session.Configure({std::string(40, 'x'), "bad\x01"});
  CHECK(session.DisplayName(0) == "User");
  CHECK(session.DisplayName(1) == "Player 2");
  CHECK(session.DisplayName(2) == "Player 3");
}

void TestEditing() {
  std::printf("the field only ever holds an allowed value\n");
  using names::AcceptEdit;
  names::Result blocked = names::Result::Ok;
  const std::string e = "\xC3\xA9";
  const std::string fourteen(14, 'a');
  const std::string fifteen(15, 'a');
  CHECK(AcceptEdit(fourteen, fourteen + "b", &blocked) == fourteen + "b" && blocked == names::Result::Ok);
  CHECK(AcceptEdit(fifteen, fifteen + "b", &blocked) == fifteen && blocked == names::Result::TooManyCharacters);
  const std::string fifteen_bytes = std::string(13, 'a') + e;
  CHECK(AcceptEdit(std::string(13, 'a'), fifteen_bytes, &blocked) == fifteen_bytes && blocked == names::Result::Ok);
  CHECK(AcceptEdit(fifteen_bytes, fifteen_bytes + e, &blocked) == fifteen_bytes && blocked == names::Result::TooManyBytes);
  const std::string byte_limit = std::string(11, 'a') + e + "b";
  CHECK(AcceptEdit(byte_limit, byte_limit + e, &blocked) == byte_limit && blocked == names::Result::TooManyBytes);
  const std::string few_chars = std::string(7, 'a') + e + e + e + e;
  CHECK(few_chars.size() == 15);
  CHECK(AcceptEdit(few_chars, few_chars + e, &blocked) == few_chars && blocked == names::Result::TooManyBytes);
  CHECK(AcceptEdit("abc", std::string(200, 'x'), &blocked) == "abc" && blocked == names::Result::TooManyCharacters);
  CHECK(AcceptEdit("abc", std::string(60, '\xC3') + "\xA9", &blocked) == "abc" && blocked != names::Result::Ok);
  CHECK(AcceptEdit(fifteen, "abcdefghij" + std::string("xyz"), &blocked) == "abcdefghijxyz" && blocked == names::Result::Ok);
  CHECK(AcceptEdit(fifteen, fifteen.substr(1), &blocked) == fifteen.substr(1));
  CHECK(AcceptEdit(fifteen, fifteen.substr(0, 14), &blocked) == fifteen.substr(0, 14));
  CHECK(AcceptEdit("abc", "", &blocked).empty() && blocked == names::Result::Ok);
  CHECK(AcceptEdit("abc", "ab\xFF", &blocked) == "abc" && blocked == names::Result::BadCharacter);
  CHECK(AcceptEdit("abc", "ab\xC3", &blocked) == "abc" && blocked == names::Result::BadCharacter);
  CHECK(AcceptEdit("abc", "ab\x01", &blocked) == "abc" && blocked == names::Result::BadCharacter);
  CHECK(AcceptEdit("abc", "ab\t", &blocked) == "abc" && blocked == names::Result::BadCharacter);
}

void TestSaveGuard() {
  std::printf("an invalid stored name is replaced before it is written\n");
  const std::string settings = ReadFile(std::string(SSF2T_SRC_DIR) + "/settings.cpp");
  const std::string launcher = ReadFile(std::string(SSF2T_SRC_DIR) + "/launcher.cpp");
  CHECK(Has(settings, "name->second = player.default_name;"));
  CHECK(Has(settings, "local_player_name::SdkValue(rex::cvar::GetFlagByName(player.key)"));
  CHECK(Has(launcher, "local_player_name::AcceptEdit("));
  CHECK(Has(launcher, "ImGuiInputTextFlags_CallbackEdit"));
  CHECK(names::SdkValue(std::string(40, 'x'), "Player 2").empty());
  CHECK(names::SdkValue("bad\xFF", "Player 2").empty());
  CHECK(names::SdkValue("bad\x01", "Player 2").empty());
}

void TestSettingsWiring() {
  std::printf("settings, reset, launcher and patch are wired\n");
  const std::string src = SSF2T_SRC_DIR;
  const std::string settings = ReadFile(src + "/settings.cpp");
  const std::string launcher = ReadFile(src + "/launcher.cpp");
  const std::string patch = ReadFile(src + "/../patches/rexglue-sdk/0054-local-user-display-names.patch");
  for (const PlayerNameSetting& player : kPlayerNames) {
    CHECK(Has(settings, std::string("REXCVAR_DEFINE_STRING(") + player.key + ", \"" + player.default_name + "\""));
    CHECK(Has(settings, std::string("\"") + player.key + "\","));
    CHECK(Has(patch, std::string("REXCVAR_DEFINE_STRING(") + player.sdk_key + ", \"\""));
  }
  CHECK(Has(settings, "ApplyPlayerNames();"));
  CHECK(Has(launcher, "ui.Section(\"Player names\", true)"));
  CHECK(Has(launcher, "kPlayerNamesHelp"));
  CHECK(kPlayerNames.size() == 2);
  CHECK(!Has(settings, "ssf2t_player_3_name") && !Has(settings, "ssf2t_player_4_name"));
  CHECK(!Has(ReadFile(src + "/settings.h"), "ssf2t_player_3_name"));
  CHECK(!Has(ReadFile(src + "/player_name_setting.h"), "player_4"));
  for (const char* sdk : {"local_player_3_name", "local_player_4_name"}) {
    CHECK(Has(patch, std::string("REXCVAR_DEFINE_STRING(") + sdk + ", \"\""));
  }
  CHECK(std::string(kPlayerNamesHelp) == "Changes the name shown in-game. Save data is not affected.");

  CHECK(Has(patch, "user_index == 0 ? REX_KERNEL_STATE()->user_profile()->name()"));
  CHECK(Has(patch, "input ? input->PlayerNumber(user_index) : 0"));
  CHECK(Has(patch, "UserDisplayName(0)"));
  CHECK(Has(patch, "+  profile_id_ = \"User\";"));
  CHECK(Has(patch, "+  const std::string& profile_id() const { return profile_id_; }"));
  CHECK(Has(patch, "kGameUserContentDirName / user_name"));
  CHECK(Has(patch, "user_profile()->profile_id());"));
  CHECK(!Has(patch, "\n+  xuid_ =") && !Has(patch, "\n-  xuid_ ="));
  CHECK(!Has(patch, "\n+uint64_t GuestXuid") && !Has(patch, "\n-uint64_t GuestXuid"));
  CHECK(!Has(patch, "DEBUG_SAULO"));
}

}  // namespace

int main() {
  TestDefaults();
  TestLogicalMapping();
  TestRenameKeepsIdentity();
  TestValidation();
  TestFallback();
  TestEditing();
  TestSaveGuard();
  TestSettingsWiring();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
