#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#include "character/evil_ryu_audio.h"
#include "character/ultra_audio.h"
#include "character/ultra_fight.h"
#include "guest_memory.h"
#include "netplay/guest_netplay.h"
#include "title_update_image.h"

namespace ssf2t::character {
namespace {

struct Base013Audio {
  using Netplay = netplay::Base013Netplay;
  static constexpr uint32_t kSoundFlags = 0x83A37DB0;
  static constexpr uint32_t kSoundSlots = 0x83C2AEE8;
};

#if SSF2T_TITLE_UPDATE_IMAGE
struct TitleUpdate023Audio {
  using Netplay = netplay::TitleUpdate023Netplay;
  static constexpr uint32_t kSoundFlags = 0x83A37E28;
  static constexpr uint32_t kSoundSlots = 0x83C2B260;
};
#endif

constexpr uint32_t kSlotBytes = 28;
constexpr uint32_t kSlotFormat = 0;
constexpr uint32_t kSlotData = 4;
constexpr uint32_t kSlotDataBytes = 8;
constexpr uint32_t kSlotVoice = 12;
constexpr uint32_t kSlotId = 16;
constexpr uint32_t kSlotName = 24;
constexpr uint32_t kUnusedSound = 0xFFFFFFFF;

constexpr uint32_t kFormatBytes = 18;
constexpr uint16_t kFormatPcm = 1;
constexpr uint16_t kChannels = 1;
constexpr uint16_t kBytesPerSample = 2;
constexpr uint16_t kBitsPerSample = 16;

constexpr uint32_t kPlayCommand = 1;
constexpr uint32_t kFightWorkLoserSide = 1356;
constexpr uint32_t kFightWorkWinner = 1412;
constexpr uint32_t kAnnouncerCharacter = 33;
constexpr uint32_t kAnnouncerDelay = 130;

std::atomic<bool> g_installed{false};
std::atomic<bool> g_announce_name{false};
std::array<std::atomic<bool>, 2> g_raging_demon{};
std::atomic<int> g_announcer_frames{0};

uint32_t GuestAlloc(uint32_t bytes) { return REX_KERNEL_MEMORY()->SystemHeapAlloc(bytes); }

// The guest's Sound.cpp loader accepts 16-bit PCM beside XMA2 and keeps it big endian in a format record of its own, so
// a slot built the same way mixes through the guest's own voices: same volume, pan, pause and voice stealing.
uint32_t BuildFormat(const EvilClipData& clip, uint8_t* base) {
  const uint32_t format = GuestAlloc(kFormatBytes);
  if (!format) return 0;
  GuestStoreBE16(base, format + 0, kFormatPcm);
  GuestStoreBE16(base, format + 2, kChannels);
  GuestStoreBE32(base, format + 4, clip.rate);
  GuestStoreBE32(base, format + 8, clip.rate * kBytesPerSample);
  GuestStoreBE16(base, format + 12, kBytesPerSample);
  GuestStoreBE16(base, format + 14, kBitsPerSample);
  GuestStoreBE16(base, format + 16, 0);
  return format;
}

uint32_t BuildSamples(const EvilClipData& clip, uint8_t* base) {
  const uint32_t bytes = uint32_t(clip.samples.size()) * kBytesPerSample;
  const uint32_t data = GuestAlloc(bytes);
  if (!data) return 0;
  uint8_t* out = GuestHost(base, data);
  for (const int16_t sample : clip.samples) {
    *out++ = uint8_t(uint16_t(sample) >> 8);
    *out++ = uint8_t(sample);
  }
  return data;
}

template <typename H>
bool InstallSounds(uint8_t* base) {
  const EvilAudioPackage* package = LoadedEvilAudio();
  if (!package) return false;
  struct Built {
    uint32_t format = 0;
    uint32_t data = 0;
  };
  std::array<Built, kEvilClipCount> built{};
  const uint32_t name = GuestAlloc(16);
  if (!name) return false;
  std::memcpy(GuestHost(base, name), "EvilRyuSound", 13);
  for (const EvilSound& sound : kEvilSounds) {
    if (LoadBE32(GuestHost(base, H::kSoundFlags + sound.id * 4u), 0) != kUnusedSound) return false;
    const EvilClipData& clip = package->Clip(sound.clip);
    Built& entry = built[size_t(sound.clip) - 1];
    if (!entry.data) {
      entry.format = BuildFormat(clip, base);
      entry.data = BuildSamples(clip, base);
      if (!entry.format || !entry.data) return false;
    }
    const uint32_t slot = H::kSoundSlots + sound.id * kSlotBytes;
    GuestStoreBE32(base, slot + kSlotFormat, entry.format);
    GuestStoreBE32(base, slot + kSlotData, entry.data);
    GuestStoreBE32(base, slot + kSlotDataBytes, uint32_t(clip.samples.size()) * kBytesPerSample);
    GuestStoreBE32(base, slot + kSlotVoice, 0);
    GuestStoreBE32(base, slot + kSlotId, sound.id);
    GuestStoreBE32(base, slot + kSlotName, name);
    GuestStoreBE32(base, H::kSoundFlags + sound.id * 4u,
                   LoadBE32(GuestHost(base, H::kSoundFlags + sound.carrier * 4u), 0));
  }
  g_announcer_frames.store(EvilAnnouncerFrames(package->Clip(EvilClip::kAnnouncerName)), std::memory_order_relaxed);
  return true;
}

// Runs once at boot; the slots of ids the flag table marks unused are never touched by the guest's loader.
template <typename H>
void LoadSounds(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  original(ctx, base);
  if (!UltraFightersEnabled()) return;
  const bool installed = InstallSounds<H>(base);
  g_installed.store(installed, std::memory_order_relaxed);
  if (installed) {
    REXLOG_INFO("DEBUG_SAULO EVIL_RYU audio installed sounds={}", kEvilSounds.size());
  } else {
    REXLOG_ERROR("DEBUG_SAULO EVIL_RYU audio unavailable, Ryu voices stay");
  }
}

void LogSwap(uint16_t sound, uint16_t replacement) {
  static std::array<bool, 16> logged{};
  bool& seen = logged[size_t(replacement - kEvilVoiceP1) % logged.size()];
  if (seen) return;
  seen = true;
  REXLOG_INFO("DEBUG_SAULO EVIL_RYU audio sound={} plays={:#x}", sound, replacement);
}

template <typename H>
EvilSoundState CurrentState(uint8_t* base) {
  using G = typename H::Netplay;
  EvilSoundState state;
  state.evil = {EvilRyuOnSide<G>(base, 0), EvilRyuOnSide<G>(base, 1)};
  state.raging_demon = {g_raging_demon[0].load(std::memory_order_relaxed),
                        g_raging_demon[1].load(std::memory_order_relaxed)};
  const uint32_t work = LoadBE32(GuestHost(base, G::kFightWork), 0);
  if (work >= 0x10000) state.ko_side = int(*GuestHost(base, work + kFightWorkLoserSide));
  state.announce_name = g_announce_name.load(std::memory_order_relaxed);
  return state;
}

// Every queued sound reaches the guest here as (command, channel, id, level). The volume and pan commands ran first and
// are global, so swapping the id keeps them. The guest counts a zero return as played, so nothing retries the Ryu clip.
template <typename H>
void RequestSound(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (g_installed.load(std::memory_order_relaxed) && (ctx.r3.u32 & 0xFFFF) == kPlayCommand) {
    const uint16_t sound = uint16_t(ctx.r5.u32);
    const EvilSoundState state = CurrentState<H>(base);
    const uint16_t replacement = EvilSoundFor(sound, state);
    if (replacement) {
      ctx.r5.u64 = replacement;
      LogSwap(sound, replacement);
      if (sound == kRyuAnnouncerNameSound) g_announce_name.store(false, std::memory_order_relaxed);
      if (replacement == kEvilVoiceP1 + kEvilRagingDemonOffset) g_raging_demon[0].store(false, std::memory_order_relaxed);
      if (replacement == kEvilVoiceP2 + kEvilRagingDemonOffset) g_raging_demon[1].store(false, std::memory_order_relaxed);
    }
  }
  original(ctx, base);
}

// First state of the winner announcer object: says the name, then waits the delay it stores before saying "wins".
template <typename H>
void AnnounceWinnerName(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  using G = typename H::Netplay;
  const uint32_t work = LoadBE32(GuestHost(base, G::kFightWork), 0);
  const uint32_t object = LoadBE32(GuestHost(base, G::kFightWork - 4), 0);
  bool evil = false;
  if (g_installed.load(std::memory_order_relaxed) && work >= 0x10000 && object >= 0x10000) {
    const int winner = int(*GuestHost(base, work + kFightWorkWinner)) - 1;
    evil = *GuestHost(base, object + kAnnouncerCharacter) == uint8_t(kRyuGuestCharacter) &&
           EvilRyuOnSide<G>(base, winner);
  }
  if (evil) g_announce_name.store(true, std::memory_order_relaxed);
  original(ctx, base);
  if (evil) *GuestHost(base, object + kAnnouncerDelay) = uint8_t(g_announcer_frames.load(std::memory_order_relaxed));
}

}  // namespace

bool MarkRagingDemonVoice(int side) {
  if (side < 0 || side > 1 || !g_installed.load(std::memory_order_relaxed)) return false;
  g_raging_demon[side].store(true, std::memory_order_relaxed);
  return true;
}

}  // namespace ssf2t::character

