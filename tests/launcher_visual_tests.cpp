// Launcher visual contracts that need no window: the page order and icons, the sidebar's fit, the layout constants of the
// patched TRG Launcher and the places the launcher is wired to them. Nothing here compares pixels.
// Run: ssf2t_launcher_visual_tests

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "launcher_page_icons.h"
#include "launcher_pages.h"
#include "online_tabs.h"
#include "trg/layout.h"

#ifndef SSF2T_SOURCE_DIR
#error "SSF2T_SOURCE_DIR is required"
#endif
#ifndef SSF2T_TRG_PATCHED_DIR
#error "SSF2T_TRG_PATCHED_DIR is required"
#endif

namespace fs = std::filesystem;
namespace layout = trg::layout;
using namespace ssf2t;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

std::string ReadFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

std::string Source(const char* relative) { return ReadFile(fs::path(SSF2T_SOURCE_DIR) / relative); }

bool Has(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }

float Near(float a, float b, float tolerance = 0.01f) { return std::fabs(a - b) <= tolerance; }

constexpr float kScales[] = {1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.25f};

void TestPageOrderIsUnchanged() {
  std::printf("pages\n");
  const std::vector<std::string> expected = {"Play",         "Display",  "Graphics", "Gameplay",   "Online",
                                             "Achievements", "Controls", "Bug Report", "About"};
  CHECK(kLauncherPages.size() == expected.size());
  std::set<LauncherPage> pages;
  for (size_t i = 0; i < kLauncherPages.size() && i < expected.size(); ++i) {
    CHECK(expected[i] == kLauncherPages[i].name);
    CHECK(pages.insert(kLauncherPages[i].page).second);
    CHECK(&PageInfo(kLauncherPages[i].page) == &kLauncherPages[i]);
  }
  const std::string launcher = Source("src/launcher.cpp");
  size_t at = 0;
  for (const LauncherPageInfo& info : kLauncherPages) {
    const std::string needle = "MakePage(LauncherPage::k";
    const size_t found = launcher.find(needle, at);
    CHECK(found != std::string::npos);
    if (found == std::string::npos) break;
    const std::string kinds[] = {"Play", "Display", "Graphics", "Gameplay", "Online", "Achievements", "Controls", "BugReport", "About"};
    CHECK(launcher.compare(found + needle.size(), kinds[size_t(info.page)].size() + 1, kinds[size_t(info.page)] + ",") == 0);
    at = found + needle.size();
  }
  CHECK(Has(launcher, "result.icon = [this, page](int pixels)"));
}

void TestOnlineTabsAreUnchanged() {
  std::printf("online tabs\n");
  CHECK(std::size(kOnlineTabs) == 3);
  CHECK(std::string_view(OnlineTabLabel(kOnlineTabs[0])) == "Settings");
  CHECK(std::string_view(OnlineTabLabel(kOnlineTabs[1])) == "Friends");
  CHECK(std::string_view(OnlineTabLabel(kOnlineTabs[2])) == "Lobby");
}

