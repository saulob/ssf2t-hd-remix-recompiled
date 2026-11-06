// Character packages: the table against every runtime consumer, which package each fighter needs, and how a missing or
// different package disables that fighter. Fixture files only; the installed packages are checked when present.

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>

#include "character/character_packages.h"
#include "character/evil_ryu_assets.h"
#include "character/evil_ryu_audio.h"
#include "character/evil_ryu_palette.h"
#include "character/evil_ryu_sprites.h"
#include "character/ultra_ending.h"
#include "character/ultra_portraits.h"
#include "character/violent_ken_audio.h"
#include "character/violent_ken_data.h"
#include "updater/sha256.h"

using namespace ssf2t::character;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

constexpr std::array<std::string_view, 16> kConsumerFiles = {
    kUltraPortraitFile,      kEvilRyuAssetFile,     kViolentKenAssetFile,   kEvilRyuSpriteFile,
    kEvilRyuPaletteFile,     kEvilRyuHdPaletteFile, kEvilRyuHudIconFile,    kEvilRyuAudioFile,
    kEvilRyuEndingFile,      kViolentKenGameplayFile, kViolentKenSpriteFile, kViolentKenPaletteFile,
    kViolentKenHdPaletteFile, kViolentKenHudIconFile, kViolentKenAudioFile,  kViolentKenEndingFile};

const CharacterPackage* Find(std::string_view file) {
  const auto found = std::find_if(kCharacterPackages.begin(), kCharacterPackages.end(),
                                  [file](const CharacterPackage& package) { return package.file == file; });
  return found == kCharacterPackages.end() ? nullptr : &*found;
}

bool Required(std::string_view file, PackageOwner owner) {
  const CharacterPackage* package = Find(file);
  return package && package->need == PackageNeed::kRequired && package->owner == owner;
}

bool Optional(std::string_view file, PackageOwner owner) {
  const CharacterPackage* package = Find(file);
  return package && package->need == PackageNeed::kOptional && package->owner == owner;
}

fs::path MakeDir(const char* name) {
  const fs::path dir = fs::temp_directory_path() / "ssf2t_character_packages_tests" / name;
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  return dir;
}

void TestTableMatchesConsumers() {
  CHECK(kCharacterPackages.size() == kConsumerFiles.size());
  std::set<std::string_view> names;
  for (const CharacterPackage& package : kCharacterPackages) {
    names.insert(package.file);
    CHECK(package.size > 0);
    CHECK(trg::update::IsValidSha256(package.sha256));
    CHECK(package.sha256 == trg::update::NormalizeSha256(package.sha256));
  }
  CHECK(names.size() == kCharacterPackages.size());
  for (const std::string_view file : kConsumerFiles) CHECK(Find(file) != nullptr);
}

void TestRequiredSets() {
  CHECK(Required(kUltraPortraitFile, PackageOwner::kBoth));
  CHECK(Required(kEvilRyuSpriteFile, PackageOwner::kEvilRyu));
  CHECK(Required(kEvilRyuPaletteFile, PackageOwner::kEvilRyu));
  CHECK(Required(kEvilRyuHdPaletteFile, PackageOwner::kEvilRyu));
  CHECK(Required(kViolentKenGameplayFile, PackageOwner::kViolentKen));
  CHECK(Required(kViolentKenSpriteFile, PackageOwner::kViolentKen));
  CHECK(Required(kViolentKenPaletteFile, PackageOwner::kViolentKen));
  CHECK(Required(kViolentKenHdPaletteFile, PackageOwner::kViolentKen));
  CHECK(Optional(kEvilRyuAssetFile, PackageOwner::kEvilRyu));
  CHECK(Optional(kEvilRyuHudIconFile, PackageOwner::kEvilRyu));
  CHECK(Optional(kEvilRyuAudioFile, PackageOwner::kEvilRyu));
  CHECK(Optional(kEvilRyuEndingFile, PackageOwner::kEvilRyu));
  CHECK(Optional(kViolentKenAssetFile, PackageOwner::kViolentKen));
  CHECK(Optional(kViolentKenHudIconFile, PackageOwner::kViolentKen));
  CHECK(Optional(kViolentKenAudioFile, PackageOwner::kViolentKen));
  CHECK(Optional(kViolentKenEndingFile, PackageOwner::kViolentKen));
}

void TestPackageFolderIsNotTheGameFolder() {
  const fs::path folder = CharacterPackageFolder(fs::path("install"));
  CHECK(folder == fs::path("install") / "assets" / "characters");
  CHECK(std::find(folder.begin(), folder.end(), fs::path("game")) == folder.end());
}

void TestCheckPackage() {
  const fs::path folder = MakeDir("check");
  const std::string content = "package";
  const std::string hash = trg::update::Sha256Hex(content);
  const CharacterPackage package{"a.bin", content.size(), hash, PackageOwner::kBoth, PackageNeed::kRequired};
  CHECK(CheckCharacterPackage(folder, package) == PackageStatus::kMissing);
  std::ofstream(folder / "a.bin", std::ios::binary) << content;
  CHECK(CheckCharacterPackage(folder, package) == PackageStatus::kValid);
  std::ofstream(folder / "a.bin", std::ios::binary | std::ios::trunc) << "packagf";
  CHECK(CheckCharacterPackage(folder, package) == PackageStatus::kDifferent);
  std::ofstream(folder / "a.bin", std::ios::binary | std::ios::trunc) << "pack";
  CHECK(CheckCharacterPackage(folder, package) == PackageStatus::kDifferent);
}

