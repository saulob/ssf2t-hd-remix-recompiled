#include "online_page.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <string>

#include <imgui.h>

#include "character/akuma.h"
#include "character/character_packages.h"
#include "character/ultra_fighters.h"
#include "friend_code_field.h"
#include "infinite_time.h"
#include "localization/ui_text.h"
#include "online/friend_view.h"
#include "online/identity.h"
#include "online/protocol.h"
#include "online/secret_view.h"
#include "online_runtime.h"
#include "online_setting.h"

namespace ssf2t {
namespace {

constexpr char kFriendCodeHelp[] = "Share this code so another player can add you.";
constexpr char kTestHelp[] =
    "Checks that you can reach the online service and that other players can reach you. Takes a few seconds.";
constexpr char kUnreachable[] = "Cannot reach the online service right now. Run the Connection test for details.";
constexpr char kNoFriends[] = "No friends yet. Add someone with their friend code.";
constexpr char kInfiniteTimeOff[] = "Infinite Time was turned off because Online play is on.";
constexpr char kSecretHelp[] =
    "Akuma, Evil Ryu and Violent Ken are in an Online Match only when both players have them on. Turn them on in "
    "Gameplay.";
constexpr char kLobbyHeading[] = "Players in the Lobby";
constexpr char kLobbyRefreshFailed[] = "Could not refresh the Lobby.";

int OnlineNameFilter(ImGuiInputTextCallbackData* data) {
  return data->EventChar < 0x80 && online::OnlineNameChar(char(data->EventChar)) ? 0 : 1;
}

// Typing and pasting keep only what the service accepts, so the field never shows a name other players will not see.
void OnlineNameRow(trg::Ui& ui) {
  ui.Row(kOnlineNameLabel, kOnlineNameHelp);
  const std::string saved = ui.settings().Get(kOnlineNameSetting);
  char text[online::kMaxOnlineNameChars + 1] = {};
  saved.copy(text, online::kMaxOnlineNameChars);
  ImGui::PushID(kOnlineNameSetting);
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (ImGui::InputTextWithHint("##text", kDefaultOnlineNameValue, text, sizeof text,
                               ImGuiInputTextFlags_CallbackCharFilter, OnlineNameFilter)) {
    ui.settings().Set(kOnlineNameSetting, text);
  }
  ImGui::PopID();
  const std::string shown = online::CleanOnlineName(saved);
  if (!saved.empty() && shown != saved) ui.Help((Tr("Other players see ") + shown + ".").c_str());
}

// Why the service itself cannot be used right now; empty once it is online.
std::string ServiceNotice(const online::Snapshot& state) {
  if (state.connection == online::Connection::kOffline || state.connection == online::Connection::kError) {
    return state.connection_text.empty() ? Tr(kUnreachable) : state.connection_text;
  }
  if (state.connection == online::Connection::kChecking) {
    return state.connection_text.empty() ? Tr("Connecting...") : state.connection_text;
  }
  return {};
}

bool AcceptButton(trg::Ui& ui, const char* id) {
  const trg::Theme& t = ui.theme();
  ImGui::PushStyleColor(ImGuiCol_Button, t.accent);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.accent_hot);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.accent);
  ImGui::PushStyleColor(ImGuiCol_Text, t.on_accent);
  const float mark = ImGui::GetFontSize() * 0.8f;
  const ImVec2 at = ImGui::GetCursorScreenPos();
  const bool clicked = ImGui::Button(id);
  const ImVec2 pad = ImGui::GetStyle().FramePadding;
  const ImVec2 origin(at.x + pad.x, at.y + pad.y + ImGui::GetFontSize() * 0.1f);
  const ImVec2 tick[3] = {ImVec2(origin.x + mark * 0.1f, origin.y + mark * 0.55f),
                          ImVec2(origin.x + mark * 0.4f, origin.y + mark * 0.85f),
                          ImVec2(origin.x + mark * 0.95f, origin.y + mark * 0.2f)};
  ImGui::GetWindowDrawList()->AddPolyline(tick, 3, ImGui::GetColorU32(t.on_accent), ImDrawFlags_None, mark * 0.18f);
  ImGui::PopStyleColor(4);
  return clicked;
}

