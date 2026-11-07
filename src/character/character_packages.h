#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

namespace ssf2t::character {

enum class PackageOwner : uint8_t { kBoth, kEvilRyu, kViolentKen };

// A required package defines the fighter: without it the fighter is disabled. An optional one has an intentional
// fallback to the carrier's own voice, ending, icon or name.
enum class PackageNeed : uint8_t { kRequired, kOptional };

struct CharacterPackage {
  std::string_view file;
  uint64_t size;
  std::string_view sha256;
  PackageOwner owner;
  PackageNeed need;
};

// Beside the executable, never in the game folder: Reinstall Game replaces that folder.
inline std::filesystem::path CharacterPackageFolder(const std::filesystem::path& executable_folder) {
  return executable_folder / "assets" / "characters";
}

// The one list of packages. CMake and the packaging scripts parse these lines, so keep one entry per line;
// tools/update-character-packages.ps1 rewrites the sizes and hashes after the packages are regenerated.
inline constexpr std::array<CharacterPackage, 16> kCharacterPackages = {{
    {"ultra_portraits.bin", 131108, "9bcfadada611fa28fee9dec46f6734cda671e7506719a1cc33163b9e9a3e6b87", PackageOwner::kBoth, PackageNeed::kRequired},
    {"evil_ryu_sprites.bin", 647612, "bf8597f035b72d59f832f2bdbd0d69fd0677b935edd0eaf2290b5eb6af15a534", PackageOwner::kEvilRyu, PackageNeed::kRequired},
    {"evil_ryu_palette.bin", 652, "6a4072845cc451257aef8b1124dc54d540b37e4513b7de0258940e3e90a818f7", PackageOwner::kEvilRyu, PackageNeed::kRequired},
    {"evil_ryu_hd_palette.bin", 11276, "8ff6e5e1284f50659cce0a9e68113ad1588b7e4dfa972022a8fcfa9ee2c255dc", PackageOwner::kEvilRyu, PackageNeed::kRequired},
    {"evil_ryu_assets.bin", 1222692, "6810007fd45259747c406213ebb048232e9a2a24735014b5a6caa77d57b2fc98", PackageOwner::kEvilRyu, PackageNeed::kOptional},
    {"evil_ryu_hud_icon.bin", 65544, "6d984f939b68a9e80bb129638b77b9f5925a13c8cc2f57e89c2f8b0ecf918c11", PackageOwner::kEvilRyu, PackageNeed::kOptional},
    {"evil_ryu_audio.bin", 980814, "2f549a626642ead0e9af36f57338dc58a86291125fc40ebf93870cfdb7a5824c", PackageOwner::kEvilRyu, PackageNeed::kOptional},
    {"evil_ryu_ending.bin", 2097164, "c601d72156cc6bdf16696d627c0d388044c1e84a90b98502ab6956da880d4d47", PackageOwner::kEvilRyu, PackageNeed::kOptional},
    {"violent_ken_gameplay.bin", 3578, "d6e244f45e069910dcbc9d56b240a6bf9ec3d6f8f29c38fa67e11f3ecaefbeaf", PackageOwner::kViolentKen, PackageNeed::kRequired},
    {"violent_ken_sprites.bin", 782380, "c0c2cf616ad4af67511c0713b1e85c50f26ddc7da1a8181b819bd98d9958302b", PackageOwner::kViolentKen, PackageNeed::kRequired},
    {"violent_ken_palette.bin", 652, "4177201d93403ae301a34ec7af6fa5d797f031bdbfa94853105cae469dc281a6", PackageOwner::kViolentKen, PackageNeed::kRequired},
    {"violent_ken_hd_palette.bin", 11276, "40df5d546348924d1566b47a39107cd40afc868b98dde39f54c2b8635bd436ce", PackageOwner::kViolentKen, PackageNeed::kRequired},
    {"violent_ken_assets.bin", 2332752, "c5d1010601fd7da09b4243a4fa3a93d0d9f9a622ac9e1e3c7418bf0877d4e8ca", PackageOwner::kViolentKen, PackageNeed::kOptional},
    {"violent_ken_hud_icon.bin", 65544, "2379605554886b2f4572b11f4da4c8d06bbfdeb7ce4f276fb045a5df887d10d2", PackageOwner::kViolentKen, PackageNeed::kOptional},
    {"violent_ken_audio.bin", 1341544, "d47cd72f712af75995c5dcda0fb18827bc78b529ff086a847650f61e297d54cf", PackageOwner::kViolentKen, PackageNeed::kOptional},
    {"violent_ken_ending.bin", 2097164, "56ac81efaf66af0e57632397c681449a9e3da0858111c8a60deb8161c64cd221", PackageOwner::kViolentKen, PackageNeed::kOptional},
}};

enum class PackageStatus : uint8_t { kValid, kMissing, kDifferent };

const char* PackageStatusName(PackageStatus status);

PackageStatus CheckCharacterPackage(const std::filesystem::path& folder, const CharacterPackage& package);

// `statuses` runs parallel to `table`. True when every required package of `owner` is valid.
bool CharacterRequiredPackagesValid(std::span<const CharacterPackage> table, std::span<const PackageStatus> statuses,
                                    PackageOwner owner);

struct CharacterPackageReport {
  std::array<PackageStatus, kCharacterPackages.size()> statuses{};
  size_t valid = 0;
  bool evil_ryu = false;
  bool violent_ken = false;
};

// Checks the installed packages and fixes the result for this run; a fighter is unavailable until it has been called.
CharacterPackageReport ValidateCharacterPackages(const std::filesystem::path& folder);
bool EvilRyuPackagesReady();
bool ViolentKenPackagesReady();

// What the launcher says when a required package of either fighter is not valid; null when both are ready.
const char* CharacterPackageNotice();

}  // namespace ssf2t::character
