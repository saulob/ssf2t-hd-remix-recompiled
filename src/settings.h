#pragma once

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>

#include <rex/cvar.h>

#include "trg/settings.h"

#include "window_placement.h"

REXCVAR_DECLARE(bool, ssf2t_launcher);
REXCVAR_DECLARE(bool, ssf2t_skip_launcher);
REXCVAR_DECLARE(bool, ssf2t_show_fps);
REXCVAR_DECLARE(bool, ssf2t_show_render_resolution);
REXCVAR_DECLARE(bool, ssf2t_show_frame_counter);
REXCVAR_DECLARE(bool, ssf2t_msaa_compatibility);
REXCVAR_DECLARE(bool, detailed_logging);
REXCVAR_DECLARE(std::string, ssf2t_graphics_adapter);
REXCVAR_DECLARE(std::string, ssf2t_language);
REXCVAR_DECLARE(int32_t, ssf2t_launcher_x);
REXCVAR_DECLARE(int32_t, ssf2t_launcher_y);
REXCVAR_DECLARE(int32_t, ssf2t_launcher_width);
REXCVAR_DECLARE(int32_t, ssf2t_launcher_height);
REXCVAR_DECLARE(bool, ssf2t_launcher_maximized);
REXCVAR_DECLARE(bool, ssf2t_launcher_fullscreen);
REXCVAR_DECLARE(bool, ssf2t_infinite_time);
REXCVAR_DECLARE(std::string, ssf2t_player_1_name);
REXCVAR_DECLARE(std::string, ssf2t_player_2_name);

namespace ssf2t {

struct MonitorInfo;
class GameSettings;

// Allowing tearing is VSync Off. Each platform's presenter has its own setting for it.
#if defined(_WIN32)
inline constexpr char kTearingSetting[] = "d3d12_allow_variable_refresh_rate_and_tearing";
#else
inline constexpr char kTearingSetting[] = "vulkan_allow_present_mode_immediate";
#endif

// The player's settings file, loaded over the shipped ssf2t_hd_remix.toml.
inline std::filesystem::path PlayerSettingsPath(const std::filesystem::path& user_data_root) {
  return user_data_root / "settings.toml";
}
void LoadPlayerSettings(const std::filesystem::path& path);

// Sets the Xenos native_2x_msaa cvar from the launcher's Multisampling choice. Call once the GPU plugin
// has registered its cvars and before emulation starts.
void ApplyMultisampling();

// Sets the guest video mode for ssf2t_aspect_ratio. Call with ApplyMultisampling.
void ApplyAspectRatio();

// Sets the SDK's user_language, which the title reads once at startup, from ssf2t_language.
void ApplyLanguage();

// Sets the SDK's local_player_N_name cvars from ssf2t_player_N_name. Call with ApplyLanguage.
void ApplyPlayerNames();

// Sets the guest hook for ssf2t_fight_area. Call with ApplyLanguage.
void ApplyFightArea();

// Sets the guest timer hook from ssf2t_infinite_time. Call with ApplyFightArea.
void ApplyInfiniteTime();

// Windows: makes the D3D12 render target path the host one when Internal render resolution is below 100%. Call
// once the launcher choices are applied and before the command processor is created.
void ApplyInternalRenderPath();

// Sets d3d12_adapter from the launcher's Graphics card choice. Call once the player settings are loaded and
// before the graphics device is created; Automatic leaves the SDK's own preference alone.
void ApplyGraphicsAdapter();

struct StartupWindowInfo {
  int selected_width = -1;
  int selected_height = -1;
  int configured_width = 0;
  int configured_height = 0;
  int output_width = 0;
  int output_height = 0;
  bool fullscreen = false;
  std::string controller;
};

// Logs the launcher's own selections separately from the effective runtime state, plus a crash-safe summary.
void LogStartupDiagnostics(const GameSettings& settings, const MonitorInfo& display, int monitor,
                           const std::filesystem::path& game_dir, const StartupWindowInfo& window);

// The launcher window placement loaded from the player file.
WindowPlacement SavedLauncherPlacement();
bool SavedLauncherFullscreen();

// Edits stay pending until Save() or Apply(), so nothing reaches the window while the launcher is open.
class GameSettings final : public trg::SettingsStore {
 public:
  // The settings file is TOML, so toggles are written as true and false.
  GameSettings() {
    true_value = "true";
    false_value = "false";
  }

  // Takes the settings in effect now as the saved state.
  void Capture(const std::filesystem::path& player_file, const std::filesystem::path& shipped_config);
  // Takes the settings that were not registered at Capture (the Xenos graphics cvars).
  // Call once after SetupPresentation and before the launcher is shown.
  void CaptureLateRegistered();

  std::string Get(std::string_view key, std::string_view def = {}) const override;
  void Set(std::string_view key, std::string value) override;
  bool Save() override;
  // Writes the given values at once. Pending edits stay pending, and Reset never touches them.
  bool SaveNow(const SettingValues& values);
  void ResetToDefaults() override;

  // Sets the running game's cvars that differ from the pending values.
  void Apply() const;

  // Records the window settings this process creates its window with.
  void MarkWindowCreated();
  // Whether the pending window size, monitor, VSync or graphics card needs a new process.
  bool WindowChanged() const;

 private:
  std::filesystem::path player_file_;
  std::filesystem::path shipped_config_;
  std::map<std::string, std::string> saved_;
  std::map<std::string, std::string> pending_;
  std::map<std::string, std::string> window_created_;
  std::set<std::string> unregistered_;
};

// Flips ssf2t_infinite_time, applies it and saves it to the player file, where the launcher reads it.
bool ToggleInfiniteTime(GameSettings& settings);

}  // namespace ssf2t
