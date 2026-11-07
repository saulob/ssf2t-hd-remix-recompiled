#pragma once

#include <cstdint>
#include <iterator>

#include "character/select_mode.h"

namespace ssf2t::character {

inline constexpr char kAkumaBugFixesSetting[] = "ssf2t_akuma_bug_fixes";
inline constexpr bool kDefaultAkumaBugFixes = false;
inline constexpr char kAkumaBugFixesLabel[] = "Akuma bug fixes";
inline constexpr char kAkumaBugFixesHelp[] =
    "Experimental. Fixes HD Remix Akuma bugs documented by designer David Sirlin: red fireball startup, Dragon Punch "
    "startup and the Raging Demon escape after blockstun. Classic Akuma and online matches stay original.";

struct Base013AkumaFixes {
  static constexpr uint32_t kInitFighter1 = 0x82389310;
  static constexpr uint32_t kThrowAttempt1 = 0x8238D028;
  static constexpr uint32_t kRemixFighterData = 0x83A37250;
  static constexpr uint32_t kHdStyle = 0x83A3876D;
  static constexpr uint32_t kFighterBlock = 0x83E09CC0;
};

struct TitleUpdate023AkumaFixes {
  static constexpr uint32_t kInitFighter1 = 0x823893E0;
  static constexpr uint32_t kThrowAttempt1 = 0x8238D018;
  static constexpr uint32_t kRemixFighterData = 0x83A37250;
  static constexpr uint32_t kHdStyle = 0x83A387E5;
  static constexpr uint32_t kFighterBlock = 0x83E0A1D8;
};

inline constexpr uint32_t kAkumaFighterSlot = 16;
// A selected Akuma animates from the fourth table of his block (fighter table index 0x21), not the first.
inline constexpr uint32_t kAkumaSelectedActionsOffset = 0xC;
inline constexpr uint32_t kAkumaFrameBytes = 24;
inline constexpr uint32_t kAkumaHurtOffset = 8;
inline constexpr uint32_t kAkumaAttackOffset = 12;
inline constexpr uint8_t kAkumaFireballAction = 0x4C;
inline constexpr uint8_t kAkumaDragonPunchAction = 0x4E;
inline constexpr uint8_t kAkumaRedFireballSub = 3;

// Two unset ids are the bug: the guest's throw test refuses a frame whose three hurt ids are all zero, and such a frame
// has no hurt box. Id 1 is what the same move uses from its third frame on.
inline constexpr uint8_t kAkumaStandingHurtId = 1;

// Frames 0 and 1 of the Dragon Punch are one pose, the startup pose Ryu shows for 4 ticks, and only Akuma's second half
// carries a hit. Without it the first hit is the attack id Ryu's move starts with, on the same pose.
inline constexpr uint8_t kAkumaEarlyDragonPunchFrame = 1;
inline constexpr uint8_t kAkumaEarlyDragonPunchAttacks[] = {54, 55, 56};

enum class AkumaFrameField : uint8_t { kAttack, kHurtHead, kHurtBody, kHurtLegs };

struct AkumaFramePatch {
  uint8_t action;
  uint8_t sub;
  uint8_t frame;
  AkumaFrameField field;
  uint8_t original;
  uint8_t fixed;
};

template <uint8_t Sub, uint8_t Frame>
inline constexpr AkumaFramePatch kRedHead = {kAkumaFireballAction, Sub, Frame, AkumaFrameField::kHurtHead, 0,
                                             kAkumaStandingHurtId};
template <uint8_t Sub, uint8_t Frame>
inline constexpr AkumaFramePatch kRedBody = {kAkumaFireballAction, Sub, Frame, AkumaFrameField::kHurtBody, 0,
                                             kAkumaStandingHurtId};
template <uint8_t Sub, uint8_t Frame>
inline constexpr AkumaFramePatch kRedLegs = {kAkumaFireballAction, Sub, Frame, AkumaFrameField::kHurtLegs, 0,
                                             kAkumaStandingHurtId};
template <uint8_t Sub>
inline constexpr AkumaFramePatch kDragonPunch = {kAkumaDragonPunchAction, Sub, kAkumaEarlyDragonPunchFrame,
                                                 AkumaFrameField::kAttack, kAkumaEarlyDragonPunchAttacks[Sub], 0};

inline constexpr AkumaFramePatch kAkumaFramePatches[] = {
    kRedHead<3, 0>, kRedBody<3, 0>, kRedLegs<3, 0>, kRedHead<3, 1>, kRedBody<3, 1>, kRedLegs<3, 1>,
    kRedHead<4, 0>, kRedBody<4, 0>, kRedLegs<4, 0>, kRedHead<4, 1>, kRedBody<4, 1>, kRedLegs<4, 1>,
    kRedHead<5, 0>, kRedBody<5, 0>, kRedLegs<5, 0>, kRedHead<5, 1>, kRedBody<5, 1>, kRedLegs<5, 1>,
    kDragonPunch<0>, kDragonPunch<1>, kDragonPunch<2>,
};

struct AkumaFixesInstalled {
  bool raging_demon = false;
  bool red_fireball = false;
  bool dragon_punch = false;
  bool air_fireball = false;
};

constexpr AkumaFixesInstalled kAkumaFixesWhenApplied = {true, true, true, false};

// No build negotiates this option, so both online peers must play the original data or their simulations diverge.
// Style is no input: these frames exist only in the Remix fighter data, which a Classic fight never reads.
constexpr bool AkumaBugFixesActive(bool option, uint8_t game_mode, bool developer_netplay) {
  return option && !OnlineSelectMode(game_mode) && !developer_netplay;
}

inline constexpr uint8_t kFighterStateBlockStance = 8;
inline constexpr uint8_t kFighterStateSpecial = 0xC;
inline constexpr uint8_t kAkumaRagingDemonMove = 10;
inline constexpr uint8_t kAkumaDemonDashStep = 6;
inline constexpr uint16_t kFighterInputUp = 8;

struct DemonThrower {
  bool akuma;
  uint8_t state;
  uint8_t step;
  uint8_t move;
};

struct DemonTarget {
  uint8_t state;
  uint8_t jump_locked;
  uint16_t input;
};

// Blockstun ends in block stance and the stance's next tick starts the jump, so a fighter holding up is one tick from
// the pre-jump the dash already respects. The per-frame fighter order decides whether the dash sees that gap.
constexpr bool DemonWaitsForJump(const DemonThrower& thrower, const DemonTarget& target) {
  return thrower.akuma && thrower.state == kFighterStateSpecial && thrower.move == kAkumaRagingDemonMove &&
         thrower.step == kAkumaDemonDashStep && target.state == kFighterStateBlockStance && target.jump_locked == 0 &&
         (target.input & kFighterInputUp) != 0;
}

enum class AkumaFrameState : uint8_t { kOriginal, kFixed, kUnknown };

template <typename Memory>
uint32_t RemixAkumaActions(const Memory& memory, uint32_t remix_fighter_data) {
  const uint32_t block = memory.U32(remix_fighter_data + kAkumaFighterSlot * 4);
  return block == 0 ? 0 : memory.U32(block + kAkumaSelectedActionsOffset);
}

template <typename Memory>
uint32_t AkumaFrameAddress(const Memory& memory, uint32_t actions, uint8_t action, uint8_t sub, uint8_t frame) {
  const uint32_t subs = actions + memory.U16(actions + action);
  return subs + memory.U16(subs + uint32_t(sub) * 2) + uint32_t(frame) * kAkumaFrameBytes;
}

template <typename Memory>
uint32_t AkumaPatchAddress(const Memory& memory, uint32_t actions, const AkumaFramePatch& patch) {
  const uint32_t record = AkumaFrameAddress(memory, actions, patch.action, patch.sub, patch.frame);
  switch (patch.field) {
    case AkumaFrameField::kAttack: return record + kAkumaAttackOffset;
    case AkumaFrameField::kHurtHead: return record + kAkumaHurtOffset;
    case AkumaFrameField::kHurtBody: return record + kAkumaHurtOffset + 1;
    case AkumaFrameField::kHurtLegs: return record + kAkumaHurtOffset + 2;
  }
  return 0;
}

template <typename Memory>
AkumaFrameState ReadAkumaFrameState(const Memory& memory, uint32_t actions) {
  if (actions == 0 || memory.U16(actions) <= kAkumaDragonPunchAction) return AkumaFrameState::kUnknown;
  size_t original = 0;
  size_t fixed = 0;
  for (const AkumaFramePatch& patch : kAkumaFramePatches) {
    const uint8_t value = memory.U8(AkumaPatchAddress(memory, actions, patch));
    if (value == patch.original) ++original;
    if (value == patch.fixed) ++fixed;
  }
  if (original == std::size(kAkumaFramePatches)) return AkumaFrameState::kOriginal;
  if (fixed == std::size(kAkumaFramePatches)) return AkumaFrameState::kFixed;
  return AkumaFrameState::kUnknown;
}

// All or nothing: a table that is not exactly the expected build's bytes is left alone.
template <typename Memory>
bool SetAkumaFrames(Memory& memory, uint32_t actions, bool fixed) {
  const AkumaFrameState state = ReadAkumaFrameState(memory, actions);
  if (state == AkumaFrameState::kUnknown) return false;
  if ((state == AkumaFrameState::kFixed) == fixed) return true;
  for (const AkumaFramePatch& patch : kAkumaFramePatches) {
    memory.SetU8(AkumaPatchAddress(memory, actions, patch), fixed ? patch.fixed : patch.original);
  }
  return true;
}

}  // namespace ssf2t::character
