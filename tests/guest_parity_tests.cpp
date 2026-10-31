// Parity of the two supported game images, 0.0.1.3 (base013) and Title Update #1 0.0.2.3 (tu023).
//
//   ssf2t_guest_parity_tests source   every per-build table and hook has its counterpart (source only)
//   ssf2t_guest_parity_tests pairs    against both real images: each base013 address names the same code or data as
//                                     its tu023 counterpart, and each base013 hook has the mapped tu023 hook
//   ssf2t_guest_parity_tests base013  the production fingerprints accept the real 0.0.1.3 image and reject the other
//   ssf2t_guest_parity_tests tu023    the same for the real 0.0.2.3 image
//
// The image modes exit 77 when a real fixture (game\analysis, generated\<v>) is not on this machine.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "achievement_ui.h"
#include "fight_area.h"
#include "infinite_time.h"
#include "message_box.h"
#include "netplay/guest_netplay.h"
#include "player_match_lobby.h"
#include "parity_scan.h"
#include "updater/sha256.h"

namespace {

using namespace parity;

constexpr int kFixtureMissing = 77;
constexpr uint32_t kImageBase = 0x82000000;

int g_failures = 0;
int g_checks = 0;

void Fail(const std::string& what) {
  ++g_failures;
  std::printf("  PARITY FAIL: %s\n", what.c_str());
}

void Check(bool ok, const std::string& what) {
  ++g_checks;
  if (!ok) Fail(what);
}


// ---------------------------------------------------------------------------------------------------------------------

void TestTraitsHaveCounterparts() {
  const std::vector<TraitsPair> pairs = FindTraitsPairs();
  Check(pairs.size() >= 7, "fewer per-build traits structs than the known Achievements, Fight, Timer, MessageBox, "
                           "PlayerMatch, Netplay and lobby tables");
  for (const TraitsPair& pair : pairs) {
    const std::string where = pair.base.file + " " + pair.base.name;
    Check(pair.has_update, where + " exists for Base013 but TitleUpdate023" + pair.base.name.substr(7) + " is missing");
    if (!pair.has_update) continue;
    for (const auto& [name, member] : pair.base.members) {
      Check(pair.update.members.contains(name), where + "::" + name + " exists for Base013 but the TU023 member is missing");
    }
    for (const auto& [name, member] : pair.update.members) {
      Check(pair.base.members.contains(name), where + ": TU023 member " + name + " has no Base013 counterpart");
      const auto other = pair.base.members.find(name);
      if (other == pair.base.members.end()) continue;
      Check(member.type == other->second.type && member.array == other->second.array,
            where + "::" + name + " has a different type in the two builds");
    }
    for (const Traits* traits : {&pair.base, &pair.update}) {
      for (const auto& [name, member] : traits->members) {
        if (member.type != "uint32_t" || member.array) continue;
        const auto value = HexValue(member.value);
        if (value && name != "kName") {
          Check(*value != 0, traits->name + "::" + name + " is zero (a placeholder address)");
        }
      }
    }
    for (const auto& [name, member] : pair.base.members) {
      const auto other = pair.update.members.find(name);
      if (other == pair.update.members.end() || member.type != "const char*") continue;
      Check(member.value != other->second.value, where + "::" + name + " names both builds the same");
    }
  }
}

void TestHooksHaveCounterparts() {
  const std::vector<Hook> hooks = FindHooks();
  std::map<std::string, std::multiset<std::string>> base_bodies;
  std::map<std::string, std::multiset<std::string>> update_bodies;
  std::map<std::string, std::set<uint32_t>> seen[2];
  for (const Hook& hook : hooks) {
    (hook.update ? update_bodies : base_bodies)[hook.file].insert(hook.body);
    Check(seen[hook.update][hook.file].insert(hook.address).second,
          hook.file + " hooks " + Hex(hook.address) + " twice for " + (hook.update ? "TU023" : "Base013"));
  }
  Check(hooks.size() >= 100, "fewer guest hooks than expected: the hook scanner no longer recognises them");
  std::set<std::string> files;
  for (const auto& [file, bodies] : base_bodies) files.insert(file);
  for (const auto& [file, bodies] : update_bodies) files.insert(file);
  for (const std::string& file : files) {
    const auto& base = base_bodies[file];
    const auto& update = update_bodies[file];
    for (const std::string& body : base) {
      Check(update.count(body) >= base.count(body),
            file + ": hook " + body.substr(0, 90) + " exists for Base013 but the TU023 hook is missing");
    }
    for (const std::string& body : update) {
      Check(base.count(body) >= update.count(body),
            file + ": hook " + body.substr(0, 90) + " exists for TU023 but the Base013 hook is missing");
    }
    const std::string text = ReadText(Root() / file);
    const auto regions = TitleUpdateRegions(text);
    for (const Hook& hook : hooks) {
      if (hook.file != file) continue;
      const bool guarded = std::any_of(regions.begin(), regions.end(),
                                       [&](const auto& r) { return hook.offset > r.first && hook.offset < r.second; });
      if (hook.update) {
        Check(guarded, file + ": TU023 hook " + Hex(hook.address) +
                           " is outside #if SSF2T_TITLE_UPDATE_IMAGE, so a base-only build cannot link");
      } else {
        Check(!guarded, file + ": Base013 hook " + Hex(hook.address) +
                            " is inside #if SSF2T_TITLE_UPDATE_IMAGE, so a base-only build loses it");
      }
    }
  }
}

void TestBuildSelectorsCoverBothImages() {
  const std::string image = ReadText(Root() / "src/guest_image.cpp");
  Check(image.find("return tu023_PPCImageConfig;") != std::string::npos &&
            image.find("return base013_PPCImageConfig;") != std::string::npos,
        "src/guest_image.cpp no longer selects both recompiled images");
  const std::string packet = ReadText(Root() / "src/netplay/packet.h");
  Check(packet.find("kBase013 = 1,") != std::string::npos && packet.find("kTitleUpdate023 = 2,") != std::string::npos,
        "the netplay GuestId no longer carries both builds with their wire values");
  Check(std::string_view(ssf2t::netplay::Base013Netplay::kName) == "base013" &&
            std::string_view(ssf2t::netplay::TitleUpdate023Netplay::kName) == "tu023",
        "netplay traits names changed");
  Check(ssf2t::netplay::Base013Netplay::kGuest != ssf2t::netplay::TitleUpdate023Netplay::kGuest,
        "both netplay traits report the same guest build on the wire");
  for (const ssf2t::LobbyFatalSite& site : ssf2t::kLobbyFatalSites) {
    Check(site.base013 && site.tu023 && site.base013 != site.tu023,
          std::string("lobby fatal site ") + site.reason + " lacks a distinct address for one build");
  }
  const std::string online = ReadText(Root() / "src/online_runtime.cpp");
  Check(online.find("guest == GuestVersion::kTitleUpdate023 ? online::kGuestTu023 : online::kGuestBase013") !=
            std::string::npos,
        "online presence no longer reports which build the player runs");
}

// ---------------------------------------------------------------------------------------------------------------------
// Real images.

struct Section {
  uint32_t address = 0;
  uint32_t size = 0;
};

struct Image {
  std::vector<uint8_t> bytes;
  Section text, data, pdata;
  std::map<uint32_t, uint32_t> functions;  // .pdata: start -> size in bytes
  std::vector<uint32_t> generated;         // every function the recompiled image has, sorted