// The name, then the status line in the row's help style under it. ui.Row has already moved to the control column, so
// the line is drawn back in the label column.
void FriendHeading(trg::Ui& ui, const std::string& name, const online::FriendRowView& view) {
  ui.Row(name.c_str());
  const bool in_table = ImGui::TableGetColumnCount() > 0;
  if (in_table) {
    const float below_name = ImGui::GetItemRectMax().y + ImGui::GetStyle().ItemSpacing.y;
    ImGui::TableSetColumnIndex(0);
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, below_name));
  }
  ImGui::PushFont(nullptr, ui.fonts().size * 0.84f);
  ImGui::TextColored(ui.theme().dim, "%s", view.status.c_str());
  if (view.incompatible) {
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextColored(ui.theme().dim, " \xC2\xB7 ");
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextColored(ui.theme().warn_dot, "%s", Tr("Game versions do not match"));
  }
  ImGui::PopFont();
  if (in_table) {
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
  }
}

}  // namespace

std::vector<std::string> OnlinePage::Tick(trg::SettingsStore& settings) {
  online::UserSettings edited;
  edited.enabled = settings.GetBool(kOnlineEnabledSetting, kDefaultOnlineEnabled);
  edited.name = settings.Get(kOnlineNameSetting, kDefaultOnlineNameValue);
  edited.lobby_visible = settings.GetBool(kOnlineLobbySetting, kDefaultOnlineLobby);
  tabs_.TrackOnlinePlay(edited.enabled);
  std::vector<std::string> notices;
  if (edited.enabled && settings.GetBool(kInfiniteTimeSetting, kDefaultInfiniteTime)) {
    settings.Set(kInfiniteTimeSetting, "false");
    notices.push_back(Tr(kInfiniteTimeOff));
  }
  TickOnline(edited);
  for (std::string& notice : Online().TakeNotices()) notices.push_back(std::move(notice));
  ForgetConnectionTestWhenOff(settings);
  const int finished = Online().snapshot().tests_finished;
  if (finished != recorded_tests_) {
    recorded_tests_ = finished;
    if (RecordConnectionTest(settings, save_now_)) {
      Online().LogLine("online connection test completion saved " + OnlineUdpConfiguration());
    }
  }
  return notices;
}

bool OnlinePage::PlayNeedsTest(const trg::SettingsStore& settings) const {
  return ConnectionTestNeeded(settings, Online().snapshot().testing);
}

bool OnlinePage::NeedsAttention() const {
  const online::Snapshot state = Online().snapshot();
  return !state.incoming.empty();
}

void OnlinePage::Draw(trg::Ui& ui) {
  const bool on = ui.settings().GetBool(kOnlineEnabledSetting, kDefaultOnlineEnabled);
  const online::Snapshot state = Online().snapshot();
  for (const OnlineSection section : OnlinePageSections(on, tabs_.selected(), state.incoming.size())) {
    switch (section) {
      case OnlineSection::kOnlinePlay:
        ui.Toggle(kOnlineEnabledLabel, kOnlineEnabledHelp, kOnlineEnabledSetting, kDefaultOnlineEnabled, "Off", "On");
        break;
      case OnlineSection::kTabs: DrawTabs(ui, state.incoming.size()); break;
      case OnlineSection::kFriendRequests: DrawFriendRequests(ui, state); break;
      case OnlineSection::kOnlineName: OnlineNameRow(ui); break;
      case OnlineSection::kFriendCode: DrawFriendCode(ui, state); break;
      case OnlineSection::kConnectionTest: DrawConnectionTest(ui, state); break;
      case OnlineSection::kPingDisplay:
        ui.Toggle(kOnlinePingLabel, kOnlinePingHelp, kOnlinePingSetting, kDefaultOnlinePing, "Off", "On");
        break;
      case OnlineSection::kSecretCharacters: DrawSecretCharacters(ui, state); break;
      case OnlineSection::kFriends: DrawFriends(ui, state); break;
      case OnlineSection::kLobby: DrawLobby(ui, state); break;
    }
  }
  ui.EndRows();
}

