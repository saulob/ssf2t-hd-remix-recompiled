# ReXGlue runtime patches 0040 to 0059

The runtime and Xenos GPU plugin are built from ReXGlue v0.10.0 with the patches in `patches/rexglue-sdk`
(`tools/build-patched-rexruntime.ps1`). These reduce CPU cost per draw and per frame. Each is checked by a headless
test that models the patched logic and verifies the patch text (`ssf2t_runtime_tests`).

| Patch | Platform | Cvar | Change |
| --- | --- | --- | --- |
| 0040-lock-free-guest-clock | both | - | Guest clock reads take no lock: a ratio change starts a new clock segment that readers pick up through an atomic pointer (`clock.cpp`). |
| 0041-d3d12-transfer-short-circuit | Windows | `ssf2t_d3d12_transfer_short_circuit` | A draw with no render target transfers and no resolve clear skips the transfer pass. Clears and bound render targets with a transfer keep the full path. |
| 0042-d3d12-sampler-memo | Windows | `ssf2t_d3d12_sampler_memo` | Sampler parameters are recomputed only when the shader, a fetch constant (generation counter) or `anisotropic_override` changed. |
| 0043-shared-memory-fast-path | both | - | `SharedMemory::RequestRanges` returns without the global critical region when every requested page is valid and allocated. |
| 0044-gpu-draw-state-memo | both | `gpu_draw_state_memo` | The render target cache skips the ownership claim of a draw that claims the same ranges as the previous update while the ownership generation is unchanged. |
| 0045-d3d12-async-submission | Windows | `d3d12_async_submission`, `d3d12_async_submission_draws` | A dedicated thread replays, executes and fences the recorded command list. Swap, primary buffer end and occlusion query boundaries still drain submissions where they did; a submission may also end every 2000 draws. |
| 0049-command-processor-progress-wake | both | - | The command processor wakes guest word waiters after the read pointer write-back and a scratch register store, so a guest wait on either returns at once. |
| 0050-gpu-side-effect-free-register-ranges | both | - | Register runs are written in bulk everywhere except the shader constants, the scratch registers, `COHER_STATUS_HOST` and the gamma ramp ports. |
| 0051-draw-extent-scissor-signed-compare | both | - | A negative scissor edge is no longer read as 8192 or more, which skipped a needless CPU vertex shader pass. |
| 0052-d3d12-host-threads-avoid-cpu0 | Windows | `gpu_host_threads_avoid_cpu0` | With 8 or more logical CPUs, the command processor and submission threads stay off CPUs 0 and 1. |
| 0055-inline-bit-scan-forward | Windows | - | `rex::bit_scan_forward` is defined inline in `math.h` instead of out-of-line in `math_msvc.cpp`/`math_gcc.cpp`, so `rexgpu-xenos` does not call it through rexruntime's import table. |
| 0056-gpu-plugin-thinlto | Windows | - | `rexgpu-xenos` gets ThinLTO in Release builds (Clang/lld only), separately from this project's own executable ThinLTO. |
| 0057-d3d12-unchanged-constant-writes | Windows | `gpu_skip_unchanged_constant_writes` | A float, bool, loop or fetch constant rewritten with the value it already has leaves the uploaded constant buffers, texture bindings and fetch-constants generation untouched. Vertex-buffer residency is still invalidated on every fetch-constant write regardless. |
| 0058-texture-cache-resolve-target-index | Windows | - | `TextureCache::FindResolveCopyTargets` looks up candidates by an index of textures by base page (`textures_by_base_page_`) instead of scanning every cached texture. |
| 0059-d3d12-per-draw-loops | Windows | - | `InvalidateVertexBufferResidencyRange` clears 64-bit words instead of one fetch bit at a time; float constant uploads copy runs of consecutive used constants instead of one 16-byte memcpy per constant; `GetStencilBits`/`AddStencilBits` process one or two contiguous EDRAM ranges instead of looping tile-by-tile with wraparound masking. |

Setting a cvar to `false` restores the stock behavior. Patch 0045 creates its thread at setup only when the cvar is on and
falls back to the synchronous path if the thread cannot be created.

The shared memory patch does not shrink the invalidation granularity (`gpu_invalidation_expand_pages`): 0004 keeps GPU
page validity across frames, so the trade-off needs measurements on this game first.

## Frame timing

Hard Corps Uprising's low-FPS catch-up (`game_delta_clock`) is not part of this port. That game advances its stage
scene from an accumulator that can be topped up by whole missed steps. This game calls its update with the literal
1/60 s once per main loop iteration, paced by the 60 Hz virtual vblank, so the only way to keep speed at low frame
rates would be to run the loop body more than once between presents, which needs a decoupled simulation and render.
