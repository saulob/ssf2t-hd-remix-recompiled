#include "message_box.h"

#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <utility>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#include "title_update_image.h"

namespace {

constexpr uint32_t kErrorIoPending = 997;
constexpr uint32_t kSystemUiNotification = 0x9;  // XN_SYS_UI

ssf2t::MessageBoxOpener g_open;

std::string GuestText(const uint8_t* base, uint32_t address) {
  return address ? ssf2t::Utf16BeToUtf8(ssf2t::GuestHost(base, address)) : std::string();
}

// The game's message boxes (the invite confirmation among them) as the port's own overlay, which the controller and
// the keyboard can answer. Completes the overlapped the way ReXGlue's own dialog does.
template <typename G>
void ShowMessageBox(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  static const bool wrapper_matches = ssf2t::IsMessageBoxWrapper(base + G::kShowMessageBoxWrapper);
  const uint32_t count = ctx.r6.u32;
  const uint32_t result_ptr = ctx.r10.u32;
  const uint32_t overlapped = ssf2t::LoadBE32(base, ctx.r1.u32 + ssf2t::kMessageBoxOverlappedStackOffset);
  if (!g_open || !wrapper_matches || !ssf2t::MessageBoxShape(count, result_ptr, overlapped)) {
    original(ctx, base);
    return;
  }
  ssf2t::MessageBoxRequest request;
  request.title = GuestText(base, ctx.r4.u32);
  request.text = GuestText(base, ctx.r5.u32);
  for (uint32_t i = 0; i < count; ++i) {
    request.buttons.push_back(GuestText(base, ssf2t::LoadBE32(ssf2t::GuestHost(base, ctx.r7.u32), i * 4)));
  }
  request.focused = ssf2t::FocusedButton(ctx.r8.u32, count);
  REXLOG_INFO("DEBUG_SAULO message box shown title=\"{}\" buttons={} focused={} caller={:08X}", request.title, count,
              request.focused, uint32_t(ctx.lr));

  auto run = [request = std::move(request), result_ptr, base]() mutable -> rex::X_RESULT {
    auto promise = std::make_shared<std::promise<ssf2t::MessageBoxResult>>();
    std::future<ssf2t::MessageBoxResult> answer = promise->get_future();
    g_open(std::move(request), [promise](ssf2t::MessageBoxResult result) { promise->set_value(result); });
    const ssf2t::MessageBoxResult result = answer.get();
    if (result.button) ssf2t::StoreBE32(const_cast<uint8_t*>(ssf2t::GuestHost(base, result_ptr)), 0, *result.button);
    REXLOG_INFO("DEBUG_SAULO message box closed status={} button={}", result.status,
                result.button ? int32_t(*result.button) : -1);
    return result.status;
  };
  auto pre = [] { REX_KERNEL_STATE()->BroadcastNotification(kSystemUiNotification, true); };
  auto post = [] {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REX_KERNEL_STATE()->BroadcastNotification(kSystemUiNotification, false);
  };
  REX_KERNEL_STATE()->CompleteOverlappedDeferred(std::function<rex::X_RESULT()>(std::move(run)), overlapped, pre,
                                                 post);
  ctx.r3.u64 = kErrorIoPending;
}

}  // namespace

namespace ssf2t {

void SetMessageBoxOpener(MessageBoxOpener open) {
  g_open = std::move(open);
}

}  // namespace ssf2t

REX_EXTERN(__imp__base013_sub_839C3D48);
REX_HOOK_RAW(base013_sub_839C3D48) {
  ShowMessageBox<ssf2t::Base013MessageBox>(ctx, base, __imp__base013_sub_839C3D48);
}

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_839C44A8);
REX_HOOK_RAW(tu023_sub_839C44A8) {
  ShowMessageBox<ssf2t::TitleUpdate023MessageBox>(ctx, base, __imp__tu023_sub_839C44A8);
}
#endif
