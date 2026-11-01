// Português Brasil (pt-BR) localization.
//
//   ssf2t_localization_tests source   registration, hooks of both builds, sources of both platforms, the embedded tables
//                                     (UTF-8, glyphs, placeholders, duplicates, English left over). No game files.
//   ssf2t_localization_tests game     the tables against the real English string table and the real fonts in
//                                     game\extracted. Exits 77 when the game files are not on this machine.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "language_setting.h"
#include "localization/active_language.h"
#include "localization/arcade_quotes.h"
#include "localization/baked_texture.h"
#include "localization/game_text.h"
#include "localization/game_text_guest.h"
#include "localization/ui_text.h"
#include "menu_text.h"
#include "updater/third_party/miniz/miniz.h"

#ifndef SSF2T_SOURCE_DIR
#error "SSF2T_SOURCE_DIR is required"
#endif

namespace {

namespace fs = std::filesystem;
using ssf2t::localization::GameTextRow;

constexpr int kFixtureMissing = 77;
const fs::path kRoot = SSF2T_SOURCE_DIR;

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::printf("  LOCALIZATION FAIL: %s\n", what.c_str());
  }
}

std::string ReadText(const char* relative) {
  std::ifstream in(kRoot / relative, std::ios::binary);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

bool Has(const std::string& text, std::string_view needle) { return text.find(needle) != std::string::npos; }

bool ValidUtf8(std::string_view s) {
  for (size_t i = 0; i < s.size();) {
    const uint8_t c = uint8_t(s[i]);
    size_t length = 0;
    uint32_t code = 0;
    if (c < 0x80) {
      ++i;
      continue;
    } else if ((c & 0xE0) == 0xC0) {
      length = 2;
      code = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
      length = 3;
      code = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
      length = 4;
      code = c & 0x07;
    } else {
      return false;
    }
    if (i + length > s.size()) return false;
    for (size_t k = 1; k < length; ++k) {
      if ((uint8_t(s[i + k]) & 0xC0) != 0x80) return false;
      code = (code << 6) | (uint8_t(s[i + k]) & 0x3F);
    }
    const uint32_t minimum[] = {0, 0, 0x80, 0x800, 0x10000};
    if (code < minimum[length] || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) return false;
    i += length;
  }
  return true;
}

std::vector<uint32_t> CodePoints(std::string_view s) {
  std::vector<uint32_t> out;
  for (size_t i = 0; i < s.size();) {
    const uint8_t c = uint8_t(s[i]);
    size_t length = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
    uint32_t code = length == 1 ? c : c & (0xFF >> (length + 1));
    for (size_t k = 1; k < length && i + k < s.size(); ++k) code = (code << 6) | (uint8_t(s[i + k]) & 0x3F);
    out.push_back(code);
    i += length;
  }
  return out;
}

// The markup the game and the printf family give meaning to: [tags], $gamertag names, %conversions, {} fields and the
// two-byte button glyphs U+0080..U+009F. Counted, order does not matter.
std::map<std::string, int> Tokens(std::string_view s) {
  std::map<std::string, int> tokens;
  for (size_t i = 0; i < s.size(); ++i) {
    const char c = s[i];
    size_t end = i;
    if (c == '[') {
      const size_t close = s.find(']', i);
      if (close == std::string_view::npos || close - i > 48) continue;
      bool tag = close > i + 1;
      for (size_t k = i + 1; tag && k < close; ++k) {
        const char t = s[k];
        tag = std::isalnum(uint8_t(t)) || t == '=' || t == '_' || t == '.' || t == ',';
      }
      if (!tag || !std::isalpha(uint8_t(s[i + 1]))) continue;
      end = close;
    } else if (c == '$' && s.substr(i + 1, 8) == "gamertag") {
      end = i + 8;
      while (end + 1 < s.size() && (std::isalnum(uint8_t(s[end + 1])))) ++end;
    } else if (c == '%') {
      end = i + 1;
      while (end < s.size() && std::strchr("-+#0123456789.", s[end])) ++end;
      if (end >= s.size() || !std::strchr("diuoxXfFeEgGaAcspn", s[end])) continue;
    } else if (c == '{') {
      const size_t close = s.find('}', i);
      if (close == std::string_view::npos || close - i > 12) continue;
      end = close;
    } else if (uint8_t(c) == 0xC2 && i + 1 < s.size() && uint8_t(s[i + 1]) >= 0x80 && uint8_t(s[i + 1]) <= 0x9F) {
      end = i + 1;
    } else {
      continue;
    }
    ++tokens[std::string(s.substr(i, end - i + 1))];
    i = end;
  }
  return tokens;
}

std::string Describe(const std::map<std::string, int>& tokens) {
  std::string out;
  for (const auto& [token, count] : tokens) out += token + "x" + std::to_string(count) + " ";
  return out.empty() ? "(none)" : out;
}

// Characters no game font holds (the em dash is dropped silently when drawn).
bool ForbiddenGlyph(uint32_t code) { return code == 0x2014; }

const std::set<uint32_t>& PortugueseGlyphs() {
  static const std::set<uint32_t> glyphs = {0xE1, 0xE0, 0xE2, 0xE3, 0xE9, 0xEA, 0xED, 0xF3, 0xF4, 0xF5, 0xFA, 0xE7,
                                            0xC9, 0xCA, 0xCD, 0xD3, 0xD4, 0xD5, 0xDA, 0xC7, 0xFC, 0xAA, 0xBA};
  return glyphs;
}

bool PermittedChar(uint32_t code) {
  if (code == '\t') return true;
  if (code >= 0x20 && code < 0x7F) return true;
  if (code >= 0x80 && code <= 0x9F) return true;  // button glyphs
  if (code >= 0xA0 && code <= 0xBF) return true;  // the fonts hold the whole Latin-1 symbol block
  if (code == 0x2013 || code == 0x2014 || code == 0x2018 || code == 0x2019 || code == 0x201C || code == 0x201D ||
      code == 0x2026 || code == 0x2122 || code == 0xFE)
    return true;
  return PortugueseGlyphs().count(code) != 0 || (code >= 0xC0 && code <= 0xFF);
}

// English words that give an untranslated line away. Portuguese never uses them.
const std::set<std::string>& EnglishWords() {
  static const std::set<std::string> words = {
      "the", "and", "you", "your", "with", "this", "that", "from", "have", "press", "to", "of", "is", "are",
      "be", "or", "it", "can", "has", "was", "by", "an", "at", "into", "when", "while", "then", "settings", "select",
      "cancel", "enable", "disable", "player", "players", "friend", "friends", "achievements", "language", "keyboard",
      "controller", "accept", "decline", "request", "winner", "retry", "ready", "connecting", "searching", "connection",
      "invite", "quit"};
  return words;
}

// Names and canonical move names that contain English words.
const char* const kProtectedPhrases[] = {"Off-the-Wall", "Off the Wall", "The Grammar Club",
                                          "Blood on the Asphalt"};

std::vector<std::string> Words(std::string_view text) {
  std::string cleaned(text.substr(0, text.find("##")));
  for (const char* phrase : kProtectedPhrases) {
    for (size_t at = cleaned.find(phrase); at != std::string::npos; at = cleaned.find(phrase)) {
      cleaned.erase(at, std::strlen(phrase));
    }
  }
  const std::string_view s = cleaned;
  std::vector<std::string> words;
  std::string word;
  for (const char c : s) {
    if (std::isalpha(uint8_t(c)) || uint8_t(c) >= 0x80) {
      word.push_back(uint8_t(c) < 0x80 ? char(std::tolower(uint8_t(c))) : c);
    } else if (!word.empty()) {
      words.push_back(word);
      word.clear();
    }
  }
  if (!word.empty()) words.push_back(word);
  return words;
}

int EnglishWordCount(std::string_view s, std::string* found = nullptr) {
  int count = 0;
  for (const std::string& word : Words(s)) {
    if (!EnglishWords().count(word)) continue;
    ++count;
    if (found && found->find(" " + word) == std::string::npos) *found += " " + word;
  }
  return count;
}

bool HasLetters(std::string_view s) {
  for (const char c : s) {
    if (std::isalpha(uint8_t(c))) return true;
  }
  return false;
}

// ---------------------------------------------------------------------------------------------------------------------
// Registration

void TestLanguageRegistration() {
  using namespace ssf2t;
  Check(std::size(kGameLanguages) == 6, "the language list holds the five original languages and Português Brasil");
  const struct {
    const char* key;
    const char* label;
    uint32_t xconfig;
  } original[] = {{"en-US", "English", 1}, {"fr-FR", "French", 4}, {"de-DE", "German", 3}, {"it-IT", "Italian", 6},
                  {"es-ES", "Spanish", 5}};
  for (size_t i = 0; i < 5; ++i) {
    Check(std::string_view(kGameLanguages[i].key) == original[i].key &&
              std::string_view(kGameLanguages[i].label) == original[i].label &&
              kGameLanguages[i].xconfig_language == original[i].xconfig,
          std::string("original language ") + original[i].key + " is unchanged");
  }
  const GameLanguage& pt = kGameLanguages[5];
  Check(std::string_view(pt.key) == "pt-BR", "pt-BR is registered as language key pt-BR");
  Check(std::string_view(pt.label) == "Portugu\xC3\xAAs Brasil", "the launcher label is exactly 'Português Brasil'");
  Check(pt.xconfig_language == 1, "pt-BR runs on the English XC_LANGUAGE slot");
  Check(std::string_view(LanguageFor("pt-BR").key) == "pt-BR", "LanguageFor finds pt-BR");
  Check(std::string_view(LanguageFor("pt-br").key) == kDefaultLanguage, "an unknown spelling falls back to English");
  Check(std::string_view(kPortugueseBrazilKey) == "pt-BR", "kPortugueseBrazilKey is pt-BR");

  const std::string launcher = ReadText("src/launcher.cpp");
  Check(Has(launcher, "for (const GameLanguage& language : kGameLanguages)") && Has(launcher, "language.label"),
        "the launcher dropdown lists every kGameLanguages entry by its label");
  const std::string settings = ReadText("src/settings.cpp");
  Check(Has(settings, "pt-BR"), "the ssf2t_language setting names pt-BR");
  Check(Has(settings, "localization::SetActiveLanguage(language.key)"), "ApplyLanguage selects the text language");
  Check(Has(settings, "kLanguageSetting,"), "the language choice is a saved launcher setting");
  Check(Has(launcher, "SetActiveLanguage"), "the launcher follows the language choice");
}

void TestActiveLanguage() {
  using namespace ssf2t;
  localization::SetActiveLanguage("pt-BR");
  Check(localization::PortugueseBrazilSelected(), "pt-BR selects the pt-BR text");
  localization::SetActiveLanguage("es-ES");
  Check(!localization::PortugueseBrazilSelected(), "Spanish does not select pt-BR");
  Check(std::string_view(Tr("Settings")) == "Settings", "English text is returned untouched outside pt-BR");
  localization::SetActiveLanguage("pt-BR");
  Check(std::string_view(Tr("zzz no such text")) == "zzz no such text", "text with no translation falls back to English");
  Check(Tr(std::string("zzz no such text")) == "zzz no such text", "a string without translation falls back to English");
  localization::SetActiveLanguage("en-US");
}

std::optional<uint32_t> TraitValue(const std::string& text, const char* strukt, const char* member) {
  const size_t body = text.find(std::string("struct ") + strukt);
  if (body == std::string::npos) return std::nullopt;
  const size_t at = text.find(member, body);
  if (at == std::string::npos) return std::nullopt;
  const size_t hex = text.find("0x", at);
  if (hex == std::string::npos) return std::nullopt;
  return uint32_t(std::stoul(text.substr(hex + 2, 8), nullptr, 16));
}

void TestHooksOfBothBuilds() {
  const std::string guest = ReadText("src/localization/game_text_guest.h");
  const std::string hook = ReadText("src/localization/game_text_hook.cpp");
  const auto base_read = TraitValue(guest, "Base013GameText", "kTextTableRead =");
  const auto tu_read = TraitValue(guest, "TitleUpdate023GameText", "kTextTableRead =");
  Check(base_read.has_value(), "Base013 has a pt-BR text table address");
  Check(tu_read.has_value(), "TU023 has a pt-BR text table address");
  Check(TraitValue(guest, "Base013GameText", "kTextTableReadReturn =").has_value(), "Base013 pins the table loader call site");
  Check(TraitValue(guest, "TitleUpdate023GameText", "kTextTableReadReturn =").has_value(), "TU023 pins the table loader call site");
  const auto base_texture = TraitValue(guest, "Base013GameText", "kTextureParse =");
  const auto tu_texture = TraitValue(guest, "TitleUpdate023GameText", "kTextureParse =");
  Check(base_texture.has_value(), "Base013 has a pt-BR baked texture address");
  Check(tu_texture.has_value(), "TU023 has a pt-BR baked texture address");
  char symbol[64];
  if (base_texture) {
    std::snprintf(symbol, sizeof(symbol), "REX_HOOK_RAW(base013_sub_%08X)", *base_texture);
    Check(Has(hook, symbol), "Base013 hooks the baked texture parse");
  }
  if (tu_texture) {
    std::snprintf(symbol, sizeof(symbol), "REX_HOOK_RAW(tu023_sub_%08X)", *tu_texture);
    Check(Has(hook, symbol), "TU023 hooks the baked texture parse");
  }
  Check(Has(hook, "PortugueseBrazilSelected() && ctx.r4.u32 != 0"), "baked textures are replaced only while pt-BR is selected");
  for (const char* member : {"kPromptText", "kLeaderboardError", "kArcadeQuoteInit"}) {
    const std::string name = std::string(member) + " =";
    const auto base_address = TraitValue(guest, "Base013GameText", name.c_str());
    const auto tu_address = TraitValue(guest, "TitleUpdate023GameText", name.c_str());
    Check(base_address.has_value() && tu_address.has_value(), std::string("both builds have the pt-BR runtime hook address ") + member);
    if (base_address) {
      std::snprintf(symbol, sizeof(symbol), "REX_HOOK_RAW(base013_sub_%08X)", *base_address);
      Check(Has(hook, symbol), std::string("Base013 hooks the pt-BR runtime string ") + member);
    }
    if (tu_address) {
      std::snprintf(symbol, sizeof(symbol), "REX_HOOK_RAW(tu023_sub_%08X)", *tu_address);
      Check(Has(hook, symbol), std::string("TU023 hooks the pt-BR runtime string ") + member);
    }
  }
  Check(Has(hook, "GuestHost(base, G::kArcadeQuotePool)") && Has(hook, "PatchArcadeQuotes(pool, G::kArcadeQuotePoolSize)") &&
            !Has(hook, "kArcadeQuoteShift"),
        "the arcade quotes are patched in the writable pool of the running build, never in the image");
  Check(TraitValue(guest, "Base013GameText", "kArcadeQuotePool =") != TraitValue(guest, "TitleUpdate023GameText", "kArcadeQuotePool ="),
        "each build has its own arcade quote pool address");
  Check(Has(guest, "kPressStartActivateLiteral = 0x820064A8"), "the Training prompt literal address is pinned");
  Check(Has(hook, "ssf2t::kPressStartActivateLiteral && ssf2t::localization::PortugueseBrazilSelected()"),
        "the Training prompt is replaced only while pt-BR is selected");
  Check(Has(hook, "!pending || !ssf2t::localization::PortugueseBrazilSelected()"),
        "the leaderboard error is replaced only while pt-BR is selected");
  const std::string app = ReadText("src/ssf2t_hd_remix_app.h");
  Check(Has(app, "CreateAchievementNotificationDialog() override") && Has(app, "std::make_unique<AchievementToast>"),
        "the achievement unlock toast is the port's localized toast");
  Check(Has(ReadText("src/achievement_toast.cpp"), "Tr(\"Achievement Unlocked\")") &&
            Has(ReadText("src/achievement_toast.cpp"), "Tr(toast.event.achievement.label)"),
        "the toast translates its heading and the achievement name");
  for (const GameTextRow& row : ssf2t::localization::PortugueseBrazilGameText().rows) {
    if (row.id == "PRESS_START_ACTIVATE") Check(row.text == "Pressione START para ativar", "pt-BR maps the Training Start prompt to 'Pressione START para ativar'");
    if (row.id == "PRESS_START") Check(row.text == "PRESSIONE START", "pt-BR maps the title Start prompt to PRESSIONE START");
  }
  if (base_read) {
    std::snprintf(symbol, sizeof(symbol), "REX_HOOK_RAW(base013_sub_%08X)", *base_read);
    Check(Has(hook, symbol), "Base013 hooks the pt-BR text table read");
  }
  if (tu_read) {
    std::snprintf(symbol, sizeof(symbol), "REX_HOOK_RAW(tu023_sub_%08X)", *tu_read);
    Check(Has(hook, symbol), "TU023 hooks the pt-BR text table read");
  }
  const size_t guard = hook.find("#if SSF2T_TITLE_UPDATE_IMAGE");
  const size_t tu = hook.find("REX_HOOK_RAW(tu023_");
  Check(guard != std::string::npos && tu != std::string::npos && guard < tu && hook.find("REX_HOOK_RAW(base013_") < guard,
        "the Title Update hook is the only build-conditional part");
  Check(!Has(hook, "_WIN32") && !Has(hook, "__linux__") && !Has(hook, "__unix__"), "the hook has no platform branch");
  Check(Has(hook, "ssf2t::localization::PortugueseBrazilSelected()") && Has(hook, "kEnglishSlot"),
        "the hook replaces the English table only while pt-BR is selected");
  for (const char* file : {"src/localization/game_text.cpp", "src/localization/ui_text.cpp", "src/localization/game_text.h",
                           "src/localization/ui_text.h", "src/localization/active_language.h"}) {
    const std::string text = ReadText(file);
    Check(!Has(text, "_WIN32") && !Has(text, "__linux__") && !Has(text, "__unix__") && !Has(text, "WideChar") &&
              !Has(text, "setlocale"),
          std::string(file) + " has no platform or code page dependency");
  }
}

void TestBuildSourcesOnBothPlatforms() {
  const std::string cmake = ReadText("CMakeLists.txt");
  const size_t start = cmake.find("set(SSF2T_SOURCES");
  const size_t end = start == std::string::npos ? start : cmake.find("\n)", start);
  Check(start != std::string::npos && end != std::string::npos, "CMakeLists.txt lists SSF2T_SOURCES");
  if (start == std::string::npos || end == std::string::npos) return;
  const std::string block = cmake.substr(start, end - start);
  for (const char* source : {"src/localization/game_text.cpp", "src/localization/game_text_hook.cpp",
                             "src/localization/pt_br_game_text.cpp", "src/localization/pt_br_ui_text.cpp",
                             "src/localization/ui_text.cpp"}) {
    Check(Has(block, source), std::string("the Windows and Linux program builds ") + source);
  }
  Check(!Has(block, "if(WIN32)") && !Has(block, "if(UNIX"), "no localization source is platform-conditional");
  Check(Has(cmake, "ssf2t_localization_tests"), "the localization suite is built with the C++ suites");
  const std::string windows = ReadText("tools/test.ps1");
  const std::string shell = ReadText("tools/test.sh");
  Check(Has(windows, "'localization source'") && Has(windows, "'localization game'") && Has(windows, "'localization runtime'"),
        "tools/test.ps1 runs both localization modes");
  Check(Has(shell, "ssf2t_localization_tests source") && Has(shell, "ssf2t_localization_tests game") &&
            Has(shell, "ssf2t_localization_tests runtime"),
        "tools/test.sh runs both localization modes");
  const std::string registry = ReadText("src/feature_parity.h");
  Check(Has(registry, "Language: Portugu\xC3\xAAs Brasil") && Has(registry, "tests/localization_tests.cpp"),
        "the global feature parity registry requires pt-BR on every build and platform");
}

// ---------------------------------------------------------------------------------------------------------------------
// Tables

const std::set<std::string>& GameKeepExceptions() {
  // Lines kept in English that carry English words on purpose: the credits' names and the table's own developer text.
  static const std::set<std::string> ids = {"CREDITS_O02", "selectionHelpText", "screenHelpText", "fonttags"};
  return ids;
}

bool KeptEnglishAllowed(const std::string& id, std::string_view english) {
  if (GameKeepExceptions().count(id)) return true;
  return EnglishWordCount(english) == 0;
}

void CheckText(const std::string& where, const std::string& text, bool ui = false) {
  Check(ValidUtf8(text), where + ": the text is valid UTF-8");
  Check(text.find('\0') == std::string::npos, where + ": the text holds no NUL");
  bool bad = false;
  uint32_t first = 0;
  for (const uint32_t code : CodePoints(text)) {
    if ((ForbiddenGlyph(code) && !ui) || (!PermittedChar(code) && !(ui && code == '\n'))) {
      bad = true;
      first = code;
      break;
    }
  }
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), ": U+%04X is not a glyph the game fonts hold", first);
  Check(!bad, where + buffer);
}

