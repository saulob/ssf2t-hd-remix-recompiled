// A launcher for an imaginary game, using every part of TRG Launcher. It is
// also the template for a new port: copy it, rename, and replace the pages.
//
//   trg_launcher_demo [--theme midnight|ocean|ember] [--page N]
//                     [--screenshot out.ppm] [--ready] [--prompt]
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "trg/headers.h"
#include "trg/launcher.h"
#include "trg/platform.h"
#include "trg/standalone.h"

namespace {

struct Binding {
  const char* key;
  const char* label;
  const char* def;
};
const Binding kBindings[] = {
    {"key_jump", "Jump", "Space"},       {"key_attack", "Attack", "J"},     {"key_pause", "Pause", "Enter"},
    {"key_up", "Move up", "W"},          {"key_down", "Move down", "S"},     {"key_left", "Move left", "A"},
    {"key_right", "Move right", "D"},
};

struct Demo {
  trg::TextFileSettings settings{"demo_settings.txt"};
  trg::Task install;
  std::string picked;  // image chosen on the Install page
  std::string install_error;
  std::vector<uint32_t> icon_pixels;
  bool force_ready = false;

  bool Installed() const {
    if (force_ready) return true;
    const std::string img = settings.Get("game_image");
    return !img.empty() && trg::FileSize(img) > 0;
  }

  void PageInstall(trg::Ui& ui) {
    if (const auto state = install.Finish(&install_error); state != trg::Task::State::kIdle) {
      if (state == trg::Task::State::kDone) ui.SetStatus("Installed. Press Play to start the game.", 6);
      if (state == trg::Task::State::kFailed) ui.SetStatus("Install failed: " + install_error, 8);
      if (state == trg::Task::State::kCancelled) ui.SetStatus("Install cancelled.");
    }
    if (install.running())
      ui.StatusCard(trg::Status::kBusy, "Installing...", nullptr, std::max(0.0f, install.fraction()));
    else if (Installed())
      ui.StatusCard(trg::Status::kReady, "Ready to play",
                    force_ready ? "Demo mode: pretending the game is installed." : settings.Get("game_image").c_str());
    else
      ui.StatusCard(trg::Status::kAttention, "Game files needed", "Select your own copy of the game below.");

    ui.Row("Game image",
           "Your own copy of the game, as a disc image. You can also drop the file onto this window.");
    if (ui.AccentButton(picked.empty() ? "Browse..." : picked.c_str()) && !install.running()) {
      const std::string s = trg::BrowseForFile("Select your game image", {{"Disc images", "*.iso;*.bin;*.img"}});
      if (!s.empty()) picked = s;
    }
    ui.Choice("Install method",
              "Copying keeps working if the original moves. Using it in place needs no space, but the file must stay "
              "where it is.",
              "install_method", "copy", {{"copy", "Copy into the game folder"}, {"in_place", "Use it where it is"}});
    ui.EndRows();
    ui.Spacer(4);
    if (!ui.TaskProgress(install)) {
      ImGui::BeginDisabled(picked.empty());
      if (ui.AccentButton(Installed() ? "Install this image instead" : "Install", ImVec2(300 * ui.scale(), 44 * ui.scale())))
        StartInstall(ui);
      ImGui::EndDisabled();
    }
  }

  void StartInstall(trg::Ui& ui) {
    if (settings.Get("install_method", "copy") == "in_place") {
      settings.Set("game_image", trg::AbsolutePath(picked));
      ui.SetStatus("Done. Press Play to start the game.");
      return;
    }
    const std::string dest = "game.img";
    const std::string src = picked;
    install.Start("Copying", [this, src, dest](trg::Task& t) {
      std::string err = trg::CopyFileJob(t, src, dest);
      if (err.empty()) settings.Set("game_image", trg::AbsolutePath(dest));
      return err;
    });
  }