REX_EXTERN(__imp__base013_sub_823D5D18);
REX_HOOK_RAW(base013_sub_823D5D18) {
  ssf2t::character::LoadSounds<ssf2t::character::Base013Audio>(ctx, base, __imp__base013_sub_823D5D18);
}
REX_EXTERN(__imp__base013_sub_82375C60);
REX_HOOK_RAW(base013_sub_82375C60) {
  ssf2t::character::RequestSound<ssf2t::character::Base013Audio>(ctx, base, __imp__base013_sub_82375C60);
}
REX_EXTERN(__imp__base013_sub_82342148);
REX_HOOK_RAW(base013_sub_82342148) {
  ssf2t::character::AnnounceWinnerName<ssf2t::character::Base013Audio>(ctx, base, __imp__base013_sub_82342148);
}

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_823D63D8);
REX_HOOK_RAW(tu023_sub_823D63D8) {
  ssf2t::character::LoadSounds<ssf2t::character::TitleUpdate023Audio>(ctx, base, __imp__tu023_sub_823D63D8);
}
REX_EXTERN(__imp__tu023_sub_82375D30);
REX_HOOK_RAW(tu023_sub_82375D30) {
  ssf2t::character::RequestSound<ssf2t::character::TitleUpdate023Audio>(ctx, base, __imp__tu023_sub_82375D30);
}
REX_EXTERN(__imp__tu023_sub_82342048);
REX_HOOK_RAW(tu023_sub_82342048) {
  ssf2t::character::AnnounceWinnerName<ssf2t::character::TitleUpdate023Audio>(ctx, base, __imp__tu023_sub_82342048);
}
#endif
