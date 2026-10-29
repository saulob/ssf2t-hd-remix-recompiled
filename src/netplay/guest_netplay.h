#pragma once

#include <cstdint>
#include <span>

#include "guest_memory.h"
#include "netplay/packet.h"

namespace ssf2t::netplay {

// What the game's own save callback copies: the first 64 KiB of the fight arena into a snapshot buffer, and the
// game-state globals into a struct that sits right below that buffer's pointer.
inline constexpr uint32_t kArenaSaveSize = 0x10000;
inline constexpr uint32_t kGlobalsSaveSize = 0x29A00;
inline constexpr uint32_t kGuestStateSize = kArenaSaveSize + kGlobalsSaveSize;

// Commands of the game's rollback callback.
inline constexpr uint32_t kCmdSave = 0;
inline constexpr uint32_t kCmdLoad = 1;
inline constexpr uint32_t kCmdSetInput = 2;
inline constexpr uint32_t kCmdChecksum = 4;

// Input tables hold one 156-byte entry per player: the pad word of this frame and of the one before.
inline constexpr uint32_t kPlayersInTable = 4;
inline constexpr uint32_t kInputEntryStride = 156;
inline constexpr uint32_t kInputCurrentOffset = 8;
inline constexpr uint32_t kInputPreviousOffset = 12;
inline constexpr uint32_t kInputTableStateBytes = kPlayersInTable * kInputEntryStride;

// Two 16-halfword rings the step shifts every frame, one per player (the input-delay history).
inline constexpr uint32_t kInputRingBytes = 64;

// The step's random generator (a seed word and 31 state words with their index) and the fighter order a coin flip
// sets once per fight.
inline constexpr uint32_t kRngStateBytes = 0x84;
inline constexpr uint32_t kSideOrderBytes = 16;

// Not saved by the game's callback but mutated by the step and read back by it; the host snapshot adds them.
inline constexpr uint32_t kSimStateSize =
    kGuestStateSize + kInputTableStateBytes + kInputRingBytes + kRngStateBytes + kSideOrderBytes;
inline constexpr uint32_t kSimBlobSize = kSimStateSize + 2 * sizeof(uint16_t);

// Guest data scanned only by the diagnostic, to name state a restore leaves behind.
inline constexpr uint32_t kDiagWindowLow = 0x83BE0000;
inline constexpr uint32_t kDiagWindowHigh = 0x83E40000;

struct Base013Netplay {
  static constexpr const char* kName = "base013";
  static constexpr GuestId kGuest = GuestId::kBase013;

  static constexpr uint32_t kCallback = 0x823E90C0;
  static constexpr uint32_t kSimStep = 0x8223DDB8;
  static constexpr uint32_t kEffectsGate = 0x8241CC48;
  static constexpr uint32_t kFightLoop = 0x8223DEA8;

  static constexpr uint32_t kArenaPointer = 0x83E09CBC;
  static constexpr uint32_t kSnapshotPointer = 0x83E37CC8;
  static constexpr uint32_t kGlobalsSnapshot = 0x83E0E2C8;
  static constexpr uint32_t kFrameCounter = 0x83E37F78;
  static constexpr uint32_t kStateFunction = 0x83C1A820;
  static constexpr uint32_t kNetActive = 0x847D3A2D;
  static constexpr uint32_t kInputTable = 0x83CD62F0;
  static constexpr uint32_t kInputRing = 0x83C19F40;
  static constexpr uint32_t kInputDecode = 0x82250968;
  static constexpr uint32_t kInputHistoryStep = 0x8237B170;
  static constexpr uint32_t kRngState = 0x83C3393C;
  static constexpr uint32_t kSideOrder = 0x83C19C4C;
  static constexpr uint32_t kRandStep = 0x823E1CF0;
  static constexpr uint32_t kSideSetup = 0x82318618;
  static constexpr uint32_t kPaceCounter = 0x83E37F5C;
  static constexpr uint32_t kDrawListLow = 0x83BE2E80;
  static constexpr uint32_t kDrawListHigh = 0x83BE5C80;

