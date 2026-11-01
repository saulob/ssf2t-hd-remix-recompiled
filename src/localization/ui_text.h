#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace ssf2t {

// The text of the active language for an English UI string. English text with no translation is returned as is, so a
// missing entry never shows an empty or foreign string. The result outlives the call.
const char* Tr(const char* english);
std::string Tr(const std::string& english);

namespace localization {

struct UiTextRow {
  std::string english;
  std::string text;
};

struct UiTextTable {
  std::vector<UiTextRow> rows;
  std::vector<std::string> errors;
};

// Rows are `english<TAB>text`; both columns use the escapes of DecodeText plus `\n`.
UiTextTable ParseUiTextTable(std::string_view tsv);

const UiTextTable& PortugueseBrazilUiText();

}  // namespace localization
}  // namespace ssf2t
