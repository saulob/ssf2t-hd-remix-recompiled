#include "message_box_ui.h"

#include <cfloat>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <utility>

#include <imgui.h>

#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>

#include "launcher.h"
#include "message_box.h"
#include "modal_dialog.h"
#include "overlay_rules.h"

namespace ssf2t {
namespace {

constexpr float kMinWidth = 420.0f;
constexpr float kMaxWidth = 640.0f;
constexpr float kButtonHeightScale = 1.8f;
constexpr float kHintScale = 1.2f;
constexpr ImGuiWindowFlags kWindowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                          ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize;

struct Pending {
  MessageBoxRequest request;
  std::function<void(MessageBoxResult)> done;
};

rex::ui::ImGuiDrawer* g_drawer = nullptr;
std::function<void(std::function<void()>)> g_post;
std::mutex g_mutex;
std::deque<Pending> g_pending;
bool g_opening = false;  // UI thread only

bool ConfirmHeld() {
  return ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown) || ImGui::IsKeyDown(ImGuiKey_Enter) ||
         ImGui::IsKeyDown(ImGuiKey_KeypadEnter) || ImGui::IsKeyDown(ImGuiKey_Space);
}

class GuestMessageBox final : public ModalDialog {
 public:
  GuestMessageBox(rex::ui::ImGuiDrawer* drawer, Pending pending)
      : ModalDialog(drawer), request_(std::move(pending.request)), done_(std::move(pending.done)),
        selected_(request_.focused) {}

  ~GuestMessageBox() override {
    if (done_) done_(result_.value_or(CancelledBox()));
  }

 protected:
  void DrawContent(ImGuiIO& io) override {
    const auto& style = imgui_drawer()->style().achievements;
    const float width = FitWindowExtent(io.DisplaySize.x, kMinWidth, kMaxWidth);
    ImGui::PushFont(LauncherRegularFont());
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, FLT_MAX));
    ImGui::SetNextWindowBgAlpha(0.94f);
    if (!focused_) ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, style.window_padding);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, style.item_spacing);
    if (ImGui::Begin("##guest_message_box", nullptr, kWindowFlags)) {
      // A press still held from the game's menu must not choose here.
      if (!armed_ && !ConfirmHeld()) armed_ = true;
      if (!request_.title.empty()) {
        ImGui::TextColored(style.header_text, "%s", request_.title.c_str());
        ImGui::Separator();
      }
      ImGui::Spacing();
      ImGui::TextWrapped("%s", request_.text.c_str());
      ImGui::Spacing();
      ImGui::Spacing();
      DrawButtons(style.progress_bar);
      ImGui::Spacing();
      ImGui::PushFont(nullptr, ImGui::GetFontSize() * kHintScale);
      ImGui::TextColored(style.header_text, "A  Select     B  Cancel");
      ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopFont();
    focused_ = true;
  }

 private:
  void DrawButtons(const ImVec4& accent) {
    const float height = ImGui::GetFontSize() * kButtonHeightScale;
    for (uint32_t i = 0; i < request_.buttons.size(); ++i) {
      ImGui::PushID(int(i));
      const bool selected = i == selected_;
      if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(accent.x, accent.y, accent.z, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent);
      }
      if (!focused_ && i == request_.focused) ImGui::SetKeyboardFocusHere();
      const bool pressed = ImGui::Button(request_.buttons[i].c_str(), ImVec2(-FLT_MIN, height));
      if (selected) ImGui::PopStyleColor(3);
      if (ImGui::IsItemFocused()) selected_ = i;
      if (pressed && armed_ && !result_) {
        result_ = ChosenButton(i);
        RequestClose();
      }
      ImGui::PopID();
    }
  }

  MessageBoxRequest request_;
  std::function<void(MessageBoxResult)> done_;
  std::optional<MessageBoxResult> result_;
  uint32_t selected_ = 0;
  bool focused_ = false;
  bool armed_ = false;
};

void OpenNext() {
  g_opening = false;
  if (ModalDialog::AnyOpen()) return;
  Pending next;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_pending.empty()) return;
    next = std::move(g_pending.front());
    g_pending.pop_front();
  }
  new GuestMessageBox(g_drawer, std::move(next));
}

// Opens a waiting box once no other overlay is up; a box that never opens is answered as cancelled.
class MessageBoxWatcher final : public rex::ui::ImGuiDialog {
 public:
  explicit MessageBoxWatcher(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

  ~MessageBoxWatcher() override {
    std::deque<Pending> left;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      left.swap(g_pending);
    }
    for (Pending& pending : left) pending.done(CancelledBox());
  }

 protected:
  void OnDraw(ImGuiIO&) override {
    if (g_opening || ModalDialog::AnyOpen()) return;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (g_pending.empty()) return;
    }
    g_opening = true;
    g_post(OpenNext);
  }
};

}  // namespace

void CreateMessageBoxUi(rex::ui::ImGuiDrawer* drawer, std::function<void(std::function<void()>)> post) {
  g_drawer = drawer;
  g_post = std::move(post);
  new MessageBoxWatcher(drawer);  // retained by the drawer for the session
  SetMessageBoxOpener([](MessageBoxRequest request, std::function<void(MessageBoxResult)> done) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_pending.push_back({std::move(request), std::move(done)});
  });
}

}  // namespace ssf2t
