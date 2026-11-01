#include "updater/update_ui.h"

#include <algorithm>
#include <cfloat>
#include <string>

#include <imgui.h>

#include "localization/ui_text.h"
#include "trg/platform.h"

namespace trg::update {

using ssf2t::Tr;

namespace {

// The update errors are built as English text by code shared with the update helper and its tests, so they are
// translated here, where they are shown: whole sentences first, then the known prefixes and the one suffix.
std::string LocalizedMessage(const std::string& english) {
  const std::string whole = Tr(english);
  if (whole != english) {
    return whole;
  }
  static const char* const kPrefixes[] = {
      "The latest release does not include ",
      "The server answered ",
      "Cannot write the update file ",
      "Cannot write ",
      "Cannot read ",
      "Could not start the program (",
      "The release response is missing ",
      "The release response has an invalid ",
      "The release tag is not a semantic version: ",
      "The update archive contains an unsafe path (",
      "The update archive contains a protected path: ",
      "The update archive contains the same path twice: ",
      "The update archive contains a symbolic link: ",
      "The update does not contain ",
      "The update staging folder contains an unsafe path (",
      "The update staging folder contains a protected path: ",
      "The updater requires an invalid file: ",
      "The update is missing ",
  };
  static const char kRestoreFailed[] = " The previous AppImage could not be restored.";
  const std::string restore = kRestoreFailed;
  if (english.size() > restore.size() && english.compare(english.size() - restore.size(), restore.size(), restore) == 0) {
    return LocalizedMessage(english.substr(0, english.size() - restore.size())) + Tr(restore);
  }
  const char* best = nullptr;
  size_t best_length = 0;
  for (const char* prefix : kPrefixes) {
    const size_t length = std::char_traits<char>::length(prefix);
    if (length > best_length && english.compare(0, length, prefix) == 0) {
      best = prefix;
      best_length = length;
    }
  }
  if (!best) {
    return english;
  }
  std::string rest = english.substr(best_length);
  if (best[best_length - 1] == '(' && rest.size() > 2 && rest.compare(rest.size() - 2, 2, ").") == 0) {
    rest = Tr(rest.substr(0, rest.size() - 2)) + ").";
  }
  return Tr(std::string(best)) + rest;
}

bool Visible(const UpdateSnapshot& snapshot) {
  return snapshot.state == UpdateState::kAvailable || snapshot.state == UpdateState::kDownloading ||
         snapshot.state == UpdateState::kVerifying || snapshot.state == UpdateState::kReadyToRestart ||
         snapshot.state == UpdateState::kFailed ||
         (snapshot.state == UpdateState::kChecking && snapshot.manual_check);
}

void Text(const char* text, float size, ImFont* font, const ImVec4& color) {
  ImGui::PushFont(font, size);
  ImGui::PushStyleColor(ImGuiCol_Text, color);
  ImGui::TextUnformatted(text);
  ImGui::PopStyleColor();
  ImGui::PopFont();
}

void WrappedText(const std::string& text, float size, ImFont* font, const ImVec4& color, float width) {
  ImGui::PushFont(font, size);
  ImGui::PushStyleColor(ImGuiCol_Text, color);
  ImGui::PushTextWrapPos(width);
  ImGui::TextUnformatted(text.c_str());
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
  ImGui::PopFont();
}

bool AccentButton(const char* label, const trg::Theme& theme, const ImVec2& size) {
  ImGui::PushStyleColor(ImGuiCol_Button, theme.accent);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.accent_hot);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(theme.accent.x * 0.85f, theme.accent.y * 0.85f,
                                                      theme.accent.z * 0.85f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Text, theme.on_accent);
  const bool clicked = ImGui::Button(label, size);
  ImGui::PopStyleColor(4);
  return clicked;
}

}  // namespace