  static constexpr uint32_t kCallbackWords[] = {
      0x7D8802A6, 0x9181FFF8, 0xFBE1FFF0, 0x9421FF80, 0x90610094, 0x9081009C, 0x81610094, 0x91610068,
      0x81610068, 0x2B0B000C, 0x4199037C, 0x81610068, 0x3D808201, 0x398CD0D8, 0x7C0C58AE, 0x5400103A,
  };
  static constexpr uint32_t kSimStepWords[] = {
      0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x481DEE55, 0x546B063F, 0x40820018, 0x3D6083E3, 0x816B7F5C,
      0x396B0001, 0x3D4083E3, 0x916A7F5C, 0x3D6083CD, 0x816B52DC, 0xA16B050C, 0x2B0B0000, 0x419A0034,
  };
  static constexpr uint32_t kEffectsGateWords[] = {
      0x3D60847D, 0x396B3A2B, 0x894B0002, 0x280A0000, 0x41820014, 0x896B0000, 0x280B0000, 0x39600000,
      0x41820008, 0x39600001, 0x5563063E, 0x4E800020, 0x3D60847D, 0xC00B39AC, 0xFC00001E, 0xD801FFF0,
  };
  static constexpr uint32_t kFightLoopWords[] = {
      0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083C1, 0x39400000, 0x994B3390, 0x3D6083CD, 0x816B52DC,
      0x39400000, 0x994B039F, 0x3D6083CD, 0x39400001, 0x914B52CC, 0x4813D235, 0x481772A9, 0x3D6083C2,
  };
  static constexpr uint32_t kInputDecodeWords[] = {
      0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x396B62F0, 0x816B0010, 0x3D4083CD, 0x394A62F0,
      0x814A00AC, 0x7D6B5378, 0x3D4083C1, 0x394A3418, 0x916A0010, 0x3D6083CD, 0x396B62F0, 0x816B0008,
  };
  static constexpr uint32_t kInputHistoryStepWords[] = {
      0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083C2, 0x396B9F40, 0xA16B001C, 0x3D4083C2, 0x394A9F40,
      0xB16A001E, 0x3D6083C2, 0x396B9F40, 0xA16B001A, 0x3D4083C2, 0x394A9F40, 0xB16A001C, 0x3D6083C2,
  };
  static constexpr uint32_t kRandStepWords[] = {
      0x8163007C, 0x3D4083A4, 0x3D2083A4, 0x556B103A, 0x394A88D0, 0x39298950, 0x7D4B502E, 0x7D6B482E,
      0x5549103A, 0x556B103A, 0x9143007C, 0x554A003E, 0x5548103A, 0x7D49182E, 0x7D6B182E, 0x7D6A5A14,
  };
  static constexpr uint32_t kSideSetupWords[] = {
      0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x396B52CC, 0x39400000, 0xB14B0002, 0x49632B5D,
      0x48000054, 0x3D6083E1, 0x816B9CC0, 0x3940FFFF, 0xB14B072C, 0x3D6083E1, 0x816B9CC0, 0x3940FFFF,
  };
};

struct TitleUpdate023Netplay {
  static constexpr const char* kName = "tu023";
  static constexpr GuestId kGuest = GuestId::kTitleUpdate023;

  static constexpr uint32_t kCallback = 0x823E97F8;
  static constexpr uint32_t kSimStep = 0x8223DEE0;
  static constexpr uint32_t kEffectsGate = 0x8241D320;
  static constexpr uint32_t kFightLoop = 0x8223DFD0;

  static constexpr uint32_t kArenaPointer = 0x83E0A1D4;
  static constexpr uint32_t kSnapshotPointer = 0x83E381E0;
  static constexpr uint32_t kGlobalsSnapshot = 0x83E0E7E0;
  static constexpr uint32_t kFrameCounter = 0x83E38490;
  static constexpr uint32_t kStateFunction = 0x83C1A8A0;
  static constexpr uint32_t kNetActive = 0x847D3F4C;
  static constexpr uint32_t kInputTable = 0x83CD6808;
  static constexpr uint32_t kInputRing = 0x83C19FC0;
  static constexpr uint32_t kInputDecode = 0x822509B0;
  static constexpr uint32_t kInputHistoryStep = 0x8237B240;
  static constexpr uint32_t kRngState = 0x83C33E54;
  static constexpr uint32_t kSideOrder = 0x83C19CCC;
  static constexpr uint32_t kRandStep = 0x823E23C8;
  static constexpr uint32_t kSideSetup = 0x82318558;
  static constexpr uint32_t kPaceCounter = 0x83E38474;
  static constexpr uint32_t kDrawListLow = 0x83BE2F00;
  static constexpr uint32_t kDrawListHigh = 0x83BE5D00;