  const uint8_t* base() const { return reinterpret_cast<const uint8_t*>(uintptr_t(bytes.data()) - kImageBase); }
  bool Contains(uint32_t address, uint32_t size = 4) const {
    return address >= kImageBase && address - kImageBase + uint64_t(size) <= bytes.size();
  }
  uint32_t Word(uint32_t address) const { return ssf2t::LoadBE32(base(), address); }
  bool InText(uint32_t address) const { return address >= text.address && address < text.address + text.size; }
  bool InData(uint32_t address) const { return address >= data.address && address < data.address + data.size; }
  bool IsPdata(uint32_t address) const { return functions.contains(address); }
  bool IsGenerated(uint32_t address) const { return std::binary_search(generated.begin(), generated.end(), address); }
  std::optional<std::pair<uint32_t, uint32_t>> PdataFunction(uint32_t address) const {
    auto it = functions.upper_bound(address);
    if (it == functions.begin()) return std::nullopt;
    --it;
    if (address >= it->first + it->second) return std::nullopt;
    return *it;
  }
  uint32_t GeneratedSize(uint32_t start) const {
    const auto it = std::upper_bound(generated.begin(), generated.end(), start);
    return it == generated.end() ? 4 : *it - start;
  }
};

uint32_t LE32(const std::vector<uint8_t>& b, size_t at) {
  return uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24;
}

std::optional<Image> LoadImage(const fs::path& path, const fs::path& init, const std::string& prefix) {
  Image image;
  image.bytes = ReadBytes(path);
  if (image.bytes.size() < 0x1000 || image.bytes[0] != 'M' || image.bytes[1] != 'Z') return std::nullopt;
  const uint32_t pe = LE32(image.bytes, 0x3C);
  const uint32_t sections = image.bytes[pe + 6] | image.bytes[pe + 7] << 8;
  const uint32_t optional = image.bytes[pe + 20] | image.bytes[pe + 21] << 8;
  Check(LE32(image.bytes, pe + 0x34) == kImageBase, path.filename().string() + " is not loaded at 0x82000000");
  for (uint32_t i = 0; i < sections; ++i) {
    const size_t header = pe + 24 + optional + i * 40;
    const std::string name(reinterpret_cast<const char*>(&image.bytes[header]),
                           strnlen(reinterpret_cast<const char*>(&image.bytes[header]), 8));
    const Section section{kImageBase + LE32(image.bytes, header + 12), LE32(image.bytes, header + 8)};
    if (name == ".text") image.text = section;
    if (name == ".data") image.data = section;
    if (name == ".pdata") image.pdata = section;
  }
  for (uint32_t at = image.pdata.address; at + 8 <= image.pdata.address + image.pdata.size; at += 8) {
    const uint32_t start = image.Word(at);
    if (start) image.functions[start] = ((image.Word(at + 4) >> 8) & 0x3FFFFF) * 4;
  }
  image.generated = LoadGeneratedFunctions(init, prefix);
  return image;
}

struct Fixtures {
  Image base;
  Image update;
  std::map<uint32_t, uint32_t> function_map;  // base013 .pdata start -> tu023 start
};

std::optional<Fixtures> LoadFixtures() {
  const fs::path analysis = Root() / "game/analysis";
  const fs::path files[] = {analysis / "default-0.0.1.3.image.bin", analysis / "default.image.bin",
                            analysis / "title-update/title-update-map.json",
                            Root() / "generated/base/ssf2t_hd_remix_init.cpp",
                            Root() / "generated/tu/ssf2t_hd_remix_init.cpp"};
  bool missing = false;
  for (const fs::path& file : files) {
    if (!fs::exists(file)) {
      std::printf("  REAL FIXTURE MISSING: %s\n", Relative(file).c_str());
      missing = true;
    }
  }
  if (missing) return std::nullopt;
  Fixtures fixtures;
  auto base = LoadImage(files[0], files[3], "base013_");
  auto update = LoadImage(files[1], files[4], "tu023_");
  if (!base || !update) {
    Fail("a real image under game\\analysis is not a PE image");
    return std::nullopt;
  }
  fixtures.base = std::move(*base);
  fixtures.update = std::move(*update);

  const std::string map = ReadText(files[2]);
  for (const auto& [key, image] : {std::pair{"\"base_sha256\": \"", &fixtures.base},
                                   std::pair{"\"updated_sha256\": \"", &fixtures.update}}) {
    const size_t at = map.find(key);
    const std::string expected = at == std::string::npos ? "" : map.substr(at + std::strlen(key), 64);
    trg::update::Sha256 hash;
    hash.Update(image->bytes.data(), image->bytes.size());
    std::string actual;
    for (const uint8_t byte : hash.Final()) {
      char two[3];
      std::snprintf(two, sizeof(two), "%02X", byte);
      actual += two;
    }
    Check(actual == expected, std::string("real image hash differs from title-update-map.json ") + key);
  }
  const size_t start = map.find("\"function_map\"");
  const size_t end = map.find('}', start);
  const std::regex entry(R"#("0x([0-9A-F]{8})": "0x([0-9A-F]{8})")#");
  for (std::sregex_iterator it(map.begin() + start, map.begin() + end, entry), stop; it != stop; ++it) {
    fixtures.function_map[uint32_t(std::stoul((*it)[1], nullptr, 16))] = uint32_t(std::stoul((*it)[2], nullptr, 16));
  }
  Check(fixtures.function_map.size() > 10000, "title-update-map.json has no function_map");
  Check(fixtures.base.generated.size() > 10000 && fixtures.update.generated.size() > 10000,
        "generated\\<v>\\ssf2t_hd_remix_init.cpp lists no functions");
  return fixtures;
}

// Code skeleton: opcodes and registers, with branch displacements and immediates (which a relink moves) masked.
std::vector<uint32_t> Skeleton(const Image& image, uint32_t start, uint32_t size) {
  std::vector<uint32_t> words;
  for (uint32_t i = 0; i < size; i += 4) {
    uint32_t word = image.Word(start + i);
    const uint32_t op = word >> 26;
    if (op == 18) word &= 0xFC000003;
    else if (op == 16) word &= 0xFFFF0003;
    else if (op == 14 || op == 15 || (op >= 32 && op <= 55) || op == 58 || op == 62) word &= 0xFFFF0000;
    words.push_back(word);
  }
  return words;
}

// Effective addresses formed by `lis rX, hi` and a D-form use of rX, one per instruction (0 where none).
std::vector<uint32_t> DataReferences(const Image& image, uint32_t start, uint32_t size) {
  std::vector<uint32_t> refs;
  std::optional<uint32_t> high[32];
  for (uint32_t i = 0; i < size; i += 4) {
    const uint32_t word = image.Word(start + i);
    const uint32_t op = word >> 26;
    const uint32_t rd = (word >> 21) & 31;
    const uint32_t ra = (word >> 16) & 31;
    const uint32_t imm = word & 0xFFFF;
    if (op == 15 && ra == 0) {
      high[rd] = imm << 16;
      refs.push_back(0);
      continue;
    }
    const bool dform = op == 14 || (op >= 32 && op <= 55) || op == 58 || op == 62;
    uint32_t ea = 0;
    if (dform && ra && high[ra]) {
      const uint32_t disp = (op == 58 || op == 62) ? imm & ~3u : imm;
      ea = *high[ra] + uint32_t(int32_t(int16_t(disp)));
    }
    refs.push_back(ea);
    const bool store = (op >= 36 && op <= 39) || op == 44 || op == 45 || op == 47 || (op >= 52 && op <= 55) || op == 62;
    if (dform && !store) high[rd].reset();
  }
  return refs;
}

class Correspondence {
 public:
  explicit Correspondence(const Fixtures& f) : f_(f) {
    for (const auto& [base, update] : f.function_map) {
      const auto b = f.base.functions.find(base);
      const auto u = f.update.functions.find(update);
      if (b == f.base.functions.end() || u == f.update.functions.end() || b->second != u->second) continue;
      const auto rb = DataReferences(f.base, base, b->second);
      const auto ru = DataReferences(f.update, update, u->second);
      for (size_t i = 0; i < rb.size(); ++i) {
        if (rb[i] >= kImageBase && ru[i] >= kImageBase && !f.base.InText(rb[i])) data_[rb[i]].insert(ru[i]);
      }
    }
  }

