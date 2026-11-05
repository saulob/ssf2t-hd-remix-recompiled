// Global feature parity: every supported game build and platform exposes the whole feature set.
//
// The feature set is the union of what the product implements anywhere (src/feature_parity.h, checked against
// production source from both directions), and every target (platform x game build) must satisfy all of it.
//
//   ssf2t_global_feature_parity_tests static   source, CMake, settings, hotkeys, tables and hooks of every target
//   ssf2t_global_feature_parity_tests images   the same tables and hooks against the real game images
//   ssf2t_global_feature_parity_tests          both
//
// The images mode exits 77 when the real images (game\analysis, generated\<v>) are not on this machine.

#include <array>
#include <cstring>
#include <functional>
#include <iterator>
#include <string_view>

#include "feature_parity.h"
#include "parity_scan.h"

namespace sp = ssf2t::parity;
using namespace parity;

namespace {

constexpr int kFixtureMissing = 77;
constexpr size_t kBuildCount = std::size(sp::kGuestBuilds);
constexpr size_t kPlatformCount = std::size(sp::kPlatforms);
constexpr size_t kTargetCount = kBuildCount * kPlatformCount;
constexpr size_t kFeatureCount = std::size(sp::kFeatures);
constexpr const char* kBannedReasons[] = {"todo", "not implemented", "not tested", "unfinished", "yet", "later", "tbd"};

size_t Target(size_t platform, size_t build) { return platform * kBuildCount + build; }

std::string PlatformLabel(size_t platform) {
  std::string label = sp::kPlatforms[platform];
  label[0] = char(std::toupper(uint8_t(label[0])));
  return label;
}

std::string TargetLabel(size_t target) {
  return PlatformLabel(target / kBuildCount) + " " + sp::kGuestBuilds[target % kBuildCount].label;
}

std::vector<std::string> Split(std::string_view list) {
  std::vector<std::string> out;
  size_t at = 0;
  while (at <= list.size()) {
    size_t comma = list.find(',', at);
    if (comma == std::string_view::npos) comma = list.size();
    const std::string item = Trim(std::string(list.substr(at, comma - at)));
    if (!item.empty()) out.push_back(item);
    at = comma + 1;
  }
  return out;
}

bool Has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

std::string Lower(std::string text) {
  for (char& c : text) c = char(std::tolower(uint8_t(c)));
  return text;
}

// ---------------------------------------------------------------------------------------------------------------------
// Verdicts: every failure names the feature and the targets it breaks.

struct Failure {
  size_t feature;
  std::vector<bool> targets;
  std::string what;
};

std::vector<Failure> g_failures;
std::vector<std::string> g_global_failures;
int g_checks = 0;

void FailTargets(size_t feature, const std::vector<bool>& targets, const std::string& what) {
  g_failures.push_back({feature, targets, what});
}

void FailBuild(size_t feature, size_t build, const std::string& what) {
  std::vector<bool> mask(kTargetCount);
  for (size_t p = 0; p < kPlatformCount; ++p) mask[Target(p, build)] = true;
  FailTargets(feature, mask, what);
}

void FailPlatform(size_t feature, size_t platform, const std::string& what) {
  std::vector<bool> mask(kTargetCount);
  for (size_t b = 0; b < kBuildCount; ++b) mask[Target(platform, b)] = true;
  FailTargets(feature, mask, what);
}

void FailFeature(size_t feature, const std::string& what) {
  FailTargets(feature, std::vector<bool>(kTargetCount, true), what);
}

void FailGlobal(const std::string& what) { g_global_failures.push_back(what); }

void Expect(bool ok, size_t feature, const std::string& what) {
  ++g_checks;
  if (!ok) FailFeature(feature, what);
}

// ---------------------------------------------------------------------------------------------------------------------
// Platform regions of a source file.

enum class Region { kCommon, kWindows, kLinux };

std::vector<Region> RegionsOf(const std::string& text, const std::string& file) {
  const std::string name = Lower(fs::path(file).filename().string());
  std::optional<Region> whole;
  if (Has(name, "_win")) whole = Region::kWindows;
  if (Has(name, "_posix") || Has(name, "_sdl")) whole = Region::kLinux;
  struct Branch {
    bool platform;
    bool windows_in_if;
    bool in_else;
  };
  std::vector<Branch> stack;
  std::vector<Region> regions;
  std::istringstream lines(text);
  for (std::string line; std::getline(lines, line);) {
    const std::string trimmed = Trim(line);
    if (trimmed.starts_with("#")) {
      const std::string directive = Trim(trimmed.substr(1));
      if (directive.starts_with("ifdef") || directive.starts_with("ifndef") || directive.starts_with("if")) {
        const bool platform = Has(directive, "_WIN32");
        const bool negated = directive.starts_with("ifndef") || Has(directive, "!defined(_WIN32)");
        stack.push_back({platform, !negated, false});
      } else if (directive.starts_with("else") && !stack.empty()) {
        stack.back().in_else = true;
      } else if (directive.starts_with("endif") && !stack.empty()) {
        stack.pop_back();
      }
    }
    Region region = Region::kCommon;
    for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
      if (it->platform) {
        region = (it->windows_in_if != it->in_else) ? Region::kWindows : Region::kLinux;
        break;
      }
    }
    regions.push_back(whole.value_or(region));
  }
  return regions;
}

// The text a platform compiles: its own branches and the common lines.
std::string PlatformText(const std::string& text, const std::string& file, size_t platform) {
  const std::vector<Region> regions = RegionsOf(text, file);
  const Region mine = platform == 0 ? Region::kWindows : Region::kLinux;
  std::string out;
  std::istringstream lines(text);
  size_t n = 0;
  for (std::string line; std::getline(lines, line); ++n) {
    if (regions[n] == Region::kCommon || regions[n] == mine) out += line + "\n";
  }
  return out;
}