// Plain buttons, so keyboard navigation and its highlight work as anywhere else on the page.
void OnlinePage::DrawTabs(trg::Ui& ui, size_t friend_requests) {
  ui.Spacer(4);
  const trg::Theme& t = ui.theme();
  const float s = ui.scale();
  const float left = ImGui::GetCursorScreenPos().x;
  const float width = ImGui::GetContentRegionAvail().x;
  float bottom = 0.0f;
  float selected_left = 0.0f;
  float selected_right = 0.0f;
  ImGui::PushFont(ui.fonts().semibold, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f * s, 7.0f * s));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.frame_hot);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.frame_active);
  for (const OnlineTab tab : kOnlineTabs) {
    const bool selected = tab == tabs_.selected();
    if (tab != kOnlineTabs[0]) ImGui::SameLine(0.0f, 0.0f);
    ImGui::PushID(int(tab));
    ImGui::PushStyleColor(ImGuiCol_Text, selected ? t.accent : t.dim);
    if (ImGui::Button(OnlineTabTitle(Tr(OnlineTabLabel(tab)), tab, friend_requests).c_str())) tabs_.Select(tab);
    ImGui::PopStyleColor();
    ImGui::PopID();
    bottom = ImGui::GetItemRectMax().y;
    if (selected) {
      selected_left = ImGui::GetItemRectMin().x;
      selected_right = ImGui::GetItemRectMax().x;
    }
  }
  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar(3);
  ImGui::PopFont();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const float base = std::floor(bottom);
  draw->AddRectFilled(ImVec2(left, base - 1.0f), ImVec2(left + width, base),
                      ImGui::GetColorU32(ImGuiCol_TableBorderLight));
  const float thick = std::max(2.0f, std::round(2.0f * s));
  draw->AddRectFilled(ImVec2(selected_left, base - thick), ImVec2(selected_right, base), trg::Col(t.accent));
  ui.Spacer(4);
}

void OnlinePage::DrawFriendRequests(trg::Ui& ui, const online::Snapshot& state) {
  const trg::Theme& t = ui.theme();
  const float s = ImGui::GetFontSize() / 18.0f;
  ui.Spacer(4);
  for (const online::Request& sender : state.incoming) {
    ImGui::PushID(("request" + sender.player).c_str());
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f * s, 10.0f * s));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(t.panel.x, t.panel.y, t.panel.z, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.55f));
    ImGui::BeginChild("##card", ImVec2(-FLT_MIN, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
    ImGui::PushFont(ui.fonts().semibold, 0.0f);
    ImGui::TextUnformatted(Tr("New friend request"));
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Text, t.dim);
    ImGui::TextUnformatted((sender.name + Tr(" wants to be your friend")).c_str());
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 2.0f * s));
    if (AcceptButton(ui, Tr("     Accept"))) Online().AcceptFriend(sender.player);
    ImGui::SameLine();
    if (ImGui::Button(Tr("Decline"))) Online().DeclineFriend(sender.player);
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    ImGui::PopID();
    ui.Spacer(4);
  }
}

void OnlinePage::NoteSendState(bool enabled) {
  if (send_enabled_ == int(enabled)) return;
  send_enabled_ = int(enabled);
  std::string reason;
  if (!enabled) {
    size_t letters = 0;
    for (const char* c = add_code_; *c; ++c) letters += std::isalnum(static_cast<unsigned char>(*c)) ? 1 : 0;
    reason = add_code_[0] == 0 ? " reason=empty" : letters < 8 ? " reason=incomplete" : " reason=invalid";
  }
  Online().LogLine(std::string("online friend send enabled=") + (enabled ? "1" : "0") + reason +
                   " value=" + add_code_);
}

void OnlinePage::DrawFriendCode(trg::Ui& ui, const online::Snapshot& state) {
  ui.Row("Your friend code", kFriendCodeHelp);
  if (state.friend_code.empty()) {
    ImGui::TextDisabled("%s", state.connection == online::Connection::kOffline ? Tr("Not available right now") : Tr("Connecting..."));
    return;
  }
  ImGui::TextUnformatted(state.friend_code.c_str());
  ImGui::SameLine();
  if (ImGui::SmallButton(Tr("Copy"))) {
    const auto write = [this](const std::string& text, const FieldLog& log) {
      return WriteClipboardText(owner_window_, text, log);
    };
    const bool copied = CopyFriendCode(state.friend_code, write, [](const std::string& line) { Online().LogLine(line); });
    ui.SetStatus(copied ? "Friend code copied." : "Could not copy the friend code.");
  }
}