  // The tu023 address of base013 code at `address`; `words` is its fingerprint, for code inside a changed function.
  std::optional<uint32_t> Code(uint32_t address, const std::vector<uint32_t>* words, std::string& why) const {
    if (const auto function = f_.base.PdataFunction(address)) {
      const auto mapped = f_.function_map.find(function->first);
      if (mapped == f_.function_map.end()) {
        why = "its function " + Hex(function->first) + " has no TU023 counterpart";
        return std::nullopt;
      }
      const uint32_t offset = address - function->first;
      const auto update = f_.update.functions.find(mapped->second);
      if (offset == 0 || (update != f_.update.functions.end() && update->second == function->second)) {
        return mapped->second + offset;
      }
      return Unique(mapped->second, update->second, words, why);
    }
    return Leaf(address, why);
  }

  // Data: both builds' code must reference the pair at the same instruction, or the pair sits between referenced
  // neighbours that both moved by the same distance.
  bool Data(uint32_t base, uint32_t update, std::string& why) const {
    const auto exact = data_.find(base);
    if (exact != data_.end()) {
      if (exact->second.contains(update)) return true;
      why = "code referencing it pairs it with";
      for (const uint32_t other : exact->second) why += " " + Hex(other);
      return false;
    }
    auto above = data_.upper_bound(base);
    if (above == data_.begin() || above == data_.end()) {
      why = "no referenced neighbours";
      return false;
    }
    auto below = std::prev(above);
    const int64_t delta = int64_t(update) - int64_t(base);
    const auto moved = [&](const auto& it) {
      return it->second.size() == 1 && int64_t(*it->second.begin()) - int64_t(it->first) == delta;
    };
    if (moved(below) && moved(above)) return true;
    why = "unreferenced, and its neighbours " + Hex(below->first) + "/" + Hex(above->first) + " moved differently";
    return false;
  }

