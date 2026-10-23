// Common render target draw state memoization (patches/rexglue-sdk/0044-gpu-draw-state-memo.patch):
// a draw claiming exactly the same render target ranges as the previous update, with the ownership
// unchanged since, skips ChangeOwnership. The memo condition, its invalidation by a generation
// counter and the conservative recording are mirrored here; the cache runs only with a GPU, so the
// patch text is inspected for the wiring.
// Run: ssf2t_gpu_draw_state_memo_tests

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

std::string ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

int Count(const std::string& text, const char* needle) {
  int count = 0;
  const size_t length = std::char_traits<char>::length(needle);
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + length)) {
    ++count;
  }
  return count;
}

// One claim of Update: the render target key and the used tile range.
struct Claim {
  uint32_t key;
  uint32_t start_tiles;
  uint32_t length_tiles;
  bool operator==(const Claim& other) const {
    return key == other.key && start_tiles == other.start_tiles &&
           length_tiles == other.length_tiles;
  }
};

// Mirrors the memo of RenderTargetCache: the generation of ownership_ranges_ and the claims of the
// last Update that recorded them.
struct MemoState {
  bool enabled = true;
  uint64_t generation = 1;
  uint64_t memo_generation = 0;
  std::vector<Claim> memo;
};

// Mirrors the guard of Update and what it does on a hit (skip ChangeOwnership and its transfer) and
// on a miss (claim, bumping the generation if something changed, then record). Returns whether the
// full path ran.
bool Update(MemoState& state, bool interlock_barrier_only, const std::vector<Claim>& claims,
            bool ownership_changes) {
  bool memo_hit = false;
  if (!interlock_barrier_only && state.enabled && state.memo_generation == state.generation &&
      state.memo.size() == claims.size() && state.memo == claims) {
    memo_hit = true;
  }
  if (interlock_barrier_only) {
    return false;
  }
  if (!memo_hit && ownership_changes) {
    ++state.generation;
  }
  if (!memo_hit) {
    state.memo = claims;
    state.memo_generation = state.generation;
  }
  return memo_hit;
}

// A clear, a resolve or a transfer is ownership work that claims tiles.
void OwnershipChanges(MemoState& state) { ++state.generation; }

const std::vector<Claim> kClaimsA = {{100, 2, 30}};
const std::vector<Claim> kClaimsB = {{100, 2, 30}, {200, 40, 12}};
const std::vector<Claim> kClaimsAOtherRange = {{100, 4, 30}};
const std::vector<Claim> kClaimsAOtherKey = {{150, 2, 30}};

void TestReuse() {
  std::printf("identical ranges and unchanged ownership reuse the memo\n");
  MemoState state;
  CHECK(!Update(state, false, kClaimsA, true));   // First update claims and records.
  CHECK(Update(state, false, kClaimsA, false));   // Same update again.
  CHECK(Update(state, false, kClaimsA, false));   // Still the same.
}

void TestDifferentRanges() {
  std::printf("a different range, key or count does not reuse the memo\n");
  MemoState state;
  CHECK(!Update(state, false, kClaimsA, true));
  CHECK(!Update(state, false, kClaimsB, true));          // More render targets.
  CHECK(!Update(state, false, kClaimsAOtherRange, true));  // Different used tiles.
  CHECK(!Update(state, false, kClaimsAOtherKey, true));    // Different render target.
  CHECK(!Update(state, false, {}, true));                  // No render targets.
  CHECK(Update(state, false, {}, false));                  // Now the empty set is the memo.
}

void TestGenerationChange() {
  std::printf("an ownership generation change does not reuse the memo\n");
  MemoState state;
  CHECK(!Update(state, false, kClaimsA, true));
  CHECK(Update(state, false, kClaimsA, false));
  // Something else changed the ownership of the EDRAM (a clear, a resolve or a transfer).
  OwnershipChanges(state);
  CHECK(!Update(state, false, kClaimsA, false));
  // The memo was restored, so the next identical draw reuses it again.
  CHECK(Update(state, false, kClaimsA, false));
}