  static void PageDisplay(trg::Ui& ui) {
    ui.Choice("Window mode", "Borderless fills the screen at your desktop resolution. F11 toggles it while playing.",
              "window_mode", "windowed",
              {{"windowed", "Windowed"}, {"borderless", "Borderless"}, {"fullscreen", "Exclusive"}});
    ui.Combo("Window size", "The size the window opens at.", "window_size", "auto",
             {{"auto", "Automatic"}, {"1280x720", "1280 \xC3\x97 720"}, {"1600x900", "1600 \xC3\x97 900"},
              {"1920x1080", "1920 \xC3\x97 1080"}, {"2560x1440", "2560 \xC3\x97 1440"}});
    ui.Choice("VSync", "Waits for the display before showing a frame, which stops tearing.", "vsync", "on",
              {{"off", "Off"}, {"on", "On"}, {"adaptive", "Adaptive"}});
    ui.Choice("Aspect ratio", "Keep the original shape with black bars, or stretch to fill the window.", "aspect",
              "keep", {{"keep", "Keep"}, {"stretch", "Stretch"}, {"integer", "Integer"}});
  }

  static void PageGraphics(trg::Ui& ui) {
    ui.Choice("Internal resolution", "Renders at a multiple of the original resolution.", "resolution", "2",
              {{"1", "1\xC3\x97 (original)"}, {"2", "2\xC3\x97"}, {"3", "3\xC3\x97"}, {"4", "4\xC3\x97"},
               {"6", "6\xC3\x97"}, {"8", "8\xC3\x97"}});
    ui.Choice("Anti-aliasing", "Smooths jagged edges.", "msaa", "4",
              {{"0", "Off"}, {"2", "2\xC3\x97"}, {"4", "4\xC3\x97"}, {"8", "8\xC3\x97"}});
    ui.Toggle("FXAA", "A fast post-process edge smoother.", "fxaa", false);
    ui.SliderInt("Sharpening", "Contrast-adaptive sharpening of the final picture.", "sharpen", 0, 0, 100, "%d%%", 5);
    ui.SliderFloat("Brightness", "1.00 is the original image.", "brightness", 1.0f, 0.5f, 2.0f);
  }

  static void PageGameplay(trg::Ui& ui) {
    ui.Choice("Frame rate", "30 is the original. Above it, PLAY asks first (see before_play in main).", "frame_rate",
              "30", {{"30", "30 fps (original)"}, {"60", "60 fps"}, {"120", "120 fps"}, {"0", "Unlimited"}});
    ui.Toggle("Skip intro movies", "Go straight to the title screen.", "skip_movies", false);
    ui.Toggle("Performance overlay", "Shows the frame rate. Toggle in game with F3.", "overlay", false, "Hidden",
              "Shown");
    ui.Combo("Language", nullptr, "language", "en",
             {{"en", "English"}, {"de", "Deutsch"}, {"es", "Espa\xC3\xB1ol"}, {"fr", "Fran\xC3\xA7" "ais"},
              {"it", "Italiano"}, {"ja", "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"}});
  }

  void PageControls(trg::Ui& ui) {
    ui.Toggle("Vibration", "Controller rumble.", "vibration", true);
    ui.SliderInt("Stick deadzone", "Ignores small stick movements. Raise it if the camera drifts.", "deadzone", 15, 0,
                 40, "%d%%");
    if (ui.Section("Keyboard bindings", true)) {
      if (ImGui::Button("Reset to defaults##keys"))
        for (const Binding& b : kBindings) settings.Set(b.key, b.def);
      for (const Binding& b : kBindings)
        if (auto key = ui.KeyBindRow(b.label, nullptr, settings.Get(b.key, b.def)))
          settings.Set(b.key, ImGui::GetKeyName(*key));
    }
  }

  void PageAchievements(trg::Ui& ui) {
    ui.Toggle("Notifications", "A pop-up when you unlock an achievement in game.", "achievement_toasts", true);
    const ImTextureID icon = ui.launcher().config().branding.icon;
    const std::vector<trg::AchievementCard> cards = {
        {icon, "First steps", "Finish the first level.", "", 10, true},
        {icon, "Collector", "Find every hidden coin.", "Some coins are well hidden.", 30, false},
        {icon, "Speed runner", "Finish the game in under two hours.", "", 50, false},
        {icon, "No damage", "Beat a boss without being hit.", "", 20, true},
    };
    int unlocked = 0, points = 0, total = 0;
    for (const auto& c : cards) {
      total += c.points;
      if (c.unlocked) ++unlocked, points += c.points;
    }
    ui.Spacer();
    ui.AchievementSummary(unlocked, int(cards.size()), points, total);
    ui.AchievementGrid(cards);
  }

