#include "trg/settings.h"

#include <cstdio>
#include <cstdlib>
#include <set>

#include "trg/platform.h"

namespace trg {
namespace {

std::string Trim(std::string_view s) {
  const size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string_view::npos) return {};
  const size_t b = s.find_last_not_of(" \t\r\n");
  return std::string(s.substr(a, b - a + 1));
}

bool ReadLines(const std::string& path, std::vector<std::string>& out) {
  out.clear();
  FILE* f = OpenFile(path, "rb");
  if (!f) return false;
  std::string data;
  char buf[8192];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) data.append(buf, n);
  fclose(f);
  size_t start = 0;
  if (data.compare(0, 3, "\xEF\xBB\xBF") == 0) start = 3;  // UTF-8 BOM
  while (start < data.size()) {
    size_t nl = data.find('\n', start);
    if (nl == std::string::npos) nl = data.size();
    std::string line = data.substr(start, nl - start);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    out.push_back(std::move(line));
    start = nl + 1;
  }
  return true;
}

// Written to a temporary file first so a crash never leaves half a file.
bool WriteLines(const std::string& path, const std::vector<std::string>& lines) {
  const std::string tmp = path + ".tmp";
  FILE* f = OpenFile(tmp, "wb");
  if (!f) return false;
  bool ok = true;
  for (const std::string& l : lines) ok = ok && fprintf(f, "%s\n", l.c_str()) >= 0;
  ok = fclose(f) == 0 && ok;
  if (!ok) {
    RemoveFile(tmp);
    return false;
  }
  return MoveFileOver(tmp, path);
}

bool IsTrue(const std::string& v) { return v == "1" || v == "on" || v == "yes" || v == "true"; }
bool IsFalse(const std::string& v) { return v == "0" || v == "off" || v == "no" || v == "false"; }

}  // namespace

// ------------------------------------------------------------ SettingsStore --
bool SettingsStore::GetBool(std::string_view key, bool def) const {
  const std::string v = Get(key);
  if (v == true_value || IsTrue(v)) return true;
  if (v == false_value || IsFalse(v)) return false;
  return def;
}

int SettingsStore::GetInt(std::string_view key, int def) const {
  const std::string v = Get(key);
  if (v.empty()) return def;
  char* end = nullptr;
  const long n = std::strtol(v.c_str(), &end, 10);
  return end == v.c_str() ? def : int(n);
}

float SettingsStore::GetFloat(std::string_view key, float def) const {
  const std::string v = Get(key);
  if (v.empty()) return def;
  char* end = nullptr;
  const float f = std::strtof(v.c_str(), &end);
  return end == v.c_str() ? def : f;
}

void SettingsStore::SetFloat(std::string_view key, float v, int decimals) {
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.*f", decimals, double(v));
  Set(key, buf);
}

bool ParseSettingLine(std::string_view line, bool allow_comment, std::string& key, std::string& value) {
  std::string s = Trim(line);
  if (allow_comment && !s.empty() && s[0] == '#')
    s = Trim(std::string_view(s).substr(1));
  else if (s.empty() || s[0] == '#')
    return false;
  const size_t eq = s.find('=');
  if (eq == std::string::npos) return false;
  key = Trim(std::string_view(s).substr(0, eq));
  if (key.empty() || key.find_first_of(" \t") != std::string::npos) return false;
  value = Trim(std::string_view(s).substr(eq + 1));
  if (const size_t c = value.find(" #"); c != std::string::npos) value = Trim(std::string_view(value).substr(0, c));
  return true;
}

// --------------------------------------------------------- TextFileSettings --
void TextFileSettings::Load() {
  values_.clear();
  cleared_.clear();
  ReadLines(path_, lines_);
  for (const std::string& l : lines_) {
    std::string k, v;
    if (ParseSettingLine(l, false, k, v)) values_[k] = v;
  }
}

std::string TextFileSettings::Get(std::string_view key, std::string_view def) const {
  const auto it = values_.find(key);
  return it == values_.end() || it->second.empty() ? std::string(def) : it->second;
}

void TextFileSettings::Set(std::string_view key, std::string value) { values_[std::string(key)] = std::move(value); }

void TextFileSettings::ResetToDefaults() {
  for (auto& [k, v] : values_) cleared_.push_back(k);
  values_.clear();
}

bool TextFileSettings::Save() {
  std::set<std::string> done;
  std::set<std::string> cleared(cleared_.begin(), cleared_.end());
  for (std::string& l : lines_) {  // active lines first
    std::string k, v;
    if (!ParseSettingLine(l, false, k, v) || done.count(k)) continue;
    if (const auto it = values_.find(k); it != values_.end()) {
      if (v != it->second) l = k + " = " + it->second;
      done.insert(k);
    } else if (cleared.count(k)) {
      l = "# " + Trim(l);  // back to its default
    }
  }
  for (std::string& l : lines_) {  // then the commented examples
    std::string k, v;
    if (Trim(l).rfind('#', 0) != 0 || !ParseSettingLine(l, true, k, v) || done.count(k)) continue;
    if (const auto it = values_.find(k); it != values_.end()) {
      l = k + " = " + it->second;
      done.insert(k);
    }
  }
  bool header = false;
  for (const auto& [k, v] : values_) {
    if (done.count(k)) continue;
    if (!header) {
      if (!lines_.empty() && !Trim(lines_.back()).empty()) lines_.push_back("");
      lines_.push_back("# Set from the launcher");
      header = true;
    }
    lines_.push_back(k + " = " + v);
  }
  cleared_.clear();
  return WriteLines(path_, lines_);
}

// --------------------------------------------------------- CallbackSettings --
std::string CallbackSettings::Get(std::string_view key, std::string_view def) const {
  std::string v = get ? get(key) : std::string();
  return v.empty() ? std::string(def) : v;
}

void CallbackSettings::Set(std::string_view key, std::string value) {
  if (set) set(key, value);
}

// ----------------------------------------------------------- MemorySettings --
std::string MemorySettings::Get(std::string_view key, std::string_view def) const {
  const auto it = values_.find(key);
  return it == values_.end() || it->second.empty() ? std::string(def) : it->second;
}

}  // namespace trg