void TestClearResolveTransfer() {
  std::printf("clears, resolves and transfers do not incorrectly reuse\n");
  // A clear writes and changes ownership, so the memo of the previous draw is invalid for it.
  {
    MemoState state;
    CHECK(!Update(state, false, kClaimsA, true));
    CHECK(Update(state, false, kClaimsA, false));
    OwnershipChanges(state);  // The clear claims its rectangle.
    CHECK(!Update(state, false, kClaimsA, false));
  }
  // A resolve clears into a render target the same way.
  {
    MemoState state;
    CHECK(!Update(state, false, kClaimsB, true));
    CHECK(Update(state, false, kClaimsB, false));
    OwnershipChanges(state);  // The resolve clear claims the resolved tiles.
    CHECK(!Update(state, false, kClaimsB, false));
  }
  // A transfer is another draw claiming tiles of the same render targets.
  {
    MemoState state;
    CHECK(!Update(state, false, kClaimsA, true));
    CHECK(Update(state, false, kClaimsA, false));
    CHECK(!Update(state, false, kClaimsAOtherRange, true));  // A transfer took the tiles.
    CHECK(!Update(state, false, kClaimsA, false));            // Back to the first draw.
    CHECK(Update(state, false, kClaimsA, false));
  }
}

void TestInterlockAndDisabled() {
  std::printf("the interlock-only path and the cvar never reuse the memo\n");
  MemoState state;
  CHECK(!Update(state, true, kClaimsA, true));
  CHECK(!Update(state, true, kClaimsA, false));
  // Interlock updates do not record either.
  CHECK(state.memo.empty());
  CHECK(!Update(state, false, kClaimsA, true));
  CHECK(Update(state, false, kClaimsA, false));
  state.enabled = false;
  CHECK(!Update(state, false, kClaimsA, false));
  CHECK(!Update(state, false, kClaimsA, false));
}

void TestPatchContent() {
  std::printf("patch memo keys, invalidation and wiring\n");
  const std::string patch =
      ReadFile(std::string(SSF2T_SOURCE_DIR) + "/patches/rexglue-sdk/0044-gpu-draw-state-memo.patch");
  CHECK(!patch.empty());

  // The cvar and its default.
  CHECK(Has(patch, "REXCVAR_DEFINE_BOOL(gpu_draw_state_memo, true, \"GPU\","));

  // The memo state.
  CHECK(Has(patch, "uint64_t ownership_generation_ = 1;"));
  CHECK(Has(patch, "uint64_t ownership_memo_generation_ = 0;"));
  CHECK(Has(patch, "uint32_t ownership_memo_count_ = 0;"));
  CHECK(Has(patch, "RenderTargetKey ownership_memo_keys_[1 + xenos::kMaxColorRenderTargets];"));
  CHECK(Has(patch, "uint32_t ownership_memo_start_tiles_[1 + xenos::kMaxColorRenderTargets];"));
  CHECK(Has(patch, "uint32_t ownership_memo_length_tiles_[1 + xenos::kMaxColorRenderTargets];"));

  // Every change of ownership_ranges_ increments the generation.
  CHECK(Count(patch, "++ownership_generation_;") == 4);

  // The guard compares the generation, the count and every key, start and length.
  CHECK(Has(patch, "if (!interlock_barrier_only && REXCVAR_GET(gpu_draw_state_memo) &&"));
  CHECK(Has(patch, "ownership_memo_generation_ == ownership_generation_ &&"));
  CHECK(Has(patch, "ownership_memo_count_ == edram_bases_sorted_count) {"));
  CHECK(Has(patch, "ownership_memo_keys_[i] != rt_keys[edram_bases_sorted[i].second] ||"));
  CHECK(Has(patch, "ownership_memo_start_tiles_[i] != rt_skipped_tiles[i] ||"));
  CHECK(Has(patch, "ownership_memo_length_tiles_[i] != rt_lengths_tiles[i] - rt_skipped_tiles[i]"));

  // On a hit, ChangeOwnership (and its transfers) is skipped, and the used tiles are still set.
  CHECK(Has(patch, "if (!ownership_memo_hit) {"));
  CHECK(Has(patch, "ChangeOwnership(rt_keys[rt_bit_index], rt_skipped_tiles[i],"));
  CHECK(Has(patch, "last_update_used_start_tiles_[rt_bit_index] ="));
  // The memo is recorded only by the full path, after the claims it made.
  CHECK(Has(patch, "if (!interlock_barrier_only && !ownership_memo_hit) {"));
  CHECK(Has(patch, "ownership_memo_generation_ = ownership_generation_;"));

  // The rest of the path (barrier when needed, bindings, accumulated targets) is untouched.
  CHECK(!Has(patch, "-  if (interlock_barrier_only) {"));
  CHECK(!Has(patch, "-  last_update_used_render_targets_[i] ="));
  CHECK(!Has(patch, "-    are_accumulated_render_targets_valid_ = true;"));
}

}  // namespace

int main() {
  TestReuse();
  TestDifferentRanges();
  TestGenerationChange();
  TestClearResolveTransfer();
  TestInterlockAndDisabled();
  TestPatchContent();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