void TestEmbeddedSourcesAreUtf8() {
  for (const char* file : {"src/localization/pt_br_game_text.cpp", "src/localization/pt_br_ui_text.cpp"}) {
    const std::string text = ReadText(file);
    Check(!text.empty(), std::string(file) + " exists");
    Check(ValidUtf8(text), std::string(file) + " is valid UTF-8");
    Check(text.find("\r") == std::string::npos, std::string(file) + " uses LF line endings");
  }
}

void TestGameTableShape() {
  const auto& table = ssf2t::localization::PortugueseBrazilGameText();
  for (const std::string& error : table.errors) Check(false, "pt-BR game table: " + error);
  Check(table.rows.size() > 1800, "the pt-BR game table covers the translatable lines");
  std::set<uint32_t> lines;
  std::map<std::string, std::string> by_id_and_source;
  for (const GameTextRow& row : table.rows) {
    const std::string where = "game line " + std::to_string(row.line) + " (" + row.id + ")";
    Check(lines.insert(row.line).second, where + ": duplicate line");
    Check(row.line > 0, where + ": the header line has no translation");
    Check(!row.id.empty(), where + ": empty id");
    if (row.keep_english) continue;
    Check(!row.text.empty(), where + ": empty translation");
    CheckText(where, row.text);
    Check(row.text.find('\n') == std::string::npos && row.text.find('\r') == std::string::npos,
          where + ": the text holds a line break");
    // Rows of one id must agree.
    const auto [it, inserted] = by_id_and_source.emplace(row.id + "/" + std::to_string(row.english_crc), row.text);
    if (!inserted) Check(it->second == row.text, where + ": the same id and English text are translated two ways");
    std::string words;
    const int english = EnglishWordCount(row.text, &words);
    Check(english == 0 || GameKeepExceptions().count(row.id) != 0,
          where + ": looks untranslated (English words left in the text:" + words + ")");
  }
}