  void PageAbout(trg::Ui& ui) {
    ui.Paragraph(
        "A demo of TRG Launcher, the shared launcher for TekRant Gaming PC ports. No game is included: each port "
        "runs from the player's own copy of the game.");
    ui.Spacer(4);
    ui.LauncherVisibilityRow();
    if (ui.Choice("Theme", "The colour presets that come with TRG Launcher.", "theme", "midnight",
                  {{"midnight", "Midnight"}, {"ocean", "Ocean"}, {"ember", "Ember"}}))
      ApplyTheme(ui.launcher());
    ui.FolderRow("Settings file", "Every launcher setting, as plain text.", "Open settings file",
                 trg::AbsolutePath(settings.path()));
    ui.ResetAllRow();
    ui.Info("Version", "TRG Launcher 1.0.0");
  }

  void ApplyTheme(trg::Launcher& l) {
    const std::string t = settings.Get("theme", "midnight");
    trg::LauncherConfig& c = l.config();
    c.branding.subtitle_color.reset();
    c.branding.title_shadow = IM_COL32(0, 0, 0, 150);
    c.branding.shade_title_side = true;
    c.branding.fade_into_page = true;
    if (t == "ocean") {
      c.theme = trg::Theme::Ocean();
      c.branding.background = trg::headers::SkyAndSea();
      c.branding.shade_title_side = false;
      c.branding.fade_into_page = false;
      c.branding.subtitle_color = ImVec4(1.0f, 0.88f, 0.47f, 1.0f);
      c.branding.title_shadow = IM_COL32(10, 40, 90, 140);
    } else if (t == "ember") {
      c.theme = trg::Theme::Ember();
      c.branding.background = trg::headers::Space({ImVec4(0.85f, 0.35f, 0.22f, 1.0f), 120});
    } else {
      c.theme = trg::Theme::Midnight();
      c.branding.background = trg::headers::Space();
    }
  }

