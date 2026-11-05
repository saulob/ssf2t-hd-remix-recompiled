#include "character/violent_ken_data.h"

#include <cstring>
#include <fstream>
#include <mutex>
#include <optional>

namespace ssf2t::character {
namespace {

constexpr char kMagic[] = "SSF2VKG1";
constexpr size_t kMagicSize = 8;
constexpr size_t kHeaderSize = kMagicSize + 12;
constexpr size_t kActionHeader = 4;
constexpr size_t kFrameBytes = 14;
constexpr size_t kPatchBytes = 8;
constexpr size_t kAttackBytes = 18;
constexpr uint32_t kMaxActions = 64;
constexpr uint32_t kMaxPatches = 512;
constexpr uint32_t kMaxAttacks = 128;
constexpr uint8_t kLastField = uint8_t(PackagedField::kAttack);
constexpr std::streamoff kMaxFileSize = 8 << 20;

uint32_t ReadLE32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
uint16_t ReadLE16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

std::optional<std::vector<uint8_t>> ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) return std::nullopt;
  const std::streamoff size = in.tellg();
  if (size <= 0 || size > kMaxFileSize) return std::nullopt;
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  in.seekg(0);
  if (!in.read(reinterpret_cast<char*>(bytes.data()), size)) return std::nullopt;
  return bytes;
}

// Every package loads once, on first use, or stays absent.
template <typename T>
struct Lazy {
  T value{};
  bool attempted = false;
  bool loaded = false;

  template <typename Load>
  const T* Get(Load load) {
    if (!attempted) {
      attempted = true;
      loaded = load(value);
    }
    return loaded ? &value : nullptr;
  }
};

std::mutex g_mutex;
std::filesystem::path g_directory;
Lazy<ViolentKenGameplay> g_gameplay;
Lazy<EvilSprites> g_sprites;
Lazy<EvilRyuPalette> g_palette;
Lazy<EvilRyuHdPalette> g_hd_palette;
Lazy<EvilHudIcon> g_icon;

}  // namespace

bool ParseViolentKenGameplay(const uint8_t* file, size_t size, ViolentKenGameplay* out) {
  if (size < kHeaderSize || std::memcmp(file, kMagic, kMagicSize) != 0) return false;
  const uint32_t actions = ReadLE32(file + kMagicSize);
  const uint32_t patches = ReadLE32(file + kMagicSize + 4);
  const uint32_t attacks = ReadLE32(file + kMagicSize + 8);
  if (actions > kMaxActions || patches > kMaxPatches || attacks > kMaxAttacks) return false;
  *out = {};
  size_t at = kHeaderSize;
  for (uint32_t i = 0; i < actions; ++i) {
    if (size - at < kActionHeader) return false;
    PackagedAction action;
    action.action = file[at];
    action.sub = file[at + 1];
    const size_t frames = file[at + 2];
    at += kActionHeader;
    if (frames == 0 || size - at < frames * kFrameBytes) return false;
    for (size_t f = 0; f < frames; ++f, at += kFrameBytes) {
      const uint8_t* p = file + at;
      action.frames.push_back({ReadLE16(p), ReadLE16(p + 2), ReadLE16(p + 4), p[6], p[7], p[8], p[9], p[10], p[11],
                               p[12], p[13] != 0});
    }
    out->actions.push_back(std::move(action));
  }
  if (size - at < size_t(patches) * kPatchBytes) return false;
  for (uint32_t i = 0; i < patches; ++i, at += kPatchBytes) {
    const uint8_t* p = file + at;
    if (p[3] > kLastField) return false;
    out->patches.push_back({p[0], p[1], p[2], PackagedField(p[3]), p[4], p[5]});
  }
  if (size - at != size_t(attacks) * kAttackBytes) return false;
  for (uint32_t i = 0; i < attacks; ++i, at += kAttackBytes) {
    PackagedAttack attack;
    attack.id = file[at];
    attack.replace = file[at + 1] != 0;
    std::memcpy(attack.record, file + at + 2, kAttackRecordBytes);
    out->attacks.push_back(attack);
  }
  return true;
}

void SetViolentKenDirectory(const std::filesystem::path& directory) {
  std::lock_guard lock(g_mutex);
  g_directory = directory;
}

const ViolentKenGameplay* LoadedViolentKenGameplay() {
  std::lock_guard lock(g_mutex);
  return g_gameplay.Get([](ViolentKenGameplay& value) {
    const auto bytes = ReadFile(g_directory / kViolentKenGameplayFile);
    return bytes && ParseViolentKenGameplay(bytes->data(), bytes->size(), &value);
  });
}

bool ViolentKenGameplayReady() { return LoadedViolentKenGameplay() != nullptr; }

const EvilSpriteImage* LoadedViolentKenArt(EvilArt art) {
  std::lock_guard lock(g_mutex);
  const EvilSprites* sprites = g_sprites.Get([](EvilSprites& value) {
    const auto bytes = ReadFile(g_directory / kViolentKenSpriteFile);
    return bytes && ParseEvilSprites(bytes->data(), bytes->size(), &value);
  });
  return sprites ? &sprites->Get(art) : nullptr;
}

const EvilRyuPalette* LoadedViolentKenPalette() {
  std::lock_guard lock(g_mutex);
  return g_palette.Get([](EvilRyuPalette& value) {
    return LoadEvilRyuPalette(g_directory / kViolentKenPaletteFile, &value);
  });
}

const EvilRyuHdPalette* LoadedViolentKenHdPalette() {
  std::lock_guard lock(g_mutex);
  return g_hd_palette.Get([](EvilRyuHdPalette& value) {
    const auto bytes = ReadFile(g_directory / kViolentKenHdPaletteFile);
    return bytes && ParseEvilRyuHdPalette(bytes->data(), bytes->size(), &value);
  });
}

const EvilHudIcon* LoadedViolentKenHudIcon() {
  std::lock_guard lock(g_mutex);
  return g_icon.Get([](EvilHudIcon& value) {
    const auto bytes = ReadFile(g_directory / kViolentKenHudIconFile);
    return bytes && ParseEvilHudIcon(bytes->data(), bytes->size(), &value);
  });
}

}  // namespace ssf2t::character
