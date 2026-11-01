#include "localization/game_text.h"

#include <array>
#include <charconv>
#include <map>

namespace ssf2t::localization {

extern const char kPtBrGameTextTsv[];

namespace {

constexpr std::array<uint32_t, 256> MakeCrcTable() {
  std::array<uint32_t, 256> table{};
  for (uint32_t i = 0; i < 256; ++i) {
    uint32_t value = i;
    for (int bit = 0; bit < 8; ++bit) {
      value = (value & 1) ? (value >> 1) ^ 0xEDB88320u : value >> 1;
    }
    table[i] = value;
  }
  return table;
}

int HexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::vector<std::string_view> SplitTabs(std::string_view line) {
  std::vector<std::string_view> fields;
  size_t start = 0;
  for (;;) {
    const size_t tab = line.find('\t', start);
    if (tab == std::string_view::npos || fields.size() == 3) {
      fields.push_back(line.substr(start));
      return fields;
    }
    fields.push_back(line.substr(start, tab - start));
    start = tab + 1;
  }
}

}  // namespace

uint32_t Crc32(std::string_view bytes) {
  static constexpr std::array<uint32_t, 256> table = MakeCrcTable();
  uint32_t crc = 0xFFFFFFFFu;
  for (const char c : bytes) {
    crc = table[(crc ^ static_cast<uint8_t>(c)) & 0xFF] ^ (crc >> 8);
  }
  return ~crc;
}

bool DecodeText(std::string_view escaped, std::string& out) {
  out.clear();
  for (size_t i = 0; i < escaped.size(); ++i) {
    if (escaped[i] != '\\') {
      out.push_back(escaped[i]);
      continue;
    }
    if (++i >= escaped.size()) return false;
    switch (escaped[i]) {
      case '\\':
        out.push_back('\\');
        break;
      case 't':
        out.push_back('\t');
        break;
      case 'x': {
        if (i + 2 >= escaped.size()) return false;
        const int high = HexDigit(escaped[i + 1]);
        const int low = HexDigit(escaped[i + 2]);
        if (high < 8 || high > 9 || low < 0) return false;
        out.push_back(static_cast<char>(0xC2));
        out.push_back(static_cast<char>(high * 16 + low));
        i += 2;
        break;
      }
      default:
        return false;
    }
  }
  return true;
}

GameTextTable ParseGameTextTable(std::string_view tsv) {
  GameTextTable table;
  size_t start = 0;
  uint32_t number = 0;
  while (start < tsv.size()) {
    size_t end = tsv.find('\n', start);
    if (end == std::string_view::npos) end = tsv.size();
    const std::string_view line = tsv.substr(start, end - start);
    start = end + 1;
    ++number;
    if (line.empty()) continue;
    const std::vector<std::string_view> fields = SplitTabs(line);
    GameTextRow row;
    const auto fail = [&](const char* why) {
      table.errors.push_back("row " + std::to_string(number) + ": " + why);
    };
    if (fields.size() != 4) {
      fail("expected line, id, crc and text");
      continue;
    }
    const auto [line_end, line_ec] = std::from_chars(fields[0].data(), fields[0].data() + fields[0].size(), row.line);
    if (line_ec != std::errc() || line_end != fields[0].data() + fields[0].size()) {
      fail("bad line number");
      continue;
    }
    const auto [crc_end, crc_ec] =
        std::from_chars(fields[2].data(), fields[2].data() + fields[2].size(), row.english_crc, 16);
    if (crc_ec != std::errc() || crc_end != fields[2].data() + fields[2].size()) {
      fail("bad crc");
      continue;
    }
    row.id = std::string(fields[1]);
    if (fields[3] == "=") {
      row.keep_english = true;
    } else if (!DecodeText(fields[3], row.text)) {
      fail("bad escape in text");
      continue;
    }
    table.rows.push_back(std::move(row));
  }
  return table;
}

LocalizedTable BuildLocalizedTable(std::string_view english, const std::vector<GameTextRow>& rows) {
  std::map<uint32_t, const GameTextRow*> by_line;
  for (const GameTextRow& row : rows) {
    by_line.emplace(row.line, &row);
  }
  LocalizedTable result;
  result.text.reserve(english.size() + english.size() / 4);
  size_t start = 0;
  uint32_t line_number = 0;
  while (start < english.size()) {
    size_t end = english.find('\n', start);
    const bool has_newline = end != std::string_view::npos;
    if (!has_newline) end = english.size();
    const std::string_view line = english.substr(start, end - start);
    const auto found = by_line.find(line_number);
    if (found == by_line.end()) {
      result.text.append(line);
    } else if (found->second->english_crc != Crc32(line)) {
      ++result.stale;
      result.text.append(line);
    } else if (found->second->keep_english) {
      ++result.kept;
      result.text.append(line);
    } else {
      ++result.replaced;
      result.text.append(found->second->text);
    }
    if (has_newline) result.text.push_back('\n');
    start = end + 1;
    ++line_number;
  }
  return result;
}

const GameTextTable& PortugueseBrazilGameText() {
  static const GameTextTable table = ParseGameTextTable(kPtBrGameTextTsv);
  return table;
}

}  // namespace ssf2t::localization
