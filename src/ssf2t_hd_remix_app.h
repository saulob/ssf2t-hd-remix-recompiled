#pragma once

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/version.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/flags.h>
#include <rex/ui/windowed_app_context.h>

#include "achievement_ui.h"
#include "achievements_overlay.h"
#include "active_log.h"
#include "aspect_ratio.h"
#include "diagnostics.h"
#include "frame_counter.h"
#include "game_files.h"
#include "guest_functions.h"
#include "input_summary.h"
#include "internal_render_scale.h"
#include "launcher.h"
#include "log_filter.h"
#include "log_path.h"
#include "modal_dialog.h"
#include "overlay_rules.h"
#include "platform.h"
#include "session_log.h"
#include "settings.h"
#include "system_info.h"
#include "trg/game_helpers.h"
#include "user_data.h"
#include "window_host.h"

namespace ssf2t {

class SuperStreetFighterApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    SetApplicationId();
    InstallGuestFunctionTable(PPCImageConfig.func_mappings);
    return std::unique_ptr<SuperStreetFighterApp>(
        new SuperStreetFighterApp(ctx, "ssf2t_hd_remix", PPCImageConfig));
  }

 protected:
  void OnConfigurePaths(rex::PathConfig& paths) override {
    shipped_config_ = paths.config_path;
    const std::filesystem::path port_folder = PortFolder();
    if (paths.game_data_root.empty()) {
      paths.game_data_root = port_folder / "game";
    }
    ChooseUserData(paths, port_folder);
    if (const auto inherited = TakeInheritedSessionLog({port_folder / "logs", paths.user_data_root / "logs"})) {
      rex::cvar::SetFlagFromCommandLine("log_file", Utf8(*inherited));
      InstallCrashReports();
    }
    // ReXGlue writes its logs beside the executable, which is read-only inside an AppImage.
    const bool appimage = port_folder != rex::filesystem::GetExecutableFolder();
    if (appimage && rex::cvar::GetFlagByName("log_file").empty()) {
      const std::filesystem::path logs = LogsFolder(port_folder, paths.user_data_root, {}, appimage);
      if (const std::filesystem::path file = NextLogFile(logs, "ssf2t_hd_remix"); !file.empty()) {
        rex::cvar::SetFlagByName("log_file", Utf8(file));
      }
    }
  }

  bool SetupPresentation() override {
    if (rex::ReXApp::SetupPresentation()) {
      return true;
    }
    REXLOG_WARN("Graphics unavailable; opening the launcher without graphics");
    RunLauncherWithoutGraphics(DescribeGameFiles());
    return false;
  }

  void OnPostInitLogging() override {
    InitializeProcessUptime();
    InstallCrashReports();
    const UserDataMessage message = DescribeUserData(user_data_);
    if (message.warning) {
      REXLOG_WARN("{}", message.text);
    } else {
      REXLOG_INFO("{}", message.text);
    }
    LoadPlayerSettings(PlayerSettingsPath(user_data_root()));
    ApplyDetailedLogging();
    InstallSessionLogCleanup();
    RemoveDiscardedSessionLogs();
    const ApplicationIdentity identity{
        .product = "Super Street Fighter II Turbo HD Remix",
        .version = SSF2T_PORT_VERSION,
        .build_config = BuildConfigurationName(),
        .architecture = ArchitectureName(),
        .sdk = REXGLUE_VERSION_STRING,
        .backend = GraphicsBackendName(),
    };
    REXLOG_INFO("{}", FormatApplicationVersion(identity));
    REXLOG_INFO("{}", FormatBuildIdentity(identity));
    REXLOG_INFO("{}", GameFilesLogText(CheckGameFiles(game_data_root())));
    ApplyGraphicsAdapter();
  }

  void OnConfigureFonts(ImFontAtlas* atlas) override {
    LoadLauncherFonts(atlas, window() ? float(window()->GetDpi()) / float(window()->GetMediumDpi()) : 1.0f);
  }

  void OnConfigureStyle(ImGuiStyle&, rex::ui::Style& ui_style) override {
    ApplyAchievementsStyle(ui_style.achievements);
  }

  void OnDpiScaleChanged(float scale) override {
    UpdateLauncherFonts(imgui_drawer(), immediate_drawer(), scale);
  }

  void OnPreSetup(rex::RuntimeConfig&) override {
    game_settings_.Capture(PlayerSettingsPath(user_data_root()), shipped_config_);
    show_launcher_ = ShouldShowLauncher(game_data_root());
    if (show_launcher_) {
      PrepareLauncherWindow();
    }
    game_settings_.MarkWindowCreated();
  }

  std::optional<rex::PathConfig> OnFinalizePaths(const rex::PathConfig& defaults,
                                                 std::function<void(rex::PathConfig)> resume) override {
    if (window()) {
      window()->SetTitle(kWindowTitle);
    }
    ApplyWindowIcon();
    game_settings_.CaptureLateRegistered();
    if (!show_launcher_ && ShiftHeldAfterWindowOpened()) {
      show_launcher_ = true;
    }
    if (!show_launcher_) {
      ApplyMultisampling();
      ApplyAspectRatio();
      ApplyInternalRenderPath();
      ApplyLanguage();
      ApplyFightArea();
      return defaults;
    }
    if (!imgui_drawer()) {
      game_settings_.Apply();
      return defaults;
    }
    REXLOG_INFO("Launcher shown");
    LauncherCallbacks callbacks;
    callbacks.play = [this, resume, defaults] {
      app_context().CallInUIThreadDeferred([this, resume, defaults] {
        game_settings_.Apply();
        ApplyDetailedLogging();
        resume(defaults);
      });
    };
    callbacks.window_changed = [this] { return game_settings_.WindowChanged(); };
    callbacks.restart = [this] {
      if (!RelaunchSelf(L"--ssf2t_skip_launcher=true")) {
        return false;
      }
      app_context().CallInUIThreadDeferred([] {
        REXLOG_INFO("Restarted in a new window; hard-exiting this process.");
        PrepareHardExit();
        std::_Exit(0);
      });
      return true;
    };
    callbacks.quit = [this] { app_context().CallInUIThreadDeferred([this] { app_context().QuitFromUIThread(); }); };
    callbacks.window_handle = [this] { return window() ? window()->GetNativeWindowHandle() : nullptr; };
    callbacks.window_fullscreen = [this] { return window() && window()->IsFullscreen(); };
    callbacks.set_window_fullscreen = [this](bool fullscreen) {
      if (window()) {
        window()->SetFullscreen(fullscreen);
      }
    };
    const std::filesystem::path port_folder = PortFolder();
    const std::filesystem::path logs_dir = LogsFolder(port_folder, defaults.user_data_root, ActiveLogFile(),
                                                      port_folder != rex::filesystem::GetExecutableFolder());
    ShowLauncher(imgui_drawer(),
                 {defaults.game_data_root, defaults.user_data_root, PlayerSettingsPath(defaults.user_data_root),
                  logs_dir, user_data_.outcome, immediate_drawer()},
                 game_settings_, std::move(callbacks));
    return std::nullopt;
  }

  void LaunchModule() override {
    const int monitor = game_settings_.GetInt("monitor", std::atoi(rex::cvar::GetFlagByName("monitor").c_str()));
    StartupWindowInfo startup_window;
    startup_window.fullscreen = window() != nullptr && window()->IsFullscreen();
    startup_window.selected_width =
        game_settings_.GetInt("window_width", std::atoi(rex::cvar::GetFlagByName("window_width").c_str()));
    startup_window.selected_height =
        game_settings_.GetInt("window_height", std::atoi(rex::cvar::GetFlagByName("window_height").c_str()));
    startup_window.configured_width =
        window() != nullptr ? int(window()->GetDesiredLogicalWidth()) : startup_window.selected_width;
    startup_window.configured_height =
        window() != nullptr ? int(window()->GetDesiredLogicalHeight()) : startup_window.selected_height;
    const DrawableSize drawable = CurrentDrawableSize(window());
    startup_window.output_width = drawable.width;
    startup_window.output_height = drawable.height;
    startup_window.controller = ControllerSummary();
    LogStartupDiagnostics(game_settings_, MonitorForSetting(monitor), monitor, game_data_root(), startup_window);
    ReXApp::LaunchModule();
  }

  void OnPostSetup() override {
    REXLOG_INFO("{}", DescribeInternalRenderResolution(
                          std::max(1, std::atoi(rex::cvar::GetFlagByName("resolution_scale").c_str())),
                          std::atoi(rex::cvar::GetFlagByName("ssf2t_internal_render_scale").c_str()),
                          GuestWidth(ParseAspectRatio(rex::cvar::GetFlagByName("ssf2t_aspect_ratio")))));
#if defined(_WIN32)
    REXLOG_INFO("Scheduling: {}", trg::TuneProcessScheduling());
#endif
    AttachWindowHost(window(), app_context(), [this] {
      rex::ui::ImGuiDrawer* drawer = imgui_drawer();
      return drawer && drawer->GetIO().WantCaptureMouse;
    });
    if (window()) {
      CheckWindowDisplay(window()->GetNativeWindowHandle(), REXCVAR_GET(monitor));
    }
    if (window() && imgui_drawer()) {
      CreateFrameCounter(imgui_drawer());
      CreateCursorSync(imgui_drawer());
      ModalDialog::AttachEscapeKey(window());
      // Replaces ReXApp's own callback, which would let the controller through while the overlay is open.
      if (auto* input = static_cast<rex::input::InputSystem*>(runtime()->input_system())) {
        input->SetActiveCallback([this] { return GameInputActive(); });
      }
      SetAchievementsOpener([this] { app_context().CallInUIThreadDeferred([this] { OpenAchievements(); }); });
    }
  }

  void OnShutdown() override {
    // ReXApp destroys the window before the runtime; the input drivers must detach while it still exists.
    if (auto* input = runtime() ? runtime()->input_system() : nullptr) {
      input->Shutdown();
    }
    ModalDialog::DetachEscapeKey();
    delete achievements_;
    DetachWindowHost();
    REXLOG_INFO("{}", FormatCleanExit(ProcessUptimeText()));
  }

  // Window close ends the process without teardown: the guest main thread may be inside D3D code that never waits.
  void OnClosing(rex::ui::UIEvent&) override {
    REXLOG_INFO("Window closing, hard-exiting process.");
    DiscardSessionLogBeforeHardExit();
    PrepareHardExit();
    std::_Exit(0);
  }

  // F7 toggles the overlay. The overlay is owned here, not by ReXApp.
  std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override {
    switch (ToggleOverlay(achievements_ != nullptr, ModalDialog::AnyOpen())) {
      case OverlayToggle::kClose:
        achievements_->RequestClose();
        break;
      case OverlayToggle::kOpen:
        OpenAchievements();
        break;
      case OverlayToggle::kNone:
        break;
    }
    return nullptr;
  }

 private:
  GraphicsFallbackInfo DescribeGameFiles() const {
    switch (CheckGameFiles(game_data_root()).state) {
      case GameFiles::kReady:
        return {Utf8(game_data_root()), "Ready", true};
      case GameFiles::kUnsupported:
        return {Utf8(game_data_root()), "Unsupported version", false};
      default:
        return {Utf8(game_data_root()), "Setup required", false};
    }
  }

  bool GameInputActive() const {
    rex::ui::ImGuiDrawer* drawer = imgui_drawer();
    return ssf2t::GameInputActive(ModalDialog::AnyOpen(), drawer && drawer->HasDialogs(),
                                  drawer && drawer->GetIO().WantCaptureMouse);
  }

  void OpenAchievements() {
    if (achievements_ || ModalDialog::AnyOpen() || !runtime() || !runtime()->kernel_state() || !imgui_drawer() ||
        !immediate_drawer()) {
      return;
    }
    achievements_ = new AchievementsOverlay(imgui_drawer(), immediate_drawer(), runtime(), &achievements(), [this] {
      achievements_ = nullptr;
    });
  }

  // The cache folder ReXGlue derived from the system root follows the user data root unless it was set explicitly.
  void ChooseUserData(rex::PathConfig& paths, const std::filesystem::path& port_folder) {
    user_data_ = SelectUserData({port_folder, paths.user_data_root, !REXCVAR_GET(user_data_root).empty()});
    if (user_data_.root == paths.user_data_root) {
      return;
    }
    paths.user_data_root = user_data_.root;
    if (REXCVAR_GET(cache_root).empty()) {
      paths.cache_root = user_data_.root / "cache";
    }
  }

  std::filesystem::path shipped_config_;
  GameSettings game_settings_;
  UserDataSelection user_data_;
  AchievementsOverlay* achievements_ = nullptr;
  bool show_launcher_ = false;
};

}  // namespace ssf2t