UpdateUiAction DrawUpdateBanner(const UpdateSnapshot& snapshot, const trg::Theme& theme, const trg::Fonts& fonts) {
  if (!Visible(snapshot)) {
    return UpdateUiAction::kNone;
  }

  const float base_size = fonts.size > 0.0f ? fonts.size : 18.0f;
  ImFont* base = fonts.regular ? fonts.regular : ImGui::GetFont();
  ImGui::PushFont(base, base_size);
  const float s = std::max(0.75f, ImGui::GetFontSize() / base_size);
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const float header = std::clamp(viewport->Size.y * 0.2f, 120.0f * s, 190.0f * s);
  const float width = std::min(std::max(viewport->Size.x * 0.42f, 260.0f * s), 400.0f * s);
  const float padding = 14.0f * s;
  const ImVec2 position(viewport->Pos.x + viewport->Size.x - width - 22.0f * s,
                        viewport->Pos.y + std::max(8.0f * s, (header - 104.0f * s) * 0.5f));

  ImGui::SetNextWindowPos(position, ImGuiCond_Always);
  ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, FLT_MAX));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, 8.0f * s));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f * s);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f * s);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f * s);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f * s, 2.0f * s));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(theme.panel.x, theme.panel.y, theme.panel.z, 0.96f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(theme.accent.x, theme.accent.y, theme.accent.z, 0.55f));
  ImGui::PushStyleColor(ImGuiCol_Text, theme.text);
  ImGui::PushStyleColor(ImGuiCol_TextLink, theme.accent);
  ImGui::Begin("##trg_update_banner", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_AlwaysAutoResize |
                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNavFocus);

  ImFont* semibold = fonts.semibold ? fonts.semibold : base;
  ImFont* regular = fonts.regular ? fonts.regular : base;
  const float title_size = 16.0f * s;
  const float body_size = 13.0f * s;
  const float button_height = 28.0f * s;
  const float wrap_width = width - padding * 2.0f;
  UpdateUiAction action = UpdateUiAction::kNone;
  std::string title;
  std::string detail;

  switch (snapshot.state) {
    case UpdateState::kChecking:
      title = Tr("Checking for updates");
      break;
    case UpdateState::kAvailable:
      title = Tr("Update available");
      break;
    case UpdateState::kDownloading:
      title = Tr("Downloading update");
      break;
    case UpdateState::kVerifying:
      title = Tr("Verifying update");
      break;
    case UpdateState::kReadyToRestart:
      title = Tr("Update ready");
      break;
    case UpdateState::kFailed:
      title = snapshot.retry_check ? Tr("Update check failed") : Tr("Update failed");
      break;
    default:
      break;
  }

  if (snapshot.state == UpdateState::kAvailable && !snapshot.release_url.empty()) {
    ImGui::PushFont(semibold, title_size);
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopFont();
    ImGui::PushFont(regular, 11.5f * s);
    const std::string release_notes = Tr("Release notes");
    const float link_width = ImGui::CalcTextSize(release_notes.c_str()).x;
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(padding, width - padding - link_width));
    if (ImGui::TextLink(release_notes.c_str())) {
      trg::OpenUrl(snapshot.release_url);
    }
    ImGui::PopFont();
  } else {
    Text(title.c_str(), title_size, semibold, theme.text);
  }

  if (snapshot.state == UpdateState::kChecking) {
    WrappedText(Tr("Contacting GitHub..."), body_size, regular, theme.dim, wrap_width);
  } else if (snapshot.state == UpdateState::kAvailable) {
    detail = Tr("Version ") + snapshot.latest_version + Tr(" is available");
    WrappedText(detail, body_size, regular, theme.dim, wrap_width);
    detail = Tr("You are using ") + snapshot.current_version;
    WrappedText(detail, body_size, regular, theme.dim, wrap_width);
    ImGui::Dummy(ImVec2(0.0f, 4.0f * s));
    if (snapshot.install_supported) {
      if (AccentButton(Tr("UPDATE NOW"), theme, ImVec2(-FLT_MIN, button_height))) {
        action = UpdateUiAction::kDownload;
      }
    } else {
      WrappedText(snapshot.install_unsupported_reason.empty()
                      ? std::string(Tr("Automatic installation is not available."))
                      : LocalizedMessage(snapshot.install_unsupported_reason),
                  body_size, regular, theme.dim, wrap_width);
    }
  } else if (snapshot.state == UpdateState::kDownloading) {
    detail = snapshot.progress >= 0.0f
                 ? Tr("Downloading update ") +
                       std::to_string(int(std::clamp(snapshot.progress, 0.0f, 1.0f) * 100.0f + 0.5f)) + "%"
                 : std::string(Tr("Downloading update"));
    WrappedText(detail, body_size, regular, theme.dim, wrap_width);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, theme.accent);
    ImGui::ProgressBar(std::clamp(snapshot.progress, 0.0f, 1.0f), ImVec2(-FLT_MIN, 6.0f * s), "");
    ImGui::PopStyleColor();
  } else if (snapshot.state == UpdateState::kVerifying) {
    WrappedText(Tr("Checking the download and preparing the files."), body_size, regular, theme.dim, wrap_width);
  } else if (snapshot.state == UpdateState::kReadyToRestart) {
    detail = Tr("Version ") + snapshot.latest_version + Tr(" is ready. The launcher will restart.");
    WrappedText(detail, body_size, regular, theme.dim, wrap_width);
    ImGui::Dummy(ImVec2(0.0f, 4.0f * s));
    if (AccentButton(Tr("RESTART TO UPDATE"), theme, ImVec2(-FLT_MIN, button_height))) {
      action = UpdateUiAction::kRestart;
    }
  } else if (snapshot.state == UpdateState::kFailed) {
    WrappedText(snapshot.retry_check ? std::string(Tr("Check your connection and try again."))
                                     : LocalizedMessage(snapshot.error),
                body_size, regular, theme.dim, wrap_width);
    if (snapshot.retry_available) {
      ImGui::Dummy(ImVec2(0.0f, 4.0f * s));
      if (AccentButton(Tr("RETRY"), theme, ImVec2(-FLT_MIN, button_height))) {
        action = UpdateUiAction::kRetry;
      }
    }
  }

  ImGui::End();
  ImGui::PopStyleColor(4);
  ImGui::PopStyleVar(5);
  ImGui::PopFont();
  return action;
}

}  // namespace trg::update
