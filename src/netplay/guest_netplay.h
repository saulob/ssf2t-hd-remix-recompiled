#pragma once

#include <cstdint>
#include <iterator>
#include <span>
#include <vector>

#include "guest_memory.h"
#include "netplay/packet.h"

namespace ssf2t::netplay {

// What the game's own save callback copies: the first 64 KiB of the fight arena into a snapshot buffer, and the
// game-state globals into a struct that sits right below that buffer's pointer.
inline constexpr uint32_t kArenaSaveSize = 0x10000;
inline constexpr uint32_t kGlobalsSaveSize = 0x29A00;
inline constexpr uint32_t kGuestStateSize = kArenaSaveSize + kGlobalsSaveSize;

// Commands of the game's rollback callback. Step runs one whole simulated frame: the sim step and the per-frame work
// the fight loop's frame tail otherwise runs after it.
inline constexpr uint32_t kCmdSave = 0;
inline constexpr uint32_t kCmdLoad = 1;
inline constexpr uint32_t kCmdSetInput = 2;
inline constexpr uint32_t kCmdChecksum = 4;
inline constexpr uint32_t kCmdStep = 5;

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
  static constexpr uint32_t kGameMode = 0x83CA294F;
  static constexpr uint32_t kInputTable = 0x83CD62F0;
  static constexpr uint32_t kControllerInputTable = 0x83CD6560;
  static constexpr uint32_t kActiveUser = 0x83A38770;
  static constexpr uint32_t kUiInputPointer = 0x8480BD30;
  static constexpr uint32_t kInputRing = 0x83C19F40;
  static constexpr uint32_t kInputDecode = 0x82250968;
  static constexpr uint32_t kInputHistoryStep = 0x8237B170;
  static constexpr uint32_t kRngState = 0x83C3393C;
  static constexpr uint32_t kSideOrder = 0x83C19C4C;
  static constexpr uint32_t kRandStep = 0x823E1CF0;
  static constexpr uint32_t kSideSetup = 0x82318618;
  static constexpr uint32_t kNetActiveQuery = 0x8241CC08;
  static constexpr uint32_t kFrameTailNetCheck = 0x823EDF04;
  static constexpr uint32_t kFramePump = 0x82252B90;
  static constexpr uint32_t kFightWork = 0x83CD52DC;
  static constexpr uint32_t kFighterBlockPointer = 0x83E09CC0;
  static constexpr uint32_t kRoundWinPlayer1 = 0x8224B340;
  static constexpr uint32_t kRoundWinPlayer2 = 0x8224B6F0;
  static constexpr uint32_t kFightCharacterInit = 0x822392F0;
  static constexpr uint32_t kOnlineStage = 0x83C24D98;
  static constexpr uint32_t kButtonConfigFlag = 0x83CA6D66;
  static constexpr uint32_t kButtonConfigA = 0x83CA6D2C;
  static constexpr uint32_t kButtonConfigB = 0x83CD5158;
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
  static constexpr uint32_t kNetActiveQueryWords[] = {0x3D60847D, 0x886B3A2D, 0x4E800020};
  static constexpr uint32_t kFrameTailNetCheckWords[] = {0x4802ED05, 0x546B063F, 0x4082013C, 0x4BFFBAE9};
  static constexpr uint32_t kFramePumpWords[] = {0x7D8802A6, 0x9181FFF8, 0x9421FFA0,
                                                 0x90610074, 0x4819AF79, 0x481CA065};
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
  static constexpr uint32_t kGameMode = 0x83CA2E67;
  static constexpr uint32_t kInputTable = 0x83CD6808;
  static constexpr uint32_t kControllerInputTable = 0x83CD6A78;
  static constexpr uint32_t kActiveUser = 0x83A387E8;
  static constexpr uint32_t kUiInputPointer = 0x8480C250;
  static constexpr uint32_t kInputRing = 0x83C19FC0;
  static constexpr uint32_t kInputDecode = 0x822509B0;
  static constexpr uint32_t kInputHistoryStep = 0x8237B240;
  static constexpr uint32_t kRngState = 0x83C33E54;
  static constexpr uint32_t kSideOrder = 0x83C19CCC;
  static constexpr uint32_t kRandStep = 0x823E23C8;
  static constexpr uint32_t kSideSetup = 0x82318558;
  static constexpr uint32_t kNetActiveQuery = 0x8241D2E0;
  static constexpr uint32_t kFrameTailNetCheck = 0x823EE670;
  static constexpr uint32_t kFramePump = 0x82252BD8;
  static constexpr uint32_t kFightWork = 0x83CD57F4;
  static constexpr uint32_t kFighterBlockPointer = 0x83E0A1D8;
  static constexpr uint32_t kRoundWinPlayer1 = 0x8224B388;
  static constexpr uint32_t kRoundWinPlayer2 = 0x8224B738;
  static constexpr uint32_t kFightCharacterInit = 0x82239418;
  static constexpr uint32_t kOnlineStage = 0x83C25110;
  static constexpr uint32_t kButtonConfigFlag = 0x83CA727E;
  static constexpr uint32_t kButtonConfigA = 0x83CA7244;
  static constexpr uint32_t kButtonConfigB = 0x83CD5670;
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
  static constexpr uint32_t kNetActiveQueryWords[] = {0x3D60847D, 0x886B3F4C, 0x4E800020};
  static constexpr uint32_t kFrameTailNetCheckWords[] = {0x4802EC71, 0x546B063F, 0x4082013C, 0x4BFFBAB5};
  static constexpr uint32_t kFramePumpWords[] = {0x7D8802A6, 0x9181FFF8, 0x9421FFA0,
                                                 0x90610074, 0x4819B621, 0x481CA6F5};
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
         WordsMatch(base, G::kRandStep, G::kRandStepWords) && WordsMatch(base, G::kSideSetup, G::kSideSetupWords) &&
         WordsMatch(base, G::kNetActiveQuery, G::kNetActiveQueryWords) &&
         WordsMatch(base, G::kFrameTailNetCheck, G::kFrameTailNetCheckWords) &&
         WordsMatch(base, G::kFramePump, G::kFramePumpWords);
}