void TestEveryPageHasAnIcon() {
  std::printf("page icons\n");
  const fs::path folder = fs::path(SSF2T_SOURCE_DIR) / "assets" / "launcher" / "icons";
  std::set<std::string> files;
  for (const LauncherPageInfo& info : kLauncherPages) {
    CHECK(info.icon_file != nullptr && std::string_view(info.icon_file).ends_with(".svg"));
    CHECK(info.icon_scale > 0.5f && info.icon_scale <= 1.0f);
    CHECK(files.insert(info.icon_file).second);
    CHECK(fs::is_regular_file(folder / info.icon_file));
  }
  std::set<std::string> on_disk;
  for (const fs::directory_entry& entry : fs::directory_iterator(folder)) {
    if (entry.path().extension() == ".svg") on_disk.insert(entry.path().filename().string());
  }
  CHECK(on_disk == files);
  CHECK(fs::is_regular_file(folder / "SOURCES.md"));

  const float optical_scales[] = {0.84f, 0.90f, 0.84f, 0.92f, 0.90f, 0.86f, 0.94f, 0.90f, 0.88f};
  const char* icon_files[] = {"play.svg",   "display.svg",  "graphics.svg",   "gameplay.svg", "online.svg",
                              "trophies.svg", "controls.svg", "bug-report.svg", "about.svg"};
  for (size_t i = 0; i < kLauncherPages.size(); ++i) {
    CHECK(kLauncherPages[i].icon_scale == optical_scales[i]);
    CHECK(std::string_view(kLauncherPages[i].icon_file) == icon_files[i]);
  }

  // 21 logical pixels at 100%, 150% and 225% display scale on a framebuffer of the same scale.
  for (const LauncherPageInfo& info : kLauncherPages) {
    for (const int pixels : {21, 32, 47}) {
      const std::vector<uint8_t> rgba = RenderPageIcon(info.page, pixels);
      CHECK(rgba.size() == size_t(pixels) * size_t(pixels) * 4);
      if (rgba.size() != size_t(pixels) * size_t(pixels) * 4) continue;
      int covered = 0, strong = 0, min_x = pixels, max_x = -1, clipped = 0;
      bool white = true;
      for (int y = 0; y < pixels; ++y) {
        for (int x = 0; x < pixels; ++x) {
          const uint8_t* texel = &rgba[(size_t(y) * size_t(pixels) + size_t(x)) * 4];
          const bool edge = x == 0 || y == 0 || x == pixels - 1 || y == pixels - 1;
          clipped += edge && texel[3] == 255;
          white = white && texel[0] == 255 && texel[1] == 255 && texel[2] == 255;
          if (texel[3] > 0) {
            ++covered;
            min_x = std::min(min_x, x);
            max_x = std::max(max_x, x);
          }
          if (texel[3] > 100) ++strong;
        }
      }
      CHECK(white);
      CHECK(clipped == 0);
      CHECK(covered * 100 >= pixels * pixels * 4);
      CHECK(strong > 0);
      CHECK(min_x >= 0 && max_x < pixels);
      CHECK(std::abs((min_x + max_x) - (pixels - 1)) <= std::max(2, pixels / 6));
    }
  }
  CHECK(RenderPageIcon(LauncherPage::kPlay, 0).empty());
  CHECK(RenderPageIcon(LauncherPage::kPlay, 2048).empty());
  CHECK(RasterizeIcon(nullptr, 10, 0.9f, 21).empty());
  const char garbage[] = "not an svg";
  CHECK(RasterizeIcon(reinterpret_cast<const uint8_t*>(garbage), sizeof garbage, 0.9f, 21).empty());
}

void TestLayoutConstantsAreSane() {
  std::printf("layout constants\n");
  CHECK(layout::kTextPage > layout::kTextSection && layout::kTextSection > layout::kTextLabel);
  CHECK(layout::kTextLabel > layout::kTextBody && layout::kTextBody > layout::kTextHelp);
  CHECK(layout::kTextSidebar >= layout::kTextBody && layout::kTextFooter <= layout::kTextBody);
  CHECK(layout::kTextBody == 16.0f && layout::kControlHeight >= layout::kTextBody + 12.0f);
  CHECK(layout::kSidebarRowMin <= layout::kSidebarRow && layout::kSidebarIcon + 8.0f <= layout::kSidebarRowMin);
  CHECK(layout::SidebarLabelX() > layout::kSidebarIconInset + layout::kSidebarIcon);
  CHECK(layout::kFooterButtonHeight <= layout::kFooterHeight && layout::kPlayHeight <= layout::kFooterHeight);
  CHECK(layout::kKeycapHeight < layout::kFooterHeight && layout::kPlayHeight >= layout::kFooterButtonHeight);
  CHECK(layout::kPlayWidth > layout::kFooterButtonWidth);
  CHECK(layout::kContentPadding > 0.0f && layout::kContentTop > 0.0f && layout::kMargin > 0.0f);
  for (const float scale : kScales) {
    CHECK(layout::SidebarWidth(1280.0f * scale, scale) >= 168.0f * scale);
    CHECK(layout::SidebarWidth(4000.0f * scale, scale) <= 176.0f * scale);
    CHECK(layout::HeaderHeight(720.0f * scale, scale) >= 88.0f * scale);
    CHECK(layout::HeaderHeight(2160.0f * scale, scale) <= 140.0f * scale);
    CHECK(layout::ControlColumnWidth(1000.0f * scale, scale) >= 280.0f * scale);
    CHECK(layout::ControlColumnWidth(4000.0f * scale, scale) <= 360.0f * scale);
    const float line = layout::LineThickness(scale);
    CHECK(line >= 1.0f && line == std::floor(line));
    CHECK(layout::PassiveLineThickness(scale) >= 1.0f && layout::PassiveLineThickness(scale) <= line);
    CHECK(layout::PanelBorderWidth(scale) == line + 1.0f);
  }
  CHECK(layout::LineThickness(1.0f) == 1.0f && layout::LineThickness(1.5f) == 2.0f && layout::LineThickness(2.25f) == 2.0f);
  CHECK(layout::StableContentWidth(900.0f, 10.0f) == 890.0f);
}

