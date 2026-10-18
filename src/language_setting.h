#pragma once

#include <cstdint>
#include <string_view>

namespace ssf2t {

inline constexpr char kLanguageSetting[] = "hcu_language";
inline constexpr char kDefaultLanguage[] = "en-US";
inline constexpr char kLanguageLabel[] = "Language";
inline constexpr char kLanguageHelp[] =
    "The language of the game's text. Applied when the game starts.";

// The Xbox 360 XC_LANGUAGE value the title reads through ExGetXConfigSetting (the SDK's user_language cvar).
struct GameLanguage {
  const char* key;
  const char* label;
  uint32_t xconfig_language;
};

// The title maps only these five XC_LANGUAGE values to its string tables; every other value gives English.
inline constexpr GameLanguage kGameLanguages[] = {
    {"en-US", "English", 1},
    {"fr-FR", "French", 4},
    {"de-DE", "German", 3},
    {"it-IT", "Italian", 6},
    {"es-ES", "Spanish", 5},
};

// An unknown value selects English.
constexpr const GameLanguage& LanguageFor(std::string_view key) {
  for (const GameLanguage& language : kGameLanguages) {
    if (key == language.key) {
      return language;
    }
  }
  return kGameLanguages[0];
}

}  // namespace ssf2t
