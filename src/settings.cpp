#include "settings.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <toml++/toml.hpp>

#include <rex/logging.h>

#include "trg/settings.h"

#include "graphics_adapters.h"
#include "internal_render_scale.h"
#include "multisampling.h"

#include "platform.h"

REXCVAR_DEFINE_BOOL(hcu_launcher, true, "SSF2T",
                    "Show the launcher before the game starts (hold Shift at start to force it)");
REXCVAR_DEFINE_BOOL(hcu_skip_launcher, false, "SSF2T",
                    "Skip the launcher once (set when the launcher restarts the game)");
REXCVAR_DEFINE_BOOL(hcu_show_fps, true, "SSF2T", "Show the frame rate in the window title");
REXCVAR_DEFINE_BOOL(hcu_show_render_resolution, false, "SSF2T",
                    "Show the render resolution in the window title");
REXCVAR_DEFINE_BOOL(hcu_show_frame_counter, false, "SSF2T",
                    "Show the frame counter in the corner (F2 toggles it)");
REXCVAR_DEFINE_BOOL(hcu_msaa_compatibility, false, "SSF2T",
                    "Draw the game's 2x MSAA render targets with 4x MSAA, for GPUs or drivers whose "
                    "native 2x MSAA misrenders (sets native_2x_msaa)");
REXCVAR_DEFINE_STRING(hcu_aspect_ratio, "16:9", "SSF2T",
                      "Aspect ratio chosen in the launcher: 16:9, 16:10, 21:9, 32:9, 4:3 or 5:4");
REXCVAR_DEFINE_STRING(hcu_graphics_adapter, "auto", "SSF2T",
                      "Graphics card: auto, or the index d3d12_adapter takes (applied at start)");
REXCVAR_DEFINE_INT32(hcu_launcher_x, 0, "SSF2T", "Launcher window left edge, written by the launcher");
REXCVAR_DEFINE_INT32(hcu_launcher_y, 0, "SSF2T", "Launcher window top edge, written by the launcher");
REXCVAR_DEFINE_INT32(hcu_launcher_width, 0, "SSF2T", "Launcher window restored width, written by the launcher");
REXCVAR_DEFINE_INT32(hcu_launcher_height, 0, "SSF2T",
                     "Launcher window restored height, written by the launcher");
REXCVAR_DEFINE_BOOL(hcu_launcher_maximized, false, "SSF2T", "Launcher window was maximized when closed");
REXCVAR_DEFINE_BOOL(hcu_launcher_fullscreen, false, "SSF2T",
                    "Launcher was left in its own F11 fullscreen, written by the launcher");

namespace ssf2t {
namespace {

const std::vector<std::string> kGameSettingKeys = {
    "hcu_launcher",
    "fullscreen",
    "window_width",
    "window_height",
    "monitor",
    kTearingSetting,
    "hcu_graphics_adapter",
    "hcu_aspect_ratio",
    "resolution_scale",
    "hcu_internal_render_scale",
    "swap_post_effect",
    "hcu_msaa_compatibility",
    "anisotropic_override",
    "hcu_show_fps",
    "hcu_show_render_resolution",
    "hcu_show_frame_counter",
    "mnk_mode",
};

const std::vector<std::string> kWindowKeys = {
    "window_width",
    "window_height",
    "monitor",
    kTearingSetting,
    "hcu_graphics_adapter",
};

bool IsWindowKey(std::string_view key) {
  return std::find(kWindowKeys.begin(), kWindowKeys.end(), key) != kWindowKeys.end();
}

// Monitor 0 (Default) is the primary display, as monitor 1 is.
std::string WindowValue(std::string_view key, const std::string& value) {
  if (key == "monitor") {
    return std::to_string(std::max(1, std::atoi(value.c_str())));
  }
  return value;
}

std::string TomlValue(std::string_view key, const std::string& value) {
  const rex::cvar::FlagEntry* info = rex::cvar::GetFlagInfo(key);
  if (!info || info->type != rex::cvar::FlagType::String) {
    return value;
  }
  std::string quoted = "\"";
  for (char c : value) {
    if (c == '"' || c == '\\') {
      quoted += '\\';
    }
    quoted += c;
  }
  return quoted + '"';
}

std::optional<std::string> ConfigValue(const toml::table& table, std::string_view key) {
  const toml::node* node = table.get(key);
  if (!node) {
    return std::nullopt;
  }
  if (auto v = node->value_exact<bool>()) {
    return *v ? "true" : "false";
  }
  if (auto v = node->value_exact<int64_t>()) {
    return std::to_string(*v);
  }
  if (auto v = node->value_exact<double>()) {
    return std::to_string(*v);
  }
  return node->value_exact<std::string>();
}

trg::TextFileSettings OpenPlayerFile(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (!std::filesystem::exists(path, ec)) {
    std::ofstream(path) << "# Super Street Fighter II Turbo HD Remix: player settings, written by the launcher.\n"
                           "# Loaded after ssf2t_hd_remix.toml. Remove a line to use the shipped value.\n";
  }
  return trg::TextFileSettings(Utf8(path));
}

}  // namespace

void LoadPlayerSettings(const std::filesystem::path& path) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    return;
  }
  rex::cvar::LoadConfig(path);
  REXLOG_INFO("Player settings: {}", Utf8(path));
}

WindowPlacement SavedLauncherPlacement() {
  struct Cvars {
    int GetInt(const char* key, int) const { return std::atoi(rex::cvar::GetFlagByName(key).c_str()); }
    bool GetBool(const char* key, bool) const { return rex::cvar::GetFlagByName(key) == "true"; }
  };
  return ReadPlacement(Cvars{});
}

bool SavedLauncherFullscreen() {
  return REXCVAR_GET(hcu_launcher_fullscreen);
}