std::vector<std::string> Placeholders(const std::string& s) {
  std::vector<std::string> out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '%') continue;
    size_t end = i + 1;
    while (end < s.size() && std::strchr("-+#0123456789.l", s[end]) && s[end] != '\0') ++end;
    if (end < s.size() && std::strchr("diuoxXfFeEgGaAcspn", s[end])) out.push_back(s.substr(i, end - i + 1));
    i = end;
  }
  return out;
}

void TestUiTableShape() {
  const auto& table = ssf2t::localization::PortugueseBrazilUiText();
  for (const std::string& error : table.errors) Check(false, "pt-BR UI table: " + error);
  Check(table.rows.size() >= 100, "the pt-BR UI table covers the launcher and overlays");
  std::set<std::string> seen;
  for (const auto& row : table.rows) {
    const std::string where = "UI text '" + row.english + "'";
    Check(!row.english.empty(), "UI table: an empty English key");
    Check(seen.insert(row.english).second, where + ": duplicate entry");
    Check(!row.text.empty(), where + ": empty translation");
    CheckText(where, row.text, true);
    CheckText(where + " (English key)", row.english, true);
    Check(Tokens(row.english) == Tokens(row.text),
          where + ": placeholders differ. English " + Describe(Tokens(row.english)) + "pt-BR " + Describe(Tokens(row.text)));
    Check(Placeholders(row.english) == Placeholders(row.text), where + ": format conversions differ");
    const bool same = row.english == row.text;
    if (same) {
      Check(!HasLetters(row.english) || EnglishWordCount(row.english) == 0,
            where + ": left in English (translate it, or add a technical-term exception)");
    } else {
      std::string words;
      const int left = EnglishWordCount(row.text, &words);
      Check(left == 0, where + ": looks partly untranslated (English words:" + words + ") '" + row.text + "'");
    }
  }
  for (const char* required : {"Settings", "Achievements", "Find Match", "Create Match"}) {
    const auto found = std::find_if(table.rows.begin(), table.rows.end(),
                                    [&](const auto& row) { return row.english == required; });
    if (found == table.rows.end()) continue;
    Check(found->text != required, std::string("UI text '") + required + "' must be translated");
  }
  ssf2t::localization::SetActiveLanguage("pt-BR");
  for (const auto& row : table.rows) {
    Check(std::string_view(ssf2t::Tr(row.english.c_str())) == row.text, "Tr('" + row.english + "') returns the pt-BR text");
  }
  ssf2t::localization::SetActiveLanguage("en-US");
}


// Every Tr("...") in the program names an English text the pt-BR table holds, so no call silently falls back.
std::optional<std::string> DecodeCLiterals(const std::string& text, size_t& at) {
  std::string out;
  bool any = false;
  for (;;) {
    while (at < text.size() && (text[at] == ' ' || text[at] == '\t' || text[at] == '\r' || text[at] == '\n')) ++at;
    if (at >= text.size() || text[at] != '"') break;
    ++at;
    any = true;
    while (at < text.size() && text[at] != '"') {
      if (text[at] != '\\') {
        out.push_back(text[at++]);
        continue;
      }
      ++at;
      if (at >= text.size()) return std::nullopt;
      const char c = text[at++];
      if (c == 'x') {
        unsigned value = 0;
        while (at < text.size() && std::isxdigit(uint8_t(text[at]))) {
          value = value * 16 + (std::isdigit(uint8_t(text[at])) ? text[at] - '0' : (std::tolower(text[at]) - 'a' + 10));
          ++at;
        }
        out.push_back(char(value));
      } else if (c == 'n') {
        out.push_back('\n');
      } else if (c == 't') {
        out.push_back('\t');
      } else if (c == '"' || c == '\\' || c == '\'') {
        out.push_back(c);
      } else {
        return std::nullopt;
      }
    }
    if (at >= text.size()) return std::nullopt;
    ++at;
  }
  return any ? std::optional<std::string>(out) : std::nullopt;
}

