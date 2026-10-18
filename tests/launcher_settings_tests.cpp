// Launcher setting tests: the Presentation segments and the Language table. Pure logic, no window or game files.
// Run: ssf2t_launcher_settings_tests

#include <cstdint>
#include <cstdio>
#include <iterator>
#include <set>
#include <string_view>

#include "language_setting.h"
#include "presentation_setting.h"

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

void TestPresentationUsesSdkCvar() {
  CHECK(std::string_view(kLetterboxSetting) == "present_letterbox");
}

void TestPresentationSegments() {
  CHECK(LetterboxSegment(true) == 0);
  CHECK(LetterboxSegment(false) == 1);
  CHECK(LetterboxFromSegment(LetterboxSegment(true)));
  CHECK(!LetterboxFromSegment(LetterboxSegment(false)));
}

void TestLanguageTable() {
  CHECK(std::size(kGameLanguages) == 5);
  CHECK(std::string_view(kLanguageSetting) == "ssf2t_language");
  CHECK(std::string_view(kGameLanguages[0].key) == kDefaultLanguage);
  std::set<std::string_view> keys;
  std::set<uint32_t> xconfig;
  for (const GameLanguage& language : kGameLanguages) {
    keys.insert(language.key);
    xconfig.insert(language.xconfig_language);
  }
  CHECK(keys.size() == std::size(kGameLanguages));
  CHECK(xconfig.size() == std::size(kGameLanguages));
}

// The title maps XC_LANGUAGE 1, 3, 4, 5 and 6 to English, German, French, Spanish and Italian.
void TestLanguageXConfigValues() {
  CHECK(LanguageFor("en-US").xconfig_language == 1);
  CHECK(LanguageFor("de-DE").xconfig_language == 3);
  CHECK(LanguageFor("fr-FR").xconfig_language == 4);
  CHECK(LanguageFor("es-ES").xconfig_language == 5);
  CHECK(LanguageFor("it-IT").xconfig_language == 6);
}

void TestLanguageFallback() {
  CHECK(LanguageFor("").xconfig_language == 1);
  CHECK(LanguageFor("ja-JP").xconfig_language == 1);
  CHECK(LanguageFor("fr").xconfig_language == 1);
  CHECK(std::string_view(LanguageFor("xx").key) == kDefaultLanguage);
}

}  // namespace

int main() {
  TestPresentationUsesSdkCvar();
  TestPresentationSegments();
  TestLanguageTable();
  TestLanguageXConfigValues();
  TestLanguageFallback();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
