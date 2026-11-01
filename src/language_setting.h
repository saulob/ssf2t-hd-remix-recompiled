#pragma once

#include <cstdint>
#include <string_view>

namespace ssf2t {

inline constexpr char kLanguageSetting[] = "ssf2t_language";
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

inline constexpr char kPortugueseBrazilKey[] = "pt-BR";

// The title maps only five XC_LANGUAGE values to its string tables; every other value gives English. Português Brasil has
// no table of its own: it runs on the English slot and the port replaces the text.
inline constexpr GameLanguage kGameLanguages[] = {
    {"en-US", "English", 1},
    {"fr-FR", "French", 4},
    {"de-DE", "German", 3},
    {"it-IT", "Italian", 6},
    {"es-ES", "Spanish", 5},
    {kPortugueseBrazilKey, "Portugu\xC3\xAA" "s Brasil", 1},
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