void GameSettings::Capture(const std::filesystem::path& player_file, const std::filesystem::path& shipped_config) {
  player_file_ = player_file;
  shipped_config_ = shipped_config;
  saved_.clear();
  unregistered_.clear();
  for (const std::string& key : kGameSettingKeys) {
    saved_[key] = rex::cvar::GetFlagByName(key);
    if (!rex::cvar::GetFlagInfo(key)) {
      unregistered_.insert(key);
    }
  }
  pending_ = saved_;
}

void GameSettings::CaptureLateRegistered() {
  for (auto it = unregistered_.begin(); it != unregistered_.end();) {
    const std::string& key = *it;
    if (!rex::cvar::GetFlagInfo(key)) {
      ++it;
      continue;
    }
    if (pending_[key].empty()) {
      saved_[key] = pending_[key] = rex::cvar::GetFlagByName(key);
    }
    it = unregistered_.erase(it);
  }
}

std::string GameSettings::Get(std::string_view key, std::string_view def) const {
  const auto it = pending_.find(std::string(key));
  return it == pending_.end() || it->second.empty() ? std::string(def) : it->second;
}

void GameSettings::Set(std::string_view key, std::string value) {
  pending_[std::string(key)] = std::move(value);
}

bool GameSettings::Save() {
  std::vector<std::pair<std::string, std::string>> changes;
  for (const auto& [key, value] : pending_) {
    const auto saved = saved_.find(key);
    if (saved == saved_.end() || saved->second != value) {
      changes.emplace_back(key, value);
    }
  }
  if (changes.empty()) {
    return true;
  }
  for (const auto& [key, value] : changes) {
    if (!rex::cvar::GetFlagInfo(key)) {
      REXLOG_ERROR("Cannot save {}: the setting is not registered", key);
      return false;
    }
  }

  trg::TextFileSettings file = OpenPlayerFile(player_file_);
  for (const auto& [key, value] : changes) {
    file.Set(key, TomlValue(key, value));
  }
  if (!file.Save()) {
    REXLOG_ERROR("Cannot write player settings to {}", Utf8(player_file_));
    return false;
  }
  saved_ = pending_;
  return true;
}

bool GameSettings::SaveNow(const SettingValues& values) {
  trg::TextFileSettings file = OpenPlayerFile(player_file_);
  for (const auto& [key, value] : values) {
    file.Set(key, value);
  }
  if (!file.Save()) {
    REXLOG_ERROR("Cannot write player settings to {}", Utf8(player_file_));
    return false;
  }
  return true;
}

void GameSettings::ResetToDefaults() {
  toml::table shipped;
  try {
    shipped = toml::parse_file(Utf8(shipped_config_));
  } catch (const toml::parse_error& e) {
    REXLOG_WARN("Cannot read {}: {}", Utf8(shipped_config_), e.what());
  }
  for (const std::string& key : kGameSettingKeys) {
    const rex::cvar::FlagEntry* info = rex::cvar::GetFlagInfo(key);
    pending_[key] = ConfigValue(shipped, key).value_or(info ? info->default_value : std::string());
  }
}

void GameSettings::Apply() const {
  for (const auto& [key, value] : pending_) {
    if (IsWindowKey(key) || rex::cvar::GetFlagByName(key) == value) {
      continue;
    }
    rex::cvar::SetFlagByName(key, value);
  }
  ApplyAspectRatio();
  ApplyInternalRenderPath();
  ApplyMultisampling();
}

// Also replaces a native_2x_msaa = false written by launchers that labelled it Off.
void ApplyMultisampling() {
  rex::cvar::SetFlagByName("native_2x_msaa",
                           Native2xMsaaFor(REXCVAR_GET(hcu_msaa_compatibility)) ? "true" : "false");
}

void ApplyInternalRenderPath() {
#if defined(_WIN32)
  const std::optional<std::string_view> path = RequiredRenderTargetPath(
      std::atoi(rex::cvar::GetFlagByName("hcu_internal_render_scale").c_str()),
      rex::cvar::GetFlagByName("render_target_path_d3d12"));
  if (path && rex::cvar::SetFlagByName("render_target_path_d3d12", *path)) {
    REXLOG_INFO("Internal render resolution: using D3D12 host render targets");
  }
#endif
}

void ApplyGraphicsAdapter() {
#if defined(_WIN32)
  const std::string setting = REXCVAR_GET(hcu_graphics_adapter);
  if (!ParseGraphicsAdapter(setting)) {
    REXLOG_INFO("Graphics card: Automatic");
    return;
  }
  const std::vector<GraphicsAdapter> adapters = ListGraphicsAdapters();
  const int index = ResolveGraphicsAdapter(setting, adapters);
  if (index < 0) {
    REXLOG_WARN("Graphics card {} is not available, using Automatic", setting);
    return;
  }
  rex::cvar::SetFlagByName("d3d12_adapter", std::to_string(index));
  for (const GraphicsAdapter& adapter : adapters) {
    if (adapter.index == index) {
      REXLOG_INFO("Graphics card: {} (adapter {})", adapter.name, index);
    }
  }
#endif
}

void GameSettings::MarkWindowCreated() {
  window_created_.clear();
  for (const std::string& key : kWindowKeys) {
    window_created_[key] = rex::cvar::GetFlagByName(key);
  }
}

bool GameSettings::WindowChanged() const {
  for (const std::string& key : kWindowKeys) {
    const auto created = window_created_.find(key);
    const std::string created_value = created == window_created_.end() ? std::string() : created->second;
    if (WindowValue(key, Get(key)) != WindowValue(key, created_value)) {
      return true;
    }
  }
  return false;
}

}  // namespace ssf2t
