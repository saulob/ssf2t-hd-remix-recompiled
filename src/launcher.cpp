#include "launcher.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/ui/image_decode.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/version.h>

#include "trg/launcher.h"
#include "trg/platform.h"

#include "aspect_ratio.h"
#include "dpi.h"
#include "game_files.h"
#include "graphics_adapters.h"
#include "imgui_gamepad.h"
#include "internal_render_scale.h"
#include "keybinds.h"
#include "language_setting.h"
#include "launcher_achievements.h"
#include "log_filter.h"
#include "launcher_assets.h"
#include "launcher_quit.h"
#include "platform.h"
#include "presentation_setting.h"
#include "settings.h"
#include "update_config.h"
#include "updater/update_ui.h"

namespace ssf2t {
namespace {

// Logical pixels.
constexpr std::pair<int, int> kLauncherWindowSize = {1280, 720};
// Share of the primary display's work area the launcher may take.
constexpr double kLauncherMaxShare = 0.9;
// How long the saved message stays on screen before the game starts.
constexpr double kPlayFeedbackSeconds = 0.4;

constexpr ImGuiConfigFlags kNavFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;

constexpr char kAuthorUrl[] = "https://github.com/saulob";
constexpr char kRepositoryUrl[] = "https://github.com/saulob/ssf2t-hd-remix-recompiled";

// Street Fighter orange over a dark ember background.
const ImVec4 kAccent = ImVec4(1.00f, 0.52f, 0.08f, 1.0f);
const ImVec4 kEmber = ImVec4(0.42f, 0.18f, 0.04f, 1.0f);

trg::Theme LauncherTheme() {
  trg::Theme theme = trg::Theme::Midnight().WithAccent(kAccent);
  theme.good = kEmber;
  theme.good_dot = kAccent;
  return theme;
}

constexpr float kHeaderArtAspect = 1600.0f / 180.0f;

trg::HeaderPainter LauncherHeader(ImTextureID art, float art_aspect) {
  return [art, art_aspect](const trg::HeaderContext& c) {
    c.draw->AddRectFilledMultiColor(c.min, c.max, IM_COL32(24, 14, 10, 255), IM_COL32(96, 40, 8, 255),
                                    IM_COL32(44, 20, 8, 255), IM_COL32(10, 8, 12, 255));
    if (art == ImTextureID{}) {
      return;
    }
    const float height = c.max.y - c.min.y;
    const float width = height * art_aspect;
    const ImVec2 top_left(c.max.x - width, c.min.y);
    c.draw->AddImage(ImTextureRef(art), top_left, c.max);
  };
}

std::unique_ptr<rex::ui::ImmediateTexture> LoadHeaderArt(rex::ui::ImmediateDrawer* drawer) {
  const std::filesystem::path path = LauncherHeaderArtPath(rex::filesystem::GetExecutableFolder());
  if (!drawer) {
    REXLOG_WARN("Launcher header art unavailable without an immediate drawer: {}", Utf8(path.filename()));
    return nullptr;
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    REXLOG_WARN("Launcher header art not found: {}", Utf8(path.filename()));
    return nullptr;
  }
  const std::vector<uint8_t> encoded((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  int width = 0;
  int height = 0;
  const std::vector<uint8_t> pixels = rex::ui::DecodeImageRGBA(encoded.data(), encoded.size(), width, height);
  if (pixels.empty() || width <= 0 || height <= 0) {
    REXLOG_WARN("Launcher header art could not be decoded: {}", Utf8(path.filename()));
    return nullptr;
  }
  return drawer->CreateTexture(uint32_t(width), uint32_t(height), rex::ui::ImmediateTextureFilter::kLinear, false,
                               pixels.data());
}

trg::Fonts g_fonts;
float g_font_density = 1.0f;

GameFiles CheckAndLogGameFiles(const std::filesystem::path& game_dir) {
  const GameCheck check = CheckGameFiles(game_dir);
  if (check.state == GameFiles::kUnsupported && check.xex_crc32 != 0) {
    REXLOG_WARN("default.xex is not the supported version (CRC32 {:08X})", check.xex_crc32);
  }
  return check.state;
}

// Written by the install job, read once the Task has finished.
struct InstallOutcome {
  InstallResult result;
  GameFiles files = GameFiles::kMissing;
};

// The theme's only alert colours; destructive actions use them instead of the accent.
void PushDangerButton(const trg::Theme& t) {
  ImGui::PushStyleColor(ImGuiCol_Button, t.warn);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(t.warn.x + 0.12f, t.warn.y + 0.10f, t.warn.z + 0.05f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(t.warn.x - 0.05f, t.warn.y - 0.04f, t.warn.z - 0.02f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Text, t.text);
}

std::string Size(int width, int height) {
  return std::to_string(width) + " \xC3\x97 " + std::to_string(height);
}

std::vector<trg::Option> RenderResolutionOptions(AspectRatio aspect) {
  std::vector<trg::Option> options;
  for (int scale = 1; scale <= 4; ++scale) {
    std::string label = scale == 1 ? "Original" : std::to_string(scale) + "x";
    label += " \xE2\x80\x94 " + std::to_string(GuestWidth(aspect) * scale) + "x" + std::to_string(kGuestHeight * scale);
    if (scale == 2) {
      label += " \xE2\x80\x94 Recommended for 1440p";
    }
    options.push_back({std::to_string(scale), label});
  }
  return options;
}

std::string DisplayUrl(const char* url) {
  const std::string text = url;
  const size_t scheme = text.find("://");
  return scheme == std::string::npos ? text : text.substr(scheme + 3);
}

std::vector<trg::Option> AspectRatioOptions() {
  std::vector<trg::Option> options;
  for (const AspectRatioInfo& info : kAspectRatios) {
    options.push_back({info.key, info.label});
  }
  return options;
}

class LauncherDialog final : public rex::ui::ImGuiDialog {
 public:
  LauncherDialog(rex::ui::ImGuiDrawer* drawer, LauncherPaths paths, GameSettings& settings,
                 LauncherCallbacks callbacks)
      : ImGuiDialog(drawer),
        paths_(std::move(paths)),
        cb_(std::move(callbacks)),
        settings_(settings),
        game_files_(CheckAndLogGameFiles(paths_.game_dir)),
        monitors_(ListMonitors()),
        graphics_adapters_(ListGraphicsAdapters()) {
    header_art_ = LoadHeaderArt(paths_.immediate_drawer);
    launcher_ = std::make_unique<trg::Launcher>(MakeConfig());
    updater_ = std::make_unique<trg::update::UpdateController>(MakeUpdateConfig(PortFolder()));
    updater_->StartCheck();
    if (WindowFullscreen()) {
      restore_when_windowed_ = true;
      RequestFullscreen(false);
    } else {
      StartInSavedState();
    }
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (done_) {
      return;
    }
    PollInstall();
    PollUpdate();
    TrackPlacement();
    if (!nav_taken_) {
      saved_nav_flags_ = io.ConfigFlags & kNavFlags;
      saved_has_gamepad_ = io.BackendFlags & ImGuiBackendFlags_HasGamepad;
      io.ConfigFlags |= kNavFlags;
      nav_taken_ = true;
    }
    gamepad_.Update(io);
    if (!starting_ && ImGui::IsKeyPressed(ImGuiKey_F11, false) && !fullscreen_gate_.Pending()) {
      ToggleLauncherFullscreen();
    }
    if (starting_ && ImGui::GetTime() - play_started_ >= kPlayFeedbackSeconds && !LeavingFullscreen()) {
      StartGame();
      return;
    }
    if (starting_) {
      // TRG's hotkeys read the same key state, so release it before Frame() runs.
      io.ClearInputKeys();
      io.ClearInputMouse();
    }
    ImGui::BeginDisabled(starting_);
    const trg::Result result = launcher_->Frame();
    ImGui::EndDisabled();
    if (result == trg::Result::kNone && !starting_) {
      HandleUpdateAction(
          trg::update::DrawUpdateBanner(updater_->snapshot(), launcher_->config().theme, g_fonts));
    }
    if (starting_) {
      DrawStartingMessage();
    }
    switch (result) {
      case trg::Result::kPlay:
        RequestPlay();
        break;
      case trg::Result::kQuit:
        if (!starting_ && LauncherQuitAllowed(ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
          Finish(cb_.quit);
        }
        break;
      case trg::Result::kNone:
        break;
    }
  }

  // Every PLAY (mouse, Enter, Xbox Start) ends here. The one save happens here; StartGame never saves.
  void RequestPlay() {
    if (starting_ || install_.running() || game_files_ != GameFiles::kReady) {
      return;
    }
    if (!launcher_->Save()) {
      return;
    }
    starting_ = true;
    play_started_ = ImGui::GetTime();
    launcher_->SetStatus("", 0.0);
  }

  void StartGame() {
    // The game builds its own runtime next; the one that read the achievements must be gone.
    achievements_.reset();
    if (cb_.window_changed && cb_.window_changed()) {
      if (cb_.restart && cb_.restart()) {
        Finish(nullptr);
        return;
      }
      starting_ = false;
      launcher_->SetStatus("Could not start the game in a new window. Press Play to try again.", 8);
      return;
    }
    RestoreStartupWindow();
    Finish(cb_.play);
  }

  // Drawn outside the disabled frame so the message keeps full brightness.
  void DrawStartingMessage() {
    constexpr const char* kText = "Settings saved. Starting game...";
    ImFont* font = g_fonts.semibold ? g_fonts.semibold : ImGui::GetFont();
    const float size = g_fonts.size * 1.25f;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 text_size = font->CalcTextSizeA(size, FLT_MAX, 0.0f, kText);
    const ImVec2 pos(vp->Pos.x + (vp->Size.x - text_size.x) * 0.5f,
                     vp->Pos.y + (vp->Size.y - text_size.y) * 0.5f);
    ImGui::GetForegroundDrawList()->AddText(font, size, pos, ImGui::GetColorU32(kAccent), kText);
  }

  // Runs inside the last frame; the in-game overlays share this ImGui context.
  void OnClose() override {
    ReleaseUpdater();
    ImGuiIO& io = GetIO();
    gamepad_.Release(io);
    io.ConfigFlags = (io.ConfigFlags & ~kNavFlags) | saved_nav_flags_;
    io.BackendFlags = (io.BackendFlags & ~ImGuiBackendFlags_HasGamepad) | saved_has_gamepad_;
  }

 private:
  void Finish(std::function<void()> action) {
    done_ = true;
    ReleaseUpdater();
    if (!skip_finish_save_) {
      SavePlacement(placement_.Flush());
    }
    REXLOG_INFO("Launcher closed");
    Close();
    if (action) {
      action();
    }
  }

  void ReleaseUpdater() {
    if (!updater_) {
      return;
    }
    updater_->Cancel();
    if (updater_->Busy()) {
      // Do not join a network call on the UI thread when the launcher leaves.
      updater_.release();
    }
  }

  void PollUpdate() {
    if (updater_) {
      updater_->Poll();
    }
  }

  bool InstallBusy() const { return install_.running(); }

  void HandleUpdateAction(trg::update::UpdateUiAction action) {
    switch (action) {
      case trg::update::UpdateUiAction::kDownload:
        updater_->StartDownload();
        break;
      case trg::update::UpdateUiAction::kRetry:
        if (updater_->RetryNeedsInstall()) {
          RestartToUpdate([this] { return updater_->Retry(); });
        } else {
          updater_->Retry();
        }
        break;
      case trg::update::UpdateUiAction::kRestart:
        RestartToUpdate([this] { return updater_->BeginInstall(); });
        break;
      case trg::update::UpdateUiAction::kNone:
        break;
    }
  }

  template <typename Begin>
  void RestartToUpdate(Begin begin) {
    if (InstallBusy()) {
      launcher_->SetStatus("Wait for the current install to finish.", 5.0);
      return;
    }
    if (!launcher_->Save()) {
      return;
    }
    SavePlacement(placement_.Flush());
    if (begin()) {
      skip_finish_save_ = true;
      Finish(cb_.quit);
    }
  }

  void* WindowHandle() const { return cb_.window_handle ? cb_.window_handle() : nullptr; }

  bool WindowFullscreen() const { return cb_.window_fullscreen && cb_.window_fullscreen(); }

  void StartInSavedState() {
    RestorePlacement();
    if (fullscreen_pref_.wanted()) {
      RequestFullscreen(true);
    }
  }

  void ToggleLauncherFullscreen() {
    const bool fullscreen = !WindowFullscreen();
    RequestFullscreen(fullscreen);
    fullscreen_pref_.UserSet(fullscreen);
    if (!settings_.SaveNow(LauncherFullscreenValues(fullscreen))) {
      REXLOG_WARN("Could not save the launcher fullscreen state");
    }
  }

  void RequestFullscreen(bool fullscreen) {
    fullscreen_gate_.Request(fullscreen);
    if (cb_.set_window_fullscreen) {
      cb_.set_window_fullscreen(fullscreen);
    }
  }

  // The game gets the window out of the launcher's temporary fullscreen.
  bool LeavingFullscreen() {
    if (!WindowFullscreen() || leave_gave_up_) {
      return false;
    }
    if (!leave_requested_) {
      leave_requested_ = true;
      leave_started_ = ImGui::GetTime();
      RequestFullscreen(false);
    }
    leave_gave_up_ = ImGui::GetTime() - leave_started_ >= FullscreenGate::kTimeoutSeconds;
    return !leave_gave_up_;
  }

  void RestorePlacement() {
    if (const auto placed = TakePreShowResult()) {
      startup_ = placed->startup;
      placement_ = PlacementTracker(placed->applied);
      return;
    }
    void* window = WindowHandle();
    const auto state = QueryWindowState(window);
    if (!state || WindowFullscreen()) {
      return;
    }
    const WindowPlacement saved = SavedLauncherPlacement();
    const bool restorable = PlacementIsVisible(saved, WorkAreas());
    startup_ = WindowPlacement{state->rect, state->maximized};
    placement_ = PlacementTracker(restorable ? std::optional<WindowPlacement>(saved) : std::nullopt);
    if (restorable) {
      ApplyWindowPlacement(window, saved);
    }
  }

  void TrackPlacement() {
    const double now = ImGui::GetTime();
    const FullscreenGate::Step step = fullscreen_gate_.Update(WindowFullscreen(), now);
    if (restore_when_windowed_) {
      if (!WindowFullscreen() && !fullscreen_gate_.Pending()) {
        restore_when_windowed_ = false;
        StartInSavedState();
      }
      return;
    }
    if (!startup_) {
      return;
    }
    if (step.restore) {
      if (const auto current = placement_.Current()) {
        ApplyWindowPlacement(WindowHandle(), *current);
      }
    }
    if (!step.observe) {
      return;
    }
    if (const auto state = QueryWindowState(WindowHandle())) {
      placement_.Observe(*state, WindowFullscreen(), now);
      SavePlacement(placement_.TakeSettled(now));
    }
  }

  void SavePlacement(const std::optional<WindowPlacement>& placement) {
    if (placement && !settings_.SaveNow(PlacementValues(*placement))) {
      REXLOG_WARN("Could not save the launcher window placement");
    }
  }

  // The game keeps the window it was created with, not the launcher's saved placement.
  void RestoreStartupWindow() {
    if (startup_ && !WindowFullscreen()) {
      ApplyWindowPlacement(WindowHandle(), *startup_);
    }
  }

  trg::LauncherConfig MakeConfig() {
    trg::LauncherConfig c;
    c.settings = &settings_;
    // PLAY saves in RequestPlay, so a failed save keeps the launcher open.
    c.save_on_play = false;
    c.theme = LauncherTheme();
    c.note = "Play applies the changes. Save keeps them without starting.";
    c.hints = "Enter  Play     Alt+F4  Quit     Ctrl+S  Save";
    c.fonts = g_fonts;
    c.branding.title = "SUPER STREET FIGHTER II TURBO HD REMIX";
    c.branding.subtitle_segments = {
        {"PC PORT   \xC2\xB7   ", ""},
        {"v" SSF2T_PORT_VERSION "   \xC2\xB7   ", ""},
        {"BY SAULOB", kAuthorUrl},
    };
    const float art_aspect =
        header_art_ ? float(header_art_->width) / float(header_art_->height) : kHeaderArtAspect;
    c.branding.background = LauncherHeader(
        header_art_ ? reinterpret_cast<ImTextureID>(header_art_.get()) : ImTextureID{}, art_aspect);
    c.pages = {
        {"Play", "Check the game files and start playing.", [this](trg::Ui& ui) { PagePlay(ui); },
         [this] { return game_files_ != GameFiles::kReady; }},
        {"Display", "Window, monitor and how the picture fits your screen.",
         [this](trg::Ui& ui) { PageDisplay(ui); }},
        {"Graphics", "Graphics card, render resolution, anti-aliasing and texture filtering.",
         [this](trg::Ui& ui) { PageGraphics(ui); }},
        {"Gameplay", "Language, and what the window shows while you play.", [](trg::Ui& ui) { PageGameplay(ui); }},
        {"Achievements", "Your achievements on this PC.", [this](trg::Ui& ui) { PageAchievements(ui); }},
        {"Controls", "Controller and keyboard.", [](trg::Ui& ui) { PageControls(ui); }},
        {"About", "About this port, and where your files and settings live.",
         [this](trg::Ui& ui) { PageAbout(ui); }},
    };
    c.can_play = [this] {
      if (install_.running()) {
        return trg::PlayCheck{false, "Wait for the install to finish.", 0};
      }
      if (game_files_ == GameFiles::kReady) {
        return trg::PlayCheck{};
      }
      return trg::PlayCheck{false, "The game files are not ready: see the Play page.", 0};
    };
    return c;
  }

  void PagePlay(trg::Ui& ui) {
    const std::string dir = Utf8(paths_.game_dir);
    if (install_.running()) {
      ui.StatusCard(trg::Status::kBusy, "Installing game files", nullptr, std::max(0.0f, install_.fraction()));
    } else {
      switch (game_files_) {
        case GameFiles::kReady:
          ui.StatusCard(trg::Status::kReady, "Ready to play", dir.c_str());
          break;
        case GameFiles::kUnsupported:
          ui.StatusCard(trg::Status::kAttention, "Different version of the game",
                        DescribeInstall(InstallStatus::kWrongVersion).text);
          break;
        case GameFiles::kMissing:
          ui.StatusCard(trg::Status::kAttention, "Game files needed",
                        "This port requires your own Super Street Fighter II Turbo HD Remix Xbox Live Arcade package.");
          break;
      }
    }
    if (!install_failure_.empty()) {
      DrawFailure(ui, install_failure_, install_failure_text_);
    }
    if (paths_.game_dir.empty()) {
      ui.Help("No game folder was given: start the game with --game_data_root.");
    } else {
      ui.Row("Game package",
             "Title ID: 584108A4. No game files are included with this port; the package's files (about 370 MB) are "
             "copied next to the program.");
      if (!ui.TaskProgress(install_)) {
        const bool clicked = game_files_ == GameFiles::kReady
                                 ? ImGui::Button("Reinstall from package...", ImVec2(-FLT_MIN, 0))
                                 : ui.AccentButton("Install from package...");
        if (clicked) {
          BrowseAndInstall();
        }
      }
    }
    ui.LauncherVisibilityRow("ssf2t_launcher");
    ui.EndRows();
  }

  static void DrawFailure(trg::Ui& ui, const std::string& title, const std::string& text) {
    ui.Spacer(4);
    ImGui::PushStyleColor(ImGuiCol_Text, ui.theme().warn_dot);
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopStyleColor();
    ui.Help(text.c_str());
    ui.Spacer(4);
  }

  void BrowseAndInstall() {
    PollInstall();
    if (install_.running()) {
      return;
    }
    const std::string picked = trg::BrowseForFile("Select your Super Street Fighter II Turbo HD Remix Xbox Live Arcade package",
                                                  {{"Xbox Live Arcade package", "*"}});
    if (picked.empty()) {
      return;
    }
    install_failure_.clear();
    install_failure_text_.clear();
    achievements_.reset();
    outcome_ = std::make_shared<InstallOutcome>();
    const bool started = install_.Start(
        "Reading package...",
        [outcome = outcome_, package = PathFromUtf8(picked), game_dir = paths_.game_dir](trg::Task& task) {
          InstallHooks hooks;
          hooks.phase = [&task](const char* text) {
            task.SetLabel(text);
            task.Progress(0, 0);
          };
          hooks.progress = [&task](uint64_t done, uint64_t total) {
            task.Progress(static_cast<long long>(done), static_cast<long long>(total));
            return !task.cancelled();
          };
          outcome->result = InstallFromPackage(package, game_dir, hooks);
          if (outcome->result.ok()) {
            outcome->files = CheckGameFiles(game_dir).state;
          }
          return outcome->result.ok() ? std::string() : std::string(DescribeInstall(outcome->result.status).title);
        });
    if (started) {
      REXLOG_INFO("Installing game files from {}", PathFromUtf8(picked).filename().string());
    }
  }

  // Runs every frame so a finished install is picked up whichever page is open.
  void PollInstall() {
    std::string error;
    if (install_.Finish(&error) == trg::Task::State::kIdle) {
      return;
    }
    const std::shared_ptr<InstallOutcome> outcome = std::move(outcome_);
    if (!outcome) {
      return;
    }
    const InstallResult& result = outcome->result;
    const InstallMessage message = DescribeInstall(result.status);
    if (result.ok() || result.status == InstallStatus::kCancelled) {
      if (result.ok()) {
        game_files_ = outcome->files;
        REXLOG_INFO("Game files installed: {} files, {} bytes", result.files, result.bytes);
      } else {
        REXLOG_INFO("Game install cancelled");
      }
      launcher_->SetStatus(std::string(message.title) + ". " + message.text, 6.0);
      return;
    }
    REXLOG_WARN("Game install failed: {} ({})", message.title, result.detail);
    install_failure_ = message.title;
    install_failure_text_ = message.text;
    launcher_->SetStatus("Install failed: " + install_failure_, 8.0);
  }

  void PageDisplay(trg::Ui& ui) {
    ui.Toggle("Display mode",
              "How the game starts. Borderless Fullscreen fills the monitor. F11 switches while playing.", "fullscreen",
              false, "Windowed", "Borderless Fullscreen");
    ui.Row("Window size", "The game window's size in Windowed mode.");
    WindowSizeCombo();
    ui.Row("Monitor", "The display the game opens on.");
    MonitorCombo();
    if (ui.Combo("Aspect Ratio",
                 "Sets the game's target rendering format and the Window size choices.",
                 "ssf2t_aspect_ratio", "16:9", AspectRatioOptions())) {
      SelectWindowSizeForAspect();
    }
    ui.Row("VSync", "On prevents tearing. Off allows it, for the lowest latency and G-Sync/FreeSync.");
    {
      const bool tearing = settings_.GetBool(kTearingSetting, true);
      if (int i = ui.Segmented("vsync", {"Off", "On"}, tearing ? 0 : 1); i >= 0) {
        settings_.SetBool(kTearingSetting, i == 0);
      }
    }
    ui.Row(kPresentationLabel, kPresentationHelp);
    {
      const bool letterbox = settings_.GetBool(kLetterboxSetting, true);
      if (int i = ui.Segmented("presentation", {"Letterbox", "Stretch"}, LetterboxSegment(letterbox)); i >= 0) {
        settings_.SetBool(kLetterboxSetting, LetterboxFromSegment(i));
      }
    }
  }

  const MonitorInfo* SelectedMonitor() const {
    const int index = settings_.GetInt("monitor", 0);
    if (index >= 1 && index <= int(monitors_.size())) {
      return &monitors_[size_t(index - 1)];
    }
    for (const MonitorInfo& m : monitors_) {
      if (m.primary) {
        return &m;
      }
    }
    return monitors_.empty() ? nullptr : &monitors_.front();
  }

  AspectRatio SelectedAspectRatio() const {
    return ParseAspectRatio(settings_.Get("ssf2t_aspect_ratio", "16:9"));
  }

  static bool WindowSizeFits(int width, int height, const MonitorInfo* monitor) {
    return !monitor || (width * monitor->scale < monitor->width && height * monitor->scale < monitor->height);
  }

  // Moves Window size to the new aspect ratio's default, unless the current size already belongs to it.
  void SelectWindowSizeForAspect() {
    const AspectRatio aspect = SelectedAspectRatio();
    const int width = settings_.GetInt("window_width", 0);
    const int height = settings_.GetInt("window_height", 0);
    for (const WindowSize& size : kWindowSizes) {
      if (size.aspect == aspect && size.width == width && size.height == height) {
        return;
      }
    }
    const AspectRatioInfo& info = InfoFor(aspect);
    const MonitorInfo* monitor = SelectedMonitor();
    const WindowSize* chosen = nullptr;
    for (const WindowSize& size : kWindowSizes) {
      if (size.aspect != aspect || !WindowSizeFits(size.width, size.height, monitor)) {
        continue;
      }
      if (size.width == info.default_width && size.height == info.default_height) {
        chosen = &size;
        break;
      }
      if (!chosen || size.width > chosen->width) {
        chosen = &size;
      }
    }
    if (chosen) {
      settings_.SetInt("window_width", chosen->width);
      settings_.SetInt("window_height", chosen->height);
    }
  }

  // window_width/height are logical pixels; display scaling enlarges them.
  void WindowSizeCombo() {
    const int width = settings_.GetInt("window_width", 0);
    const int height = settings_.GetInt("window_height", 0);
    const AspectRatio aspect = SelectedAspectRatio();
    const MonitorInfo* monitor = SelectedMonitor();
    const float scale = monitor ? monitor->scale : 1.0f;
    auto label = [scale](int w, int h) {
      if (w <= 0 || h <= 0) {
        return std::string("Default");
      }
      std::string text = Size(w, h);
      if (scale != 1.0f) {
        text += "   (" + Size(int(std::lround(w * scale)), int(std::lround(h * scale))) + " on screen)";
      }
      return text;
    };
    if (ImGui::BeginCombo("##window_size", label(width, height).c_str())) {
      for (const WindowSize& size : kWindowSizes) {
        if (size.aspect != aspect) {
          continue;
        }
        const bool current = size.width == width && size.height == height;
        if (!current && !WindowSizeFits(size.width, size.height, monitor)) {
          continue;
        }
        if (ImGui::Selectable(label(size.width, size.height).c_str(), current) && !current) {
          settings_.SetInt("window_width", size.width);
          settings_.SetInt("window_height", size.height);
        }
      }
      ImGui::EndCombo();
    }
  }

  void MonitorCombo() {
    const int current = settings_.GetInt("monitor", 0);
    auto label = [this](int index) {
      if (index <= 0) {
        return std::string("Default");
      }
      if (index > int(monitors_.size())) {
        return "Display " + std::to_string(index) + " (not connected)";
      }
      const MonitorInfo& m = monitors_[size_t(index - 1)];
      return "Display " + std::to_string(index) + (m.primary ? " (primary)" : "") + "   " + Size(m.width, m.height);
    };
    if (ImGui::BeginCombo("##monitor", label(current).c_str())) {
      for (int i = 1; i <= int(monitors_.size()); ++i) {
        if (ImGui::Selectable(label(i).c_str(), i == current) && i != current) {
          settings_.SetInt("monitor", i);
        }
      }
      ImGui::EndCombo();
    }
  }

  void PageGraphics(trg::Ui& ui) {
    if (!graphics_adapters_.empty()) {
      std::vector<trg::Option> adapters;
      for (const auto& [value, label] :
           GraphicsAdapterOptions(graphics_adapters_, settings_.Get("ssf2t_graphics_adapter", kGraphicsAdapterAuto))) {
        adapters.push_back({value, label});
      }
      ui.Combo("Graphics card",
               "Selects which GPU renders the game. Automatic uses the recommended high-performance GPU. "
               "A GPU chosen here overrides automatic selection and restarts the game window.",
               "ssf2t_graphics_adapter", kGraphicsAdapterAuto, adapters);
    }
    ui.Combo("Render resolution",
             "Draws the game at a multiple of its own picture, then scales it to the window. Higher is sharper and "
             "costs more GPU time. Window size is separate.",
             "resolution_scale", "1", RenderResolutionOptions(SelectedAspectRatio()));
    ui.Combo("Internal render resolution",
             "Renders the game internally at a lower resolution, then scales it to the selected render resolution. "
             "Lower values improve GPU performance but reduce image sharpness.",
             "ssf2t_internal_render_scale", "100",
             {{"100", "100% Native"}, {"75", "75% Performance"}, {"50", "50% Low-end"}});
    ui.Choice("Anti-aliasing", "Smooths jagged edges after the frame is drawn. Extreme is softer but cleaner.",
              "swap_post_effect", "none", {{"none", "Off"}, {"fxaa", "FXAA"}, {"fxaa_extreme", "FXAA Extreme"}});
    ui.Toggle("Multisampling",
              "Any parts of the scene the game draws with multisampling cannot be turned off. 2\xC3\x97 MSAA is "
              "fastest; try 4\xC3\x97 only if the 2\xC3\x97 parts show graphics problems.",
              "ssf2t_msaa_compatibility", false, "2\xC3\x97 MSAA", "4\xC3\x97 MSAA (Compatibility)");
    ui.Choice("Texture filtering", "Anisotropic filtering keeps textures sharp at oblique angles. Game leaves it to the game.",
              "anisotropic_override", "3",
              {{"-1", "Game"},
               {"0", "Off"},
               {"2", "2\xC3\x97"},
               {"3", "4\xC3\x97"},
               {"4", "8\xC3\x97"},
               {"5", "16\xC3\x97"}},
              6);
  }

  static void PageGameplay(trg::Ui& ui) {
    std::vector<trg::Option> languages;
    for (const GameLanguage& language : kGameLanguages) {
      languages.push_back({language.key, language.label});
    }
    ui.Combo(kLanguageLabel, kLanguageHelp, kLanguageSetting, kDefaultLanguage, languages);
    ui.Toggle("Frame rate in title", "Shows the game's frame rate in the window title: Super Street Fighter II Turbo HD Remix | FPS: 60.00",
              "ssf2t_show_fps", true, "Off", "On");
    ui.Toggle("Render resolution in title", "Shows the game's render resolution in the window title",
              "ssf2t_show_render_resolution", false, "Off", "On");
    ui.Toggle("Frame counter", "Shows the game's frame rate in the corner. F2 toggles it while playing.",
              "ssf2t_show_frame_counter", false, "Hidden", "Shown");
  }

  void PageAchievements(trg::Ui& ui) {
    if (game_files_ != GameFiles::kReady) {
      ui.Paragraph("Install the game to see its achievements.");
      return;
    }
    const uint32_t language = LanguageFor(settings_.Get(kLanguageSetting, kDefaultLanguage)).xconfig_language;
    if (!achievements_ || achievements_->language() != language) {
      achievements_.reset();
      achievements_ =
          std::make_unique<LauncherAchievements>(paths_.game_dir, paths_.user_dir, language, paths_.immediate_drawer);
    }
    if (!achievements_->ok()) {
      ui.Paragraph("The achievements could not be read from the game files.");
      return;
    }
    DrawAchievementSummary(ui, Summarize(achievements_->entries()));
    std::vector<trg::AchievementCard> cards;
    for (const AchievementEntry& entry : achievements_->entries()) {
      trg::AchievementCard card;
      card.icon = achievements_->Icon(entry.info);
      card.title = entry.info.label;
      card.description = entry.info.description;
      card.locked_description = entry.info.unachieved_description;
      card.points = static_cast<int>(entry.info.gamerscore);
      card.unlocked = entry.unlocked;
      cards.push_back(std::move(card));
    }
    ui.AchievementGrid(cards, 1);
  }

  static void DrawAchievementSummary(trg::Ui& ui, const AchievementSummary& summary) {
    ImGui::PushFont(ui.fonts().semibold, 0.0f);
    ImGui::Text("%d / %d unlocked", summary.unlocked, summary.total);
    ImGui::SameLine();
    ImGui::TextColored(kAccent, "%dG / %dG", summary.earned_gamerscore, summary.total_gamerscore);
    ImGui::PopFont();
    const float fraction = summary.total > 0 ? float(summary.unlocked) / float(summary.total) : 0.0f;
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kAccent);
    ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 6.0f * ui.scale()), "");
    ImGui::PopStyleColor();
    ui.Spacer(8);
  }

  static void PageControls(trg::Ui& ui) {
    ui.Info("Controller", "Automatic: Xbox controllers work without setup.");
    ui.Toggle("Keyboard", "Use the keyboard for gameplay controls together with the controller.", "mnk_mode", true,
              "Off", "On");
    if (ui.ConfirmRow("Keyboard defaults", "Puts the keyboard controls back to their standard layout.",
                      "Reset to defaults", "Put every key back to its default?", "Reset")) {
      ResetKeyboardDefaults(ui.settings());
      ui.SetStatus("Keyboard controls reset to defaults.");
    }
    if (ui.Section("Keyboard controls", true)) {
      ui.Help("Click a key, then press the new one. Esc cancels.");
      const bool enabled = ui.settings().GetBool("mnk_mode", true);
      if (!enabled) {
        ImGui::BeginDisabled();
      }
      for (const KeyboardAction& action : kKeyboardActions) {
        KeyboardRow(ui, action);
      }
      if (!enabled) {
        ImGui::EndDisabled();
      }
    }
  }

  static void KeyboardRow(trg::Ui& ui, const KeyboardAction& action) {
    const std::string shown = KeyboardBindingLabel(SavedKeyboardBinding(ui.settings(), action));
    const std::optional<ImGuiKey> key = ui.KeyBindRow(action.label, nullptr, shown);
    if (!key) {
      return;
    }
    if (*key == ImGuiKey_Enter || *key == ImGuiKey_KeypadEnter) {
      // The launcher's Play hotkey reads Enter later in this frame.
      ImGui::SetKeyOwner(*key, ImGuiKeyOwner_Any, ImGuiInputFlags_LockThisFrame);
    }
    const ImGuiIO& io = ImGui::GetIO();
    const std::string binding = KeyboardBindingFor(*key, io.KeyShift, io.KeyCtrl, io.KeyAlt);
    if (binding.empty()) {
      ui.SetStatus("That key cannot be used.");
      return;
    }
    ui.settings().Set(action.key, binding);
  }

  void PageAbout(trg::Ui& ui) {
    ui.Paragraph(
        "Super Street Fighter II Turbo HD Remix for Xbox Live Arcade, statically recompiled to run natively on PC with "
        "the ReXGlue SDK. No game code or assets are included: the game runs from your own game files.");
    ui.Spacer(4);
    if (!paths_.game_dir.empty()) {
      ui.FolderRow("Game files", "Where the game data is read from.", "Open game folder", Utf8(paths_.game_dir));
    }
    ui.FolderRow("Saves and settings", UserDataFolderHelp(paths_.user_data_outcome), "Open data folder",
                 Utf8(paths_.user_dir));
    if (ui.ButtonRow("Settings file", "Your launcher choices, as plain text. Loaded after ssf2t_hd_remix.toml.",
                     "Show settings file") &&
        launcher_->Save()) {
      std::error_code ec;
      const bool exists = std::filesystem::exists(paths_.settings_file, ec);
      trg::OpenInFileManager(Utf8(exists ? paths_.settings_file : paths_.user_dir));
    }
    ui.ResetAllRow();
    ui.Heading("Diagnostics");
    if (ui.Toggle("Detailed logging", "Keeps logs for normal sessions. Crash diagnostics are always preserved.",
                  "detailed_logging", true)) {
      rex::cvar::SetFlagByName("detailed_logging", settings_.Get("detailed_logging", "true"));
      ApplyDetailedLogging();
    }
    if (ui.ButtonRow("Logs folder", "Open the folder containing session and crash logs", "Open logs folder")) {
      OpenLogsFolder();
    }
    ui.Info("Port version", SSF2T_PORT_VERSION);
    ui.Info("Libraries", "ReXGlue SDK " REXGLUE_VERSION_STRING "   \xC2\xB7   TRG Launcher " SSF2T_TRG_VERSION);
    ui.Info("Game", "Xbox Live Arcade, title 584108A4, game version 0.0.1.3");
    ui.Link(DisplayUrl(kRepositoryUrl).c_str(), kRepositoryUrl);
  }

  void OpenLogsFolder() {
    std::error_code ec;
    std::filesystem::path folder = paths_.logs_dir;
    std::filesystem::create_directories(folder, ec);
    if (!std::filesystem::is_directory(folder, ec)) {
      REXLOG_WARN("Could not create the logs folder: {}", ec.message());
      folder = folder.parent_path();
    }
    trg::OpenInFileManager(Utf8(folder));
  }

  LauncherPaths paths_;
  LauncherCallbacks cb_;
  GameSettings& settings_;
  GameFiles game_files_;
  std::vector<MonitorInfo> monitors_;
  std::vector<GraphicsAdapter> graphics_adapters_;
  std::unique_ptr<rex::ui::ImmediateTexture> header_art_;
  std::unique_ptr<trg::Launcher> launcher_;
  ImGuiGamepad gamepad_;
  bool nav_taken_ = false;
  bool starting_ = false;
  double play_started_ = 0.0;
  ImGuiConfigFlags saved_nav_flags_ = 0;
  ImGuiBackendFlags saved_has_gamepad_ = 0;
  bool done_ = false;
  std::optional<WindowPlacement> startup_;
  PlacementTracker placement_;
  FullscreenGate fullscreen_gate_;
  LauncherFullscreenPreference fullscreen_pref_{SavedLauncherFullscreen()};
  bool restore_when_windowed_ = false;
  bool leave_requested_ = false;
  bool leave_gave_up_ = false;
  double leave_started_ = 0.0;
  std::string install_failure_;
  std::string install_failure_text_;
  std::shared_ptr<InstallOutcome> outcome_;
  trg::Task install_;
  std::unique_ptr<LauncherAchievements> achievements_;
  std::unique_ptr<trg::update::UpdateController> updater_;
  bool skip_finish_save_ = false;
};

}  // namespace

void LoadLauncherFonts(ImFontAtlas* atlas, float density) {
  // Latin-1 plus General Punctuation.
  static const ImWchar kRanges[] = {0x0020, 0x00FF, 0x2010, 0x205E, 0};
  density = ValidDpiScale(density);
  g_font_density = density;
  ImFontConfig config;
  config.RasterizerDensity = density;
  auto load = [&](const std::filesystem::path& path) -> ImFont* {
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(path, ec)) {
      return nullptr;
    }
    return atlas->AddFontFromFileTTF(Utf8(path).c_str(), g_fonts.size, &config, kRanges);
  };
  const UiFontFiles fonts = FindUiFonts();
  g_fonts.regular = load(fonts.regular);
  g_fonts.semibold = load(fonts.semibold);
  g_fonts.bold = load(fonts.bold);
}

void UpdateLauncherFonts(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate_drawer, float density) {
  if (!drawer || !immediate_drawer || !g_fonts.regular) {
    return;
  }
  density = ValidDpiScale(density);
  if (std::abs(density - g_font_density) < 0.001f) {
    return;
  }

  ImGuiIO& io = drawer->GetIO();
  for (ImFont* font : {g_fonts.regular, g_fonts.semibold, g_fonts.bold}) {
    if (!font) {
      continue;
    }
    const ImWchar fallback = font->FallbackChar;
    const ImWchar ellipsis = font->EllipsisChar;
    font->ClearOutputData();
    font->FallbackChar = fallback;
    font->EllipsisChar = ellipsis;
    font->CurrentRasterizerDensity = density;
    for (ImFontConfig* source : font->Sources) {
      source->RasterizerDensity = density;
    }
  }
  io.Fonts->TexIsBuilt = false;
  // The drawer holds the uploaded atlas until the immediate drawer is replaced.
  drawer->SetImmediateDrawer(nullptr);
  drawer->SetImmediateDrawer(immediate_drawer);
  g_font_density = density;
}

ImFont* LauncherRegularFont() {
  return g_fonts.regular;
}

ImFont* LauncherSemiboldFont() {
  return g_fonts.semibold;
}

ImVec4 LauncherAccent() {
  return kAccent;
}

namespace {
bool g_skipped_once = false;
}  // namespace

bool ShouldShowLauncher(const std::filesystem::path& game_dir) {
  g_skipped_once = REXCVAR_GET(ssf2t_skip_launcher);
  rex::cvar::ResetToDefault("ssf2t_skip_launcher");
  return LauncherWanted(CheckAndLogGameFiles(game_dir) == GameFiles::kReady, g_skipped_once, ShiftHeld(),
                        REXCVAR_GET(ssf2t_launcher));
}

bool ShiftHeldAfterWindowOpened() {
#if defined(_WIN32)
  return false;
#else
  return !g_skipped_once && ShiftHeld();
#endif
}

namespace {

std::pair<int, int> LauncherWindowSize() {
  const MonitorInfo primary = MonitorForSetting(1);
  if (primary.width == 0) {
    return kLauncherWindowSize;
  }
  const DpiWindowSize size = FitWindowToWorkArea(kLauncherWindowSize.first, kLauncherWindowSize.second,
                                                 primary.work_width, primary.work_height, primary.scale,
                                                 kLauncherMaxShare);
  return {size.width, size.height};
}

}  // namespace

void PrepareLauncherWindow() {
  const auto [width, height] = LauncherWindowSize();
  rex::cvar::SetFlagByName("fullscreen", "false");
  rex::cvar::SetFlagByName("window_width", std::to_string(width));
  rex::cvar::SetFlagByName("window_height", std::to_string(height));
  // Monitor 0 lets SDL centre the window on the primary display, whatever the display order.
  rex::cvar::SetFlagByName("monitor", "0");
  const WindowPlacement saved = SavedLauncherPlacement();
  InstallPreShowPlacement(PlanPreShow(saved, WorkAreas()), saved);
}

void ShowLauncher(rex::ui::ImGuiDrawer* drawer, LauncherPaths paths, GameSettings& settings,
                  LauncherCallbacks callbacks) {
  new LauncherDialog(drawer, std::move(paths), settings, std::move(callbacks));
}

}  // namespace ssf2t