 private:
  std::optional<uint32_t> Unique(uint32_t function, uint32_t size, const std::vector<uint32_t>* words,
                                 std::string& why) const {
    if (!words || words->empty()) {
      why = "it is inside a function the Title Update changed, and it has no fingerprint words";
      return std::nullopt;
    }
    std::optional<uint32_t> found;
    for (uint32_t at = function; at + words->size() * 4 <= function + size; at += 4) {
      bool match = true;
      for (size_t i = 0; i < words->size() && match; ++i) match = f_.update.Word(at + uint32_t(i) * 4) == (*words)[i];
      if (!match) continue;
      if (found) {
        why = "its fingerprint occurs twice in the changed TU023 function";
        return std::nullopt;
      }
      found = at;
    }
    if (!found) why = "its fingerprint is not in the changed TU023 function";
    return found;
  }

  // A leaf function without unwind data: paired by linker order between the mapped functions around it, or by being
  // the only function there with the same code skeleton.
  std::optional<uint32_t> Leaf(uint32_t address, std::string& why) const {
    const auto it = std::upper_bound(f_.base.generated.begin(), f_.base.generated.end(), address);
    if (it == f_.base.generated.begin()) {
      why = "outside every recompiled function";
      return std::nullopt;
    }
    const uint32_t start = *std::prev(it);
    auto high = f_.function_map.upper_bound(start);
    if (high == f_.function_map.begin() || high == f_.function_map.end()) {
      why = "no mapped functions around it";
      return std::nullopt;
    }
    const auto low = std::prev(high);
    const auto between = [](const Image& image, uint32_t lo, uint32_t hi) {
      std::vector<uint32_t> out;
      for (auto g = std::upper_bound(image.generated.begin(), image.generated.end(), lo);
           g != image.generated.end() && *g < hi; ++g) {
        if (!image.IsPdata(*g)) out.push_back(*g);
      }
      return out;
    };
    const auto bases = between(f_.base, low->first, high->first);
    const auto updates = between(f_.update, low->second, high->second);
    const uint32_t size = f_.base.GeneratedSize(start);
    const auto skeleton = Skeleton(f_.base, start, size);
    const auto same = [&](uint32_t candidate) {
      return f_.update.GeneratedSize(candidate) == size && Skeleton(f_.update, candidate, size) == skeleton;
    };
    const size_t index = size_t(std::find(bases.begin(), bases.end(), start) - bases.begin());
    if (index < bases.size() && bases.size() == updates.size() && same(updates[index])) {
      return updates[index] + (address - start);
    }
    std::optional<uint32_t> found;
    for (const uint32_t candidate : updates) {
      if (!same(candidate)) continue;
      if (found) {
        why = "two TU023 functions with the same code lie where it moved";
        return std::nullopt;
      }
      found = candidate;
    }
    if (!found) {
      why = "no TU023 function with the same code lies where it moved";
      return std::nullopt;
    }
    return *found + (address - start);
  }