void OnlinePage::DrawConnectionTest(trg::Ui& ui, const online::Snapshot& state) {
  const bool online_on = ui.settings().GetBool(kOnlineEnabledSetting, kDefaultOnlineEnabled);
  const ConnectionTestView view = DescribeConnectionTestView(
      online_on, ConnectionTestNeeded(ui.settings(), state.testing), state.testing, kOnWindows);
  const ConnectionTestAttention::Step step = attention_.Frame(ImGui::GetTime(), view.highlighted);
  if (!view.highlighted) {
    DrawConnectionTestBody(ui, state, view, false);
    return;
  }
  // The friend request card's look, in the accent colour; the border brightens while the attention step runs.
  const trg::Theme& t = ui.theme();
  const float s = ImGui::GetFontSize() / 18.0f;
  const float glow = step.strength;
  ui.EndRows();
  ui.Spacer(4);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f * s);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, (2.0f + 2.0f * glow) * s);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f * s, 10.0f * s));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(t.panel.x + (t.accent.x - t.panel.x) * (0.05f + 0.10f * glow),
                                                 t.panel.y + (t.accent.y - t.panel.y) * (0.05f + 0.10f * glow),
                                                 t.panel.z + (t.accent.z - t.panel.z) * (0.05f + 0.10f * glow), 0.96f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.8f + 0.2f * glow));
  ImGui::BeginChild("##connection_test_required", ImVec2(-FLT_MIN, 0),
                    ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY |
                        ImGuiChildFlags_NavFlattened);
  DrawConnectionTestBody(ui, state, view, step.focus_run_test);
  ui.EndRows();
  ImGui::EndChild();
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(3);
  ui.Spacer(4);
  if (step.scroll_into_view) {
    const float visible_top = ImGui::GetWindowPos().y;
    const float visible_bottom = visible_top + ImGui::GetWindowHeight();
    const float margin = 8.0f * s;
    const float above = visible_top + margin - ImGui::GetItemRectMin().y;
    const float below = ImGui::GetItemRectMax().y + margin - visible_bottom;
    if (above > 0.0f) ImGui::SetScrollY(ImGui::GetScrollY() - above);
    else if (below > 0.0f) ImGui::SetScrollY(ImGui::GetScrollY() + below);
  }
}

void OnlinePage::DrawConnectionTestBody(trg::Ui& ui, const online::Snapshot& state, const ConnectionTestView& view,
                                        bool focus_run_test) {
  if (focus_run_test) ImGui::SetKeyboardFocusHere();
  if (ui.ButtonRow("Connection test", kTestHelp, state.testing ? "Testing..." : "Run test", view.primary_button) &&
      !state.testing) {
    Online().TestConnection();
  }
  if (view.required_text) {
    ui.EndRows();
    ImGui::PushFont(ui.fonts().semibold, 0.0f);
    ImGui::TextColored(ui.theme().accent, "%s", Tr(kConnectionTestRequiredLabel));
    ImGui::PopFont();
  }
  if (view.windows_note) {
    if (const char* note = ConnectionTestRunningNote(true)) ui.Help(note);
  }
  if (!state.test || state.testing) return;
  ui.EndRows();
  ui.Heading(state.test->verdict.c_str());
  for (const online::ConnectionReport::Line& line : state.test->lines) {
    using Mark = online::ConnectionReport::Mark;
    const ImVec4 color = line.mark == Mark::kOk ? ui.theme().good_dot
                         : line.mark == Mark::kNote ? ui.theme().dim : ui.theme().warn_dot;
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(line.mark == Mark::kOk ? "OK" : line.mark == Mark::kNote ? "--" : "!!");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextWrapped("%s: %s", Tr(line.label.c_str()), line.text.c_str());
  }
  ui.Spacer();
}

void OnlinePage::DrawSecretCharacters(trg::Ui& ui, const online::Snapshot& state) {
  online::SecretCapabilities edited;
  const bool ultra =
      ui.settings().GetBool(character::kEnableUltraFightersSetting, character::kDefaultEnableUltraFighters);
  edited.supports = {true, true, true};
  edited.enabled = {ui.settings().GetBool(character::kEnableAkumaSetting, character::kDefaultEnableAkuma),
                    ultra && character::EvilRyuPackagesReady(), ultra && character::ViolentKenPackagesReady()};
  ui.Heading("Secret characters");
  for (const online::SecretRowView& row : online::DescribeSecretCharacters(state, edited)) {
    ui.Row(row.label.c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, row.available ? ui.theme().good_dot : ui.theme().dim);
    ImGui::TextUnformatted(row.status.c_str());
    ImGui::PopStyleColor();
  }
  ui.EndRows();
  ui.Help(kSecretHelp);
}

