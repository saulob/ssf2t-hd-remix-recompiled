#include "localization/ui_text.h"

#include <atomic>
#include <unordered_map>

#include "language_setting.h"
#include "localization/active_language.h"
#include "localization/game_text.h"

namespace ssf2t {
namespace localization {

extern const char kPtBrUiTextTsv[];

namespace {

std::atomic<bool> g_portuguese_brazil{false};

bool DecodeUi(std::string_view escaped, std::string& out) {
  std::string flattened;
  flattened.reserve(escaped.size());
  for (size_t i = 0; i < escaped.size(); ++i) {
    if (escaped[i] == '\\' && i + 1 < escaped.size() && escaped[i + 1] == 'n') {
      flattened.push_back('\n');
      ++i;
    } else if (escaped[i] == '\\' && i + 1 < escaped.size()) {
      flattened.push_back(escaped[i]);
      flattened.push_back(escaped[++i]);
    } else {
      flattened.push_back(escaped[i]);
    }
  }
  std::string decoded;
  if (!DecodeText(flattened, decoded)) return false;
  out = std::move(decoded);
  return true;
}

}  // namespace

void SetActiveLanguage(std::string_view key) { g_portuguese_brazil = key == kPortugueseBrazilKey; }

bool PortugueseBrazilSelected() { return g_portuguese_brazil; }

UiTextTable ParseUiTextTable(std::string_view tsv) {
  UiTextTable table;
  size_t start = 0;
  uint32_t number = 0;
  while (start < tsv.size()) {
    size_t end = tsv.find('\n', start);
    if (end == std::string_view::npos) end = tsv.size();
    const std::string_view line = tsv.substr(start, end - start);
    start = end + 1;
    ++number;
    if (line.empty()) continue;
    const size_t tab = line.find('\t');
    UiTextRow row;
    if (tab == std::string_view::npos || line.find('\t', tab + 1) != std::string_view::npos ||
        !DecodeUi(line.substr(0, tab), row.english) || !DecodeUi(line.substr(tab + 1), row.text)) {
      table.errors.push_back("row " + std::to_string(number) + ": expected english and text columns");
      continue;
    }
    table.rows.push_back(std::move(row));
  }
  return table;
}

const UiTextTable& PortugueseBrazilUiText() {
  static const UiTextTable table = ParseUiTextTable(kPtBrUiTextTsv);
  return table;
}

}  // namespace localization

namespace {

const std::unordered_map<std::string_view, const std::string*>& PortugueseBrazilIndex() {
  static const auto index = [] {
    std::unordered_map<std::string_view, const std::string*> map;
    for (const localization::UiTextRow& row : localization::PortugueseBrazilUiText().rows) {
      map.emplace(row.english, &row.text);
    }
    return map;
  }();
  return index;
}

}  // namespace

const char* Tr(const char* english) {
  if (english == nullptr || !localization::PortugueseBrazilSelected()) return english;
  const auto& index = PortugueseBrazilIndex();
  const auto found = index.find(english);
  return found == index.end() ? english : found->second->c_str();
}

std::string Tr(const std::string& english) {
  if (!localization::PortugueseBrazilSelected()) return english;
  const auto& index = PortugueseBrazilIndex();
  const auto found = index.find(english);
  return found == index.end() ? english : *found->second;
}

}  // namespace ssf2t