  const Fixtures& f_;
  std::map<uint32_t, std::set<uint32_t>> data_;
};

std::vector<Fingerprint> CheckedFingerprints(const Traits& traits) {
  std::vector<std::string> unresolved;
  std::vector<Fingerprint> fingerprints = Fingerprints(traits, &unresolved);
  for (const std::string& name : unresolved) Check(false, name + " holds a name the parity test cannot resolve");
  return fingerprints;
}

// Bounds of a range scanned in both builds, not addresses of one object.
const std::set<std::string> kScanWindows = {"kDiagWindowLow", "kDiagWindowHigh"};

int TestAddressPairs() {
  const auto fixtures = LoadFixtures();
  if (!fixtures) return g_failures ? 1 : kFixtureMissing;
  const Correspondence map(*fixtures);
  int pairs = 0;
  const auto check_code = [&](const std::string& what, uint32_t base, uint32_t update,
                              const std::vector<uint32_t>* words) {
    std::string why;
    const auto mapped = map.Code(base, words, why);
    ++pairs;
    Check(mapped && *mapped == update, what + ": Base013 " + Hex(base) + " is TU023 " +
                                           (mapped ? Hex(*mapped) : "(unmapped: " + why + ")") + ", not " +
                                           Hex(update));
  };

  for (const TraitsPair& pair : FindTraitsPairs()) {
    if (!pair.has_update) continue;
    std::map<std::string, std::vector<uint32_t>> words;
    for (const Fingerprint& fingerprint : CheckedFingerprints(pair.update)) {
      words[fingerprint.name.substr(0, fingerprint.name.size() - 5)] = fingerprint.words;
    }
    const auto updates = Addresses(pair.update);
    for (const auto& [name, base] : Addresses(pair.base)) {
      const auto update = updates.find(name);
      if (update == updates.end()) continue;
      const std::string what = pair.base.file + " " + pair.base.name + "::" + name;
      Check(fixtures->base.Contains(base) && fixtures->update.Contains(update->second),
            what + " lies outside the real image");
      if (fixtures->base.InText(base)) {
        Check(fixtures->update.InText(update->second), what + " is code in Base013 but not in TU023");
        const auto found = words.find(name);
        check_code(what, base, update->second, found == words.end() ? nullptr : &found->second);
      } else {
        std::string why;
        ++pairs;
        const bool same = map.Data(base, update->second, why);
        Check(same, what + ": Base013 " + Hex(base) + " does not correspond to TU023 " + Hex(update->second) + " (" +
                        why + ")");
      }
    }
  }

  for (const TraitsPair& pair : FindTraitsPairs()) {
    const std::string text = ReadText(Root() / pair.base.file);
    for (const auto& [name, address] : SharedConstants(text)) {
      if (!IsGuestAddress(address) || !fixtures->base.Contains(address)) continue;
      std::string why;
      ++pairs;
      if (kScanWindows.contains(name)) {
        Check(fixtures->base.InData(address) && fixtures->update.InData(address),
              pair.base.file + " " + name + " leaves the .data of one build");
      } else if (fixtures->base.InText(address)) {
        check_code(pair.base.file + " " + name + " (shared)", address, address, nullptr);
      } else {
        const bool same = map.Data(address, address, why);
        Check(same, pair.base.file + " " + name + " is shared by both builds, but " + why);
      }
    }
  }

  for (const ssf2t::LobbyFatalSite& site : ssf2t::kLobbyFatalSites) {
    check_code(std::string("lobby fatal site ") + site.reason, site.base013, site.tu023, nullptr);
  }

  const std::vector<Hook> hooks = FindHooks();
  for (const Hook& hook : hooks) {
    const Image& image = hook.update ? fixtures->update : fixtures->base;
    Check(image.IsGenerated(hook.address), hook.file + ": " + (hook.update ? "TU023" : "Base013") + " hook " +
                                               Hex(hook.address) + " is not a recompiled function of that image");
    if (hook.update) continue;
    std::string why;
    const auto mapped = map.Code(hook.address, nullptr, why);
    ++pairs;
    const bool paired = mapped && std::any_of(hooks.begin(), hooks.end(), [&](const Hook& other) {
                          return other.update && other.file == hook.file && other.address == *mapped &&
                                 other.body == hook.body;
                        });
    Check(paired, hook.file + ": Base013 hook " + Hex(hook.address) + " has no TU023 hook on its counterpart " +
                      (mapped ? Hex(*mapped) : "(unmapped: " + why + ")"));
  }
  std::printf("  %d Base013/TU023 address and hook pairs checked against the real images\n", pairs);
  return -1;
}

template <typename G, typename Other>
void CheckProductionFingerprints(const char* build, const Image& own, const Image& other) {
  const auto both = [&](const char* what, auto&& matches) {
    Check(matches(own.base()), std::string(what) + " rejects the real " + build + " image");
    Check(!matches(other.base()), std::string(what) + " for " + build + " also accepts the other build's image");
  };
  both("Infinite Time timer fingerprint", [](const uint8_t* b) { return ssf2t::TimerTickMatches<typename G::Timer>(b); });
  both("achievements wrapper fingerprint", [](const uint8_t* b) {
    return ssf2t::IsShowAchievementsWrapper<typename G::Achievements>(b + G::Achievements::kShowAchievementsWrapper);
  });
  both("message box wrapper fingerprint",
       [](const uint8_t* b) { return ssf2t::IsMessageBoxWrapper(b + G::MessageBox::kShowMessageBoxWrapper); });
  both("fight render path fingerprint",
       [](const uint8_t* b) { return ssf2t::FightRenderPathMatches<typename G::Fight>(b); });
  both("pre-fight path fingerprint", [](const uint8_t* b) { return ssf2t::PreFightPathMatches<typename G::Fight>(b); });
  both("side panel path fingerprint",
       [](const uint8_t* b) { return ssf2t::SidePanelPathMatches<typename G::Fight>(b); });
  both("texture bind path fingerprint",
       [](const uint8_t* b) { return ssf2t::TextureBindPathMatches<typename G::Fight>(b); });
  both("netplay code fingerprint",
       [](const uint8_t* b) { return ssf2t::netplay::NetplayCodeMatches<typename G::Netplay>(b); });
  (void)sizeof(Other);
}

struct Base013 {
  using Timer = ssf2t::Base013Timer;
  using Achievements = ssf2t::Base013Achievements;
  using MessageBox = ssf2t::Base013MessageBox;
  using Fight = ssf2t::Base013Fight;
  using Netplay = ssf2t::netplay::Base013Netplay;
};
struct TitleUpdate023 {
  using Timer = ssf2t::TitleUpdate023Timer;
  using Achievements = ssf2t::TitleUpdate023Achievements;
  using MessageBox = ssf2t::TitleUpdate023MessageBox;
  using Fight = ssf2t::TitleUpdate023Fight;
  using Netplay = ssf2t::netplay::TitleUpdate023Netplay;
};

int TestImageFingerprints(bool update) {
  const auto fixtures = LoadFixtures();
  if (!fixtures) return g_failures ? 1 : kFixtureMissing;
  const Image& own = update ? fixtures->update : fixtures->base;
  const Image& other = update ? fixtures->base : fixtures->update;
  const char* build = update ? "TU023" : "Base013";
  if (update) {
    CheckProductionFingerprints<TitleUpdate023, Base013>(build, own, other);
  } else {
    CheckProductionFingerprints<Base013, TitleUpdate023>(build, own, other);
  }
  int words = 0;
  for (const TraitsPair& pair : FindTraitsPairs()) {
    if (update && !pair.has_update) continue;
    const Traits& traits = update ? pair.update : pair.base;
    for (const Fingerprint& fingerprint : CheckedFingerprints(traits)) {
      bool match = own.Contains(fingerprint.address, uint32_t(fingerprint.words.size() * 4));
      for (size_t i = 0; match && i < fingerprint.words.size(); ++i) {
        match = own.Word(fingerprint.address + uint32_t(i) * 4) == fingerprint.words[i];
      }
      ++words;
      Check(match, traits.file + " " + traits.name + "::" + fingerprint.name + " has stale expected words at " +
                       Hex(fingerprint.address) + " in the real " + build + " image");
    }
    for (const auto& [name, address] : Addresses(traits)) {
      Check(own.Contains(address), traits.name + "::" + name + " " + Hex(address) + " is outside the real " + build +
                                       " image");
    }
  }
  std::printf("  %d %s fingerprints checked against the real image\n", words, build);
  return -1;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string mode = argc > 1 ? argv[1] : "source";
  int status = -1;
  if (mode == "source") {
    TestTraitsHaveCounterparts();
    TestHooksHaveCounterparts();
    TestBuildSelectorsCoverBothImages();
  } else if (mode == "pairs") {
    status = TestAddressPairs();
  } else if (mode == "base013" || mode == "tu023") {
    status = TestImageFingerprints(mode == "tu023");
  } else {
    std::printf("usage: %s source|pairs|base013|tu023\n", argv[0]);
    return 2;
  }
  if (status == kFixtureMissing) {
    std::printf("%s: real fixtures missing, not run\n", mode.c_str());
    return status;
  }
  std::printf("%s: %d checks, %d failures\n", mode.c_str(), g_checks, g_failures);
  return g_failures ? 1 : 0;
}
