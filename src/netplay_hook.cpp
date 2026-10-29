#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include "guest_memory.h"
#include "netplay/determinism.h"
#include "netplay/guest_netplay.h"
#include "netplay/input_model.h"
#include "netplay/rollback_core.h"
#include "title_update_image.h"

REXCVAR_DEFINE_BOOL(ssf2t_netplay_determinism_test, false, "SSF2T",
                    "Developer: replays offline fight frames and logs DEBUG_SAULO determinism checks");

REX_EXTERN(__imp__base013_sub_823E90C0);
REX_EXTERN(__imp__base013_sub_8223DDB8);
REX_EXTERN(__imp__base013_sub_8241CC48);
REX_EXTERN(__imp__tu023_sub_823E97F8);
REX_EXTERN(__imp__tu023_sub_8223DEE0);
REX_EXTERN(__imp__tu023_sub_8241D320);
REX_EXTERN(__imp__base013_sub_823E1CF0);
REX_EXTERN(__imp__base013_sub_82318618);
REX_EXTERN(__imp__tu023_sub_823E23C8);
REX_EXTERN(__imp__tu023_sub_82318558);

namespace {

namespace np = ssf2t::netplay;

constexpr uint32_t kFirstSampleFrame = 120;
constexpr uint32_t kSampleInterval = 300;
constexpr uint32_t kMaxSamples = 8;
constexpr size_t kWatchedArenaByte = 0x3F0;
constexpr size_t kRunMergeGap = 16;
constexpr size_t kMaxRuns = 16;
constexpr size_t kMaxClassifiedRuns = 256;

struct GuestCalls {
  PPCFunc* callback;
  PPCFunc* step;
};

constexpr uint32_t kTraceCallers = 6;

struct PassTrace {
  uint32_t rand_calls = 0;
  uint32_t side_setups = 0;
  uint32_t gate_queries = 0;
  uint32_t rand_callers[kTraceCallers] = {};
};

thread_local bool g_suppress_effects = false;
PassTrace* g_trace = nullptr;
np::SampleSchedule g_schedule(kFirstSampleFrame, kSampleInterval, kMaxSamples);
uint32_t g_failures = 0;

bool PointerUsable(uint32_t address) { return address >= 0x10000; }

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

template <typename G>
class GuestSimulation final : public np::Simulation {
 public:
  GuestSimulation(PPCContext& ctx, uint8_t* base, const GuestCalls& calls) : ctx_(ctx), base_(base), calls_(calls) {}

  bool Save(std::vector<uint8_t>& out) override {
    const uint32_t snapshot = ssf2t::LoadBE32(base_, G::kSnapshotPointer);
    if (!PointerUsable(snapshot)) return false;
    Callback(np::kCmdSave, 0);
    out.resize(np::kSimBlobSize);
    uint8_t* at = out.data();
    at = Put(at, HostAt(snapshot), np::kArenaSaveSize);
    at = Put(at, base_ + G::kGlobalsSnapshot, np::kGlobalsSaveSize);
    at = Put(at, base_ + G::kInputTable, np::kInputTableStateBytes);
    at = Put(at, base_ + G::kInputRing, np::kInputRingBytes);
    at = Put(at, base_ + G::kRngState, np::kRngStateBytes);
    at = Put(at, base_ + G::kSideOrder, np::kSideOrderBytes);
    std::memcpy(at, history_.words(), sizeof(np::InputWord) * np::kPlayers);
    return true;
  }

  bool Load(const std::vector<uint8_t>& in) override {
    const uint32_t snapshot = ssf2t::LoadBE32(base_, G::kSnapshotPointer);
    if (!PointerUsable(snapshot) || in.size() != np::kSimBlobSize) return false;
    const uint8_t* at = in.data();
    at = Take(HostAt(snapshot), at, np::kArenaSaveSize);
    at = Take(base_ + G::kGlobalsSnapshot, at, np::kGlobalsSaveSize);
    at = Take(base_ + G::kInputTable, at, np::kInputTableStateBytes);
    at = Take(base_ + G::kInputRing, at, np::kInputRingBytes);
    at = Take(base_ + G::kRngState, at, np::kRngStateBytes);
    at = Take(base_ + G::kSideOrder, at, np::kSideOrderBytes);
    np::InputWord previous[np::kPlayers];
    std::memcpy(previous, at, sizeof(previous));
    history_.set_previous(previous);
    Callback(np::kCmdLoad, 0);
    return true;
  }

