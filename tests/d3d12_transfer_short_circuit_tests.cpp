// D3D12 transfer-pass short-circuit (patches/rexglue-sdk/0041-d3d12-transfer-short-circuit.windows.patch):
// a draw with no pending render target transfer and no resolve clear skips
// D3D12RenderTargetCache::PerformTransfersAndResolveClears. The predicate and the wiring are checked here;
// the render target cache runs only with a GPU, so the patch text is inspected instead.
// Run: ssf2t_d3d12_transfer_short_circuit_tests

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
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

size_t Find(const std::string& text, const char* needle) {
  return text.find(needle);
}

// Mirrors the guard in the patch: a clear needs the transfer pass, and any bound render target with
// transfers does too.
bool ShouldSkipTransferPass(bool resolve_clear_needed,
                            const std::vector<std::pair<bool, bool>>& render_targets) {
  if (resolve_clear_needed) {
    return false;
  }
  for (const std::pair<bool, bool>& render_target : render_targets) {
    if (render_target.first && render_target.second) {
      return false;
    }
  }
  return true;
}

void TestPredicate() {
  std::printf("transfer-pass predicate\n");
  // No clear and no transfers: the usual draw, may skip.
  CHECK(ShouldSkipTransferPass(false, {{true, false}, {true, false}, {false, false}}));
  // A bound render target with a pending transfer must run the pass.
  CHECK(!ShouldSkipTransferPass(false, {{true, false}, {true, true}}));
  // A clear must never be skipped, even without transfers.
  CHECK(!ShouldSkipTransferPass(true, {{true, false}, {false, false}}));
  CHECK(!ShouldSkipTransferPass(true, {}));
  // A transfer for a render target that is not bound does nothing, so it does not force the pass.
  CHECK(ShouldSkipTransferPass(false, {{false, true}, {false, true}}));
}

void TestPatchContent() {
  std::printf("patch guard and wiring\n");
  const std::string patch =
      ReadFile(std::string(SSF2T_SOURCE_DIR) +
               "/patches/rexglue-sdk/0041-d3d12-transfer-short-circuit.windows.patch");
  CHECK(!patch.empty());

  // The cvar and its default.
  CHECK(Has(patch, "REXCVAR_DECLARE(bool, ssf2t_d3d12_transfer_short_circuit);"));
  CHECK(Has(patch, "REXCVAR_DEFINE_BOOL(ssf2t_d3d12_transfer_short_circuit, true,"));

  // Conservative guard: a clear keeps the pass.
  CHECK(Has(patch, "REXCVAR_GET(ssf2t_d3d12_transfer_short_circuit) &&"));
  CHECK(Has(patch, "!(render_target_resolve_clear_values && resolve_clear_rectangle)"));

  // The transfer scan and the early return.
  CHECK(Has(patch, "bool any_transfers = false;"));
  CHECK(Has(patch, "if (render_targets[i] && !render_target_transfers[i].empty()) {"));
  CHECK(Has(patch, "if (!any_transfers) {"));
  CHECK(Has(patch, "      return;"));

  // Placed at the top of PerformTransfersAndResolveClears, before anything is recorded.
  const size_t function = Find(patch, "void D3D12RenderTargetCache::PerformTransfersAndResolveClears(");
  const size_t path_assert = Find(patch, "assert_true(GetPath() == Path::kHostRenderTargets);");
  const size_t guard = Find(patch, "!(render_target_resolve_clear_values && resolve_clear_rectangle)");
  const size_t clear_rect = Find(patch, "clear_rect.left = LONG(GuestToHostX(");
  CHECK(function != std::string::npos);
  CHECK(path_assert != std::string::npos);
  CHECK(guard != std::string::npos);
  CHECK(function < path_assert && path_assert < guard);
  CHECK(clear_rect == std::string::npos);  // The clear code itself is untouched.

  // Nothing that clears, resolves or transitions ownership is removed or weakened.
  CHECK(!Has(patch, "-  bool resolve_clear_needed ="));
  CHECK(!Has(patch, "-  command_list.D3DClearRenderTargetView("));
  CHECK(!Has(patch, "-  command_list.D3DClearDepthStencilView("));
  CHECK(!Has(patch, "-  SetClearedStencilBits("));
  CHECK(!Has(patch, "-  command_processor_.PushTransitionBarrier("));
  CHECK(!Has(patch, "-  MarkEdramBufferModified("));
  CHECK(!Has(patch, "-  are_current_command_list_render_targets_valid_ = false;"));
}

}  // namespace

int main() {
  TestPredicate();
  TestPatchContent();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
