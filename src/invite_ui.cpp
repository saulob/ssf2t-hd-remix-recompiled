#include "invite_ui.h"

#include <cfloat>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include <rex/ui/imgui_dialog.h>

#include "launcher.h"
#include "localization/ui_text.h"
#include "modal_dialog.h"
#include "online/friend_view.h"
#include "online_runtime.h"
#include "player_match_invite.h"

namespace ssf2t {
namespace {

constexpr ImGuiWindowFlags kWindowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                          ImGuiWindowFlags_AlwaysAutoResize;
constexpr double kInviteCheckSeconds = 0.5;

rex::ui::ImGuiDrawer* g_drawer = nullptr;
std::function<void(std::function<void()>)> g_post;
std::set<std::string> g_prompted;  // UI thread only

bool BeginCenteredWindow(const char* id, const ImGuiIO& io, bool* focused) {
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSizeConstraints(ImVec2(io.DisplaySize.x * 0.3f, 0.0f), ImVec2(io.DisplaySize.x * 0.6f, FLT_MAX));
  ImGui::SetNextWindowBgAlpha(0.94f);
  if (!*focused) {
    *focused = true;
    ImGui::SetNextWindowFocus();
  }
  return ImGui::Begin(id, nullptr, kWindowFlags);
}

// The key that activates a button is still down on the frame it does; a mouse click is a release.
InputSource ActivationSource() {
  if (ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown)) return InputSource::kController;
  if (ImGui::IsKeyDown(ImGuiKey_Enter) || ImGui::IsKeyDown(ImGuiKey_KeypadEnter) || ImGui::IsKeyDown(ImGuiKey_Space)) {
    return InputSource::kKeyboard;
  }
  return InputSource::kMouse;
}

void Hint(const char* text) {
  ImGui::Spacing();
  ImGui::TextDisabled("%s", text);
}

class InvitePicker final : public ModalDialog {
 public:
  explicit InvitePicker(rex::ui::ImGuiDrawer* drawer) : ModalDialog(drawer) {}

 protected:
  void DrawContent(ImGuiIO& io) override {
    ImGui::PushFont(LauncherRegularFont());
    if (BeginCenteredWindow((std::string(Tr("Invite a friend")) + "##invite_picker").c_str(), io, &focused_)) {
      ImGui::TextUnformatted(Tr("Invite a friend"));
      ImGui::Separator();
      const std::vector<online::InviteChoice> choices = online::InviteChoices(Online().snapshot().friends);
      if (choices.empty()) ImGui::TextWrapped("%s", Tr("No friends yet. Add them on the launcher's Online page."));
      for (const online::InviteChoice& choice : choices) {
        ImGui::PushID(choice.player.c_str());
        const std::string row = choice.name + "    " + choice.status;
        const ImGuiSelectableFlags flags = choice.selectable ? 0 : ImGuiSelectableFlags_Disabled;
        if (ImGui::Selectable(row.c_str(), false, flags) && choice.selectable) {
          Online().SendInvite(choice.player);
          RequestClose();
        }
        ImGui::PopID();
      }
      Hint(Tr("A  Invite     B  Back"));
    }
    ImGui::End();
    ImGui::PopFont();
  }

 private:
  bool focused_ = false;
};

class InvitePrompt final : public ModalDialog {
 public:
  InvitePrompt(rex::ui::ImGuiDrawer* drawer, online::Invite invite) : ModalDialog(drawer), invite_(std::move(invite)) {}

 protected:
  void DrawContent(ImGuiIO& io) override {
    ImGui::PushFont(LauncherRegularFont());
    if (BeginCenteredWindow((std::string(Tr("Online Match invite")) + "##invite_prompt").c_str(), io, &focused_)) {
      ImGui::TextUnformatted((invite_.ad.name + Tr(" invited you to an Online Match.")).c_str());
      ImGui::Spacing();
      if (ImGui::Button(Tr("Accept"))) {
        const InputSource source = ActivationSource();
        if (const auto ad = Online().AcceptInvite(invite_.id)) DeliverAcceptedInvite(*ad, source);
        RequestClose();
      }
      ImGui::SameLine();
      if (ImGui::Button(Tr("Decline"))) {
        Online().DeclineInvite(invite_.id);
        RequestClose();
      }
      Hint(Tr("A  Choose     B  Later"));
    }
    ImGui::End();
    ImGui::PopFont();
  }

 private:
  online::Invite invite_;
  bool focused_ = false;
};

// Opens the prompt once per invite, whenever no other dialog is up.
class InviteWatcher final : public rex::ui::ImGuiDialog {
 public:
  explicit InviteWatcher(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO&) override {
    const double now = ImGui::GetTime();
    if (now < next_check_) return;
    next_check_ = now + kInviteCheckSeconds;
    if (ModalDialog::AnyOpen()) return;
    for (online::Invite& invite : Online().Invites()) {
      if (g_prompted.count(invite.id)) continue;
      g_post([invite = std::move(invite)]() mutable {
        if (ModalDialog::AnyOpen() || !g_prompted.insert(invite.id).second) return;
        new InvitePrompt(g_drawer, std::move(invite));
      });
      return;
    }
  }

 private:
  double next_check_ = 0;
};

}  // namespace

void CreateInviteUi(rex::ui::ImGuiDrawer* drawer, std::function<void(std::function<void()>)> post) {
  g_drawer = drawer;
  g_post = std::move(post);
  new InviteWatcher(drawer);  // retained by the drawer for the session
  SetInvitePickerOpener([] {
    g_post([] {
      if (!ModalDialog::AnyOpen()) new InvitePicker(g_drawer);
    });
  });
}

}  // namespace ssf2t