void OnlinePage::DrawFriends(trg::Ui& ui, const online::Snapshot& state) {
  if (state.connection == online::Connection::kOffline || state.connection == online::Connection::kError) {
    ui.Help(state.connection_text.empty() ? kUnreachable : state.connection_text.c_str());
  } else if (state.connection == online::Connection::kChecking) {
    ui.Help(state.connection_text.empty() ? "Connecting..." : state.connection_text.c_str());
  } else if (!state.notice.empty()) {
    ui.Help(state.notice.c_str());
  }
  ui.Row("Add a friend", "Their friend code, for example ABCD-EFGH.");
  add_field_.Draw(add_code_, sizeof(add_code_), [](const std::string& line) { Online().LogLine(line); });
  const std::string code = online::NormalizeFriendCode(add_code_);
  NoteSendState(!code.empty());
  ImGui::SameLine();
  ImGui::BeginDisabled(code.empty());
  const bool clicked = ImGui::Button(Tr("Send request"));
  ImGui::EndDisabled();
  if (clicked) Online().LogLine("online friend send clicked code=" + code);
  if (clicked && !code.empty() && Online().AddFriend(code) == online::FriendRequestResult::kSent) add_code_[0] = 0;
  if (code.empty() && std::strlen(add_code_) >= 8) ImGui::TextDisabled("%s", Tr("That is not a valid friend code."));
  for (const online::Request& r : state.outgoing) {
    ui.Row(r.name.c_str(), "Your request is waiting for an answer.");
    ImGui::PushID(("out" + r.player).c_str());
    if (ImGui::Button(Tr("Cancel request"))) Online().DeclineFriend(r.player);
    ImGui::PopID();
  }
  for (const online::Friend& f : state.friends) {
    const online::FriendRowView view = online::DescribeFriend(state, f);
    FriendHeading(ui, f.name, view);
    ImGui::PushID(("f" + f.player).c_str());
    if (ImGui::Button(Tr("Remove"))) Online().RemoveFriend(f.player);
    ImGui::PopID();
  }
  if (state.fetched && state.friends.empty() && state.incoming.empty() && state.outgoing.empty()) {
    ui.EndRows();
    ui.Help(kNoFriends);
  }
  ui.EndRows();
}

void OnlinePage::DrawLobby(trg::Ui& ui, const online::Snapshot& state) {
  Online().NoteLobbyViewed();
  const bool visible = ui.settings().GetBool(kOnlineLobbySetting, kDefaultOnlineLobby);
  ui.Toggle(kOnlineLobbyLabel, visible ? kOnlineLobbyOnHelp : kOnlineLobbyOffHelp, kOnlineLobbySetting,
            kDefaultOnlineLobby, "Off", "On");
  ui.Heading(kLobbyHeading);
  std::string notice = ServiceNotice(state);
  if (notice.empty()) notice = online::LobbyMessage(state.lobby);
  if (!notice.empty()) {
    ui.Help(notice.c_str());
    return;
  }
  if (state.lobby.failed) ui.Help(kLobbyRefreshFailed);
  for (const online::LobbyPlayer& player : state.lobby.players) DrawLobbyPlayer(ui, state, player);
  ui.EndRows();
}

void OnlinePage::DrawLobbyPlayer(trg::Ui& ui, const online::Snapshot& state, const online::LobbyPlayer& player) {
  const online::LobbyRowView view = online::DescribeLobbyPlayer(state, player);
  FriendHeading(ui, player.name, online::FriendRowView{view.status, view.incompatible});
  ImGui::PushID(("lobby" + player.player).c_str());
  switch (view.relation) {
    case online::LobbyRelation::kFriend: ImGui::TextDisabled("%s", Tr("Friend")); break;
    case online::LobbyRelation::kOutgoing: ImGui::TextDisabled("%s", Tr("Request sent")); break;
    case online::LobbyRelation::kIncoming:
      if (AcceptButton(ui, Tr("     Accept"))) Online().AcceptFriend(player.player);
      ImGui::SameLine();
      if (ImGui::Button(Tr("Decline"))) Online().DeclineFriend(player.player);
      break;
    case online::LobbyRelation::kNone:
      ImGui::BeginDisabled(!view.can_add);
      if (ImGui::Button(Tr("Add friend"))) Online().AddFriend(player.player);
      ImGui::EndDisabled();
      break;
  }
  ImGui::PopID();
}

}  // namespace ssf2t
