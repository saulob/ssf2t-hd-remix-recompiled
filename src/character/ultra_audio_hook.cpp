#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <tuple>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#include "character/evil_ryu_audio.h"
#include "character/ultra_audio.h"
#include "character/ultra_fight.h"
#include "character/violent_ken_audio.h"
#include "guest_memory.h"
#include "netplay/fight_snapshot.h"
#include "netplay/guest_netplay.h"
#include "netplay/guest_simulation.h"
#include "title_update_image.h"

namespace ssf2t::character {
namespace {

struct Base013Audio {
  using Netplay = netplay::Base013Netplay;
  static constexpr uint32_t kSoundFlags = 0x83A37DB0;
  static constexpr uint32_t kSoundSlots = 0x83C2AEE8;
  static constexpr uint32_t kSoundQueueHead = 0x83CD204C;
  static constexpr uint32_t kSoundQueueTail = 0x83CD4050;
  static constexpr uint32_t kSoundQueue = 0x83CD4058;
};

#if SSF2T_TITLE_UPDATE_IMAGE
struct TitleUpdate023Audio {
  using Netplay = netplay::TitleUpdate023Netplay;
  static constexpr uint32_t kSoundFlags = 0x83A37E28;
  static constexpr uint32_t kSoundSlots = 0x83C2B260;
  static constexpr uint32_t kSoundQueueHead = 0x83CD2564;
  static constexpr uint32_t kSoundQueueTail = 0x83CD4568;
  static constexpr uint32_t kSoundQueue = 0x83CD4570;
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
constexpr uint32_t kQueueEntryBytes = 16;
constexpr uint32_t kQueueEntrySound = 2;
constexpr uint32_t kQueueEntries = 256;
constexpr uint32_t kAnnouncerCharacter = 33;
constexpr uint32_t kAnnouncerDelay = 130;

std::atomic<bool> g_installed{false};
std::atomic<bool> g_announce_name{false};
std::array<std::atomic<bool>, 2> g_raging_demon{};
std::atomic<int> g_announcer_frames{0};
std::atomic<bool> g_violent_installed{false};
std::atomic<bool> g_violent_announce_name{false};
std::array<std::atomic<ViolentMoveVoice>, 2> g_violent_move_voice{};
std::atomic<int> g_violent_announcer_frames{0};
std::atomic<int> g_round_end_ko_slot{-1};
std::atomic<int> g_round_end_ko_side{-1};

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

// The guest frees what each slot points at when it unloads, so no two slots may share an allocation.
template <typename H, typename Package, typename Sounds>
bool InstallSounds(uint8_t* base, const Package* package, const Sounds& sounds, const char* label) {
  if (!package) return false;
  for (const auto& sound : sounds) {
    if (LoadBE32(GuestHost(base, H::kSoundFlags + sound.id * 4u), 0) != kUnusedSound) return false;
    const EvilClipData& clip = package->Clip(sound.clip);
    const uint32_t format = BuildFormat(clip, base);
    const uint32_t data = BuildSamples(clip, base);
    const uint32_t name = GuestAlloc(16);
    if (!format || !data || !name) return false;
    std::memcpy(GuestHost(base, name), label, std::strlen(label) + 1);
    const uint32_t slot = H::kSoundSlots + sound.id * kSlotBytes;
    GuestStoreBE32(base, slot + kSlotFormat, format);
    GuestStoreBE32(base, slot + kSlotData, data);
    GuestStoreBE32(base, slot + kSlotDataBytes, uint32_t(clip.samples.size()) * kBytesPerSample);
    GuestStoreBE32(base, slot + kSlotVoice, 0);
    GuestStoreBE32(base, slot + kSlotId, sound.id);
    GuestStoreBE32(base, slot + kSlotName, name);
    GuestStoreBE32(base, H::kSoundFlags + sound.id * 4u,
                   LoadBE32(GuestHost(base, H::kSoundFlags + sound.carrier * 4u), 0));
  }
  return true;
}

// Runs once at boot; the slots of ids the flag table marks unused are never touched by the guest's loader.
template <typename H>
void LoadSounds(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  original(ctx, base);
  if (!UltraFightersEnabled()) return;
  const EvilAudioPackage* evil = LoadedEvilAudio();
  const bool installed = InstallSounds<H>(base, evil, kEvilSounds, "EvilRyuSound");
  g_installed.store(installed, std::memory_order_relaxed);
  if (installed) {
    g_announcer_frames.store(EvilAnnouncerFrames(evil->Clip(EvilClip::kAnnouncerName)), std::memory_order_relaxed);
    REXLOG_INFO("DEBUG_SAULO EVIL_RYU audio installed sounds={}", kEvilSounds.size());
  } else {
    REXLOG_ERROR("DEBUG_SAULO EVIL_RYU audio unavailable, Ryu voices stay");
  }
  const ViolentKenAudioPackage* violent = LoadedViolentKenAudio();
  const bool violent_installed = InstallSounds<H>(base, violent, kViolentSounds, "VioKenSound");
  g_violent_installed.store(violent_installed, std::memory_order_relaxed);
  if (violent_installed) {
    g_violent_announcer_frames.store(EvilAnnouncerFrames(violent->Clip(ViolentClip::kAnnouncerName)),
                                     std::memory_order_relaxed);
    REXLOG_INFO("DEBUG_SAULO VIOLENT_KEN audio installed sounds={}", kViolentSounds.size());
  } else {
    REXLOG_ERROR("DEBUG_SAULO VIOLENT_KEN audio unavailable, Ken voices stay");
  }
}

void LogSwap(uint16_t sound, uint16_t replacement) {
  static std::array<bool, 16> logged{};
  bool& seen = logged[size_t(replacement - kEvilVoiceP1) % logged.size()];
  if (seen) return;
  seen = true;
  REXLOG_INFO("DEBUG_SAULO EVIL_RYU audio sound={} plays={:#x}", sound, replacement);
}

void LogViolentSwap(uint16_t sound, uint16_t replacement) {
  static std::array<bool, 16> logged{};
  bool& seen = logged[size_t(replacement - kViolentVoiceP1) % logged.size()];
  if (seen) return;
  seen = true;
  REXLOG_INFO("DEBUG_SAULO VIOLENT_KEN audio sound={} plays={:#x}", sound, replacement);
}

// The queue drains later, when the fight work may already have moved on, so the side is kept by queue slot.
template <typename H>
int RoundEndKoSide(const uint8_t* base) {
  const int head = LoadBE16(GuestHost(base, H::kSoundQueueHead), 0);
  return head == g_round_end_ko_slot.load(std::memory_order_relaxed) ? g_round_end_ko_side.load(std::memory_order_relaxed)
                                                                     : -1;
}

template <typename H>
EvilSoundState CurrentState(uint8_t* base) {
  using G = typename H::Netplay;
  EvilSoundState state;
  state.evil = {EvilRyuOnSide<G>(base, 0), EvilRyuOnSide<G>(base, 1)};
  state.raging_demon = {g_raging_demon[0].load(std::memory_order_relaxed),
                        g_raging_demon[1].load(std::memory_order_relaxed)};
  state.round_end_ko_side = RoundEndKoSide<H>(base);
  state.announce_name = g_announce_name.load(std::memory_order_relaxed);
  return state;
}

template <typename H>
ViolentSoundState CurrentViolentState(uint8_t* base) {
  using G = typename H::Netplay;
  ViolentSoundState state;
  state.violent = {ViolentKenOnSide<G>(base, 0), ViolentKenOnSide<G>(base, 1)};
  state.move_voice = {g_violent_move_voice[0].load(std::memory_order_relaxed),
                      g_violent_move_voice[1].load(std::memory_order_relaxed)};
  state.round_end_ko_side = RoundEndKoSide<H>(base);
  state.announce_name = g_violent_announce_name.load(std::memory_order_relaxed);
  return state;
}

void ConsumeViolentMarks(uint16_t sound, uint16_t replacement) {
  if (sound == kKenAnnouncerNameSound) g_violent_announce_name.store(false, std::memory_order_relaxed);
  for (int side = 0; side < 2; ++side) {
    const uint16_t first = side == 0 ? kViolentVoiceP1 : kViolentVoiceP2;
    if (replacement == first + kViolentRasetsuOffset || replacement == first + kViolentShinbuOffset) {
      g_violent_move_voice[side].store(ViolentMoveVoice::kNone, std::memory_order_relaxed);
    }
  }
}

// Every queued sound reaches the guest here as (command, channel, id, level). The volume and pan commands ran first and
// are global, so swapping the id keeps them. The guest counts a zero return as played, so nothing retries the Ryu clip.
template <typename H>
void RequestSound(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  const uint16_t sound = uint16_t(ctx.r5.u32);
  if (g_violent_installed.load(std::memory_order_relaxed) && (ctx.r3.u32 & 0xFFFF) == kPlayCommand) {
    const uint16_t replacement = ViolentSoundFor(sound, CurrentViolentState<H>(base));
    if (replacement) {
      ctx.r5.u64 = replacement;
      LogViolentSwap(sound, replacement);
      ConsumeViolentMarks(sound, replacement);
    }
  }
  if (g_installed.load(std::memory_order_relaxed) && (ctx.r3.u32 & 0xFFFF) == kPlayCommand) {
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
  if ((sound == kKenKoSound || sound == kRyuKoSound || sound == kKenContinueSound) && RoundEndKoSide<H>(base) >= 0) {
    g_round_end_ko_slot.store(-1, std::memory_order_relaxed);
  }
  original(ctx, base);
}

// The only KO call that uses the P1 id for either side, and the Continue call whose P2 id does the same. A replayed
// frame queues nothing, so nothing is tagged.
template <typename H>
void RoundEndScream(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  using G = typename H::Netplay;
  uint32_t slot = LoadBE16(GuestHost(base, H::kSoundQueueTail), 0);
  original(ctx, base);
  const uint32_t tail = LoadBE16(GuestHost(base, H::kSoundQueueTail), 0);
  const uint32_t work = LoadBE32(GuestHost(base, G::kFightWork), 0);
  if (work < 0x10000) return;
  for (; slot != tail; slot = (slot + 1) % kQueueEntries) {
    const uint32_t entry = H::kSoundQueue + slot * kQueueEntryBytes;
    const uint16_t sound = LoadBE16(GuestHost(base, entry + kQueueEntrySound), 0);
    if (sound != kKenKoSound && sound != kRyuKoSound && sound != kKenContinueSound) continue;
    g_round_end_ko_side.store(int(*GuestHost(base, work + kFightWorkLoserSide)), std::memory_order_relaxed);
    g_round_end_ko_slot.store(int(slot), std::memory_order_relaxed);
  }
}

// First state of the winner announcer object: says the name, then waits the delay it stores before saying "wins".
template <typename H>
void AnnounceWinnerName(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  using G = typename H::Netplay;
  const uint32_t work = LoadBE32(GuestHost(base, G::kFightWork), 0);
  const uint32_t object = LoadBE32(GuestHost(base, G::kFightWork - 4), 0);
  bool evil = false;
  bool violent = false;
  if (work >= 0x10000 && object >= 0x10000) {
    const int winner = int(*GuestHost(base, work + kFightWorkWinner)) - 1;
    const uint8_t character = *GuestHost(base, object + kAnnouncerCharacter);
    evil = g_installed.load(std::memory_order_relaxed) && character == uint8_t(kRyuGuestCharacter) &&
           EvilRyuOnSide<G>(base, winner);
    violent = g_violent_installed.load(std::memory_order_relaxed) && character == uint8_t(kKenGuestCharacter) &&
              ViolentKenOnSide<G>(base, winner);
  }
  if (evil) g_announce_name.store(true, std::memory_order_relaxed);
  if (violent) g_violent_announce_name.store(true, std::memory_order_relaxed);
  original(ctx, base);
  if (evil) *GuestHost(base, object + kAnnouncerDelay) = uint8_t(g_announcer_frames.load(std::memory_order_relaxed));
  if (violent) {
    *GuestHost(base, object + kAnnouncerDelay) = uint8_t(g_violent_announcer_frames.load(std::memory_order_relaxed));
  }
}

}  // namespace

bool MarkRagingDemonVoice(int side) {
  if (!HostVoiceQueues(g_installed.load(std::memory_order_relaxed), side, netplay::SuppressEffects())) return false;
  g_raging_demon[side].store(true, std::memory_order_relaxed);
  return true;
}

bool MarkViolentMoveVoice(int side, ViolentMoveVoice voice) {
  if (!HostVoiceQueues(g_violent_installed.load(std::memory_order_relaxed), side, netplay::SuppressEffects())) return false;
  g_violent_move_voice[side].store(voice, std::memory_order_relaxed);
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
REX_EXTERN(__imp__base013_sub_8223CDD0);
REX_HOOK_RAW(base013_sub_8223CDD0) {
  ssf2t::character::RoundEndScream<ssf2t::character::Base013Audio>(ctx, base, __imp__base013_sub_8223CDD0);
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
REX_EXTERN(__imp__tu023_sub_8223CEF8);
REX_HOOK_RAW(tu023_sub_8223CEF8) {
  ssf2t::character::RoundEndScream<ssf2t::character::TitleUpdate023Audio>(ctx, base, __imp__tu023_sub_8223CEF8);
}
REX_EXTERN(__imp__tu023_sub_82342048);
REX_HOOK_RAW(tu023_sub_82342048) {
  ssf2t::character::AnnounceWinnerName<ssf2t::character::TitleUpdate023Audio>(ctx, base, __imp__tu023_sub_82342048);
}
#endif