  static constexpr uint32_t kCallbackWords[] = {
      0x7D8802A6, 0x9181FFF8, 0xFBE1FFF0, 0x9421FF80, 0x90610094, 0x9081009C, 0x81610094, 0x91610068,
      0x81610068, 0x2B0B000C, 0x4199037C, 0x81610068, 0x3D808201, 0x398CD130, 0x7C0C58AE, 0x5400103A,
  };
  static constexpr uint32_t kSimStepWords[] = {
      0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x481DF405, 0x546B063F, 0x40820018, 0x3D6083E4, 0x816B8474,
      0x396B0001, 0x3D4083E4, 0x916A8474, 0x3D6083CD, 0x816B57F4, 0xA16B050C, 0x2B0B0000, 0x419A0034,
  };
  static constexpr uint32_t kEffectsGateWords[] = {
      0x3D60847D, 0x396B3F4B, 0x894B0001, 0x280A0000, 0x41820014, 0x896B0000, 0x280B0000, 0x39600000,
      0x41820008, 0x39600001, 0x5563063E, 0x4E800020, 0x3D60847D, 0xC00B3ECC, 0xFC00001E, 0xD801FFF0,
  };
  static constexpr uint32_t kFightLoopWords[] = {
      0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083C1, 0x39400000, 0x994B3410, 0x3D6083CD, 0x816B57F4,
      0x39400000, 0x994B039F, 0x3D6083CD, 0x39400001, 0x914B57E4, 0x4813D1DD, 0x481771B1, 0x3D6083C2,
  };
  static constexpr uint32_t kInputDecodeWords[] = {
      0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x396B6808, 0x816B0010, 0x3D4083CD, 0x394A6808,
      0x814A00AC, 0x7D6B5378, 0x3D4083C1, 0x394A3498, 0x916A0010, 0x3D6083CD, 0x396B6808, 0x816B0008,
  };
  static constexpr uint32_t kInputHistoryStepWords[] = {
      0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083C2, 0x396B9FC0, 0xA16B001C, 0x3D4083C2, 0x394A9FC0,
      0xB16A001E, 0x3D6083C2, 0x396B9FC0, 0xA16B001A, 0x3D4083C2, 0x394A9FC0, 0xB16A001C, 0x3D6083C2,
  };
  static constexpr uint32_t kRandStepWords[] = {
      0x8163007C, 0x3D4083A4, 0x3D2083A4, 0x556B103A, 0x394A8948, 0x392989C8, 0x7D4B502E, 0x7D6B482E,
      0x5549103A, 0x556B103A, 0x9143007C, 0x554A003E, 0x5548103A, 0x7D49182E, 0x7D6B182E, 0x7D6A5A14,
  };
  static constexpr uint32_t kSideSetupWords[] = {
      0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x396B57E4, 0x39400000, 0xB14B0002, 0x49469AA5,
      0x48000054, 0x3D6083E1, 0x816BA1D8, 0x3940FFFF, 0xB14B072C, 0x3D6083E1, 0x816BA1D8, 0x3940FFFF,
  };
};

// `base` is the guest memory.
inline bool WordsMatch(const uint8_t* base, uint32_t address, std::span<const uint32_t> words) {
  uint32_t offset = 0;
  for (const uint32_t word : words) {
    if (LoadBE32(base, address + offset) != word) return false;
    offset += 4;
  }
  return true;
}

// Every function the netplay hooks touch must be the one that was analysed, or the hooks stay out of the way.
template <typename G>
bool NetplayCodeMatches(const uint8_t* base) {
  return WordsMatch(base, G::kCallback, G::kCallbackWords) && WordsMatch(base, G::kSimStep, G::kSimStepWords) &&
         WordsMatch(base, G::kEffectsGate, G::kEffectsGateWords) &&
         WordsMatch(base, G::kFightLoop, G::kFightLoopWords) &&
         WordsMatch(base, G::kInputDecode, G::kInputDecodeWords) &&
         WordsMatch(base, G::kInputHistoryStep, G::kInputHistoryStepWords) &&
         WordsMatch(base, G::kRandStep, G::kRandStepWords) && WordsMatch(base, G::kSideSetup, G::kSideSetupWords);
}

}  // namespace ssf2t::netplay