  bool Advance(const np::InputWord (&inputs)[np::kPlayers], bool replay) override {
    for (uint32_t player = 0; player < np::kPlayers; ++player) {
      SetInput(player, inputs[player], history_.previous(player));
    }
    Step(replay);
    history_.Record(inputs);
    return true;
  }

  uint32_t Checksum() override {
    StackScratch scratch(ctx_);
    Callback(np::kCmdChecksum, scratch.address());
    return ssf2t::LoadBE32(base_, scratch.address());
  }

  void Step(bool replay) {
    const uint64_t lr = ctx_.lr;
    g_suppress_effects = replay;
    calls_.step(ctx_, base_);
    g_suppress_effects = false;
    ctx_.lr = lr;
  }

  void SetPrevious(const np::InputWord (&previous)[np::kPlayers]) { history_.set_previous(previous); }

  np::InputWord TableWord(uint32_t player, uint32_t field) const {
    return np::InputWord(ssf2t::LoadBE32(base_, G::kInputTable + player * np::kInputEntryStride + field) & 0xFFFF);
  }

  std::vector<uint8_t> DiagWindow() const {
    return std::vector<uint8_t>(base_ + np::kDiagWindowLow, base_ + np::kDiagWindowHigh);
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

  void SetInput(uint32_t player, np::InputWord current, np::InputWord previous) {
    StackScratch scratch(ctx_);
    ssf2t::StoreBE32(base_, scratch.address(), player);
    ssf2t::StoreBE32(base_, scratch.address() + 4, current);
    ssf2t::StoreBE32(base_, scratch.address() + 8, previous);
    Callback(np::kCmdSetInput, scratch.address());
  }

  PPCContext& ctx_;
  uint8_t* base_;
  GuestCalls calls_;
  np::InputHistory history_;
};

void Log(const std::string& line) { REXLOG_INFO("{}", line); }

uint32_t BlobWordBE(const std::vector<uint8_t>& blob, size_t offset) {
  return ssf2t::LoadBE32(blob.data(), static_cast<uint32_t>(offset));
}

void LogInputState(np::Frame frame, const std::vector<uint8_t>& state) {
  for (uint32_t player = 0; player < np::kPlayers; ++player) {
    const size_t entry = np::kGuestStateSize + player * np::kInputEntryStride;
    REXLOG_INFO("DEBUG_SAULO netplay frame={} table{} state={} cur={:04X} prev={:04X} pressed={:04X} released={:04X}",
                frame, player, BlobWordBE(state, entry), BlobWordBE(state, entry + np::kInputCurrentOffset),
                BlobWordBE(state, entry + np::kInputPreviousOffset), BlobWordBE(state, entry + 16),
                BlobWordBE(state, entry + 20));
  }
}

template <typename G>
const char* ClassifyAddress(uint32_t address) {
  const auto inside = [address](uint32_t start, uint32_t size) { return address >= start && address < start + size; };
  if (inside(G::kRngState, np::kRngStateBytes)) return "rng";
  if (inside(G::kSideOrder, np::kSideOrderBytes)) return "side";
  if (inside(G::kPaceCounter, 4)) return "pace";
  if (address >= G::kDrawListLow && address < G::kDrawListHigh) return "draw";
  return "other";
}

template <typename G>
void LogLeak(np::Frame frame, const char* label, const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
  const np::DiffRuns all = np::FindRuns(a, b, kRunMergeGap, kMaxClassifiedRuns);
  uint32_t rng = 0, side = 0, pace = 0, draw = 0;
  np::DiffRuns other;
  for (const np::DiffRun& run : all.runs) {
    const std::string kind = ClassifyAddress<G>(np::kDiagWindowLow + static_cast<uint32_t>(run.offset));
    if (kind == "rng") ++rng;
    else if (kind == "side") ++side;
    else if (kind == "pace") ++pace;
    else if (kind == "draw") ++draw;
    else other.runs.push_back(run);
  }
  other.total = other.runs.size();
  if (other.runs.size() > kMaxRuns) other.runs.resize(kMaxRuns);
  const std::string tag = std::string(label) + "(rng=" + std::to_string(rng) + " side=" + std::to_string(side) +
                          " pace=" + std::to_string(pace) + " draw=" + std::to_string(draw) + ") other";
  Log(np::FormatRuns(frame, tag.c_str(), np::kDiagWindowLow, other));
}

void LogTrace(np::Frame frame, const char* pass, const PassTrace& trace) {
  std::string callers;
  char text[16];
  for (uint32_t i = 0; i < trace.rand_calls && i < kTraceCallers; ++i) {
    std::snprintf(text, sizeof text, "%s%08X", i ? "," : "", trace.rand_callers[i]);
    callers += text;
  }
  REXLOG_INFO("DEBUG_SAULO netplay frame={} pass={} rand={} side_setup={} gate={} lr={}", frame, pass,
              trace.rand_calls, trace.side_setups, trace.gate_queries, callers);
}

template <typename G>
void RunDeterminismSample(PPCContext& ctx, uint8_t* base, const GuestCalls& calls, np::Frame frame) {
  GuestSimulation<G> sim(ctx, base, calls);
  np::InputWord current[np::kPlayers];
  np::InputWord previous[np::kPlayers];
  for (uint32_t player = 0; player < np::kPlayers; ++player) {
    current[player] = sim.TableWord(player, np::kInputCurrentOffset);
    previous[player] = sim.TableWord(player, np::kInputPreviousOffset);
  }
  sim.SetPrevious(previous);

  std::vector<uint8_t> a, a_again, speculative, normal, native;
  g_schedule.Taken(frame);
  if (!sim.Save(a)) {
    Log("DEBUG_SAULO netplay determinism save failed");
    sim.Step(false);
    return;
  }
  const std::vector<uint8_t> window_start = sim.DiagWindow();
  const np::InputWord inputs[np::kPlayers] = {current[0], current[1]};

  PassTrace trace_speculative, trace_normal, trace_native;
  g_trace = &trace_speculative;
  sim.Advance(inputs, true);
  g_trace = nullptr;
  sim.Save(speculative);
  const uint32_t speculative_checksum = sim.Checksum();
  const std::vector<uint8_t> window_after_first = sim.DiagWindow();

  sim.Load(a);
  sim.Save(a_again);
  const np::BlobDiff roundtrip = np::CompareBlobs(a, a_again);
  const std::vector<uint8_t> window_second_start = sim.DiagWindow();

  g_trace = &trace_normal;
  sim.Advance(inputs, false);
  g_trace = nullptr;
  sim.Save(normal);
  const uint32_t normal_checksum = sim.Checksum();
  const np::BlobDiff resim = np::CompareBlobs(speculative, normal);
  const std::vector<uint8_t> window_after_second = sim.DiagWindow();

  sim.Load(a);
  sim.SetPrevious(previous);
  g_trace = &trace_native;
  sim.Step(false);
  g_trace = nullptr;
  sim.Save(native);
  const np::BlobDiff native_diff = np::CompareBlobs(normal, native, np::kGuestStateSize);

  const bool ok = roundtrip.equal && resim.equal && speculative_checksum == normal_checksum;
  if (!ok) ++g_failures;
  Log(np::FormatCheck(frame, "roundtrip", roundtrip));
  Log(np::FormatCheck(frame, "resim", resim));
  Log(np::FormatCheck(frame, "native", native_diff));
  REXLOG_INFO("DEBUG_SAULO netplay frame={} checksum={:08X}/{:08X} inputs={:04X},{:04X}", frame, speculative_checksum,
              normal_checksum, current[0], current[1]);
  if (!ok || !native_diff.equal || g_schedule.taken() == 1) {
    REXLOG_INFO("DEBUG_SAULO netplay frame={} arena3F0 A={:02X} B1={:02X} A2={:02X} B2={:02X} B0={:02X}", frame,
                a[kWatchedArenaByte], speculative[kWatchedArenaByte], a_again[kWatchedArenaByte],
                normal[kWatchedArenaByte], native[kWatchedArenaByte]);
    LogInputState(frame, a);
    LogTrace(frame, "B1", trace_speculative);
    LogTrace(frame, "B2", trace_normal);
    LogTrace(frame, "B0", trace_native);
    LogLeak<G>(frame, "leak", window_start, window_second_start);
    LogLeak<G>(frame, "post", window_after_first, window_after_second);
  }
  if (g_schedule.taken() >= kMaxSamples) {
    REXLOG_INFO("DEBUG_SAULO netplay determinism samples={} failures={}", g_schedule.taken(), g_failures);
  }
}

template <typename G>
bool CodeMatches(const uint8_t* base) {
  static const bool matches = [base] {
    const bool ok = np::NetplayCodeMatches<G>(base);
    if (!ok) {
      REXLOG_INFO("DEBUG_SAULO netplay determinism test off: {} code differs from the analysed image", G::kName);
    }
    return ok;
  }();
  return matches;
}

template <typename G>
void FightStep(PPCContext& ctx, uint8_t* base, const GuestCalls& calls) {
  if (REXCVAR_GET(ssf2t_netplay_determinism_test) && CodeMatches<G>(base) && base[G::kNetActive] == 0 &&
      ssf2t::LoadBE32(base, G::kStateFunction) == G::kFightLoop &&
      PointerUsable(ssf2t::LoadBE32(base, G::kArenaPointer))) {
    const np::Frame frame = ssf2t::LoadBE32(base, G::kFrameCounter);
    if (g_schedule.ShouldSample(frame)) {
      RunDeterminismSample<G>(ctx, base, calls, frame);
      return;
    }
  }
  calls.step(ctx, base);
}

void EffectsGate(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (g_trace != nullptr) ++g_trace->gate_queries;
  if (g_suppress_effects) {
    ctx.r3.u64 = 0;
    return;
  }
  original(ctx, base);
}

void TraceRand(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (g_trace != nullptr) {
    if (g_trace->rand_calls < kTraceCallers) g_trace->rand_callers[g_trace->rand_calls] = uint32_t(ctx.lr);
    ++g_trace->rand_calls;
  }
  original(ctx, base);
}

void TraceSideSetup(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (g_trace != nullptr) ++g_trace->side_setups;
  original(ctx, base);
}

}  // namespace

REX_HOOK_RAW(base013_sub_823E1CF0) { TraceRand(ctx, base, __imp__base013_sub_823E1CF0); }
REX_HOOK_RAW(base013_sub_82318618) { TraceSideSetup(ctx, base, __imp__base013_sub_82318618); }
REX_HOOK_RAW(base013_sub_8223DDB8) {
  FightStep<np::Base013Netplay>(ctx, base, {__imp__base013_sub_823E90C0, __imp__base013_sub_8223DDB8});
}
REX_HOOK_RAW(base013_sub_8241CC48) { EffectsGate(ctx, base, __imp__base013_sub_8241CC48); }

#if SSF2T_TITLE_UPDATE_IMAGE
REX_HOOK_RAW(tu023_sub_8223DEE0) {
  FightStep<np::TitleUpdate023Netplay>(ctx, base, {__imp__tu023_sub_823E97F8, __imp__tu023_sub_8223DEE0});
}
REX_HOOK_RAW(tu023_sub_8241D320) { EffectsGate(ctx, base, __imp__tu023_sub_8241D320); }
REX_HOOK_RAW(tu023_sub_823E23C8) { TraceRand(ctx, base, __imp__tu023_sub_823E23C8); }
REX_HOOK_RAW(tu023_sub_82318558) { TraceSideSetup(ctx, base, __imp__tu023_sub_82318558); }
#endif