std::optional<std::string> FunctionBody(const std::string& text, const std::string& name) {
  static const std::regex after(R"(^\s*(const\s*)?(noexcept\s*)?(override\s*)?(final\s*)?\{)");
  for (size_t at = text.find(name); at != std::string::npos; at = text.find(name, at + 1)) {
    if (at > 0 && (std::isalnum(uint8_t(text[at - 1])) || text[at - 1] == '_' || text[at - 1] == '.' ||
                   text[at - 1] == '>')) {
      continue;
    }
    size_t open = at + name.size();
    while (open < text.size() && std::isspace(uint8_t(text[open]))) ++open;
    if (open >= text.size() || text[open] != '(') continue;
    int depth = 0;
    size_t close = open;
    for (; close < text.size(); ++close) {
      if (text[close] == '(') ++depth;
      if (text[close] == ')' && --depth == 0) break;
    }
    if (close >= text.size()) continue;
    const std::string tail = text.substr(close + 1, 96);
    std::smatch match;
    if (!std::regex_search(tail, match, after)) continue;
    const size_t brace = close + 1 + size_t(match.length()) - 1;
    const size_t end = MatchingBrace(text, brace);
    if (end == std::string::npos) continue;
    return text.substr(brace + 1, end - brace - 1);
  }
  return std::nullopt;
}

bool IsStubBody(std::string body) {
  static const std::regex line_comment(R"(//[^\n]*)");
  static const std::regex block_comment(R"(/\*[\s\S]*?\*/)");
  static const std::regex space(R"(\s+)");
  body = std::regex_replace(body, block_comment, "");
  body = std::regex_replace(body, line_comment, "");
  body = std::regex_replace(body, space, "");
  if (body.empty()) return true;
  static const std::regex early_return(R"re(^return(false|nullptr|0|-1|\{\})?;)re");
  if (std::regex_search(body, early_return)) return true;
  const size_t statements = size_t(std::count(body.begin(), body.end(), ';'));
  const size_t last = body.rfind("return");
  if (last == std::string::npos || statements > 3) return false;
  const std::string value = body.substr(last + 6, body.find(';', last) - last - 6);
  return value.empty() || value == "false" || value == "nullptr" || value == "0" || value == "-1" || value == "{}" ||
         value == "std::nullopt" || value == "std::string()";
}

// ---------------------------------------------------------------------------------------------------------------------
// CMake: which source files each platform builds into the program.

struct CmakeSources {
  std::set<std::string> common, windows, linux_files;

  bool Compiled(size_t platform, const std::string& file) const {
    return common.contains(file) || (platform == 0 ? windows : linux_files).contains(file);
  }

  std::set<std::string> All() const {
    std::set<std::string> all = common;
    all.insert(windows.begin(), windows.end());
    all.insert(linux_files.begin(), linux_files.end());
    return all;
  }
};

CmakeSources ParseCmake() {
  CmakeSources sources;
  const std::regex token(R"(src/[A-Za-z0-9_./-]+\.(?:cpp|c)\b)");
  std::vector<char> stack;
  std::istringstream in(ReadText(Root() / "CMakeLists.txt"));
  bool collecting = false;
  int depth = 0;
  char region = 'C';
  for (std::string line; std::getline(in, line);) {
    const std::string t = Trim(line);
    if (!collecting) {
      if (t.starts_with("if(")) stack.push_back(t == "if(WIN32)" ? 'W' : (t == "if(NOT WIN32)" ? 'L' : 'N'));
      else if (t.starts_with("foreach(") || t.starts_with("function(") || t.starts_with("macro(") ||
               t.starts_with("while(")) stack.push_back('N');
      else if (t == "else()" && !stack.empty()) stack.back() = stack.back() == 'W' ? 'L' : (stack.back() == 'L' ? 'W' : 'N');
      else if ((t == "endif()" || t.starts_with("endforeach(") || t.starts_with("endfunction(") ||
                t.starts_with("endmacro(") || t.starts_with("endwhile(")) && !stack.empty()) stack.pop_back();
      const bool starts = (t.starts_with("set(SSF2T_") && Has(t, "SOURCES")) ||
                          (t.starts_with("list(APPEND SSF2T_") && Has(t, "SOURCES")) ||
                          t.starts_with("add_executable(ssf2t_hd_remix") || t.starts_with("add_executable(trg-updater");
      if (starts) {
        collecting = true;
        depth = 0;
        region = 'C';
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
          if (*it != 'N') {
            region = *it;
            break;
          }
        }
        if (t.starts_with("add_executable(trg-updater")) region = 'W';
      }
    }
    if (collecting) {
      for (std::sregex_iterator it(t.begin(), t.end(), token), end; it != end; ++it) {
        (region == 'W' ? sources.windows : (region == 'L' ? sources.linux_files : sources.common)).insert(it->str());
      }
      depth += int(std::count(t.begin(), t.end(), '(')) - int(std::count(t.begin(), t.end(), ')'));
      if (depth <= 0) collecting = false;
    }
  }
  return sources;
}

bool IsCompiledSource(const std::string& file) { return file.ends_with(".cpp") || file.ends_with(".c"); }

// ---------------------------------------------------------------------------------------------------------------------
// The per-build tables found in production source.

struct BuildTables {
  std::map<std::string, Traits> structs;  // short name -> traits of this build
  std::map<std::string, uint32_t> addresses;  // "Struct::kMember" -> guest address
};

std::vector<BuildTables> ScanBuildTables() {
  std::vector<BuildTables> tables(kBuildCount);
  for (const fs::path& path : SourceFiles()) {
    const std::string text = ReadText(path);
    for (size_t b = 0; b < kBuildCount; ++b) {
      const std::regex declaration("struct (" + std::string(sp::kGuestBuilds[b].traits) + R"()(\w*)\b[^;{]*\{)");
      for (std::sregex_iterator it(text.begin(), text.end(), declaration), end; it != end; ++it) {
        const std::string short_name = (*it)[2];
        if (short_name.empty()) continue;
        const std::string full_name = (*it)[1].str() + short_name;
        Traits traits{Relative(path), full_name, ParseMembers(*StructBody(text, full_name))};
        for (const auto& [name, value] : Addresses(traits)) tables[b].addresses[short_name + "::" + name] = value;
        tables[b].structs[short_name] = std::move(traits);
      }
    }
  }
  return tables;
}

// ---------------------------------------------------------------------------------------------------------------------

struct Context {
  CmakeSources cmake;
  std::vector<BuildTables> tables;
  std::vector<Hook> hooks;
  std::string settings_cpp;
  std::string game_setting_keys;
};