// The sidebar's body is the window less the header, the gap and the footer; pages share it without a scrollbar.
float BodyHeight(float window_height, float scale) {
  const float header = layout::HeaderHeight(window_height, scale);
  return window_height - header - layout::kBodyGap * scale - layout::kFooterHeight * scale;
}

void TestSidebarFitsEveryPage() {
  std::printf("sidebar fit\n");
  const int pages = int(kLauncherPages.size());
  CHECK(pages == 9);
  const float full = layout::SidebarHeight(pages);
  const float tightest = layout::SidebarHeight(pages, layout::kSidebarRowMin);
  CHECK(tightest < full);
  for (float body = std::ceil(tightest); body <= 1600.0f; body += 1.0f) {
    const float row = layout::SidebarRowFor(pages, body);
    CHECK(row >= layout::kSidebarRowMin && row <= layout::kSidebarRow);
    CHECK(layout::SidebarHeight(pages, row) <= body);
    CHECK(row == std::floor(row));
    if (body >= full) CHECK(row == layout::kSidebarRow);
  }
  float previous = 0.0f;
  for (float body = 0.0f; body <= 1600.0f; body += 1.0f) {
    const float row = layout::SidebarRowFor(pages, body);
    CHECK(row >= previous);
    previous = row;
  }
  // The launcher's own window sizes: 1280x720 and down to the height where the rows shrink.
  CHECK(BodyHeight(720.0f, 1.0f) >= full);
  CHECK(BodyHeight(480.0f, 1.0f) >= tightest);
  for (const float scale : kScales) {
    for (const float height : {720.0f, 900.0f, 1080.0f}) {
      const float physical = height * scale;
      const float logical = BodyHeight(physical, scale) / scale;
      CHECK(Near(logical, BodyHeight(height, 1.0f)));
      const float row = layout::SidebarRowFor(pages, logical);
      CHECK(layout::SidebarHeight(pages, row) <= logical);
    }
  }
  // Room for a few more pages before the rows would have to shrink below their minimum.
  CHECK(layout::SidebarHeight(12, layout::kSidebarRowMin) <= BodyHeight(720.0f, 1.0f));
}

void TestFooterShortcutsAreUnchanged() {
  std::printf("footer\n");
  const std::vector<layout::KeyHint> hints = layout::ParseKeyHints("Enter  Play     Alt+F4  Quit     Ctrl+S  Save");
  CHECK(hints.size() == 3);
  if (hints.size() == 3) {
    CHECK(hints[0].key == "Enter" && hints[0].action == "Play");
    CHECK(hints[1].key == "Alt+F4" && hints[1].action == "Quit");
    CHECK(hints[2].key == "Ctrl+S" && hints[2].action == "Save");
  }
  const std::vector<std::string_view> lines =
      layout::SplitSentences("Play applies the changes. Save keeps them without starting.");
  CHECK(lines.size() == 2 && lines[0] == "Play applies the changes." && lines[1] == "Save keeps them without starting.");
  const std::string launcher = Source("src/launcher.cpp");
  CHECK(Has(launcher, "c.hints = \"Enter  Play     Alt+F4  Quit     Ctrl+S  Save\";"));
  CHECK(Has(launcher, "c.note = \"Play applies the changes. Save keeps them without starting.\";"));
  CHECK(Has(launcher, "c.save_on_play = false;"));
  CHECK(Has(launcher, "c.sponsor = {\"Sponsor on GitHub\", kSponsorUrl,"));
  CHECK(Has(launcher, "constexpr char kSponsorUrl[] = \"https://github.com/sponsors/saulob\";"));
  CHECK(Has(launcher, "c.fonts.size = trg::layout::kTextBody;"));
  CHECK(Has(launcher, "const ImVec4 kAccent = ImVec4(1.00f, 0.52f, 0.08f, 1.0f);"));
}

