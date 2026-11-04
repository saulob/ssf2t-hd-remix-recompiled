#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

#include <rex/hook.h>

#include "guest_memory.h"
#include "netplay/guest_netplay.h"
#include "netplay/host_state.h"
#include "netplay/input_model.h"
#include "netplay/rollback_core.h"

namespace ssf2t::netplay {

struct GuestCalls {
  PPCFunc* callback;
  PPCFunc* step;
};

inline bool PointerUsable(uint32_t address) { return address >= 0x10000; }

// Set true for the duration of a replayed (resimulated) step, so the effects-gate hook suppresses audiovisual
// side effects; false for the one step that is allowed to present. Thread-local: one guest simulation thread.
inline thread_local bool g_suppress_effects_flag = false;
inline void SetSuppressEffects(bool value) { g_suppress_effects_flag = value; }
inline bool SuppressEffects() { return g_suppress_effects_flag; }

// True while the game's step command runs a frame for the rollback core: the sim step it calls is then the original.
inline thread_local bool g_in_rollback_frame = false;
inline bool InRollbackFrame() { return g_in_rollback_frame; }

inline thread_local int g_frame_pump_depth = 0;
inline bool InFramePump() { return g_frame_pump_depth > 0; }

enum class FrameKind {
  kSimStep,        // the sim step alone, as the fight loop calls it
  kRollbackFrame,  // the game's step command: the sim step and the frame tail's simulation work
};

// Guest stack below the caller's frame, for the callback's argument buffers.
class StackScratch {
 public:
  explicit StackScratch(PPCContext& ctx) : ctx_(ctx), saved_(ctx.r1.u32) { ctx.r1.u32 = (saved_ - 64) & ~15u; }
  ~StackScratch() { ctx_.r1.u32 = saved_; }
  StackScratch(const StackScratch&) = delete;
  StackScratch& operator=(const StackScratch&) = delete;
  uint32_t address() const { return ctx_.r1.u32; }

 private:
  PPCContext& ctx_;
  uint32_t saved_;
};

// Drives one guest image's fight simulation through the game's own rollback callback and step function, for the
// RollbackCore to save, load and advance. `history_` (the previous-input edges) lives in this object, so it must
// persist across calls for as long as the fight it belongs to is running: do not reconstruct it mid-fight.
template <typename G>
class GuestSimulation final : public Simulation {
 public:
  GuestSimulation(PPCContext& ctx, uint8_t* base, const GuestCalls& calls, FrameKind kind = FrameKind::kSimStep)
      : ctx_(ctx), base_(base), calls_(calls), kind_(kind) {}

  bool Save(std::vector<uint8_t>& out) override {
    const uint32_t snapshot = LoadBE32(base_, G::kSnapshotPointer);
    if (!PointerUsable(snapshot)) return false;
    Callback(kCmdSave, 0);
    out.resize(kSimBlobSize + HostStateBytes());
    uint8_t* at = out.data();
    at = Put(at, HostAt(snapshot), kArenaSaveSize);
    at = Put(at, base_ + G::kGlobalsSnapshot, kGlobalsSaveSize);
    at = Put(at, base_ + G::kInputTable, kInputTableStateBytes);
    at = Put(at, base_ + G::kInputRing, kInputRingBytes);
    at = Put(at, base_ + G::kRngState, kRngStateBytes);
    at = Put(at, base_ + G::kSideOrder, kSideOrderBytes);
    std::memcpy(at, history_.words(), sizeof(InputWord) * kPlayers);
    SaveHostState(at + sizeof(InputWord) * kPlayers);
    return true;
  }

  bool Load(const std::vector<uint8_t>& in) override {
    const uint32_t snapshot = LoadBE32(base_, G::kSnapshotPointer);
    if (!PointerUsable(snapshot) || in.size() != kSimBlobSize + HostStateBytes()) return false;
    const uint8_t* at = in.data();
    at = Take(HostAt(snapshot), at, kArenaSaveSize);
    at = Take(base_ + G::kGlobalsSnapshot, at, kGlobalsSaveSize);
    at = Take(base_ + G::kInputTable, at, kInputTableStateBytes);
    at = Take(base_ + G::kInputRing, at, kInputRingBytes);
    at = Take(base_ + G::kRngState, at, kRngStateBytes);
    at = Take(base_ + G::kSideOrder, at, kSideOrderBytes);
    InputWord previous[kPlayers];
    std::memcpy(previous, at, sizeof(previous));
    history_.set_previous(previous);
    LoadHostState(at + sizeof(previous));
    Callback(kCmdLoad, 0);
    return true;
  }

  bool Advance(const InputWord (&inputs)[kPlayers], bool replay) override {
    for (uint32_t player = 0; player < kPlayers; ++player) {
      SetInput(player, inputs[player], history_.previous(player));
    }
    Step(replay);
    history_.Record(inputs);
    return true;
  }

  uint32_t Checksum() override {
    StackScratch scratch(ctx_);
    Callback(kCmdChecksum, scratch.address());
    return LoadBE32(base_, scratch.address());
  }

  void Step(bool replay) {
    const uint64_t lr = ctx_.lr;
    SetSuppressEffects(replay);
    if (kind_ == FrameKind::kRollbackFrame) {
      g_in_rollback_frame = true;
      Callback(kCmdStep, 0);
      g_in_rollback_frame = false;
    } else {
      calls_.step(ctx_, base_);
    }
    SetSuppressEffects(false);
    ctx_.lr = lr;
  }

  // Otherwise each peer's decoded input still holds what it decoded last, before this fight or in the previous one.
  void NeutralInputs() {
    for (uint32_t player = 0; player < kPlayers; ++player) SetInput(player, 0, 0);
  }

  void SetPrevious(const InputWord (&previous)[kPlayers]) { history_.set_previous(previous); }

  InputWord TableWord(uint32_t player, uint32_t field) const {
    return InputWord(LoadBE32(base_, G::kInputTable + player * kInputEntryStride + field) & 0xFFFF);
  }

  std::vector<uint8_t> DiagWindow() const {
    return std::vector<uint8_t>(base_ + kDiagWindowLow, base_ + kDiagWindowHigh);
  }

 private:
  static uint8_t* Put(uint8_t* out, const uint8_t* from, uint32_t size) {
    std::memcpy(out, from, size);
    return out + size;
  }
  static const uint8_t* Take(uint8_t* to, const uint8_t* from, uint32_t size) {
    std::memcpy(to, from, size);
    return from + size;
  }
  uint8_t* HostAt(uint32_t address) const { return base_ + address + ssf2t::GuestHostOffset(address); }

  void Callback(uint32_t command, uint32_t buffer) {
    const uint64_t lr = ctx_.lr;
    ctx_.r3.u64 = command;
    ctx_.r4.u64 = buffer;
    calls_.callback(ctx_, base_);
    ctx_.lr = lr;
  }

  void SetInput(uint32_t player, InputWord current, InputWord previous) {
    StackScratch scratch(ctx_);
    StoreBE32(base_, scratch.address(), player);
    StoreBE32(base_, scratch.address() + 4, current);
    StoreBE32(base_, scratch.address() + 8, previous);
    Callback(kCmdSetInput, scratch.address());
  }

  PPCContext& ctx_;
  uint8_t* base_;
  GuestCalls calls_;
  FrameKind kind_;
  InputHistory history_;
};

}  // namespace ssf2t::netplay
