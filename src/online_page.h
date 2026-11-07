#pragma once

#include <string>
#include <utility>
#include <vector>

#include "connection_test_gate.h"
#include "friend_code_field.h"
#include "clipboard.h"
#include "online/online_service.h"
#include "online_tabs.h"
#include "trg/launcher.h"
#include "trg/settings.h"

namespace ssf2t {

// The launcher's Online page. Its service keeps running on every launcher frame, not only while the page is open.
class OnlinePage {
 public:
  // Applies the edited settings and returns what happened since the last frame, for the launcher's status line.
  std::vector<std::string> Tick(trg::SettingsStore& settings);
  // Online play is on and no Connection test has finished for its UDP configuration, or one is running.
  bool PlayNeedsTest(const trg::SettingsStore& settings) const;
  // Play was refused for the missing Connection test: Settings opens with its block focused and briefly emphasized.
  void RequestConnectionTestAttention() {
    tabs_.Select(OnlineTab::kSettings);
    attention_.Request();
  }
  // Friend requests are waiting: the sidebar's attention dot.
  bool NeedsAttention() const;
  void Draw(trg::Ui& ui);
  // The launcher window, which owns what Copy puts on the clipboard.
  void SetOwnerWindow(void* window) { owner_window_ = window; }
  // Writes a finished Connection test to the player file at once.
  void SetSaveNow(SaveSettingNow save_now) { save_now_ = std::move(save_now); }

 private:
  void DrawTabs(trg::Ui& ui, size_t friend_requests);
  void DrawFriendRequests(trg::Ui& ui, const online::Snapshot& state);
  void DrawFriendCode(trg::Ui& ui, const online::Snapshot& state);
  void DrawConnectionTest(trg::Ui& ui, const online::Snapshot& state);
  void DrawConnectionTestBody(trg::Ui& ui, const online::Snapshot& state, const ConnectionTestView& view,
                              bool focus_run_test);
  void DrawSecretCharacters(trg::Ui& ui, const online::Snapshot& state);
  void DrawFriends(trg::Ui& ui, const online::Snapshot& state);
  void DrawLobby(trg::Ui& ui, const online::Snapshot& state);
  void DrawLobbyPlayer(trg::Ui& ui, const online::Snapshot& state, const online::LobbyPlayer& player);
  void NoteSendState(bool enabled);

  void* owner_window_ = nullptr;
  SaveSettingNow save_now_;
  OnlineTabSelection tabs_;
  ConnectionTestAttention attention_;
  int recorded_tests_ = 0;
  char add_code_[kFriendCodeFieldSize] = {};
  FriendCodeField add_field_{[this](const FieldLog& log) { return ReadClipboardText(owner_window_, log); }};
  int send_enabled_ = -1;
};

}  // namespace ssf2t
