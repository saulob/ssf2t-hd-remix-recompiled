#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ssf2t::localization {

// One row of a translated game string table: the line of the English table it replaces, the CRC-32 of that English line
// (a row whose CRC no longer matches is not applied), the string id for reading, and the text. `keep_english` is a row
// written as `=`.
struct GameTextRow {
  uint32_t line = 0;
  uint32_t english_crc = 0;
  std::string id;
  std::string text;
  bool keep_english = false;
};

struct GameTextTable {
  std::vector<GameTextRow> rows;
  std::vector<std::string> errors;
};

struct LocalizedTable {
  std::string text;
  uint32_t replaced = 0;
  uint32_t kept = 0;
  uint32_t stale = 0;
};

uint32_t Crc32(std::string_view bytes);

// Decodes a text column: `\xNN` (a button glyph, U+0080..U+009F, stored as UTF-8), `\\` and `\t`. False on any other escape.
bool DecodeText(std::string_view escaped, std::string& out);

// Rows are `line<TAB>id<TAB>crc32 hex<TAB>text`, one per LF-terminated line; blank lines are ignored.
GameTextTable ParseGameTextTable(std::string_view tsv);

// The English table with each matching row's line replaced; every other byte is kept. Rows are applied by line number.
LocalizedTable BuildLocalizedTable(std::string_view english, const std::vector<GameTextRow>& rows);

// The embedded Português Brasil rows (parsed once).
const GameTextTable& PortugueseBrazilGameText();

}  // namespace ssf2t::localization
