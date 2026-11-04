#include "character/ultra_portrait_overlay.h"

#include <array>
#include <memory>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/immediate_drawer.h>

#include "aspect_ratio.h"
#include "character/akuma_overlay.h"
#include "character/akuma_portrait.h"
#include "character/ultra_fighters.h"
#include "character/evil_ryu_assets.h"
#include "character/evil_ryu_sprites.h"
#include "character/ultra_navigation.h"
#include "character/ultra_portraits.h"
#include "presentation_setting.h"

namespace ssf2t::character {
namespace {

constexpr std::array<UltraPortrait, kUltraPortraitCount> kPortraits = {UltraPortrait::kEvilRyu,
                                                                        UltraPortrait::kViolentKen};

const char* PortraitName(UltraPortrait which) { return which == UltraPortrait::kEvilRyu ? "Evil Ryu" : "Violent Ken"; }

constexpr float kNameTopY = 40.0f;

class UltraPortraitOverlay final : public rex::ui::ImGuiDialog {
 public:
  UltraPortraitOverlay(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate_drawer,
                       std::filesystem::path game_dir)
      : ImGuiDialog(drawer), immediate_drawer_(immediate_drawer), game_dir_(std::move(game_dir)) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    const bool online = CharacterSelectOnline();
    const bool enabled = EvilRyuAvailableFor(online);
    const bool select_active = CharacterSelectActive();
    if (!enabled || !select_active) return;
    if (!loaded_) LoadTextures();

    const bool letterbox = rex::cvar::GetFlagByName(kLetterboxSetting) != "false";
    const int guest_width = GuestWidth(ParseAspectRatio(rex::cvar::GetFlagByName("ssf2t_aspect_ratio")));
    const ViewRect view =
        GuestView(io.DisplaySize.x, io.DisplaySize.y, float(guest_width) / float(kGuestHeight), letterbox);
    const SelectLayout layout = CharacterSelectLayout();
    for (const UltraPortrait which : kPortraits) {
      if (online && which != UltraPortrait::kEvilRyu) continue;
      const auto& texture = textures_[size_t(which)];
      if (!UltraPortraitVisible(enabled, select_active, immediate_drawer_ != nullptr, texture != nullptr)) continue;
      const ViewRect rect = UltraPortraitRect(view, float(guest_width), which, layout);
      ImGui::GetBackgroundDrawList()->AddImage(ImTextureRef(reinterpret_cast<ImTextureID>(texture.get())),
                                               ImVec2(rect.x, rect.y), ImVec2(rect.x + rect.width, rect.y + rect.height));
    }
    DrawEvilRyuPresentation(view, float(guest_width));
  }

 private:
  void DrawEvilRyuPresentation(const ViewRect& view, float guest_width) {
    ImDrawList* list = ImGui::GetBackgroundDrawList();
    for (int side = 0; side < 2; ++side) {
      if (!GlobalUltraNavigation().OnEvilRyu(side)) continue;
      const ViewRect portrait = LargePortraitPicture(side);
      if (name_ && name_height_ > 0 && !GuestNameDrawnRecently(side)) {
        const float width = portrait.width * 0.9f;
        const ViewRect plate = {portrait.x + (portrait.width - width) * 0.5f, kNameTopY, width,
                                width * float(name_height_) / float(name_width_)};
        const ViewRect rect = ToView(view, guest_width, plate);
        list->AddImage(ImTextureRef(reinterpret_cast<ImTextureID>(name_.get())), ImVec2(rect.x, rect.y),
                       ImVec2(rect.x + rect.width, rect.y + rect.height));
      }
    }
  }

  std::unique_ptr<rex::ui::ImmediateTexture> CreateAsset(EvilRyuAsset which, int* width, int* height) {
    const PortraitImage image = LoadEvilRyuAsset(game_dir_ / kEvilRyuAssetFile, which);
    if (image.error) {
      REXLOG_ERROR("DEBUG_SAULO EVIL_RYU asset {} failed at '{}'", uint32_t(which), image.error);
      return nullptr;
    }
    auto texture = immediate_drawer_->CreateTexture(uint32_t(image.width), uint32_t(image.height),
                                                    rex::ui::ImmediateTextureFilter::kLinear, false, image.rgba.data());
    if (!texture) {
      REXLOG_ERROR("DEBUG_SAULO EVIL_RYU asset {} failed at 'create texture'", uint32_t(which));
      return nullptr;
    }
    *width = image.width;
    *height = image.height;
    return texture;
  }

  void LoadTextures() {
    loaded_ = true;
    if (!immediate_drawer_) {
      REXLOG_ERROR("DEBUG_SAULO ULTRA portraits failed at 'immediate drawer'");
      return;
    }
    const PortraitImage frame = LoadAkumaPortrait(game_dir_ / "flog_x360.fpg");
    for (const UltraPortrait which : kPortraits) {
      PortraitImage image = LoadUltraPortrait(game_dir_ / kUltraPortraitFile, which);
      if (image.error) {
        REXLOG_ERROR("DEBUG_SAULO ULTRA portrait {} failed at '{}'", PortraitName(which), image.error);
        continue;
      }
      ApplyRosterFrame(image, frame);
      auto texture = immediate_drawer_->CreateTexture(uint32_t(image.width), uint32_t(image.height),
                                                      rex::ui::ImmediateTextureFilter::kLinear, false, image.rgba.data());
      if (!texture) {
        REXLOG_ERROR("DEBUG_SAULO ULTRA portrait {} failed at 'create texture'", PortraitName(which));
        continue;
      }
      textures_[size_t(which)] = std::move(texture);
    }
    name_ = CreateAsset(EvilRyuAsset::kNameGraphic, &name_width_, &name_height_);
  }

  rex::ui::ImmediateDrawer* immediate_drawer_;
  std::filesystem::path game_dir_;
  std::array<std::unique_ptr<rex::ui::ImmediateTexture>, kUltraPortraitCount> textures_;
  std::unique_ptr<rex::ui::ImmediateTexture> name_;
  int name_width_ = 0;
  int name_height_ = 0;
  bool loaded_ = false;
};

}  // namespace

void CreateUltraPortraitOverlay(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate_drawer,
                                const std::filesystem::path& game_dir) {
  new UltraPortraitOverlay(drawer, immediate_drawer, game_dir);  // retained by the drawer
}

}  // namespace ssf2t::character