  // A rounded-square icon drawn in code, so the demo needs no image files.
  void MakeIcon() {
    const int n = 128;
    icon_pixels.assign(size_t(n * n), 0);
    for (int y = 0; y < n; ++y)
      for (int x = 0; x < n; ++x) {
        const float fx = (x + 0.5f) / n - 0.5f, fy = (y + 0.5f) / n - 0.5f;
        const float d = std::max(std::abs(fx), std::abs(fy));
        const float ring = std::sqrt(fx * fx + fy * fy);
        uint8_t r = 30, g = 40, b = 90;
        if (ring < 0.30f) r = 240, g = 240, b = 255;
        if (ring < 0.22f) r = uint8_t(60 + 140 * (0.5f - fy)), g = 120, b = 230;
        const uint8_t a = d < 0.48f ? 255 : 0;
        icon_pixels[size_t(y * n + x)] = uint32_t(r) | uint32_t(g) << 8 | uint32_t(b) << 16 | uint32_t(a) << 24;
      }
  }
};

void WritePpm(const char* path, int w, int h) {
  using ReadPixelsFn = void(APIENTRY*)(int, int, int, int, unsigned, unsigned, void*);
  const auto read = reinterpret_cast<ReadPixelsFn>(SDL_GL_GetProcAddress("glReadPixels"));
  if (!read) return;
  // RGBA rows are always 4-byte aligned, so no GL_PACK_ALIGNMENT padding.
  std::vector<unsigned char> px(size_t(w) * size_t(h) * 4);
  read(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  FILE* f = std::fopen(path, "wb");
  if (!f) return;
  std::fprintf(f, "P6\n%d %d\n255\n", w, h);
  std::vector<unsigned char> row(size_t(w) * 3);
  for (int y = h - 1; y >= 0; --y) {
    const unsigned char* src = &px[size_t(y) * size_t(w) * 4];
    for (int x = 0; x < w; ++x)
      for (int c = 0; c < 3; ++c) row[size_t(x) * 3 + size_t(c)] = src[x * 4 + c];
    std::fwrite(row.data(), 1, row.size(), f);
  }
  std::fclose(f);
}

}  // namespace

int main(int argc, char** argv) {
  Demo demo;
  std::string theme, screenshot;
  int start_page = -1;
  bool show_prompt = false;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--theme") && i + 1 < argc) theme = argv[++i];
    else if (!std::strcmp(argv[i], "--page") && i + 1 < argc) start_page = std::atoi(argv[++i]);
    else if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) screenshot = argv[++i];
    else if (!std::strcmp(argv[i], "--ready")) demo.force_ready = true;
    else if (!std::strcmp(argv[i], "--prompt")) show_prompt = true;
  }
  if (!theme.empty()) demo.settings.Set("theme", theme);

  if (!trg::ShouldShowLauncher(demo.settings, "launcher", demo.Installed()) && screenshot.empty()) {
    std::puts("Launcher hidden (hold Shift to show it). Starting the game.");
    return 0;
  }

  trg::LauncherConfig config;
  config.branding.title = "DEMO QUEST";
  config.settings = &demo.settings;
  config.pages = {
      {"Install", "Point the launcher at your own copy of the game to install it.",
       [&](trg::Ui& ui) { demo.PageInstall(ui); }, [&] { return !demo.Installed(); }},
      {"Display", "Window, monitor and how the picture fits your screen.", Demo::PageDisplay},
      {"Graphics", "Resolution, anti-aliasing and the final picture.", Demo::PageGraphics},
      {"Gameplay", "Frame rate, movies, language and the overlay.", Demo::PageGameplay},
      {"Controls", "Controller options and keyboard bindings.", [&](trg::Ui& ui) { demo.PageControls(ui); }},
      {"Achievements", "What you have unlocked so far.", [&](trg::Ui& ui) { demo.PageAchievements(ui); }},
      {"About", "About this launcher, and where your settings live.", [&](trg::Ui& ui) { demo.PageAbout(ui); }},
  };
  // A pop-up before PLAY when a setting needs a word of warning (King Kong's
  // frame-rate warning). Its buttons can change the setting and then play.
  config.before_play = [&]() -> std::optional<trg::PlayPrompt> {
    const int fps = demo.settings.GetInt("frame_rate", 30);
    if (fps > 0 && fps <= 30) return std::nullopt;
    trg::PlayPrompt p;
    p.title = "Frame rate above 30 FPS";
    p.paragraphs = {"Demo Quest was made to run at 30 FPS. Above that, some animations can look wrong.",
                    "We recommend 30 FPS. 60 FPS also works well: the issues are there, but much less noticeable."};
    p.footnote = "This will be fixed in a future update.";
    p.buttons = {{"Play at 30 FPS", true, true, [&] { demo.settings.Set("frame_rate", "30"); }}};
    if (fps != 60) p.buttons.push_back({"Play at 60 FPS", false, true, [&] { demo.settings.Set("frame_rate", "60"); }});
    p.buttons.push_back({fps <= 0 ? "Keep unlimited" : "Keep " + std::to_string(fps) + " FPS", false, true, {}});
    return p;  // a "Back" button is added
  };
  // Settings the game only reads at startup: compare after PLAY.
  config.restart_keys = {"resolution"};
  config.start_page = start_page >= 0 ? start_page : demo.Installed() ? 1 : 0;
  config.can_play = [&] {
    if (demo.install.running()) return trg::PlayCheck{false, "Wait for the install to finish.", 0};
    if (!demo.Installed()) return trg::PlayCheck{false, "Install the game first: select your game image.", 0};
    return trg::PlayCheck{};
  };
  config.on_file_drop = [&](const std::string& path) {
    demo.picked = path;
  };

  trg::Launcher launcher(std::move(config));
  demo.ApplyTheme(launcher);
  if (start_page < 0 && !demo.Installed()) launcher.GoToPage(0);
  demo.MakeIcon();

  trg::WindowOptions window;
  window.title = "Demo Quest - Launcher";
  trg::StandaloneHooks hooks;
  hooks.on_start = [&](trg::Launcher& l) {
    l.config().branding.icon = trg::CreateTextureRGBA(128, 128, demo.icon_pixels.data());
  };
  hooks.on_stop = [&](trg::Launcher& l) { trg::DestroyTexture(l.config().branding.icon); };
  int frames = 0;
  if (!screenshot.empty())
    hooks.after_render = [&](trg::Launcher& l, int w, int h) {
      if (show_prompt && frames == 10) {  // press PLAY at 60 FPS to show the before_play pop-up
        demo.settings.Set("frame_rate", "60");
        l.RequestPlay();
      }
      if (++frames == 45) {
        WritePpm(screenshot.c_str(), w, h);
        l.RequestQuit();
      }
    };

  const trg::Result r = trg::RunStandalone(launcher, window, hooks);
  if (r == trg::Result::kPlay)
    std::puts(launcher.restart_needed() ? "PLAY pressed: the game would restart to apply the new resolution."
                                        : "PLAY pressed: the game would start now.");
  return 0;
}