void TestTrCallsHaveRows() {
  std::set<std::string> keys;
  for (const auto& row : ssf2t::localization::PortugueseBrazilUiText().rows) keys.insert(row.english);
  int calls = 0;
  for (const fs::directory_entry& entry : fs::recursive_directory_iterator(kRoot / "src")) {
    const std::string extension = entry.path().extension().string();
    if (!entry.is_regular_file() || (extension != ".cpp" && extension != ".h")) continue;
    const std::string relative = fs::relative(entry.path(), kRoot).generic_string();
    if (relative.starts_with("src/localization/") || relative.find("third_party") != std::string::npos) continue;
    std::ifstream in(entry.path(), std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();
    for (size_t at = text.find("Tr("); at != std::string::npos; at = text.find("Tr(", at + 3)) {
      if (at > 0 && (std::isalnum(uint8_t(text[at - 1])) || text[at - 1] == '_')) continue;
      size_t cursor = at + 3;
      const auto literal = DecodeCLiterals(text, cursor);
      if (!literal) continue;
      ++calls;
      Check(keys.count(*literal) != 0, relative + ": Tr(\"" + literal->substr(0, 60) + "\") has no pt-BR row");
    }
  }
  Check(calls > 20, "the program translates its own text through Tr() (" + std::to_string(calls) + " literal calls)");
}


// Text drawn into game textures is invisible to the string table. Every such texture is either redrawn (a BakedTexture
// row) or listed here with the reason it stays as it is.
struct KeptTexture {
  const char* pattern;  // exact name, a prefix ending in '*', or a suffix starting with '*'
  const char* reason;
};

const KeptTexture kKeptTextures[] = {
    {"*_flag", "country flag"},
    {"CS_*", "country caption on the character select map"},
    {"*_name", "character name"},
    {"continue", "arcade caption CONTINUE, canonical in Brazil"},
    {"game_over", "arcade caption GAME OVER, canonical in Brazil"},
    {"press_start", "canonical UI art kept as the stock PRESS START texture"},
    {"characterselect", "arcade screen caption"},
    {"stage_select", "arcade screen caption"},
    {"stage", "arcade screen caption"},
    {"1p_text", "player marker 1P"},
    {"2P_icon", "player marker 2P"},
    {"1p_selector_*", "selector artwork"},
    {"2P_bracket_*", "selector artwork"},
    {"versus_*", "VS artwork"},
    {"blank", "empty"},
    {"3333_*", "artwork without text"},
    {"3335_22", "artwork without text"},
    {"airplane*", "artwork without text"},
};

bool Matches(const std::string& name, const std::string& pattern) {
  const auto lower = [](std::string v) {
    for (char& c : v) c = char(std::tolower(uint8_t(c)));
    return v;
  };
  const std::string n = lower(name);
  const std::string p = lower(pattern);
  if (p.front() == '*') return n.size() >= p.size() - 1 && n.compare(n.size() - (p.size() - 1), p.size() - 1, p, 1) == 0;
  if (p.back() == '*') return n.compare(0, p.size() - 1, p, 0, p.size() - 1) == 0;
  return n == p;
}

// Arcade sprite artwork named <hex sprite code>_<palette>.
bool IsNumberedSprite(const std::string& name) {
  const size_t under = name.find('_');
  if (under == std::string::npos || under < 3 || under > 4 || under + 1 >= name.size()) return false;
  for (size_t i = 0; i < under; ++i) {
    if (!std::isxdigit(uint8_t(name[i]))) return false;
  }
  for (size_t i = under + 1; i < name.size(); ++i) {
    if (!std::isdigit(uint8_t(name[i]))) return false;
  }
  return true;
}

void TestBakedTextureRows() {
  const auto rows = ssf2t::localization::PortugueseBrazilBakedTextures();
  Check(rows.empty(), "no pt-BR texture replacement is installed: PRESS START keeps the stock artwork");
  std::set<std::string> names;
  for (const auto& row : rows) {
    Check(names.insert(row.name).second, std::string("baked texture ") + row.name + " is listed twice");
    std::vector<uint8_t> data;
    Check(ssf2t::localization::InflateBakedTexture(row, data), std::string("baked texture ") + row.name + " inflates to its size");
    Check(ssf2t::localization::BakedTextureName(data.data(), data.size()) == row.name,
          std::string("baked texture ") + row.name + " keeps its texture name");
    Check(ValidUtf8(row.text) && std::string_view(row.text) != row.english_text,
          std::string("baked texture ") + row.name + " has Portuguese text");
  }
}


// ---------------------------------------------------------------------------------------------------------------------
// Guest literals

struct GuestLiteral {
  uint32_t address = 0;
  std::string text;
};

struct GuestRegion {
  uint32_t begin;
  uint32_t end;
};

// The parts of the guest image that hold text: read-only data, the data tables and the Xbox Live Arcade resource blob.
constexpr GuestRegion kTextRegions[] = {{0x82000000u, 0x82160000u}, {0x83A00000u, 0x83C00000u}, {0x84BE0000u, 0x84C10000u}};

bool EnglishLike(const std::string& s) {
  if (s.size() < 5) return false;
  size_t letters = 0;
  for (const char c : s) letters += std::isalpha(uint8_t(c)) ? 1 : 0;
  if (double(letters) / double(s.size()) < 0.75) return false;
  for (const char* bad : {"%", "::", ".cpp", ".h", "_", "/", "\\", ".x360", "0x"}) {
    if (s.find(bad) != std::string::npos) return false;
  }
  bool spaced = false;
  size_t run = 0;
  bool three = false;
  for (const char c : s) {
    run = std::isalpha(uint8_t(c)) ? run + 1 : 0;
    three |= run >= 3;
    spaced |= c == ' ';
  }
  if (spaced && three) return true;
  bool upper = true;
  for (const char c : s) upper &= !std::islower(uint8_t(c));
  return upper;
}

std::vector<GuestLiteral> ScanGuestLiterals(const std::string& image) {
  std::vector<GuestLiteral> out;
  constexpr uint32_t kBase = 0x82000000u;
  for (const GuestRegion& region : kTextRegions) {
    const size_t begin = region.begin - kBase;
    const size_t end = std::min<size_t>(region.end - kBase, image.size());
    for (size_t at = begin; at < end;) {
      size_t stop = at;
      while (stop < end && uint8_t(image[stop]) >= 0x20 && uint8_t(image[stop]) < 0x7F) ++stop;
      if (stop < end && image[stop] == '\0' && stop - at >= 4) {
        std::string text = image.substr(at, stop - at);
        if (EnglishLike(text)) out.push_back({uint32_t(at + kBase), std::move(text)});
      }
      at = stop + 1;
    }
  }
  return out;
}

std::optional<std::string> ReadGameImage(const char* name) {
  std::ifstream in(kRoot / "game" / "analysis" / name, std::ios::binary);
  if (!in) return std::nullopt;
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

int DumpGuestLiterals() {
  for (const char* name : {"default-0.0.1.3.image.bin", "default.image.bin"}) {
    const auto image = ReadGameImage(name);
    if (!image) return kFixtureMissing;
    for (const GuestLiteral& literal : ScanGuestLiterals(*image)) {
      std::printf("%s\t%08X\t%s\n", name, literal.address, literal.text.c_str());
    }
  }
  return 0;
}


// How many instructions form `address` (lis + addi/ori/load/store displacement) in the code of an image.
uint32_t BE32(const std::string& image, size_t at) {
  return uint32_t(uint8_t(image[at])) << 24 | uint32_t(uint8_t(image[at + 1])) << 16 | uint32_t(uint8_t(image[at + 2])) << 8 |
         uint32_t(uint8_t(image[at + 3]));
}

std::vector<uint32_t> AddressUses(const std::string& image, uint32_t address) {
  std::vector<uint32_t> sites;
  constexpr uint32_t kBase = 0x82000000u;
  const size_t end = std::min<size_t>(image.size(), 0x83C00000u - kBase) & ~size_t(3);
  for (size_t at = 0; at + 4 <= end; at += 4) {
    const uint32_t word = BE32(image, at);
    if ((word >> 26) != 15 || ((word >> 16) & 31) != 0) continue;  // lis rD,imm
    const uint32_t reg = (word >> 21) & 31;
    const int64_t high = int64_t(int16_t(word & 0xFFFF)) << 16;
    for (size_t k = 1; k <= 12 && at + 4 * (k + 1) <= end; ++k) {
      const uint32_t next = BE32(image, at + 4 * k);
      const uint32_t opcode = next >> 26;
      const uint32_t ra = (next >> 16) & 31;
      const bool uses_high = ra == reg;
      if (!uses_high || (opcode != 14 && opcode != 24 && !(opcode >= 32 && opcode <= 55))) continue;
      const uint32_t imm = next & 0xFFFF;
      const int64_t value = opcode == 24 ? (high | imm) : high + int16_t(imm);
      if (uint32_t(value) == address) sites.push_back(uint32_t(kBase + at + 4 * k));
    }
  }
  return sites;
}

// Direct calls (bl) to `target`.
std::vector<uint32_t> CallSites(const std::string& image, uint32_t target) {
  std::vector<uint32_t> sites;
  constexpr uint32_t kBase = 0x82000000u;
  const size_t end = std::min<size_t>(image.size(), 0x83A00000u - kBase) & ~size_t(3);
  for (size_t at = 0; at + 4 <= end; at += 4) {
    const uint32_t word = BE32(image, at);
    if ((word & 0xFC000003u) != 0x48000001u) continue;
    int32_t offset = int32_t(word & 0x03FFFFFC);
    if (offset & 0x02000000) offset -= 0x04000000;
    if (uint32_t(kBase + at + offset) == target) sites.push_back(uint32_t(kBase + at));
  }
  return sites;
}

// 32-bit big-endian words equal to `value` outside the code (pointer tables).
std::vector<uint32_t> PointerRefs(const std::string& image, uint32_t value) {
  std::vector<uint32_t> sites;
  constexpr uint32_t kBase = 0x82000000u;
  for (size_t at = 0; at + 4 <= image.size(); at += 4) {
    if (BE32(image, at) == value) sites.push_back(uint32_t(kBase + at));
  }
  return sites;
}

int PrintUses(int argc, char** argv) {
  for (const char* name : {"default-0.0.1.3.image.bin", "default.image.bin"}) {
    const auto image = ReadGameImage(name);
    if (!image) return kFixtureMissing;
    for (int i = 2; i < argc; ++i) {
      const uint32_t value = uint32_t(std::stoul(argv[i], nullptr, 16));
      std::printf("%s %08X uses=%zu calls=%zu pointers=%zu\n", name, value, AddressUses(*image, value).size(),
                  CallSites(*image, value).size(), PointerRefs(*image, value).size());
    }
  }
  return 0;
}


// ---------------------------------------------------------------------------------------------------------------------
// Runtime literal inventory

enum class LiteralClass { kTranslated, kCanonical, kUnreachable };

struct GuestRun {
  LiteralClass cls;
  uint32_t begin;
  uint32_t end;
  const char* first;
  const char* last;
  const char* reason;
};

#include "runtime_literals.inc"

// Structural facts the unreachable runs rest on. A count that changes means new code reaches the text: audit again.
struct StructuralFact {
  const char* what;
  const char* kind;  // uses, calls or pointers
  uint32_t base013;
  uint32_t title_update_023;
  size_t expected;
  size_t expected_title_update_023 = SIZE_MAX;
};

const StructuralFact kFacts[] = {
    {"arcade stage-flow byte that gates the legacy front-end menu", "uses", 0x83CA07D7u, 0x83CA0CEFu, 15},
    {"arcade option-name table (English and Shift-JIS)", "uses", 0x83A2A2A8u, 0x83A2A2A8u, 0},
    {"arcade option-name table (English)", "uses", 0x83A2A3A8u, 0x83A2A3A8u, 0},
    {"arcade option-name table (English)", "pointers", 0x83A2A3A8u, 0x83A2A3A8u, 0},
    {"arcade option-name table (Shift-JIS)", "uses", 0x83A2A4A8u, 0x83A2A4A8u, 1},
    {"arcade pause block", "uses", 0x83A2E020u, 0x83A2E020u, 0},
    {"arcade pause block", "pointers", 0x83A2E020u, 0x83A2E020u, 2},
    {"legacy front-end menu tick", "calls", 0x821D7D28u, 0x821D7E48u, 1},
    {"legacy front-end menu tick", "pointers", 0x821D7D28u, 0x821D7E48u, 1},
    {"legacy front-end menu entry", "uses", 0x821D7478u, 0x821D7598u, 3},
    {"legacy front-end menu entry", "pointers", 0x821D7478u, 0x821D7598u, 1},
    {"arcade text queue renderer", "calls", 0x82189C58u, 0x82189C78u, 1},
    {"arcade text queue renderer", "pointers", 0x82189C58u, 0x82189C78u, 1},
    {"'PRESS START BUTTON' literal", "uses", 0x82007D20u, 0x82007D20u, 3},
    {"'Press START to activate' literal", "uses", 0x820064A8u, 0x820064A8u, 1},
    {"arcade ending text reader (pool A, task type 0x2C)", "calls", 0x821E40E0u, 0x821E4750u, 0},
    {"arcade ending text reader (pool A, task type 0x2C)", "pointers", 0x821E40E0u, 0x821E4750u, 2},
    {"arcade ending text reader (pool C, task type 0x28)", "calls", 0x821E4250u, 0x821E48C0u, 0},
    {"arcade ending text reader (pool C, task type 0x28)", "pointers", 0x821E4250u, 0x821E48C0u, 2},
    {"arcade ending text reader (pool D, task type 0x24)", "pointers", 0x821E4300u, 0x821E4970u, 2},
    {"'THANK YOU FOR YOUR PLAYING' state handler", "calls", 0x821ABDA8u, 0x821ABBE0u, 0},
    {"'THANK YOU FOR YOUR PLAYING' state handler", "pointers", 0x821ABDA8u, 0x821ABBE0u, 2},
    {"arcade task spawn table", "uses", 0x83A35DB8u, 0x83A35DB8u, 2},
    {"arcade task spawn", "calls", 0x82252AC0u, 0x82252B08u, 2},
    {"arcade direct spawn", "calls", 0x823A9968u, 0x823A9958u, 35},
    {"arcade queued spawn", "calls", 0x823A97C0u, 0x823A97B0u, 3},
    {"arcade spawn queue push", "calls", 0x82379580u, 0x82379650u, 52},
    {"arcade task type 0x20 handler", "calls", 0x821DC5E0u, 0x821DCC50u, 0},
    {"arcade task type 0x20 handler", "pointers", 0x821DC5E0u, 0x821DCC50u, 2},
    {"arcade task type 0x20 disabled body", "calls", 0x821DC250u, 0x821DC8C0u, 1},
    {"arcade task type 0x20 disabled body", "pointers", 0x821DC250u, 0x821DC8C0u, 1},
    {"arcade task type 0x20 state reset", "calls", 0x82252C18u, 0x82252C60u, 22},
    {"arcade task type 0x20 state reset", "pointers", 0x82252C18u, 0x82252C60u, 1},
    {"arcade task type 0x20 colour store", "calls", 0x823E1C08u, 0x823E22D0u, 2},
    {"arcade request record fill", "calls", 0x823B4408u, 0x823B4438u, 2},
    {"arcade request record fill", "pointers", 0x823B4408u, 0x823B4438u, 1},
    {"arcade request record setup", "calls", 0x823B4308u, 0x823B4338u, 2},
    {"arcade software bitmap presenter", "calls", 0x823E2078u, 0x823E2750u, 1},
    {"arcade software bitmap presenter", "pointers", 0x823E2078u, 0x823E2750u, 1},
    {"arcade fight-active byte", "uses", 0x83E09DB0u, 0x83E0A2C8u, 54, 55},
};

// Code that holds no text path: any change to its instructions invalidates the classification.
struct CodeFact {
  const char* what;
  uint32_t base013;
  uint32_t title_update_023;
  size_t bytes_base013;
  size_t bytes_title_update_023;
  uint32_t crc_base013;
  uint32_t crc_title_update_023;
};

const CodeFact kCodeFacts[] = {
    {"arcade task type 0x20 handler", 0x821DC5E0u, 0x821DCC50u, 76, 76, 0xC0A5071Bu, 0x5A9892B3u},
    {"arcade task type 0x20 disabled body entry", 0x821DC250u, 0x821DC8C0u, 16, 16, 0x107C4B4Cu, 0x107C4B4Cu},
    {"arcade task type 0x20 disabled body exit", 0x821DC5D0u, 0x821DCC40u, 16, 16, 0x84D6FD9Du, 0x84D6FD9Du},
    {"arcade task type 0x20 state reset", 0x82252C18u, 0x82252C60u, 120, 120, 0x342D96FAu, 0x580119E7u},
    {"arcade task type 0x20 colour store", 0x823E1C08u, 0x823E22D0u, 12, 12, 0xE65844E1u, 0xE65844E1u},
    {"arcade request record setup", 0x823B4308u, 0x823B4338u, 256, 256, 0x3E0F3A5Cu, 0x4C8D33ECu},
    {"arcade request record fill", 0x823B4408u, 0x823B4438u, 236, 236, 0x8C15B677u, 0x98454A75u},
    {"arcade quote pool init", 0x8362C0B0u, 0x8362C828u, 1972, 1972, 0x0B87A0B9u, 0xEE4BCD41u},
    {"arcade software bitmap present guard", 0x82181BACu, 0x82181BBCu, 72, 88, 0x2708155Bu, 0x0D54A734u},
};

size_t CountFor(const std::string& image, const char* kind, uint32_t address) {
  const std::string k = kind;
  if (k == "uses") return AddressUses(image, address).size();
  if (k == "calls") return CallSites(image, address).size();
  return PointerRefs(image, address).size();
}

bool HasUiRow(const std::string& text) {
  for (const auto& row : ssf2t::localization::PortugueseBrazilUiText().rows) {
    if (row.english == text) return true;
  }
  return false;
}

// Mechanism of a translated literal: a UI table row (the length-prefixed XDBF strings drop their prefix byte) or a hook.
bool HasRuntimeTranslation(const std::string& text) {
  if (text == "Press START to activate") {
    for (const GameTextRow& row : ssf2t::localization::PortugueseBrazilGameText().rows) {
      if (row.id == "PRESS_START_ACTIVATE") return !row.keep_english && row.text == "Pressione START para ativar";
    }
    return false;
  }
  for (const auto& line : ssf2t::localization::ArcadeQuoteLines()) {
    if (text == line.english) return true;
  }
  return HasUiRow(text) || (text.size() > 1 && HasUiRow(text.substr(1)));
}

uint32_t Le32(const std::string& s, size_t at);

// Characteristics of the image section holding `address` (PE section table), 0 when no section does.
uint32_t SectionCharacteristics(const std::string& image, uint32_t address) {
  constexpr uint32_t kBase = 0x82000000u;
  const auto le32 = [&](size_t at) { return Le32(image, at); };
  const size_t pe = le32(0x3C);
  const size_t sections = uint8_t(image[pe + 6]) | size_t(uint8_t(image[pe + 7])) << 8;
  const size_t optional = uint8_t(image[pe + 20]) | size_t(uint8_t(image[pe + 21])) << 8;
  for (size_t i = 0; i < sections; ++i) {
    const size_t at = pe + 24 + optional + 40 * i;
    const uint32_t begin = kBase + le32(at + 12);
    if (address >= begin && address < begin + le32(at + 8)) return le32(at + 36);
  }
  return 0;
}

constexpr uint32_t kSectionWritable = 0x80000000u;
constexpr uint32_t kInvalidArcadeQuoteWrite = 0x820AD34Bu;

// The init copies these image ranges (offset from the first quote, length) into its pool in descending order.
struct QuoteChunk {
  uint32_t offset;
  uint32_t length;
};
const QuoteChunk kQuoteChunks[] = {{0x45C, 84}, {0x420, 56}, {0x3CC, 81}, {0x394, 55}, {0x358, 58}, {0x330, 38}, {0x300, 45}, {0x2CC, 50}, {0x294, 54}, {0x260, 50}, {0x228, 53}, {0x1F4, 50}, {0x1BC, 56}, {0x184, 54}, {0x154, 47}, {0x120, 50}, {0xE0, 62}, {0x94, 74}, {0x3C, 84}, {0x0, 57}};

int TestArcadeQuotePool(const std::string& image, const char* build, bool title_update) {
  constexpr uint32_t kFirstQuote = 0x820AD344u;
  const uint32_t shift = title_update ? 0x1D0u : 0u;
  const uint32_t pool = title_update ? ssf2t::TitleUpdate023GameText::kArcadeQuotePool : ssf2t::Base013GameText::kArcadeQuotePool;
  const uint32_t size = title_update ? ssf2t::TitleUpdate023GameText::kArcadeQuotePoolSize : ssf2t::Base013GameText::kArcadeQuotePoolSize;
  const std::string name = build;
  Check((SectionCharacteristics(image, pool) & kSectionWritable) != 0 && (SectionCharacteristics(image, pool + size - 1) & kSectionWritable) != 0,
        name + ": the arcade quote pool is writable memory");
  const uint32_t rom = kFirstQuote + shift;
  Check((SectionCharacteristics(image, rom) & kSectionWritable) == 0, name + ": the arcade quote text in the image is read-only");
  Check(!title_update || pool == ssf2t::Base013GameText::kArcadeQuotePool + 0x520u, name + ": the pool address is not the Base013 one");
  Check(kInvalidArcadeQuoteWrite < pool || kInvalidArcadeQuoteWrite >= pool + size, name + ": the read-only quote address is not a patch target");

  std::string ram;
  uint32_t total = 0;
  for (const QuoteChunk& chunk : kQuoteChunks) {
    ram += image.substr(rom - 0x82000000u + chunk.offset, chunk.length);
    total += chunk.length;
  }
  Check(total == size, name + ": the quote chunks fill the pool exactly");
  size_t written = ssf2t::localization::PatchArcadeQuotes(reinterpret_cast<uint8_t*>(ram.data()), ram.size());
  Check(written == ssf2t::localization::ArcadeQuoteLines().size(), name + ": every arcade quote line is patched in the pool");
  for (const auto& line : ssf2t::localization::ArcadeQuoteLines()) {
    Check(ram.find(line.portuguese) != std::string::npos && ram.find(line.english) == std::string::npos,
          name + ": arcade quote '" + line.english + "' is Portuguese in the pool");
  }
  written = ssf2t::localization::PatchArcadeQuotes(reinterpret_cast<uint8_t*>(ram.data()), ram.size());
  Check(written == 0, name + ": patching the pool twice changes nothing");
  return 0;
}

int TestRuntimeLiterals() {
  struct Image {
    const char* file;
    const char* build;
    const GuestRun* runs;
    size_t count;
    int fact_column;
  };
  const Image images[] = {{"default-0.0.1.3.image.bin", "Base013", kRunsBase013, std::size(kRunsBase013), 0},
                          {"default.image.bin", "TU023", kRunsTitleUpdate023, std::size(kRunsTitleUpdate023), 1}};
  std::map<LiteralClass, size_t> totals[2];
  int index = 0;
  for (const Image& image : images) {
    const auto data = ReadGameImage(image.file);
    if (!data) {
      std::printf("game\\analysis\\%s is not on this machine\n", image.file);
      return kFixtureMissing;
    }
    const std::vector<GuestLiteral> literals = ScanGuestLiterals(*data);
    Check(!literals.empty(), std::string(image.build) + ": guest literals were scanned");
    for (const GuestLiteral& literal : literals) {
      const GuestRun* found = nullptr;
      for (size_t i = 0; i < image.count; ++i) {
        if (literal.address >= image.runs[i].begin && literal.address <= image.runs[i].end) found = &image.runs[i];
      }
      char where[64];
      std::snprintf(where, sizeof(where), " (%s 0x%08X)", image.build, literal.address);
      if (found == nullptr) {
        Check(false, "guest literal '" + literal.text.substr(0, 60) + "'" + where + " has no runtime classification");
        continue;
      }
      ++totals[index][found->cls];
      if (found->cls == LiteralClass::kTranslated) {
        Check(HasRuntimeTranslation(literal.text), "pt-BR runtime string '" + literal.text.substr(0, 60) + "'" + where +
                                                       " has no Portuguese mapping");
      }
    }
    for (size_t i = 0; i < image.count; ++i) {
      const GuestRun& run = image.runs[i];
      const auto first = std::find_if(literals.begin(), literals.end(), [&](const auto& l) { return l.address == run.begin; });
      const auto last = std::find_if(literals.begin(), literals.end(), [&](const auto& l) { return l.address == run.end; });
      Check(first != literals.end() && first->text == run.first && last != literals.end() && last->text == run.last,
            std::string(image.build) + ": the classified run '" + run.first + "' no longer matches the image");
      Check(std::string_view(run.reason).size() > 8, std::string(image.build) + ": run '" + run.first + "' has no reason");
    }
    for (const StructuralFact& fact : kFacts) {
      const uint32_t address = image.fact_column == 0 ? fact.base013 : fact.title_update_023;
      const size_t count = CountFor(*data, fact.kind, address);
      const size_t expected = image.fact_column == 1 && fact.expected_title_update_023 != SIZE_MAX ? fact.expected_title_update_023 : fact.expected;
      char buffer[96];
      std::snprintf(buffer, sizeof(buffer), " %s of 0x%08X: expected %zu, found %zu", fact.kind, address, expected, count);
      Check(count == expected, std::string(image.build) + ": " + fact.what + buffer + ". New code reaches this arcade text");
    }
    TestArcadeQuotePool(*data, image.build, image.fact_column == 1);
    for (const CodeFact& fact : kCodeFacts) {
      const uint32_t address = image.fact_column == 0 ? fact.base013 : fact.title_update_023;
      const uint32_t expected = image.fact_column == 0 ? fact.crc_base013 : fact.crc_title_update_023;
      const size_t at = address - 0x82000000u;
      const size_t bytes = image.fact_column == 0 ? fact.bytes_base013 : fact.bytes_title_update_023;
      const bool inside = at + bytes <= data->size();
      char buffer[96];
      std::snprintf(buffer, sizeof(buffer), " code at 0x%08X changed", address);
      Check(inside && ssf2t::localization::Crc32(std::string_view(*data).substr(at, bytes)) == expected,
            std::string(image.build) + ": " + fact.what + buffer + ". It has no text path only while unchanged");
    }
    ++index;
  }
  for (const Image& image : images) {
    const auto data = ReadGameImage(image.file);
    const uint32_t shift = image.fact_column == 0 ? 0u : 0x1D0u;
    for (const auto& line : ssf2t::localization::ArcadeQuoteLines()) {
      const std::string english = line.english;
      const std::string portuguese = line.portuguese;
      const size_t at = line.base013_address + shift - 0x82000000u;
      Check(data->compare(at, english.size(), english) == 0,
            std::string(image.build) + ": arcade quote '" + english + "' is where the table says");
      Check(portuguese.size() == english.size(), "arcade quote '" + english + "': the Portuguese line keeps the byte length");
      Check(portuguese.back() == english.back() || english.back() != '/', "arcade quote '" + english + "': the line break marker stays last");
      Check(portuguese != english, "arcade quote '" + english + "' has no Portuguese text");
      for (const char c : portuguese) {
        Check(c == ' ' || (c >= 0x21 && c <= 0x5A), "arcade quote '" + portuguese + "' uses a character the arcade font cannot draw");
      }
    }
  }
  std::printf("runtime literals Base013: %zu translated, %zu canonical, %zu unreachable, 0 unknown\n",
              totals[0][LiteralClass::kTranslated], totals[0][LiteralClass::kCanonical], totals[0][LiteralClass::kUnreachable]);
  std::printf("runtime literals TU023: %zu translated, %zu canonical, %zu unreachable, 0 unknown\n",
              totals[1][LiteralClass::kTranslated], totals[1][LiteralClass::kCanonical], totals[1][LiteralClass::kUnreachable]);
  return 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Real game files

struct Fpg {
  std::string data;
  std::map<uint32_t, std::tuple<uint32_t, uint32_t, uint32_t>> entries;  // key -> offset, size, inflated size
};

uint32_t Le32(const std::string& s, size_t at) {
  return uint32_t(uint8_t(s[at])) | uint32_t(uint8_t(s[at + 1])) << 8 | uint32_t(uint8_t(s[at + 2])) << 16 |
         uint32_t(uint8_t(s[at + 3])) << 24;
}

std::optional<Fpg> OpenFpg() {
  std::ifstream in(kRoot / "game" / "extracted" / "flog_x360.fpg", std::ios::binary);
  if (!in) return std::nullopt;
  Fpg fpg;
  fpg.data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  if (fpg.data.size() < 0x808 || fpg.data.compare(0, 4, "30GF") != 0) return std::nullopt;
  const uint32_t count = Le32(fpg.data, 4);
  if (0x800 + size_t(count) * 16 > fpg.data.size()) return std::nullopt;
  for (uint32_t i = 0; i < count; ++i) {
    const size_t at = 0x800 + size_t(i) * 16;
    fpg.entries.emplace(Le32(fpg.data, at), std::make_tuple(Le32(fpg.data, at + 4), Le32(fpg.data, at + 8), Le32(fpg.data, at + 12)));
  }
  return fpg;
}

std::optional<std::string> Entry(const Fpg& fpg, uint32_t key) {
  const auto it = fpg.entries.find(key);
  if (it == fpg.entries.end()) return std::nullopt;
  const auto [offset, size, inflated] = it->second;
  if (size_t(offset) + size > fpg.data.size() || inflated > 64u * 1024 * 1024) return std::nullopt;
  std::string out(inflated, '\0');
  mz_ulong out_size = inflated;
  if (mz_uncompress(reinterpret_cast<unsigned char*>(out.data()), &out_size,
                    reinterpret_cast<const unsigned char*>(fpg.data.data()) + offset, size) != MZ_OK)
    return std::nullopt;
  out.resize(out_size);
  return out;
}

constexpr uint32_t kIdTableKey = 0x95154FCE;
constexpr uint32_t kEnglishTableKey = 0x91BAC9C9;

std::vector<std::string> Lines(const std::string& text) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    lines.push_back(text.substr(start, end - start));
    start = end + 1;
  }
  return lines;
}

uint32_t Crc(const std::string& text) { return ssf2t::localization::Crc32(text); }

// The code points a font file holds a glyph record for. Layout (little endian): byte 3 = atlas pages, u16 at +8 = glyph
// count, 64 u16 hash buckets at +12 indexed by code & 63, then 16-byte records starting with the u16 code at 140 + 4 * pages.
std::set<uint32_t> FontGlyphs(const std::string& font) {
  std::set<uint32_t> glyphs;
  if (font.size() < 144) return glyphs;
  const uint32_t pages = uint8_t(font[3]);
  const uint32_t count = uint8_t(font[8]) | uint32_t(uint8_t(font[9])) << 8;
  const size_t first = 140 + 4 * size_t(pages);
  for (uint32_t i = 0; i < count && first + size_t(i) * 16 + 2 <= font.size(); ++i) {
    const size_t at = first + size_t(i) * 16;
    const uint32_t code = uint8_t(font[at]) | uint32_t(uint8_t(font[at + 1])) << 8;
    if (code != 0) glyphs.insert(code);
  }
  return glyphs;
}

int TestRealGameFiles() {
  const std::optional<Fpg> fpg = OpenFpg();
  if (!fpg) {
    std::printf("game\\extracted\\flog_x360.fpg is not on this machine\n");
    return kFixtureMissing;
  }
  const auto ids_text = Entry(*fpg, kIdTableKey);
  const auto english_text = Entry(*fpg, kEnglishTableKey);
  Check(ids_text.has_value() && english_text.has_value(), "the string id table and the English table are in the archive");
  if (!ids_text || !english_text) return 1;
  const std::vector<std::string> ids = Lines(*ids_text);
  const std::vector<std::string> english = Lines(*english_text);
  Check(ids.size() == english.size() && english.size() == 1991, "the id table and the English table are line-aligned");

  const auto& table = ssf2t::localization::PortugueseBrazilGameText();
  std::map<uint32_t, const GameTextRow*> rows;
  for (const GameTextRow& row : table.rows) rows.emplace(row.line, &row);

  uint32_t required = 0;
  uint32_t translated = 0;
  uint32_t kept = 0;
  for (size_t line = 1; line < english.size(); ++line) {
    const std::string& en = english[line];
    const bool translatable = !en.empty() && en.compare(0, 2, "//") != 0;
    const auto found = rows.find(uint32_t(line));
    const std::string where = "line " + std::to_string(line) + " (" + (line < ids.size() ? ids[line] : "?") + ")";
    if (!translatable) {
      Check(found == rows.end(), where + ": a comment or blank line has a pt-BR row");
      continue;
    }
    ++required;
    if (found == rows.end()) {
      Check(false, where + ": no pt-BR row for '" + en.substr(0, 40) + "'");
      continue;
    }
    const GameTextRow& row = *found->second;
    Check(row.english_crc == Crc(en), where + ": the row was written for another English line (CRC)");
    Check(row.id == ids[line], where + ": the row id '" + row.id + "' differs from the game's id");
    if (row.keep_english) {
      ++kept;
      Check(KeptEnglishAllowed(row.id, en), where + ": kept in English but it reads as an English sentence: " + en.substr(0, 50));
      continue;
    }
    ++translated;
    // The English the player sees after the port's own menu text patches (Exit Game, Online Match, Find Match, lobby help).
    std::string patched = en + '\n';
    ssf2t::PatchMainMenuExitBuffer(patched.data(), patched.size());
    ssf2t::PatchOnlineMatchBuffer(patched.data(), patched.size());
    ssf2t::PatchFindMatchBuffer(patched.data(), patched.size());
    ssf2t::PatchGameLobbyHelpBuffer(patched.data(), patched.size());
    patched.pop_back();
    Check(Tokens(patched) == Tokens(row.text),
          where + ": placeholders differ. English " + Describe(Tokens(patched)) + "pt-BR " + Describe(Tokens(row.text)));
    Check(row.text != en, where + ": identical to English; use '=' when that is intended");
    const std::string& id = row.id;
    const bool title = id.size() > 6 && id.compare(id.size() - 6, 6, "_title") == 0;
    const bool message = (id.size() > 8 && id.compare(id.size() - 8, 8, "_message") == 0) || id.compare(0, 3, "em_") == 0;
    if (title) Check(row.text.size() <= 127, where + ": an error title is copied into a 128-byte guest buffer");
    if (message) Check(row.text.size() <= 511, where + ": an error message is copied into a 512-byte guest buffer");
    Check(row.text.size() <= 1024, where + ": longer than any line of the stock tables");
    const size_t limit = en.size() * 3 / 2 + 24;
    Check(row.text.size() <= limit, where + ": " + std::to_string(row.text.size()) + " bytes is far longer than the English " +
                                        std::to_string(en.size()));
  }
  Check(required == table.rows.size(), "every pt-BR row belongs to a translatable English line (" +
                                           std::to_string(table.rows.size()) + " rows, " + std::to_string(required) + " lines)");

  // The table the hook builds.
  std::string joined;
  for (size_t i = 0; i < english.size(); ++i) joined += english[i] + (i + 1 < english.size() ? "\n" : "");
  const auto built = ssf2t::localization::BuildLocalizedTable(*english_text, table.rows);
  Check(built.stale == 0, "no row is stale against the real English table");
  Check(built.replaced == translated && built.kept == kept, "the hook replaces exactly the translated rows");
  const std::vector<std::string> out = Lines(built.text);
  Check(out.size() == english.size(), "the pt-BR table keeps the English line count");
  Check(!out.empty() && out[0] == english[0], "the pt-BR table keeps the header line");
  Check(built.text.find('\0') == std::string::npos, "the pt-BR table holds no NUL");
  Check(ValidUtf8(built.text), "the pt-BR table is valid UTF-8");
  for (size_t line = 0; line < std::min(out.size(), english.size()); ++line) {
    const bool changed = rows.count(uint32_t(line)) && !rows[uint32_t(line)]->keep_english;
    if (!changed) Check(out[line] == english[line], "line " + std::to_string(line) + " must stay as the game has it");
  }

  // Glyphs: every character the table uses beyond ASCII must exist in every font the game draws text with.
  std::set<uint32_t> used;
  for (const GameTextRow& row : table.rows) {
    // A character the English line already draws behaves as in the stock game, whatever font it lands in.
    std::set<uint32_t> inherited;
    if (row.line < english.size()) {
      for (const uint32_t code : CodePoints(english[row.line])) inherited.insert(code);
    }
    for (const uint32_t code : CodePoints(row.text)) {
      if (code > 0x7E && !(code >= 0x80 && code <= 0x9F) && inherited.count(code) == 0) used.insert(code);
    }
  }
  int fonts = 0;
  for (const char* name : {"FONTS/HELVETICA55ROMANXBOX38.RF", "FONTS/HELVETICABOLDITALIC60.RF", "FONTS/HELVETICABOLD60.RF",
                           "FONTS/MONSTERMASHTITLE60.RF", "FONTS/CHARACTERMOVES.RF", "FONTS/HELVETICALIGHTXBOX38.RF"}) {
    const auto font = Entry(*fpg, ssf2t::localization::Crc32(name));
    Check(font.has_value(), std::string(name) + " is in the archive");
    if (!font) continue;
    ++fonts;
    const std::set<uint32_t> glyphs = FontGlyphs(*font);
    Check(glyphs.size() > 150 && glyphs.count('A') && glyphs.count(0xE3), std::string(name) + ": the glyph records were read");
    for (const uint32_t code : used) {
      char buffer[96];
      std::snprintf(buffer, sizeof(buffer), ": no glyph for U+%04X", code);
      Check(glyphs.count(code) != 0, std::string(name) + buffer);
    }
    for (uint32_t code = 0xC0; code <= 0xC3; ++code) {
      Check(glyphs.count(code) != 0, std::string(name) + ": the capital accents A with grave, acute, circumflex and tilde have glyphs");
    }
  }
  Check(fonts == 6, "all six text fonts were checked");
  // Baked textures against the real archive.
  {
    std::map<std::string, std::string> by_name;  // texture name -> file bytes
    for (const auto& entry : fpg->entries) {
      const auto bytes = Entry(*fpg, entry.first);
      if (!bytes || bytes->size() < 32 || bytes->compare(0, 8, "SG28XT03") != 0) continue;
      const auto name = ssf2t::localization::BakedTextureName(reinterpret_cast<const uint8_t*>(bytes->data()), bytes->size());
      by_name.emplace(std::string(name), *bytes);
    }
    for (const auto& row : ssf2t::localization::PortugueseBrazilBakedTextures()) {
      const auto found = by_name.find(row.name);
      Check(found != by_name.end(), std::string("baked texture ") + row.name + " is in the archive");
      if (found == by_name.end()) continue;
      std::string copy = found->second;
      Check(Crc(copy) == row.english_crc, std::string("baked texture ") + row.name + " was written for another stock texture (CRC)");
      Check(copy.size() == row.size, std::string("baked texture ") + row.name + " keeps the stock size");
      const auto* done = ssf2t::localization::LocalizeBakedTexture(reinterpret_cast<uint8_t*>(copy.data()), copy.size());
      Check(done != nullptr && copy != found->second, std::string("baked texture ") + row.name + " is replaced");
      Check(copy.compare(0, 45, found->second, 0, 45) == 0, std::string("baked texture ") + row.name + " keeps its header");
      std::string other = found->second;
      other[other.size() / 2] ^= 1;
      Check(ssf2t::localization::LocalizeBakedTexture(reinterpret_cast<uint8_t*>(other.data()), other.size()) == nullptr,
            std::string("baked texture ") + row.name + " is not applied to a different texture");
    }
    const auto stock = by_name.find("press_start");
    Check(stock != by_name.end() && Crc(stock->second) == 0x8A7D2A0Du && stock->second.size() == 49200u,
          "the stock PRESS START texture is in the archive");
    if (stock != by_name.end()) {
      std::string copy = stock->second;
      Check(ssf2t::localization::LocalizeBakedTexture(reinterpret_cast<uint8_t*>(copy.data()), copy.size()) == nullptr && copy == stock->second,
            "pt-BR leaves the stock PRESS START texture unchanged");
    }
    // Every text texture the sprite table can draw is redrawn or exempt with a reason.
    const auto replacements = Entry(*fpg, ssf2t::localization::Crc32("TEXTURES/REPLACEMENTS.TSV"));
    Check(replacements.has_value(), "textures/replacements.tsv is in the archive");
    int audited = 0;
    if (replacements) {
      for (const std::string& line : Lines(*replacements)) {
        if (line.empty() || line.compare(0, 2, "//") == 0) continue;
        const size_t tab = line.rfind('\t');
        if (tab == std::string::npos) continue;
        std::string file = line.substr(tab + 1);
        while (!file.empty() && (file.back() == '\r' || file.back() == ' ')) file.pop_back();
        if (file.empty() || file.compare(0, 12, "backgrounds\\") == 0 || file.compare(0, 12, "backgrounds/") == 0) continue;
        const size_t slash = file.find_last_of("\\/");
        const std::string leaf = slash == std::string::npos ? file : file.substr(slash + 1);
        ++audited;
        bool translated = false;
        for (const auto& row : ssf2t::localization::PortugueseBrazilBakedTextures()) translated |= leaf == row.name;
        bool kept = false;
        for (const KeptTexture& entry : kKeptTextures) kept |= Matches(leaf, entry.pattern);
        kept |= IsNumberedSprite(leaf);
        Check(translated || kept, "pt-BR runtime texture '" + leaf + "' has no Portuguese mapping or explicit exemption");
      }
    }
    Check(audited > 40, "the sprite replacement table was audited (" + std::to_string(audited) + " textures)");
  }
  std::printf("game table: %u translatable lines, %u translated, %u kept in English, %zu glyphs checked in %d fonts\n",
              required, translated, kept, used.size(), fonts);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string mode = argc > 1 ? argv[1] : "source";
  int code = 0;
  if (mode == "source") {
    TestLanguageRegistration();
    TestActiveLanguage();
    TestHooksOfBothBuilds();
    TestBuildSourcesOnBothPlatforms();
    TestEmbeddedSourcesAreUtf8();
    TestGameTableShape();
    TestUiTableShape();
    TestTrCallsHaveRows();
    TestBakedTextureRows();
  } else if (mode == "game") {
    code = TestRealGameFiles();
  } else if (mode == "runtime") {
    code = TestRuntimeLiterals();
  } else if (mode == "uses") {
    return PrintUses(argc, argv);
  } else if (mode == "dump-literals") {
    return DumpGuestLiterals();
  } else {
    std::printf("usage: ssf2t_localization_tests source|game\n");
    return 2;
  }
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  if (code == kFixtureMissing) return code;
  return g_failures == 0 && code == 0 ? 0 : 1;
}