// The values a setting token stands for on each platform.
std::vector<std::string> SettingValues(const Context&, const std::string& token, size_t platform) {
  static std::map<std::string, std::array<std::string, 2>> constants;
  if (constants.empty()) {
    const std::regex constant(R"re(constexpr char (k\w+Setting)\[\] = "([^"]*)";)re");
    for (const fs::path& path : SourceFiles()) {
      const std::string text = ReadText(path);
      const std::vector<Region> regions = RegionsOf(text, Relative(path));
      std::istringstream lines(text);
      size_t n = 0;
      for (std::string line; std::getline(lines, line); ++n) {
        std::smatch match;
        if (!std::regex_search(line, match, constant)) continue;
        auto& values = constants[match[1]];
        if (regions[n] != Region::kLinux) values[0] = match[2];
        if (regions[n] != Region::kWindows) values[1] = match[2];
      }
    }
  }
  if (token.size() > 1 && token[0] == 'k' && std::isupper(uint8_t(token[1]))) {
    const auto found = constants.find(token);
    if (found == constants.end() || found->second[platform].empty()) return {};
    return {found->second[platform]};
  }
  return {token};
}

// Settings the runtime patches add to the SDK; a patch for the other platform does not count.
bool PatchDefines(const std::string& key, size_t platform) {
  static std::array<std::string, 2> patches;
  if (patches[platform].empty()) {
    const char* other = platform == 0 ? ".linux.patch" : ".windows.patch";
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(Root() / "patches/rexglue-sdk", ec)) {
      const std::string name = entry.path().filename().string();
      if (entry.path().extension() == ".patch" && !name.ends_with(other)) patches[platform] += ReadText(entry.path());
    }
    if (patches[platform].empty()) patches[platform] = " ";
  }
  return std::regex_search(patches[platform],
                           std::regex(R"re(REXCVAR_(DEFINE|DECLARE)\w*\(\s*(\w+,\s*)?)re" + key + R"re(\b)re"));
}

bool SdkDeclares(const std::string& key, size_t platform) {
  static std::array<std::string, 2> headers;
  if (headers[platform].empty()) {
    const fs::path include = Root() / "tools/rexglue" / (platform == 0 ? "win-amd64" : "linux-amd64") / "include";
    std::error_code ec;
    if (!fs::exists(include, ec)) return true;  // the SDK is not unpacked on this machine
    for (const auto& entry : fs::recursive_directory_iterator(include, ec)) {
      if (entry.is_regular_file() && entry.path().filename() == "flags.h") headers[platform] += ReadText(entry.path());
    }
    if (headers[platform].empty()) headers[platform] = " ";
  }
  if (headers[platform] == " ") return true;
  return std::regex_search(headers[platform], std::regex("REXCVAR_DECLARE\\([A-Za-z0-9_:<>]+, " + key + "\\)"));
}

// ---------------------------------------------------------------------------------------------------------------------

void CheckRegistryShape() {
  std::set<std::string> names;
  for (size_t f = 0; f < kFeatureCount; ++f) {
    const sp::Feature& feature = sp::kFeatures[f];
    ++g_checks;
    if (!names.insert(feature.name).second) FailGlobal(std::string("feature ") + feature.name + " is listed twice");
    const bool windows_side = *feature.windows_sources || *feature.windows_functions || *feature.windows_settings;
    const bool linux_side = *feature.linux_sources || *feature.linux_functions || *feature.linux_settings;
    bool excused[kPlatformCount] = {false, false};
    for (const sp::Exception& e : sp::kExceptions) {
      if (std::string(e.feature) == feature.name) excused[std::string(e.target) == "windows" ? 1 : 0] = true;
    }
    if (windows_side && !linux_side && !excused[0]) {
      FailFeature(f, "Windows has platform-specific parts but Linux has no counterpart listed or excused");
    }
    if (linux_side && !windows_side && !excused[1]) {
      FailFeature(f, "Linux has platform-specific parts but Windows has no counterpart listed or excused");
    }
    if (*feature.windows_settings || *feature.linux_settings) {
      Expect(*feature.windows_settings && *feature.linux_settings, f,
             "the backend setting is listed for one platform only");
    }
  }
  for (const sp::Exception& e : sp::kExceptions) {
    ++g_checks;
    const std::string reason = Lower(e.reason);
    bool known = false;
    for (const sp::Feature& feature : sp::kFeatures) known = known || std::string(feature.name) == e.feature;
    if (!known) FailGlobal(std::string("exception names unknown feature ") + e.feature);
    if (reason.size() < 40) FailGlobal(std::string("exception for ") + e.feature + " has no technical reason");
    for (const char* banned : kBannedReasons) {
      if (Has(" " + reason + " ", std::string(" ") + banned + " ")) {
        FailGlobal(std::string("exception for ") + e.feature + " is unfinished work (\"" + banned + "\"), not a technical reason");
      }
    }
    if (!fs::exists(Root() / e.source)) FailGlobal(std::string("exception for ") + e.feature + " cites missing " + e.source);
    else if (!Has(ReadText(Root() / e.source), "_WIN32")) FailGlobal(std::string("exception for ") + e.feature + " cites " + e.source + ", which has no platform conditional");
    if (std::string(e.target) != "windows" && std::string(e.target) != "linux") {
      FailGlobal(std::string("exception for ") + e.feature + " names no platform");
    }
  }
  for (const auto& list : {std::pair{sp::kGateReviews, std::size(sp::kGateReviews)},
                           std::pair{sp::kAvailabilityMessages, std::size(sp::kAvailabilityMessages)}}) {
    for (size_t i = 0; i < list.second; ++i) {
      ++g_checks;
      const sp::GateReview& review = list.first[i];
      if (!fs::exists(Root() / review.file)) FailGlobal(std::string("review entry for missing file ") + review.file);
      if (std::string(review.reason).size() < 20) FailGlobal(std::string("review entry ") + review.file + " has no reason");
    }
  }
}

void CheckBuildEnumeration(const Context& context) {
  std::set<std::string> dirs, ids, traits;
  for (const sp::GuestBuild& build : sp::kGuestBuilds) {
    dirs.insert(build.codegen_dir);
    ids.insert(build.id);
    traits.insert(build.traits);
  }
  std::set<std::string> cmake_dirs;
  const std::string images = ReadText(Root() / "cmake/ssf2t_guest_images.cmake");
  const std::regex add(R"(ssf2t_add_guest_image\(\w+ (\w+)\))");
  for (std::sregex_iterator it(images.begin(), images.end(), add), end; it != end; ++it) cmake_dirs.insert((*it)[1]);
  ++g_checks;
  if (cmake_dirs != dirs) FailGlobal("the game images built by cmake/ssf2t_guest_images.cmake are not the builds listed in src/feature_parity.h");

  std::set<std::string> hook_builds;
  for (const Hook& hook : context.hooks) hook_builds.insert(hook.build());
  ++g_checks;
  if (hook_builds != ids) FailGlobal("the builds that have guest hooks are not the builds listed in src/feature_parity.h");

  std::set<std::string> names;
  const std::regex name(R"re(kName = "(\w+)";)re");
  for (const fs::path& path : SourceFiles()) {
    const std::string text = ReadText(path);
    for (std::sregex_iterator it(text.begin(), text.end(), name), end; it != end; ++it) names.insert((*it)[1]);
  }
  ++g_checks;
  if (names != ids) FailGlobal("the build names in the per-build tables are not the builds listed in src/feature_parity.h");

  const std::string files = ReadText(Root() / "src/game_files.h");
  std::smatch version;
  ++g_checks;
  if (!std::regex_search(files, version, std::regex(R"(enum class GuestVersion \{([^}]*)\})")) ||
      Split(version[1].str()).size() != kBuildCount) {
    FailGlobal("GuestVersion has a different number of builds than src/feature_parity.h");
  }
  const std::string packet = ReadText(Root() / "src/netplay/packet.h");
  std::smatch guest;
  ++g_checks;
  if (!std::regex_search(packet, guest, std::regex(R"(enum class GuestId : uint8_t \{([^}]*)\})")) ||
      Split(guest[1].str()).size() != kBuildCount) {
    FailGlobal("the netplay GuestId has a different number of builds than src/feature_parity.h");
  }
  for (size_t b = 0; b < kBuildCount; ++b) {
    ++g_checks;
    if (!fs::exists(Root() / "config/guest" / sp::kGuestBuilds[b].codegen_dir / "manifest.template.toml")) {
      FailGlobal(std::string(sp::kGuestBuilds[b].label) + " has no code generation config");
    }
  }
}

void CheckGuestTables(const Context& context) {
  std::set<std::string> claimed;
  for (size_t f = 0; f < kFeatureCount; ++f) {
    for (const std::string& member : Split(sp::kFeatures[f].guest)) {
      ++g_checks;
      if (!claimed.insert(member).second) FailGlobal(member + " is claimed by two features");
      for (size_t b = 0; b < kBuildCount; ++b) {
        const BuildTables& tables = context.tables[b];
        const auto found = tables.addresses.find(member);
        std::string others;
        for (size_t o = 0; o < kBuildCount; ++o) {
          if (o != b && context.tables[o].addresses.contains(member)) others += std::string(" ") + sp::kGuestBuilds[o].label;
        }
        if (found == tables.addresses.end()) {
          FailBuild(f, b, member + " exists in" + (others.empty() ? " no build" : others) + " but " +
                              sp::kGuestBuilds[b].label + " has no table entry");
        } else if (found->second == 0) {
          FailBuild(f, b, member + " is zero for " + sp::kGuestBuilds[b].label + " (a placeholder address)");
        }
      }
    }
    for (const std::string& hooked : Split(sp::kFeatures[f].hooked)) {
      ++g_checks;
      const auto list = Split(sp::kFeatures[f].guest);
      if (std::find(list.begin(), list.end(), hooked) == list.end()) FailFeature(f, hooked + " is hooked but not listed in guest");
    }
  }
  for (size_t b = 0; b < kBuildCount; ++b) {
    for (const auto& [member, address] : context.tables[b].addresses) {
      ++g_checks;
      if (!claimed.contains(member)) {
        FailGlobal(std::string(sp::kGuestBuilds[b].label) + " table entry " + member + " (" + Hex(address) +
                   ") belongs to no feature");
      }
    }
  }
}

void CheckGuestHooks(const Context& context) {
  std::vector<std::set<uint32_t>> hooked_addresses(kBuildCount);
  for (size_t f = 0; f < kFeatureCount; ++f) {
    for (const std::string& member : Split(sp::kFeatures[f].hooked)) {
      std::optional<std::string> reference_body;
      std::string reference_label;
      for (size_t b = 0; b < kBuildCount; ++b) {
        const auto address = context.tables[b].addresses.find(member);
        if (address == context.tables[b].addresses.end()) continue;
        hooked_addresses[b].insert(address->second);
        ++g_checks;
        const Hook* found = nullptr;
        for (const Hook& hook : context.hooks) {
          if (hook.build() == sp::kGuestBuilds[b].id && hook.address == address->second) found = &hook;
        }
        if (!found) {
          FailBuild(f, b, "no hook for " + member + " at " + Hex(address->second) + " in " + sp::kGuestBuilds[b].label);
          continue;
        }
        if (!reference_body) {
          reference_body = found->body;
          reference_label = sp::kGuestBuilds[b].label;
        } else if (found->body != *reference_body) {
          FailBuild(f, b, "the hook for " + member + " runs a different handler than " + reference_label + "'s");
        }
      }
    }
  }
  for (const Hook& hook : context.hooks) {
    ++g_checks;
    size_t build = 0;
    for (size_t b = 0; b < kBuildCount; ++b) {
      if (hook.build() == sp::kGuestBuilds[b].id) build = b;
    }
    if (!hooked_addresses[build].contains(hook.address)) {
      FailGlobal(std::string(sp::kGuestBuilds[build].label) + " hook at " + Hex(hook.address) + " in " + hook.file +
                 " belongs to no feature");
    }
  }
}

// The text of the launcher's page functions: what the player can see and change.
std::string PageBodies(const std::string& launcher) {
  std::string pages;
  const std::regex page(R"re(void Page\w+\(trg::Ui& ui\) \{)re");
  for (std::sregex_iterator it(launcher.begin(), launcher.end(), page), end; it != end; ++it) {
    const size_t open = size_t(it->position()) + size_t(it->length()) - 1;
    pages += launcher.substr(open, MatchingBrace(launcher, open) - open + 1);
  }
  return pages;
}

void CheckSettings(const Context& context) {
  std::string src_text;
  for (const fs::path& path : SourceFiles()) {
    if (path.filename() != "feature_parity.h") src_text += ReadText(path) + "\n";
  }
  std::string ui_text = PageBodies(ReadText(Root() / "src/launcher.cpp")) + ReadText(Root() / "src/online_page.cpp");
  if (Has(ui_text, "kPlayerNames")) ui_text += ReadText(Root() / "src/player_name_setting.h");
  const std::string toml = ReadText(Root() / "config/ssf2t_hd_remix.toml");
  const auto applied = [&](const std::string& key) {
    return Has(src_text, "REXCVAR_GET(" + key + ")") || Has(src_text, "\"" + key + "\"");
  };

  std::set<std::string> claimed_keys;
  for (size_t f = 0; f < kFeatureCount; ++f) {
    const sp::Feature& feature = sp::kFeatures[f];
    Expect(!*feature.settings || *feature.rows, f, "it has settings but no launcher row is listed for them");
    for (const std::string& row : Split(feature.rows)) {
      const bool constant = row[0] == 'k' && row.size() > 1 && std::isupper(uint8_t(row[1]));
      const std::string first_argument = constant ? "(?:\\w+::)*" + row : "\"" + row + "\"";
      const std::regex control("ui\\s*\\.\\s*(Toggle|Combo|Choice|Row|Slider|Field|InputText|Section)\\(\\s*" + first_argument + "\\s*,");
      Expect(std::regex_search(ui_text, control), f, "the launcher has no row \"" + row + "\" (hidden or removed)");
    }
    for (const std::string& token : Split(feature.settings)) {
      const bool constant = token[0] == 'k' && std::isupper(uint8_t(token[1]));
      claimed_keys.insert(token);
      for (size_t p = 0; p < kPlatformCount; ++p) {
        for (const std::string& value : SettingValues(context, token, p)) claimed_keys.insert(value);
      }
      ++g_checks;
      const bool in_keys = constant ? Has(context.game_setting_keys, token) : Has(context.game_setting_keys, "\"" + token + "\"");
      if (!in_keys) FailFeature(f, "setting " + token + " is not saved by the launcher (missing from GameSettingKeys)");
      for (size_t p = 0; p < kPlatformCount; ++p) {
        const std::vector<std::string> values = SettingValues(context, token, p);
        if (values.empty()) {
          FailPlatform(f, p, "setting " + token + " has no value on " + PlatformLabel(p));
          continue;
        }
        for (const std::string& key : values) {
          const bool project = std::regex_search(src_text, std::regex("REXCVAR_DEFINE_\\w+\\(\\s*" + key + "\\s*,"));
          const bool sdk = SdkDeclares(key, p) || PatchDefines(key, p);
          const bool configured = std::regex_search(toml, std::regex("(^|\\n)" + key + " *="));
          ++g_checks;
          if (!project && !sdk && !configured) {
            FailPlatform(f, p, "setting " + key + " is defined nowhere for " + PlatformLabel(p));
            continue;
          }
          if (project && !applied(key)) FailFeature(f, "setting " + key + " is defined but never applied by the program");
        }
      }
    }
    for (const std::string& token : Split(feature.internal)) {
      claimed_keys.insert(token);
      const std::vector<std::string> values = SettingValues(context, token, 0);
      for (const std::string& key : values) {
        claimed_keys.insert(key);
        ++g_checks;
        if (!std::regex_search(src_text, std::regex("REXCVAR_DEFINE_\\w+\\(\\s*" + key + "\\s*,"))) {
          FailFeature(f, "internal setting " + key + " is not defined");
        } else if (!applied(key)) {
          FailFeature(f, "internal setting " + key + " is defined but never applied by the program");
        }
      }
    }
    for (size_t p = 0; p < kPlatformCount; ++p) {
      const char* key = p == 0 ? feature.windows_settings : feature.linux_settings;
      if (!*key) continue;
      claimed_keys.insert(key);
      ++g_checks;
      if (!SdkDeclares(key, p)) FailPlatform(f, p, std::string("the backend setting ") + key + " does not exist in the " + PlatformLabel(p) + " runtime");
      bool used = false;
      for (const fs::path& path : SourceFiles()) {
        if (path.filename() == "feature_parity.h") continue;
        if (Has(PlatformText(ReadText(path), Relative(path), p), std::string("\"") + key + "\"")) used = true;
      }
      if (!used) FailPlatform(f, p, std::string("the backend setting ") + key + " is never set on " + PlatformLabel(p));
    }
  }

  const std::regex define(R"(REXCVAR_DEFINE_\w+\(\s*(\w+)\s*,)");
  for (std::sregex_iterator it(src_text.begin(), src_text.end(), define), end; it != end; ++it) {
    ++g_checks;
    if (!claimed_keys.contains((*it)[1])) FailGlobal("setting " + (*it)[1].str() + " is defined but belongs to no feature");
  }
  // The launcher saves exactly the settings the features list.
  std::set<std::string> by_value;
  for (const std::string& token : claimed_keys) {
    for (size_t p = 0; p < kPlatformCount; ++p) {
      for (const std::string& value : SettingValues(context, token, p)) by_value.insert(value);
    }
  }
  const std::regex quoted(R"re("([a-z0-9_]+)")re");
  const std::regex constants(R"(\b(k[A-Z]\w+Setting)\b)");
  const std::string& keys = context.game_setting_keys;
  const auto saved_known = [&](const std::string& key) {
    return by_value.contains(key) || key.starts_with("keybind_");
  };
  for (std::sregex_iterator it(keys.begin(), keys.end(), quoted), end; it != end; ++it) {
    ++g_checks;
    if (!saved_known((*it)[1])) FailGlobal("the launcher saves setting " + (*it)[1].str() + " but no feature lists it");
  }
  for (std::sregex_iterator it(keys.begin(), keys.end(), constants), end; it != end; ++it) {
    ++g_checks;
    if (!claimed_keys.contains((*it)[1])) FailGlobal("the launcher saves " + (*it)[1].str() + " but no feature lists it");
  }
}

void CheckHotkeys() {
  const std::string keybinds = ReadText(Root() / "src/keybinds.h");
  const size_t reserved_at = keybinds.find("inline bool KeyboardBindingIsReserved(");
  const std::string reserved_body = keybinds.substr(reserved_at, keybinds.find("\n}\n", reserved_at) - reserved_at);
  std::set<std::string> reserved;
  const std::regex key_case(R"(case ImGuiKey_(F\d+):)");
  for (std::sregex_iterator it(reserved_body.begin(), reserved_body.end(), key_case), end; it != end; ++it) reserved.insert((*it)[1]);

  std::set<std::string> registered;
  for (size_t f = 0; f < kFeatureCount; ++f) {
    for (const std::string& entry : Split(sp::kFeatures[f].hotkeys)) {
      const size_t equals = entry.find('=');
      const std::string key = entry.substr(0, equals);
      const std::string owner = equals == std::string::npos ? "" : entry.substr(equals + 1);
      registered.insert(key);
      ++g_checks;
      Expect(reserved.contains(key), f, "hotkey " + key + " can still be bound to a game button (not reserved)");
      if (owner == "sdk") {
        Expect(Has(ReadText(Root() / "docs/save-and-achievements.md"), key + " opens"), f,
               "hotkey " + key + " is bound by the runtime but not documented");
        continue;
      }
      std::string text = ReadText(Root() / owner);
      const fs::path header = fs::path(owner).replace_extension(".h");
      if (fs::exists(Root() / header)) text += ReadText(Root() / header);
      Expect(Has(text, "VirtualKey::k" + key) || Has(text, "ImGuiKey_" + key) || Has(text, "\"" + key + "\""), f,
             "hotkey " + key + " has no listener in " + owner);
    }
  }
  for (const std::string& key : reserved) {
    ++g_checks;
    const bool unbound_by_design = key == "F4" || key == "F9" || key == "F12";
    if (!registered.contains(key) && !unbound_by_design) FailGlobal("hotkey " + key + " is reserved but belongs to no feature");
  }
  const std::regex listener(R"re(VirtualKey::k(F\d+)\b|\[\] = "(F\d+)")re");
  for (const fs::path& path : SourceFiles()) {
    if (path.filename() == "keybinds.h") continue;
    const std::string text = ReadText(path);
    for (std::sregex_iterator it(text.begin(), text.end(), listener), end; it != end; ++it) {
      const std::string key = (*it)[1].matched ? (*it)[1].str() : (*it)[2].str();
      ++g_checks;
      // DEBUG_SAULO: temporary keys that exist for one debug module and are removed with it at final review.
      if (path.filename() == "evil_ryu_debug_keys.cpp" || path.filename() == "ultra_arcade_debug_keys.cpp") continue;
      if (!registered.contains(key)) FailGlobal("hotkey " + key + " has a listener in " + Relative(path) + " but belongs to no feature");
    }
  }
}

void CheckSourcesAndFunctions(const Context& context) {
  std::set<std::string> claimed;
  for (size_t f = 0; f < kFeatureCount; ++f) {
    const sp::Feature& feature = sp::kFeatures[f];
    const char* lists[] = {feature.sources, feature.windows_sources, feature.linux_sources};
    for (size_t kind = 0; kind < 3; ++kind) {
      for (const std::string& file : Split(lists[kind])) {
        claimed.insert(file);
        ++g_checks;
        if (!fs::exists(Root() / file)) {
          FailFeature(f, "source " + file + " does not exist");
          continue;
        }
        if (!IsCompiledSource(file)) continue;
        for (size_t p = 0; p < kPlatformCount; ++p) {
          const bool needed = kind == 0 || (kind == 1 && p == 0) || (kind == 2 && p == 1);
          if (needed && !context.cmake.Compiled(p, file)) {
            FailPlatform(f, p, file + " is not built on " + PlatformLabel(p));
          }
        }
      }
    }
    for (size_t p = 0; p < kPlatformCount; ++p) {
      for (const std::string& entry : Split(p == 0 ? feature.windows_functions : feature.linux_functions)) {
        ++g_checks;
        const size_t split = entry.find(".cpp:");
        const std::string file = entry.substr(0, split + 4);
        const std::string name = entry.substr(split + 5);
        if (!fs::exists(Root() / file)) {
          FailPlatform(f, p, "the " + PlatformLabel(p) + " implementation file " + file + " does not exist");
          continue;
        }
        const std::optional<std::string> body = FunctionBody(PlatformText(ReadText(Root() / file), file, p), name);
        if (!body) {
          FailPlatform(f, p, name + " has no " + PlatformLabel(p) + " implementation in " + file);
        } else if (IsStubBody(*body)) {
          FailPlatform(f, p, name + " is a stub on " + PlatformLabel(p) + " (" + file + ")");
        }
      }
    }
  }
  for (const sp::GateReview& review : sp::kGateReviews) claimed.insert(review.file);
  for (const sp::GateReview& support : sp::kSupportFiles) claimed.insert(support.file);
  for (const std::string& file : context.cmake.All()) {
    ++g_checks;
    if (!claimed.contains(file)) FailGlobal(file + " is built into the program but belongs to no feature");
  }
}

void CheckGatesAreReviewed() {
  std::set<std::string> known;
  for (const sp::Feature& feature : sp::kFeatures) {
    for (const char* list : {feature.sources, feature.windows_sources, feature.linux_sources}) {
      for (const std::string& file : Split(list)) known.insert(file);
    }
  }
  std::set<std::string> reviewed;
  for (const sp::GateReview& review : sp::kGateReviews) reviewed.insert(review.file);
  const std::regex gate(R"(_WIN32|__linux__|__unix__|GuestVersion::|kTitleUpdateImageBuilt|SSF2T_TITLE_UPDATE_IMAGE|\bkBase013\b|\bkTitleUpdate023\b)");
  std::set<std::string> with_gates;
  for (const fs::path& path : SourceFiles()) {
    const std::string file = Relative(path);
    if (file == "src/feature_parity.h" || Has(file, "third_party")) continue;
    if (std::regex_search(ReadText(path), gate)) with_gates.insert(file);
  }
  for (const std::string& file : with_gates) {
    ++g_checks;
    if (!known.contains(file) && !reviewed.contains(file)) {
      FailGlobal(file + " has platform or game version conditionals and is reviewed by no feature or gate review");
    }
  }
  for (const std::string& file : reviewed) {
    ++g_checks;
    if (!with_gates.contains(file)) FailGlobal("gate review for " + file + " is stale: it has no platform or game version conditional");
  }
  const std::regex phrase(R"(not available|unavailable|unsupported|not supported|only available|only used on|disabled for)", std::regex::icase);
  std::set<std::string> allowed;
  for (const sp::GateReview& review : sp::kAvailabilityMessages) allowed.insert(review.file);
  std::set<std::string> speaking;
  for (const fs::path& path : SourceFiles()) {
    const std::string file = Relative(path);
    if (file == "src/feature_parity.h") continue;
    std::istringstream lines(ReadText(path));
    for (std::string line; std::getline(lines, line);) {
      const std::string t = Trim(line);
      if (t.starts_with("//") || t.find('"') == std::string::npos) continue;
      if (std::regex_search(t, phrase)) speaking.insert(file);
    }
  }
  for (const std::string& file : speaking) {
    ++g_checks;
    if (!allowed.contains(file)) FailGlobal(file + " tells the player something is unavailable or unsupported and is reviewed by no feature");
  }
  for (const std::string& file : allowed) {
    ++g_checks;
    if (!speaking.contains(file)) FailGlobal("availability review for " + file + " is stale: it says nothing is unavailable");
  }
}

void CheckTests() {
  const std::string ps1 = ReadText(Root() / "tools/test.ps1");
  const std::string sh = ReadText(Root() / "tools/test.sh");
  std::set<std::string> listed;
  for (size_t f = 0; f < kFeatureCount; ++f) {
    Expect(*sp::kFeatures[f].tests != '\0', f, "no test proves it");
    for (const std::string& test : Split(sp::kFeatures[f].tests)) {
      listed.insert(test);
      ++g_checks;
      if (!fs::exists(Root() / test)) {
        FailFeature(f, "test " + test + " does not exist");
        continue;
      }
      std::string suite = fs::path(test).stem().string();
      if (suite.ends_with("_tests")) suite.resize(suite.size() - 6);
      const bool linux_only = test == "tests/linux_appimage_tests.cpp" || test == "tests/online_websocket_tls_tests.cpp" ||
                             test == "tests/clipboard_sdl_tests.cpp";
      if (!linux_only && !Has(ps1, "'" + suite + "'") && !Has(ps1, "'" + suite + " ")) FailFeature(f, "test " + test + " is not run by tools/test.ps1");
      if (!Has(sh, "ssf2t_" + suite + "_tests")) FailFeature(f, "test " + test + " is not run by tools/test.sh");
    }
  }
  const std::string self = "global_feature_parity";
  ++g_checks;
  if (!Has(ps1, "'" + self + " ") && !Has(ps1, "'" + self + "'")) FailGlobal("tools/test.ps1 does not run the global feature parity test");
  if (!Has(sh, "ssf2t_" + self + "_tests")) FailGlobal("tools/test.sh does not run the global feature parity test");
  ++g_checks;
  if (!Has(ps1, "tools/test.sh") || !Has(ps1, "Invoke-LinuxGate") || !Has(ps1, "'--prepare'") ||
      !Has(ps1, "'--build-dir'")) {
    FailGlobal("tools/test.ps1 does not run tools/test.sh in WSL");
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// Real images.

bool CheckImages(const Context& context) {
  struct Fixture {
    std::vector<uint8_t> bytes;
    std::vector<uint32_t> generated;
  };
  std::vector<Fixture> fixtures(kBuildCount);
  bool missing = false;
  for (size_t b = 0; b < kBuildCount; ++b) {
    const fs::path image = Root() / sp::kGuestBuilds[b].analysis_image;
    const fs::path init = Root() / "generated" / sp::kGuestBuilds[b].codegen_dir / "ssf2t_hd_remix_init.cpp";
    if (!fs::exists(image) || !fs::exists(init)) {
      std::printf("  REAL FIXTURE MISSING: %s or %s\n", Relative(image).c_str(), Relative(init).c_str());
      missing = true;
      continue;
    }
    fixtures[b].bytes = ReadBytes(image);
    fixtures[b].generated = LoadGeneratedFunctions(init, std::string(sp::kGuestBuilds[b].id) + "_");
    if (fixtures[b].bytes.size() < 0x1000 || fixtures[b].generated.empty()) {
      std::printf("  REAL FIXTURE UNUSABLE: %s\n", Relative(image).c_str());
      missing = true;
    }
  }
  if (missing) return false;
  const auto word = [&](size_t b, uint32_t address) {
    const size_t at = address - kGuestImageBase;
    const auto& bytes = fixtures[b].bytes;
    return uint32_t(bytes[at]) << 24 | uint32_t(bytes[at + 1]) << 16 | uint32_t(bytes[at + 2]) << 8 | bytes[at + 3];
  };
  for (size_t f = 0; f < kFeatureCount; ++f) {
    const std::vector<std::string> hooked = Split(sp::kFeatures[f].hooked);
    for (const std::string& member : Split(sp::kFeatures[f].guest)) {
      for (size_t b = 0; b < kBuildCount; ++b) {
        const auto address = context.tables[b].addresses.find(member);
        if (address == context.tables[b].addresses.end()) continue;
        ++g_checks;
        if (address->second < kGuestImageBase || address->second - kGuestImageBase + 4 > fixtures[b].bytes.size()) {
          FailBuild(f, b, member + " (" + Hex(address->second) + ") lies outside the real " + sp::kGuestBuilds[b].label + " image");
          continue;
        }
        if (std::find(hooked.begin(), hooked.end(), member) != hooked.end() &&
            !std::binary_search(fixtures[b].generated.begin(), fixtures[b].generated.end(), address->second)) {
          FailBuild(f, b, member + " (" + Hex(address->second) + ") is hooked but is not a recompiled function of the real " +
                              sp::kGuestBuilds[b].label + " image");
        }
      }
    }
  }
  for (size_t b = 0; b < kBuildCount; ++b) {
    for (const auto& [short_name, traits] : context.tables[b].structs) {
      for (const Fingerprint& print : Fingerprints(traits)) {
        const std::string member = short_name + "::" + print.name.substr(0, print.name.size() - 5);
        size_t owner = kFeatureCount;
        for (size_t f = 0; f < kFeatureCount; ++f) {
          for (const std::string& m : Split(sp::kFeatures[f].guest)) {
            if (m == member) owner = f;
          }
        }
        ++g_checks;
        bool match = print.address >= kGuestImageBase &&
                     print.address - kGuestImageBase + print.words.size() * 4 <= fixtures[b].bytes.size();
        for (size_t i = 0; match && i < print.words.size(); ++i) match = word(b, print.address + uint32_t(i) * 4) == print.words[i];
        if (match) continue;
        const std::string what = member + " has stale expected code at " + Hex(print.address) + " for the real " +
                                 sp::kGuestBuilds[b].label + " image";
        if (owner < kFeatureCount) FailBuild(owner, b, what);
        else FailGlobal(what);
      }
    }
  }
  return true;
}

// ---------------------------------------------------------------------------------------------------------------------

int Report(const char* title) {
  std::vector<std::vector<bool>> failed(kFeatureCount, std::vector<bool>(kTargetCount));
  for (const Failure& failure : g_failures) {
    for (size_t t = 0; t < kTargetCount; ++t) {
      if (failure.targets[t]) failed[failure.feature][t] = true;
    }
  }
  size_t width = 8;
  for (const sp::Feature& feature : sp::kFeatures) width = std::max(width, std::strlen(feature.name));
  std::printf("\nGLOBAL FEATURE PARITY (%s)\n\n%-*s", title, int(width + 2), "Feature");
  for (size_t t = 0; t < kTargetCount; ++t) std::printf("  %-16s", TargetLabel(t).c_str());
  std::printf("\n");
  size_t pass = 0, fail = 0;
  std::vector<size_t> target_failures(kTargetCount);
  for (size_t f = 0; f < kFeatureCount; ++f) {
    std::printf("%-*s", int(width + 2), sp::kFeatures[f].name);
    for (size_t t = 0; t < kTargetCount; ++t) {
      std::printf("  %-16s", failed[f][t] ? "FAIL" : "PASS");
      (failed[f][t] ? fail : pass)++;
      target_failures[t] += failed[f][t];
    }
    std::printf("\n");
  }
  std::printf("\n%zu global features, %zu targets, %zu feature/target combinations: %zu pass, %zu fail, %zu exceptions\n",
              kFeatureCount, kTargetCount, kFeatureCount * kTargetCount, pass, fail, std::size(sp::kExceptions));
  for (const sp::Exception& e : sp::kExceptions) {
    std::printf("  EXCEPTION %s on %s: %s (%s)\n", e.feature, e.target, e.reason, e.source);
  }
  for (size_t t = 0; t < kTargetCount; ++t) {
    std::string name = TargetLabel(t);
    for (char& c : name) c = c == ' ' ? '/' : char(std::tolower(uint8_t(c)));
    std::printf("TARGET %s %s %zu\n", name.c_str(), target_failures[t] ? "FAIL" : "PASS", kFeatureCount - target_failures[t]);
  }
  std::set<std::pair<size_t, std::string>> printed;
  for (const Failure& failure : g_failures) {
    if (!printed.insert({failure.feature, failure.what}).second) continue;
    std::string targets;
    size_t count = 0;
    for (size_t t = 0; t < kTargetCount; ++t) {
      if (!failure.targets[t]) continue;
      targets += (count++ ? ", " : "") + TargetLabel(t);
    }
    std::printf("PARITY FAIL: %s exists globally but %s: %s\n", sp::kFeatures[failure.feature].name,
                count == kTargetCount ? "it does not work on every target" : targets.c_str(), failure.what.c_str());
  }
  for (const std::string& what : g_global_failures) std::printf("PARITY FAIL: %s\n", what.c_str());
  std::printf("GLOBAL FEATURE PARITY: %s\n", g_failures.empty() && g_global_failures.empty() ? "PASS" : "FAIL");
  std::printf("%d checks, %zu failures\n", g_checks, g_failures.size() + g_global_failures.size());
  return g_failures.empty() && g_global_failures.empty() ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string mode = argc > 1 ? argv[1] : "all";
  if (mode != "all" && mode != "static" && mode != "images") {
    std::printf("usage: %s [static|images]\n", argv[0]);
    return 2;
  }
  Context context;
  context.cmake = ParseCmake();
  context.tables = ScanBuildTables();
  context.hooks = FindHooks();
  context.settings_cpp = ReadText(Root() / "src/settings.cpp");
  const size_t keys_at = context.settings_cpp.find("std::vector<std::string> GameSettingKeys() {");
  context.game_setting_keys = keys_at == std::string::npos
                                  ? std::string()
                                  : context.settings_cpp.substr(keys_at, context.settings_cpp.find("\n}\n", keys_at) - keys_at);
  if (context.game_setting_keys.empty()) FailGlobal("GameSettingKeys() is not in src/settings.cpp");

  bool images_missing = false;
  if (mode != "images") {
    CheckRegistryShape();
    CheckBuildEnumeration(context);
    CheckGuestTables(context);
    CheckGuestHooks(context);
    CheckSettings(context);
    CheckHotkeys();
    CheckSourcesAndFunctions(context);
    CheckGatesAreReviewed();
    CheckTests();
  }
  if (mode != "static") {
    images_missing = !CheckImages(context);
  }
  if (mode == "images" && images_missing) {
    std::printf("images: real fixtures missing, not run\n");
    return g_failures.empty() && g_global_failures.empty() ? kFixtureMissing : 1;
  }
  return Report(mode == "images" ? "real images" : (mode == "static" ? "source" : "source and real images"));
}
