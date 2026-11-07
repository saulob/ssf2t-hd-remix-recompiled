#pragma once

#include <cstddef>
#include <cstdint>

#include "character/akuma_navigation.h"
#include "character/evil_ryu_moves.h"
#include "character/ultra_navigation.h"

namespace ssf2t::character {

// The port-side state the Character Select and the Ultra fighters' moves keep outside the guest, laid out for the
// rollback blob: the Ultra cell mask, the Akuma column memory, then both sides' move trackers.
inline constexpr size_t kHostRollbackBytes = 1 + 2 * (1 + 1) + HostMoves::kBytes;

inline void SaveHostRollback(const UltraNavigation& ultra, const AkumaNavigation& akuma, const HostMoves& moves,
                             uint8_t* out) {
  *out++ = ultra.Mask();
  for (int side = 0; side < 2; ++side) {
    *out++ = uint8_t(int8_t(akuma.column[side]));
    *out++ = uint8_t(akuma.layout[side]);
  }
  moves.Save(out);
}

inline void LoadHostRollback(UltraNavigation& ultra, AkumaNavigation& akuma, HostMoves& moves, const uint8_t* in) {
  ultra.Restore(*in++);
  for (int side = 0; side < 2; ++side) {
    akuma.column[side] = int8_t(*in++);
    akuma.layout[side] = *in++ == uint8_t(SelectLayout::kVersus) ? SelectLayout::kVersus : SelectLayout::kArcade;
  }
  moves.Load(in);
}

// A new online Character Select starts from nothing on both peers.
inline void ResetHostSelect(UltraNavigation& ultra, AkumaNavigation& akuma, HostMoves& moves) {
  ultra.Reset();
  akuma.Reset();
  moves.Reset();
}

AkumaNavigation& GlobalAkumaNavigation();

// Hands the codec to the netplay layer; the static in host_rollback.cpp does it once at startup.
void InstallHostRollback();
void ResetOnlineSelectState();

}  // namespace ssf2t::character