// The frame tail asks whether the game's NetSync is active right after the sim step; when it is, the tail skips the
// per-frame simulation work (input decode, the post-step updates) because the step command runs it instead.
template <typename G>
constexpr uint32_t FrameTailNetCheckReturn() {
  return G::kFrameTailNetCheck + 4;
}

// What the fight reads outside the snapshot, so the peers must already share it when they meet: the game mode, the
// phase each met at, the online stage the stage pick reads, and the attack buttons the input decode maps each
// fighter's pad with.
inline constexpr size_t kButtonConfigBytes = 6;
inline constexpr const char* kSetupWordNames[] = {"mode",       "phase01",        "phase23",    "stage",
                                                  "buttons_p1", "buttons_p1_ext", "buttons_p2", "buttons_p2_ext"};
inline constexpr size_t kSetupWordCount = std::size(kSetupWordNames);

// The secret characters a match negotiated (SecretSet bits). It adds a word only while any is on, so a peer without
// secret characters sends the words it always did and still agrees with a client that has none on.
inline constexpr uint32_t kSecretSetupTag = 0x53430000;
inline constexpr const char* kSecretSetupWordName = "secrets";

struct SetupFields {
  uint8_t mode = 0;
  uint16_t phase[4] = {};
  uint32_t stage = 0;
  uint8_t buttons[2][kButtonConfigBytes] = {};
  uint8_t secrets = 0;
};

inline std::vector<uint32_t> SetupWords(const SetupFields& f) {
  std::vector<uint32_t> words = {f.mode, uint32_t(f.phase[0]) << 16 | f.phase[1], uint32_t(f.phase[2]) << 16 | f.phase[3],
                                 f.stage};
  for (const auto& slot : f.buttons) {
    words.push_back(uint32_t(slot[0]) << 24 | uint32_t(slot[1]) << 16 | uint32_t(slot[2]) << 8 | slot[3]);
    words.push_back(uint32_t(slot[4]) << 8 | slot[5]);
  }
  if (f.secrets) words.push_back(kSecretSetupTag | f.secrets);
  return words;
}

// The decode reads fighter slot 0's and 1's buttons from one of two tables, by a flag.
template <typename G>
constexpr uint32_t ButtonConfigAddress(bool flag, uint32_t slot) {
  return flag ? G::kButtonConfigA + slot * 10 : G::kButtonConfigB + slot * 8;
}

// A phase that waits on presentation (the stage pick before the airplane) presents whole frames through the frame pump
// from inside one simulated frame, and only that tail's work ends the wait; those tails belong to the simulated frame.
template <typename G>
constexpr bool AnswerFrameTailNetActive(uint32_t return_address, bool in_frame_pump, bool owns_fight) {
  return owns_fight && !in_frame_pump && return_address == FrameTailNetCheckReturn<G>();
}

}  // namespace ssf2t::netplay