void TestPatchedLauncherCarriesTheVisualSystem() {
  std::printf("patched TRG Launcher\n");
  const fs::path patched = fs::path(SSF2T_TRG_PATCHED_DIR) / "src" / "launcher.cpp";
  const std::string trg = ReadFile(patched);
  CHECK(!trg.empty());
  for (const char* marker :
       {"SidebarRowFor(", "layout::SidebarWidth(", "OutlinePanel(", "page.icon ? page.icon(icon_pixels)",
        "DisplayFramebufferScale", "DrawKeyHints(", "ParseKeyHints(", "StableContentWidth(", "SetNextWindowContentSize(",
        "DrawRowLines(", "PanelBorderWidth(", "RowEmphasized(", "InfoGrid(", "fits_note", "T(\"Quit\")", "T(\"Save\")",
        "DrawPlayButton(", "ImGuiKey_S, false"}) {
    CHECK(Has(trg, marker));
  }
  const fs::path patches = fs::path(SSF2T_SOURCE_DIR) / "patches" / "trg-launcher";
  std::vector<std::string> names;
  for (const fs::directory_entry& entry : fs::directory_iterator(patches)) names.push_back(entry.path().filename().string());
  std::sort(names.begin(), names.end());
  CHECK(names.size() >= 15);
  CHECK(Has(trg, "host_pixel_ratio_ > 0.0f ? host_pixel_ratio_ : ImGui::GetIO().DisplayFramebufferScale.x"));
  CHECK(Has(trg, "const float framebuffer = pixel_ratio_;"));
  CHECK(Has(trg, "int(layout::kSidebarIcon * s_ * framebuffer + 0.5f)"));
  const std::string launcher = Source("src/launcher.cpp");
  CHECK(Has(launcher, "launcher_->SetPixelRatio(g_font_density);"));
  CHECK(Has(launcher, "page_icons_.try_emplace({page, pixels})"));
  CHECK(Has(launcher, "RenderPageIcon(page, pixels)"));
  for (size_t i = 0; i < names.size(); ++i) {
    char prefix[8];
    std::snprintf(prefix, sizeof prefix, "%04zu-", i + 1);
    CHECK(names[i].rfind(prefix, 0) == 0);
  }
}

void TestNoForeignIdentifiers() {
  std::printf("identifiers\n");
  const fs::path root = SSF2T_SOURCE_DIR;
  std::vector<fs::path> files = {root / "src/launcher.cpp",           root / "src/launcher_pages.h",
                                 root / "src/launcher_page_icons.h",  root / "src/launcher_page_icons.cpp",
                                 root / "src/online_page.cpp",        root / "src/bug_report_page.cpp",
                                 root / "src/bug_report_page.h",      root / "cmake/embed_files.cmake",
                                 root / "assets/launcher/icons/SOURCES.md"};
  for (const fs::directory_entry& entry : fs::directory_iterator(root / "patches" / "trg-launcher")) {
    files.push_back(entry.path());
  }
  for (const fs::path& file : files) {
    const std::string text = ReadFile(file);
    CHECK(!text.empty());
    for (const char* foreign : {"Dragon", "dragon", "DCP", "dcp_", "Dcp", "dcpgpu"}) {
      CHECK(!Has(text, foreign));
    }
  }
}

void TestIconAttributionShips() {
  std::printf("icon attribution\n");
  const std::string sources = Source("assets/launcher/icons/SOURCES.md");
  CHECK(Has(sources, "display.svg") && Has(sources, "nickylimyeanfen") && Has(sources, "Creative Commons"));
  CHECK(Has(sources, "bug-report.svg") && Has(sources, "Vectopus"));
  const std::string licenses = Source("packaging/licenses/THIRD-PARTY-LICENSES.txt");
  CHECK(Has(licenses, "Launcher page icons") && Has(licenses, "nickylimyeanfen") && Has(licenses, "Vectopus"));
  CHECK(Has(licenses, "nanosvg") && Has(licenses, "Mikko Mononen"));
  CHECK(fs::is_regular_file(fs::path(SSF2T_SOURCE_DIR) / "third_party/nanosvg/LICENSE.txt"));
}

}  // namespace

int main() {
  TestPageOrderIsUnchanged();
  TestOnlineTabsAreUnchanged();
  TestEveryPageHasAnIcon();
  TestLayoutConstantsAreSane();
  TestSidebarFitsEveryPage();
  TestFooterShortcutsAreUnchanged();
  TestPatchedLauncherCarriesTheVisualSystem();
  TestNoForeignIdentifiers();
  TestIconAttributionShips();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
