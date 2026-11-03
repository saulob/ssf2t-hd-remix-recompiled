#include "character/akuma_overlay.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/immediate_drawer.h>

#include "aspect_ratio.h"
#include "character/akuma.h"
#include "character/akuma_portrait.h"
#include "guest_memory.h"
#include "presentation_setting.h"
#include "title_update_image.h"

namespace ssf2t::character {
namespace {

constexpr int64_t kStaleMilliseconds = 3000;

struct Base013Select {
  static constexpr uint32_t kLive = 0x83CA6C9C;
};

struct TitleUpdate023Select {
  static constexpr uint32_t kLive = 0x83CA71B4;
};

std::atomic<int64_t> g_last_touch_ms{0};
std::atomic<uint32_t> g_live{0};

int64_t NowMilliseconds() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

bool SelectActive() {
  const int64_t last = g_last_touch_ms.load(std::memory_order_relaxed);
  return last != 0 && NowMilliseconds() - last < kStaleMilliseconds;
}

template <typename S>
void TouchSelect(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  original(ctx, base);
  if (!AkumaEnabled()) return;
  g_live.store(*GuestHost(base, S::kLive) | (uint32_t(*GuestHost(base, S::kLive + 1)) << 8), std::memory_order_relaxed);
  g_last_touch_ms.store(NowMilliseconds(), std::memory_order_relaxed);
}

class AkumaPortraitOverlay final : public rex::ui::ImGuiDialog {
 public:
  AkumaPortraitOverlay(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate_drawer,
                       std::filesystem::path archive)
      : ImGuiDialog(drawer), immediate_drawer_(immediate_drawer), archive_(std::move(archive)) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    const uint32_t live = g_live.load(std::memory_order_relaxed);
    if (AkumaEnabled() && SelectActive() && GuestShowsAkuma(uint8_t(live), uint8_t(live >> 8))) {
      if (!suppress_logged_) {
        suppress_logged_ = true;
        REXLOG_INFO("DEBUG_SAULO AKUMA overlay draw-suppressed guest-akuma=1");
      }
      return;
    }
    if (!AkumaPortraitVisible(AkumaEnabled(), SelectActive(), uint8_t(live), uint8_t(live >> 8)) || !EnsureTexture()) {
      return;
    }
    const bool letterbox = rex::cvar::GetFlagByName(kLetterboxSetting) != "false";
    const int guest_width = GuestWidth(ParseAspectRatio(rex::cvar::GetFlagByName("ssf2t_aspect_ratio")));
    const ViewRect view =
        GuestView(io.DisplaySize.x, io.DisplaySize.y, float(guest_width) / float(kGuestHeight), letterbox);
    const ViewRect rect = AkumaPortraitRect(view, float(guest_width));
    ImGui::GetBackgroundDrawList()->AddImage(ImTextureRef(reinterpret_cast<ImTextureID>(texture_.get())),
                                             ImVec2(rect.x, rect.y), ImVec2(rect.x + rect.width, rect.y + rect.height));
    if (!draw_logged_) {
      draw_logged_ = true;
      REXLOG_INFO("DEBUG_SAULO AKUMA overlay draw-active");
    }
  }

 private:
  bool EnsureTexture() {
    if (texture_) return true;
    if (load_failed_) return false;
    if (!immediate_drawer_) return Fail("immediate drawer");
    const PortraitImage image = LoadAkumaPortrait(archive_);
    if (image.error) return Fail(image.error);
    texture_ = immediate_drawer_->CreateTexture(uint32_t(image.width), uint32_t(image.height),
                                                rex::ui::ImmediateTextureFilter::kLinear, false, image.rgba.data());
    if (!texture_) return Fail("create texture");
    REXLOG_INFO("DEBUG_SAULO AKUMA overlay texture-ready width={} height={}", image.width, image.height);
    return true;
  }

  bool Fail(const char* stage) {
    load_failed_ = true;
    REXLOG_ERROR("Akuma portrait overlay failed at '{}' ({})", stage, archive_.string());
    return false;
  }

  rex::ui::ImmediateDrawer* immediate_drawer_;
  std::filesystem::path archive_;
  std::unique_ptr<rex::ui::ImmediateTexture> texture_;
  bool load_failed_ = false;
  bool draw_logged_ = false;
  bool suppress_logged_ = false;
};

}  // namespace

void CreateAkumaOverlay(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate_drawer,
                        const std::filesystem::path& game_dir) {
  new AkumaPortraitOverlay(drawer, immediate_drawer, game_dir / "flog_x360.fpg");  // retained by the drawer
}

}  // namespace ssf2t::character

#define SSF2T_AKUMA_TOUCH_HOOK(name, S) \
  REX_EXTERN(__imp__##name); \
  REX_HOOK_RAW(name) { ssf2t::character::TouchSelect<ssf2t::character::S>(ctx, base, __imp__##name); }

SSF2T_AKUMA_TOUCH_HOOK(base013_sub_8223DF78, Base013Select)
SSF2T_AKUMA_TOUCH_HOOK(base013_sub_82240F78, Base013Select)
SSF2T_AKUMA_TOUCH_HOOK(base013_sub_82240870, Base013Select)
SSF2T_AKUMA_TOUCH_HOOK(base013_sub_82245310, Base013Select)

#if SSF2T_TITLE_UPDATE_IMAGE
SSF2T_AKUMA_TOUCH_HOOK(tu023_sub_8223E0A8, TitleUpdate023Select)
SSF2T_AKUMA_TOUCH_HOOK(tu023_sub_822410A8, TitleUpdate023Select)
SSF2T_AKUMA_TOUCH_HOOK(tu023_sub_822409A0, TitleUpdate023Select)
SSF2T_AKUMA_TOUCH_HOOK(tu023_sub_82245378, TitleUpdate023Select)
#endif
