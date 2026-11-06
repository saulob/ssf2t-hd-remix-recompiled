#include "gpu_fence_wait.h"

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include "guest_memory.h"

namespace {

using ssf2t::GpuBlockResult;
using ssf2t::GpuWaitSites;

struct WaitState {
  ssf2t::GpuWordWait wait;
  const uint32_t* word = nullptr;
  const uint32_t* guest_word = nullptr;
};
thread_local WaitState t_wait;

ssf2t::GpuWaitHealth g_health[ssf2t::kGpuWaitSiteCount];

uint32_t LoadWord(const uint32_t* word) { return __atomic_load_n(word, __ATOMIC_ACQUIRE); }

// Both views are the same pages; a difference that outlasts one re-read means the physical address is not the one
// the guest reads.
bool ViewsAgree(const uint32_t* physical_word, const uint32_t* guest_word) {
  const uint32_t physical = LoadWord(physical_word);
  return LoadWord(guest_word) == physical || LoadWord(physical_word) != physical;
}

bool ResolveWord(uint8_t* base, uint32_t device, uint32_t word_offset, WaitState& state) {
  const uint32_t block = ssf2t::LoadBE32(ssf2t::GuestHost(base, device + ssf2t::kGpuWritebackBlockOffset), 0);
  rex::system::KernelState* kernel = rex::system::kernel_state();
  if (!block || (block & 3) || !kernel || !kernel->memory()) {
    return false;
  }
  const uint32_t address = block + word_offset;
  state.word = kernel->memory()->TranslatePhysical<const uint32_t*>(ssf2t::GpuHardwareAddress(address));
  state.guest_word = reinterpret_cast<const uint32_t*>(ssf2t::GuestHost(base, address));
  return ViewsAgree(state.word, state.guest_word);
}

void BlockUntilWordWritten(const ssf2t::GpuWaitSite& site, ssf2t::GpuWaitHealth& health, WaitState& state) {
  const uint32_t seen = state.wait.seen();
  const GpuBlockResult result = ssf2t::BlockWhileWordHolds(state.word, seen, ssf2t::kGpuWaitBlockMs);
  const uint32_t word = LoadWord(state.word);
  const bool views_agree = ViewsAgree(state.word, state.guest_word);
  if (const char* reason = health.Record(result, word != seen, views_agree)) {
    REXLOG_WARN("GPU wait {}: blocking disabled ({}), the guest polls again", site.name, reason);
  }
  state.wait.ShouldBlock(word);
}

// The poll takes the wait object and returns nonzero while the wait should go on; its caller then re-checks the word.
void PollGpuWait(PPCContext& ctx, uint8_t* base, PPCFunc* original, const GpuWaitSites& sites) {
  const uint32_t object = ctx.r3.u32;
  const uint32_t poll_return = uint32_t(ctx.lr);
  original(ctx, base);
  if (!ctx.r3.u32) {
    return;
  }
  size_t index = 0;
  while (index < sites.size() && sites[index].poll_return != poll_return) {
    ++index;
  }
  if (index == sites.size() || !g_health[index].blocking()) {
    return;
  }
  WaitState& state = t_wait;
  const uint32_t start = ssf2t::LoadBE32(ssf2t::GuestHost(base, object + ssf2t::kGpuWaitStartOffset), 0);
  if (!state.wait.IsWait(object, start)) {
    state.wait.End();
    const uint32_t device = ssf2t::LoadBE32(ssf2t::GuestHost(base, object), 0);
    if (ResolveWord(base, device, sites[index].word_offset, state)) {
      state.wait.Begin(object, start, LoadWord(state.word));
    }
    return;
  }
  if (state.wait.ShouldBlock(LoadWord(state.word))) {
    BlockUntilWordWritten(sites[index], g_health[index], state);
  }
}

}  // namespace

REX_EXTERN(__imp__base013_sub_839A14B8);
REX_HOOK_RAW(base013_sub_839A14B8) { PollGpuWait(ctx, base, __imp__base013_sub_839A14B8, ssf2t::kBase013GpuWaits); }

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_839A1C10);
REX_HOOK_RAW(tu023_sub_839A1C10) { PollGpuWait(ctx, base, __imp__tu023_sub_839A1C10, ssf2t::kTitleUpdate023GpuWaits); }
#endif