void TestRequiredRule() {
  constexpr std::array<CharacterPackage, 4> table = {{
      {"shared.bin", 1, "", PackageOwner::kBoth, PackageNeed::kRequired},
      {"evil.bin", 1, "", PackageOwner::kEvilRyu, PackageNeed::kRequired},
      {"violent.bin", 1, "", PackageOwner::kViolentKen, PackageNeed::kRequired},
      {"voice.bin", 1, "", PackageOwner::kEvilRyu, PackageNeed::kOptional},
  }};
  using S = PackageStatus;
  constexpr std::array<S, 4> all = {S::kValid, S::kValid, S::kValid, S::kValid};
  CHECK(CharacterRequiredPackagesValid(table, all, PackageOwner::kEvilRyu));
  CHECK(CharacterRequiredPackagesValid(table, all, PackageOwner::kViolentKen));

  constexpr std::array<S, 4> no_voice = {S::kValid, S::kValid, S::kValid, S::kMissing};
  CHECK(CharacterRequiredPackagesValid(table, no_voice, PackageOwner::kEvilRyu));

  constexpr std::array<S, 4> no_evil = {S::kValid, S::kMissing, S::kValid, S::kValid};
  CHECK(!CharacterRequiredPackagesValid(table, no_evil, PackageOwner::kEvilRyu));
  CHECK(CharacterRequiredPackagesValid(table, no_evil, PackageOwner::kViolentKen));

  constexpr std::array<S, 4> stale_violent = {S::kValid, S::kValid, S::kDifferent, S::kValid};
  CHECK(CharacterRequiredPackagesValid(table, stale_violent, PackageOwner::kEvilRyu));
  CHECK(!CharacterRequiredPackagesValid(table, stale_violent, PackageOwner::kViolentKen));

  constexpr std::array<S, 4> no_shared = {S::kMissing, S::kValid, S::kValid, S::kValid};
  CHECK(!CharacterRequiredPackagesValid(table, no_shared, PackageOwner::kEvilRyu));
  CHECK(!CharacterRequiredPackagesValid(table, no_shared, PackageOwner::kViolentKen));
}

void TestUnvalidatedAndEmptyInstall() {
  CHECK(!EvilRyuPackagesReady());
  CHECK(!ViolentKenPackagesReady());
  const fs::path folder = MakeDir("empty");
  for (int run = 0; run < 2; ++run) {
    const CharacterPackageReport report = ValidateCharacterPackages(folder);
    CHECK(report.valid == 0);
    CHECK(!report.evil_ryu && !report.violent_ken);
    for (const PackageStatus status : report.statuses) CHECK(status == PackageStatus::kMissing);
    CHECK(!EvilRyuPackagesReady() && !ViolentKenPackagesReady());
    CHECK(std::string_view(CharacterPackageNotice()).starts_with("Evil Ryu and Violent Ken are unavailable"));
  }
}

void TestInstalledPackages() {
  const fs::path installed = fs::path("assets") / "characters";
  if (ValidateCharacterPackages(installed).valid != kCharacterPackages.size()) {
    std::printf("note: assets/characters is not complete, installed package checks skipped\n");
    return;
  }
  CHECK(EvilRyuPackagesReady() && ViolentKenPackagesReady());
  CHECK(CharacterPackageNotice() == nullptr);

  const fs::path folder = MakeDir("installed");
  for (const CharacterPackage& package : kCharacterPackages) fs::copy_file(installed / package.file, folder / package.file);

  fs::remove(folder / kViolentKenGameplayFile);
  const CharacterPackageReport no_gameplay = ValidateCharacterPackages(folder);
  CHECK(no_gameplay.evil_ryu && !no_gameplay.violent_ken);
  CHECK(std::string_view(CharacterPackageNotice()).starts_with("Violent Ken is unavailable"));
  fs::copy_file(installed / kViolentKenGameplayFile, folder / kViolentKenGameplayFile);

  fs::remove(folder / kEvilRyuHdPaletteFile);
  const CharacterPackageReport no_palette = ValidateCharacterPackages(folder);
  CHECK(!no_palette.evil_ryu && no_palette.violent_ken);
  CHECK(std::string_view(CharacterPackageNotice()).starts_with("Evil Ryu is unavailable"));
  fs::copy_file(installed / kEvilRyuHdPaletteFile, folder / kEvilRyuHdPaletteFile);

  fs::remove(folder / kEvilRyuAudioFile);
  const CharacterPackageReport no_voice = ValidateCharacterPackages(folder);
  CHECK(no_voice.evil_ryu && no_voice.violent_ken);
  CHECK(no_voice.valid == kCharacterPackages.size() - 1);
  fs::copy_file(installed / kEvilRyuAudioFile, folder / kEvilRyuAudioFile);

  std::ofstream(folder / kUltraPortraitFile, std::ios::binary | std::ios::trunc) << "x";
  const CharacterPackageReport damaged = ValidateCharacterPackages(folder);
  CHECK(!damaged.evil_ryu && !damaged.violent_ken);
}

}  // namespace

int main() {
  TestTableMatchesConsumers();
  TestRequiredSets();
  TestPackageFolderIsNotTheGameFolder();
  TestCheckPackage();
  TestRequiredRule();
  TestUnvalidatedAndEmptyInstall();
  TestInstalledPackages();
  std::error_code ec;
  fs::remove_all(fs::temp_directory_path() / "ssf2t_character_packages_tests", ec);
  std::printf("character packages tests: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
