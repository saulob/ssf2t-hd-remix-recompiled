// Source scanning shared by the parity suites: the per-build traits structs, the guest hooks and the expected-code
// fingerprints, read from production source text, and the recompiled function tables of generated\<v>.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunused-function"
#endif

#ifndef SSF2T_SOURCE_DIR
#error SSF2T_SOURCE_DIR must name the repository root
#endif

namespace fs = std::filesystem;

namespace parity {
namespace {

std::string Hex(uint32_t value) {
  char text[16];
  std::snprintf(text, sizeof(text), "0x%08X", value);
  return text;
}

fs::path Root() { return fs::path(SSF2T_SOURCE_DIR); }

std::string ReadText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  std::string out = text.str();
  std::erase(out, '\r');
  return out;
}

std::vector<uint8_t> ReadBytes(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::vector<fs::path> SourceFiles() {
  std::vector<fs::path> files;
  for (const auto& entry : fs::recursive_directory_iterator(Root() / "src")) {
    const std::string ext = entry.path().extension().string();
    if (entry.is_regular_file() && (ext == ".h" || ext == ".cpp")) files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());
  return files;
}

std::string Relative(const fs::path& path) { return fs::relative(path, Root()).generic_string(); }

size_t MatchingBrace(const std::string& text, size_t open) {
  int depth = 0;
  for (size_t i = open; i < text.size(); ++i) {
    if (text[i] == '{') ++depth;
    if (text[i] == '}' && --depth == 0) return i;
  }
  return std::string::npos;
}

std::string Trim(std::string text) {
  const size_t first = text.find_first_not_of(" \t\n");
  const size_t last = text.find_last_not_of(" \t\n");
  return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
}

// ---------------------------------------------------------------------------------------------------------------------
// Per-build traits structs: struct Base013<X> { static constexpr ... } and struct TitleUpdate023<X>.

struct Member {
  std::string type;
  std::string value;  // the initializer, braces included for arrays
  bool array = false;
};

struct Traits {
  std::string file;
  std::string name;
  std::map<std::string, Member> members;
};

std::map<std::string, Member> ParseMembers(const std::string& body) {
  std::map<std::string, Member> members;
  const std::string marker = "static constexpr ";
  for (size_t at = body.find(marker); at != std::string::npos; at = body.find(marker, at + 1)) {
    const size_t equals = body.find('=', at);
    const size_t semicolon = body.find(';', at);
    if (equals == std::string::npos || semicolon < equals) continue;
    std::string declaration = Trim(body.substr(at + marker.size(), equals - at - marker.size()));
    Member member;
    if (declaration.ends_with("[]")) {
      member.array = true;
      declaration = Trim(declaration.substr(0, declaration.size() - 2));
    }
    const size_t space = declaration.find_last_of(' ');
    if (space == std::string::npos) continue;
    member.type = Trim(declaration.substr(0, space));
    const std::string name = declaration.substr(space + 1);
    size_t end = semicolon;
    if (member.array) {
      const size_t open = body.find('{', equals);
      end = MatchingBrace(body, open) + 1;
    }
    member.value = Trim(body.substr(equals + 1, end - equals - 1));
    members[name] = member;
  }
  return members;
}

std::optional<std::string> StructBody(const std::string& text, const std::string& name) {
  for (size_t at = text.find("struct " + name); at != std::string::npos; at = text.find("struct " + name, at + 1)) {
    const size_t after = at + 7 + name.size();
    if (after < text.size() && (std::isalnum(uint8_t(text[after])) || text[after] == '_')) continue;
    const size_t open = text.find('{', after);
    if (open == std::string::npos || text.find(';', after) < open) continue;
    return text.substr(open + 1, MatchingBrace(text, open) - open - 1);
  }
  return std::nullopt;
}

struct TraitsPair {
  Traits base;
  Traits update;
  bool has_update = false;
};

std::vector<TraitsPair> FindTraitsPairs() {
  std::vector<TraitsPair> pairs;
  const std::regex declaration(R"(struct (Base013\w*)\b[^;{]*\{)");
  for (const fs::path& path : SourceFiles()) {
    const std::string text = ReadText(path);
    for (std::sregex_iterator it(text.begin(), text.end(), declaration), end; it != end; ++it) {
      TraitsPair pair;
      const std::string name = (*it)[1];
      const std::string other = "TitleUpdate023" + name.substr(7);
      pair.base = {Relative(path), name, ParseMembers(*StructBody(text, name))};
      if (const auto body = StructBody(text, other)) {
        pair.update = {Relative(path), other, ParseMembers(*body)};
        pair.has_update = true;
      }
      pairs.push_back(std::move(pair));
    }
  }
  return pairs;
}

std::optional<uint32_t> HexValue(const std::string& text) {
  if (!text.starts_with("0x") && !text.starts_with("0X")) return std::nullopt;
  return uint32_t(std::stoul(text, nullptr, 16));
}

constexpr uint32_t kGuestImageBase = 0x82000000;

bool IsGuestAddress(uint32_t value) { return value >= kGuestImageBase && value < 0x90000000; }

// The guest-address scalars of one traits struct.
std::map<std::string, uint32_t> Addresses(const Traits& traits) {
  std::map<std::string, uint32_t> out;
  for (const auto& [name, member] : traits.members) {
    if (member.array || member.type != "uint32_t") continue;
    if (const auto value = HexValue(member.value); value && IsGuestAddress(*value)) out[name] = *value;
  }
  return out;
}

// File-level `inline constexpr uint32_t kName = 0x...;` constants, shared by both builds.
std::map<std::string, uint32_t> SharedConstants(const std::string& text) {
  std::map<std::string, uint32_t> out;
  const std::regex constant(R"(\ninline constexpr uint32_t (k\w+) = (0x[0-9A-Fa-f]+);)");
  for (std::sregex_iterator it(text.begin(), text.end(), constant), end; it != end; ++it) {
    out[(*it)[1]] = uint32_t(std::stoul((*it)[2], nullptr, 16));
  }
  return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// Hooks: REX_HOOK_RAW(<prefix>_sub_<address>) and SSF2T_NET_HOOK(<prefix>, <address>, Traits, handler).

struct Hook {
  std::string file;
  bool update = false;
  uint32_t address = 0;
  std::string body;  // with every build-specific name replaced, so both builds' hooks compare equal
  size_t offset = 0;
  std::string build() const { return update ? "tu023" : "base013"; }
};

std::string Normalize(std::string body) {
  static const std::regex sub(R"((base013|tu023)_sub_[0-9A-F]{8})");
  static const std::regex traits(R"(\b(Base013|TitleUpdate023)(\w*))");
  static const std::regex alias(R"(\b(Base|Update)::)");
  static const std::regex space(R"(\s+)");
  body = std::regex_replace(body, sub, "sub");
  body = std::regex_replace(body, traits, "G$2");
  body = std::regex_replace(body, alias, "T::");
  return std::regex_replace(body, space, " ");
}

std::vector<std::pair<size_t, size_t>> TitleUpdateRegions(const std::string& text) {
  std::vector<std::pair<size_t, size_t>> regions;
  const std::string open = "#if SSF2T_TITLE_UPDATE_IMAGE";
  for (size_t at = text.find(open); at != std::string::npos; at = text.find(open, at + 1)) {
    int depth = 0;
    for (size_t line = at; line != std::string::npos && line < text.size(); line = text.find('\n', line + 1)) {
      const size_t start = text[line] == '\n' ? line + 1 : line;
      if (text.compare(start, 3, "#if") == 0) ++depth;
      if (text.compare(start, 6, "#endif") == 0 && --depth == 0) {
        regions.emplace_back(at, start);
        break;
      }
    }
  }
  return regions;
}

std::vector<Hook> FindHooks() {
  std::vector<Hook> hooks;
  for (const fs::path& path : SourceFiles()) {
    if (path.extension() != ".cpp") continue;
    const std::string text = ReadText(path);
    const std::string raw = "REX_HOOK_RAW(";
    for (size_t at = text.find(raw); at != std::string::npos; at = text.find(raw, at + 1)) {
      const size_t close = text.find(')', at);
      const std::string symbol = text.substr(at + raw.size(), close - at - raw.size());
      const bool base = symbol.starts_with("base013_sub_");
      const bool update = symbol.starts_with("tu023_sub_");
      if (!base && !update) continue;
      const size_t open = text.find('{', close);
      Hook hook{Relative(path), update, uint32_t(std::stoul(symbol.substr(symbol.size() - 8), nullptr, 16)),
                Normalize(text.substr(open, MatchingBrace(text, open) - open + 1)), at};
      hooks.push_back(hook);
    }
    const std::regex net(R"(\nSSF2T_NET_HOOK\((base013|tu023), ([0-9A-F]{8}), (\w+), (\w+)\))");
    for (std::sregex_iterator it(text.begin(), text.end(), net), end; it != end; ++it) {
      Hook hook{Relative(path), (*it)[1] == "tu023", uint32_t(std::stoul((*it)[2], nullptr, 16)),
                Normalize("T::" + std::string((*it)[4])), size_t(it->position())};
      hooks.push_back(hook);
    }
  }
  return hooks;
}
std::vector<uint32_t> ArrayWords(const std::string& initializer, const std::map<std::string, uint32_t>& names,
                                 bool& resolved) {
  std::vector<uint32_t> words;
  resolved = true;
  std::string inner = initializer.substr(1, initializer.size() - 2);
  std::stringstream items(inner);
  for (std::string item; std::getline(items, item, ',');) {
    item = Trim(item);
    if (item.empty()) continue;
    if (const auto value = HexValue(item)) {
      words.push_back(*value);
    } else if (const auto named = names.find(item); named != names.end()) {
      words.push_back(named->second);
    } else {
      resolved = false;
    }
  }
  return words;
}

struct Fingerprint {
  std::string name;
  uint32_t address;
  std::vector<uint32_t> words;
};

// Every `kXWords[]` of a traits struct whose `kX` is an address of the struct or of its file.
std::vector<Fingerprint> Fingerprints(const Traits& traits, std::vector<std::string>* unresolved = nullptr) {
  std::map<std::string, uint32_t> names = SharedConstants(ReadText(Root() / traits.file));
  for (const auto& [name, value] : Addresses(traits)) names[name] = value;
  std::vector<Fingerprint> out;
  for (const auto& [name, member] : traits.members) {
    if (!member.array || member.type != "uint32_t" || !name.ends_with("Words")) continue;
    const auto address = names.find(name.substr(0, name.size() - 5));
    if (address == names.end()) continue;
    bool resolved = false;
    std::vector<uint32_t> words = ArrayWords(member.value, names, resolved);
    if (!resolved && unresolved) unresolved->push_back(traits.name + "::" + name);
    out.push_back({name, address->second, std::move(words)});
  }
  return out;
}


std::vector<uint32_t> LoadGeneratedFunctions(const fs::path& init, const std::string& prefix) {
  std::vector<uint32_t> functions;
  const std::string table = ReadText(init);
  const std::string marker = "{ 0x";
  for (size_t at = table.find(marker); at != std::string::npos; at = table.find(marker, at + 1)) {
    if (table.compare(at + 12, prefix.size() + 2, ", " + prefix) != 0) continue;
    functions.push_back(uint32_t(std::stoul(table.substr(at + 4, 8), nullptr, 16)));
  }
  std::sort(functions.begin(), functions.end());
  return functions;
}

}  // namespace
}  // namespace parity
