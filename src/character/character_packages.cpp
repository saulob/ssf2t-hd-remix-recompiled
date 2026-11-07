#include "character/character_packages.h"

#include <atomic>
#include <string>
#include <system_error>

#include "updater/sha256.h"

namespace ssf2t::character {
namespace {

std::atomic<bool> g_evil_ryu_ready{false};
std::atomic<bool> g_violent_ken_ready{false};

constexpr char kBothUnavailable[] =
    "Evil Ryu and Violent Ken are unavailable: required character files are missing or damaged. Reinstall this release.";
constexpr char kEvilRyuUnavailable[] =
    "Evil Ryu is unavailable: required character files are missing or damaged. Reinstall this release.";
constexpr char kViolentKenUnavailable[] =
    "Violent Ken is unavailable: required character files are missing or damaged. Reinstall this release.";

}  // namespace

const char* PackageStatusName(PackageStatus status) {
  switch (status) {
    case PackageStatus::kValid: return "valid";
    case PackageStatus::kMissing: return "missing";
    default: return "different from this build";
  }
}

PackageStatus CheckCharacterPackage(const std::filesystem::path& folder, const CharacterPackage& package) {
  const std::filesystem::path file = folder / package.file;
  std::error_code ec;
  const std::uintmax_t size = std::filesystem::file_size(file, ec);
  if (ec) return PackageStatus::kMissing;
  if (size != package.size) return PackageStatus::kDifferent;
  std::string error;
  const std::string hash = trg::update::Sha256FileHex(file, error);
  if (hash.empty()) return PackageStatus::kMissing;
  return trg::update::NormalizeSha256(hash) == package.sha256 ? PackageStatus::kValid : PackageStatus::kDifferent;
}

bool CharacterRequiredPackagesValid(std::span<const CharacterPackage> table, std::span<const PackageStatus> statuses,
                                    PackageOwner owner) {
  for (size_t index = 0; index < table.size(); ++index) {
    const CharacterPackage& package = table[index];
    if (package.need != PackageNeed::kRequired) continue;
    if (package.owner != owner && package.owner != PackageOwner::kBoth) continue;
    if (statuses[index] != PackageStatus::kValid) return false;
  }
  return true;
}

CharacterPackageReport ValidateCharacterPackages(const std::filesystem::path& folder) {
  CharacterPackageReport report;
  for (size_t index = 0; index < kCharacterPackages.size(); ++index) {
    report.statuses[index] = CheckCharacterPackage(folder, kCharacterPackages[index]);
    if (report.statuses[index] == PackageStatus::kValid) ++report.valid;
  }
  report.evil_ryu = CharacterRequiredPackagesValid(kCharacterPackages, report.statuses, PackageOwner::kEvilRyu);
  report.violent_ken = CharacterRequiredPackagesValid(kCharacterPackages, report.statuses, PackageOwner::kViolentKen);
  g_evil_ryu_ready.store(report.evil_ryu, std::memory_order_relaxed);
  g_violent_ken_ready.store(report.violent_ken, std::memory_order_relaxed);
  return report;
}

bool EvilRyuPackagesReady() { return g_evil_ryu_ready.load(std::memory_order_relaxed); }

bool ViolentKenPackagesReady() { return g_violent_ken_ready.load(std::memory_order_relaxed); }

const char* CharacterPackageNotice() {
  const bool evil_ryu = EvilRyuPackagesReady();
  const bool violent_ken = ViolentKenPackagesReady();
  if (evil_ryu && violent_ken) return nullptr;
  if (!evil_ryu && !violent_ken) return kBothUnavailable;
  return evil_ryu ? kViolentKenUnavailable : kEvilRyuUnavailable;
}

}  // namespace ssf2t::character
