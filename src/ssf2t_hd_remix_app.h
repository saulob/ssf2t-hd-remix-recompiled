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
#include <rex/system/kernel_state.h>
#include <rex/ui/flags.h>
#include <rex/ui/windowed_app_context.h>

#include "aspect_ratio.h"
#include "frame_counter.h"
#include "game_files.h"
#include "internal_render_scale.h"
#include "launcher.h"
#include "modal_dialog.h"
#include "platform.h"
#include "settings.h"
#include "trg/game_helpers.h"
#include "user_data.h"
#include "window_host.h"

namespace ssf2t {

class SuperStreetFighterApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    SetApplicationId();
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
    // ReXGlue writes its logs beside the executable, which is read-only inside an AppImage.
    if (port_folder != rex::filesystem::GetExecutableFolder() && rex::cvar::GetFlagByName("log_file").empty()) {
      for (const auto& logs : {port_folder / "logs", paths.user_data_root / "logs"}) {
        if (const std::filesystem::path file = NextLogFile(logs, "ssf2t_hd_remix"); !file.empty()) {
          rex::cvar::SetFlagByName("log_file", Utf8(file));
          break;
        }
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
    const UserDataMessage message = DescribeUserData(user_data_);
    if (message.warning) {
      REXLOG_WARN("{}", message.text);
    } else {
      REXLOG_INFO("{}", message.text);
    }
    LoadPlayerSettings(PlayerSettingsPath(user_data_root()));
    ApplyGraphicsAdapter();
  }

  void OnConfigureFonts(ImFontAtlas* atlas) override {
    LoadLauncherFonts(atlas, window() ? float(window()->GetDpi()) / float(window()->GetMediumDpi()) : 1.0f);
  }

  void OnPreSetup(rex::RuntimeConfig&) override {
    REXLOG_INFO("DEBUG_SAULO: runtime initialization started");
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
        resume(defaults);
      });
    };
    callbacks.window_changed = [this] { return game_settings_.WindowChanged(); };
    callbacks.restart = [this] {
      if (!RelaunchSelf(L"--hcu_skip_launcher=true")) {
        return false;
      }
      app_context().CallInUIThreadDeferred([this] { app_context().QuitFromUIThread(); });
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
    ShowLauncher(imgui_drawer(),
                 {defaults.game_data_root, defaults.user_data_root, PlayerSettingsPath(defaults.user_data_root),
                  user_data_.outcome, immediate_drawer()},
                 game_settings_, std::move(callbacks));
    return std::nullopt;
  }

  void OnPostSetup() override {
    REXLOG_INFO("DEBUG_SAULO: setup complete, next is guest entry 0x839C45E8");
    REXLOG_INFO("{}", DescribeInternalRenderResolution(
                          std::max(1, std::atoi(rex::cvar::GetFlagByName("resolution_scale").c_str())),
                          std::atoi(rex::cvar::GetFlagByName("hcu_internal_render_scale").c_str()),
                          GuestWidth(ParseAspectRatio(rex::cvar::GetFlagByName("hcu_aspect_ratio")))));
#if defined(_WIN32)
    REXLOG_INFO("Scheduling: {}", trg::TuneProcessScheduling());
#endif
    AttachWindowHost(window(), app_context(), [] { return !ModalDialog::AnyOpen(); });
    if (window()) {
      CheckWindowDisplay(window()->GetNativeWindowHandle(), REXCVAR_GET(monitor));
    }
    if (window() && imgui_drawer()) {
      CreateFrameCounter(imgui_drawer());
      CreateCursorTick(imgui_drawer());
      ModalDialog::AttachEscapeKey(window());
    }
  }

  void OnShutdown() override {
    // ReXApp destroys the window before the runtime; the input drivers must detach while it still exists.
    if (auto* input = runtime() ? runtime()->input_system() : nullptr) {
      input->Shutdown();
    }
    ModalDialog::DetachEscapeKey();
    DetachWindowHost();
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
  bool show_launcher_ = false;
};

}  // namespace ssf2t
