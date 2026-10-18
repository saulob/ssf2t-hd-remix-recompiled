#include "trg/launcher.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "trg/platform.h"

namespace trg {
namespace {

ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t) {
  return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

void ApplyTheme(const Theme& t, float s) {
  ImGuiStyle& st = ImGui::GetStyle();
  st.WindowPadding = ImVec2(14 * s, 12 * s);  // popups and combos; the launcher's own windows use none
  st.WindowBorderSize = 0;
  st.ChildBorderSize = 0;
  st.PopupBorderSize = 0;
  st.FrameBorderSize = 0;
  st.WindowRounding = 0;
  st.ChildRounding = 12 * s;
  st.FrameRounding = 7 * s;
  st.PopupRounding = 8 * s;
  st.GrabRounding = 7 * s;
  st.TabRounding = 7 * s;
  st.ScrollbarRounding = 8 * s;
  st.ScrollbarSize = 10 * s;
  st.FramePadding = ImVec2(12 * s, 8 * s);
  st.ItemSpacing = ImVec2(10 * s, 10 * s);
  st.ItemInnerSpacing = ImVec2(8 * s, 6 * s);
  st.CellPadding = ImVec2(0, 12 * s);
  st.GrabMinSize = 14 * s;
  st.SelectableTextAlign = ImVec2(0, 0.5f);
  ImVec4* c = st.Colors;
  c[ImGuiCol_Text] = t.text;
  c[ImGuiCol_TextDisabled] = t.dim;
  c[ImGuiCol_WindowBg] = t.bg;
  c[ImGuiCol_ChildBg] = t.panel;
  c[ImGuiCol_PopupBg] = t.popup;
  c[ImGuiCol_FrameBg] = t.frame;
  c[ImGuiCol_FrameBgHovered] = t.frame_hot;
  c[ImGuiCol_FrameBgActive] = t.frame_active;
  c[ImGuiCol_Button] = t.frame;
  c[ImGuiCol_ButtonHovered] = t.frame_hot;
  c[ImGuiCol_ButtonActive] = t.frame_active;
  c[ImGuiCol_Header] = t.frame;
  c[ImGuiCol_HeaderHovered] = t.frame_hot;
  c[ImGuiCol_HeaderActive] = t.frame_active;
  c[ImGuiCol_SliderGrab] = t.accent;
  c[ImGuiCol_SliderGrabActive] = t.accent_hot;
  c[ImGuiCol_CheckMark] = t.accent;
  c[ImGuiCol_PlotHistogram] = t.accent;
  c[ImGuiCol_PlotHistogramHovered] = t.accent_hot;
  c[ImGuiCol_TextSelectedBg] = ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.35f);
  c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ScrollbarGrab] = t.frame_hot;
  c[ImGuiCol_ScrollbarGrabHovered] = t.frame_active;
  c[ImGuiCol_ScrollbarGrabActive] = t.frame_active;
  c[ImGuiCol_Separator] = ImVec4(1, 1, 1, 0.07f);
  c[ImGuiCol_TableBorderLight] = ImVec4(1, 1, 1, 0.06f);
  c[ImGuiCol_TableBorderStrong] = ImVec4(1, 1, 1, 0.08f);
  c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_TableRowBgAlt] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_NavCursor] = t.accent;
  c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.6f);
}

ImFont* LoadFirstFont(std::initializer_list<std::string> paths, float size) {
  for (const std::string& p : paths) {
    FILE* f = OpenFile(p, "rb");
    if (!f) continue;
    fclose(f);
    if (ImFont* font = ImGui::GetIO().Fonts->AddFontFromFileTTF(p.c_str(), size)) return font;
  }
  return nullptr;
}

void PushAccent(const Theme& t) {
  ImGui::PushStyleColor(ImGuiCol_Button, t.accent);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.accent_hot);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.accent_hot);
  ImGui::PushStyleColor(ImGuiCol_Text, t.on_accent);
}

}  // namespace

// ------------------------------------------------------------------- theme --
ImU32 Col(const ImVec4& c, float alpha_mul) {
  return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * alpha_mul));
}

Theme Theme::Midnight() {
  Theme t;
  t.bg = ImVec4(0.027f, 0.039f, 0.086f, 1.0f);
  t.panel = ImVec4(0.055f, 0.082f, 0.165f, 1.0f);
  t.frame = ImVec4(0.090f, 0.129f, 0.247f, 1.0f);
  t.frame_hot = ImVec4(0.125f, 0.176f, 0.325f, 1.0f);
  t.frame_active = ImVec4(0.153f, 0.212f, 0.384f, 1.0f);
  t.popup = ImVec4(0.075f, 0.110f, 0.212f, 0.99f);
  t.accent = ImVec4(0.545f, 0.835f, 0.314f, 1.0f);
  t.accent_hot = ImVec4(0.651f, 0.910f, 0.416f, 1.0f);
  t.on_accent = ImVec4(0.035f, 0.090f, 0.031f, 1.0f);
  t.text = ImVec4(0.95f, 0.97f, 1.0f, 1.0f);
  t.dim = ImVec4(0.580f, 0.659f, 0.800f, 1.0f);
  t.good = ImVec4(0.118f, 0.420f, 0.239f, 1.0f);
  t.warn = ImVec4(0.490f, 0.318f, 0.090f, 1.0f);
  t.good_dot = ImVec4(0.55f, 0.95f, 0.70f, 1.0f);
  t.warn_dot = ImVec4(1.0f, 0.70f, 0.30f, 1.0f);
  return t;
}

Theme Theme::Ocean() {
  Theme t;
  t.bg = ImVec4(0.035f, 0.100f, 0.200f, 1.0f);
  t.panel = ImVec4(0.070f, 0.170f, 0.320f, 1.0f);
  t.frame = ImVec4(0.120f, 0.270f, 0.490f, 1.0f);
  t.frame_hot = ImVec4(0.180f, 0.370f, 0.640f, 1.0f);
  t.frame_active = ImVec4(0.220f, 0.430f, 0.720f, 1.0f);
  t.popup = ImVec4(0.080f, 0.190f, 0.360f, 0.98f);
  t.accent = ImVec4(1.000f, 0.790f, 0.240f, 1.0f);
  t.accent_hot = ImVec4(1.000f, 0.620f, 0.200f, 1.0f);
  t.on_accent = ImVec4(0.080f, 0.120f, 0.220f, 1.0f);
  t.text = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
  t.dim = ImVec4(0.660f, 0.760f, 0.910f, 1.0f);
  t.good = ImVec4(0.110f, 0.431f, 0.282f, 1.0f);
  t.warn = ImVec4(0.588f, 0.322f, 0.078f, 1.0f);
  t.good_dot = ImVec4(0.47f, 0.90f, 0.63f, 1.0f);
  t.warn_dot = ImVec4(1.0f, 0.78f, 0.35f, 1.0f);
  return t;
}

Theme Theme::Ember() {
  Theme t;
  t.bg = ImVec4(0.055f, 0.047f, 0.051f, 1.0f);
  t.panel = ImVec4(0.098f, 0.082f, 0.086f, 1.0f);
  t.frame = ImVec4(0.157f, 0.129f, 0.133f, 1.0f);
  t.frame_hot = ImVec4(0.212f, 0.173f, 0.176f, 1.0f);
  t.frame_active = ImVec4(0.255f, 0.204f, 0.208f, 1.0f);
  t.popup = ImVec4(0.120f, 0.100f, 0.104f, 0.99f);
  t.accent = ImVec4(0.910f, 0.300f, 0.240f, 1.0f);
  t.accent_hot = ImVec4(0.970f, 0.420f, 0.340f, 1.0f);
  t.on_accent = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
  t.text = ImVec4(0.97f, 0.95f, 0.94f, 1.0f);
  t.dim = ImVec4(0.700f, 0.630f, 0.620f, 1.0f);
  t.good = ImVec4(0.118f, 0.400f, 0.239f, 1.0f);
  t.warn = ImVec4(0.520f, 0.300f, 0.080f, 1.0f);
  t.good_dot = ImVec4(0.55f, 0.95f, 0.70f, 1.0f);
  t.warn_dot = ImVec4(1.0f, 0.70f, 0.30f, 1.0f);
  return t;
}

Theme Theme::WithAccent(ImVec4 colour) const {
  Theme t = *this;
  t.accent = colour;
  t.accent_hot = Mix(colour, ImVec4(1, 1, 1, 1), 0.18f);
  const float luminance = 0.2126f * colour.x + 0.7152f * colour.y + 0.0722f * colour.z;
  t.on_accent = luminance > 0.45f ? Mix(colour, ImVec4(0, 0, 0, 1), 0.88f) : ImVec4(1, 1, 1, 1);
  return t;
}

// ------------------------------------------------------------------- fonts --
Fonts LoadSystemFonts(float size) {
  Fonts f;
  f.size = size;
#if defined(_WIN32)
  const char* windir = std::getenv("WINDIR");
  const std::string dir = std::string(windir ? windir : "C:\\Windows") + "\\Fonts\\";
  f.regular = LoadFirstFont({dir + "segoeui.ttf"}, size);
  f.semibold = LoadFirstFont({dir + "seguisb.ttf"}, size);
  f.bold = LoadFirstFont({dir + "segoeuib.ttf"}, size);
#elif defined(__APPLE__)
  f.regular = LoadFirstFont({"/System/Library/Fonts/Supplemental/Arial.ttf", "/Library/Fonts/Arial.ttf"}, size);
  f.bold = LoadFirstFont({"/System/Library/Fonts/Supplemental/Arial Bold.ttf", "/Library/Fonts/Arial Bold.ttf"}, size);
#else
  f.regular = LoadFirstFont({"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
                             "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf"},
                            size);
  f.bold = LoadFirstFont({"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                          "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
                          "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf"},
                         size);
#endif
  if (!f.regular) f.regular = ImGui::GetIO().Fonts->AddFontDefault();
  if (!f.bold) f.bold = f.regular;
  if (!f.semibold) f.semibold = f.bold;
  ImGui::GetIO().FontDefault = f.regular;
  return f;
}

// ---------------------------------------------------------------------- ui --
const Theme& Ui::theme() const { return launcher_->config_.theme; }
const Fonts& Ui::fonts() const { return launcher_->config_.fonts; }
SettingsStore& Ui::settings() { return *launcher_->config_.settings; }
void Ui::SetStatus(std::string text, double seconds) { launcher_->SetStatus(std::move(text), seconds); }

void Ui::BeginPage() {
  in_rows_ = false;
  rows_id_ = 0;
}

void Ui::EndPage() { LeaveRows(); }

void Ui::Row(const char* label, const char* help) {
  if (!in_rows_) {
    char id[32];
    std::snprintf(id, sizeof id, "##rows%d", rows_id_++);
    in_rows_ = ImGui::BeginTable(id, 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp);
    if (in_rows_) {
      ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 0.46f);
      ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthStretch, 0.54f);
    }
  }
  if (in_rows_) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
  }
  if (label && *label) {
    ImGui::PushFont(fonts().semibold, 0.0f);
    ImGui::TextUnformatted(label);
    ImGui::PopFont();
  }
  if (help && *help) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme().dim);
    ImGui::PushFont(nullptr, fonts().size * 0.84f);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 24 * scale_);
    ImGui::TextUnformatted(help);
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    ImGui::PopStyleColor();
  }
  if (in_rows_) ImGui::TableSetColumnIndex(1);
  ImGui::SetNextItemWidth(-FLT_MIN);
}

void Ui::EndRows() {
  if (!in_rows_) return;
  ImGui::EndTable();
  in_rows_ = false;
}

int Ui::Segmented(const char* id, const std::vector<std::string>& labels, int selected) {
  if (labels.empty()) return -1;
  ImGui::PushID(id);
  const float avail = ImGui::GetContentRegionAvail().x;
  const float gap = 6 * scale_;
  const float bw = (avail - gap * float(labels.size() - 1)) / float(labels.size());
  int clicked = -1;
  for (size_t i = 0; i < labels.size(); ++i) {
    if (i) ImGui::SameLine(0, gap);
    const bool sel = int(i) == selected;
    if (sel) PushAccent(theme());
    ImGui::PushID(int(i));
    if (ImGui::Button(labels[i].c_str(), ImVec2(bw, 0)) && !sel) clicked = int(i);
    ImGui::PopID();
    if (sel) ImGui::PopStyleColor(4);
  }
  ImGui::PopID();
  return clicked;
}

bool Ui::AccentButton(const char* label, ImVec2 size) {
  PushAccent(theme());
  const bool r = ImGui::Button(label, size);
  ImGui::PopStyleColor(4);
  return r;
}

bool Ui::Choice(const char* label, const char* help, const char* key, const char* def,
                const std::vector<Option>& options, int max_buttons) {
  if (int(options.size()) > max_buttons) return Combo(label, help, key, def, options);
  Row(label, help);
  const std::string cur = settings().Get(key, def);
  std::vector<std::string> labels;
  int sel = -1;
  for (size_t i = 0; i < options.size(); ++i) {
    labels.push_back(options[i].label);
    if (options[i].value == cur) sel = int(i);
  }
  if (const int i = Segmented(key, labels, sel); i >= 0) {
    settings().Set(key, options[size_t(i)].value);
    return true;
  }
  return false;
}

bool Ui::Combo(const char* label, const char* help, const char* key, const char* def,
               const std::vector<Option>& options) {
  Row(label, help);
  const std::string cur = settings().Get(key, def);
  std::string preview = cur;
  for (const Option& o : options)
    if (o.value == cur) preview = o.label;
  bool changed = false;
  ImGui::PushID(key);
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (ImGui::BeginCombo("##combo", preview.c_str(), ImGuiComboFlags_HeightLarge)) {
    for (const Option& o : options) {
      const bool on = o.value == cur;
      if (ImGui::Selectable(o.label.c_str(), on) && !on) {
        settings().Set(key, o.value);
        changed = true;
      }
      if (on) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::PopID();
  return changed;
}

bool Ui::Toggle(const char* label, const char* help, const char* key, bool def, const char* off, const char* on) {
  Row(label, help);
  const bool v = settings().GetBool(key, def);
  if (const int i = Segmented(key, {off, on}, v ? 1 : 0); i >= 0) {
    settings().SetBool(key, i == 1);
    return true;
  }
  return false;
}

bool Ui::SliderInt(const char* label, const char* help, const char* key, int def, int lo, int hi, const char* format,
                   int step) {
  Row(label, help);
  int v = settings().GetInt(key, def);
  ImGui::PushID(key);
  ImGui::SetNextItemWidth(-FLT_MIN);
  bool changed = false;
  if (ImGui::SliderInt("##slider", &v, lo, hi, format, ImGuiSliderFlags_AlwaysClamp)) {
    if (step > 1) v = std::clamp(lo + (v - lo + step / 2) / step * step, lo, hi);
    settings().SetInt(key, v);
    changed = true;
  }
  ImGui::PopID();
  return changed;
}

bool Ui::SliderFloat(const char* label, const char* help, const char* key, float def, float lo, float hi,
                     const char* format) {
  Row(label, help);
  float v = settings().GetFloat(key, def);
  ImGui::PushID(key);
  ImGui::SetNextItemWidth(-FLT_MIN);
  bool changed = false;
  if (ImGui::SliderFloat("##slider", &v, lo, hi, format, ImGuiSliderFlags_AlwaysClamp)) {
    settings().SetFloat(key, v);
    changed = true;
  }
  ImGui::PopID();
  return changed;
}

bool Ui::InputText(const char* label, const char* help, const char* key, const char* hint) {
  Row(label, help);
  char buf[2048];
  std::snprintf(buf, sizeof buf, "%s", settings().Get(key).c_str());
  ImGui::PushID(key);
  ImGui::SetNextItemWidth(-FLT_MIN);
  const bool changed = ImGui::InputTextWithHint("##text", hint, buf, sizeof buf);
  if (changed) settings().Set(key, buf);
  ImGui::PopID();
  return changed;
}

void Ui::Info(const char* label, const char* text) {
  Row(label, nullptr);
  ImGui::PushStyleColor(ImGuiCol_Text, theme().dim);
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(text);
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
}

bool Ui::ButtonRow(const char* label, const char* help, const char* button, bool accent) {
  Row(label, help);
  ImGui::PushID(label);
  const bool r = accent ? AccentButton(button) : ImGui::Button(button, ImVec2(-FLT_MIN, 0));
  ImGui::PopID();
  return r;
}

bool Ui::ConfirmRow(const char* label, const char* help, const char* button, const char* question,
                    const char* confirm, const char* cancel) {
  Row(label, help);
  ImGui::PushID(label);
  if (ImGui::Button(button, ImVec2(-FLT_MIN, 0))) ImGui::OpenPopup("##confirm");
  bool confirmed = false;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24 * scale_, 20 * scale_));
  const bool open = ImGui::BeginPopupModal("##confirm", nullptr,
                                           ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                                               ImGuiWindowFlags_NoSavedSettings);
  ImGui::PopStyleVar();
  if (open) {
    ImGui::PushFont(fonts().semibold, 0.0f);
    ImGui::TextUnformatted(question);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(320 * scale_, 6 * scale_));
    if (AccentButton(confirm, ImVec2(150 * scale_, 0))) {
      confirmed = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(cancel, ImVec2(150 * scale_, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  ImGui::PopID();
  return confirmed;
}

void Ui::FolderRow(const char* label, const char* help, const char* button, const std::string& path) {
  if (ButtonRow(label, help, button)) OpenInFileManager(path);
}

bool Ui::LauncherVisibilityRow(const char* key) {
  return Toggle("Show this launcher", "Off starts the game directly. Hold Shift while starting to bring it back.",
                key, true, "Off", "At startup");
}

void Ui::ResetAllRow() {
  if (ConfirmRow("Reset settings", "Put every setting back to its default.", "Reset all settings",
                 "Put every setting back to its default?", "Reset")) {
    settings().ResetToDefaults();
    if (launcher_->config_.on_reset) launcher_->config_.on_reset();
    SetStatus("Settings reset to defaults.");
  }
}

std::optional<ImGuiKey> Ui::KeyCapture(const char* id, const std::string& shown, float width) {
  ImGui::PushID(id);
  std::optional<ImGuiKey> result;
  if (capturing_ == id) {
    if (AccentButton("Press a key...  (Esc cancels)", ImVec2(width, 0))) capturing_.clear();
    // Wait a frame so the click or Enter that started the capture is not taken.
    if (!capturing_.empty() && ImGui::GetFrameCount() > capture_frame_) {
      if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        capturing_.clear();
      } else {
        for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_GamepadStart; ++k) {
          if (!ImGui::IsKeyPressed(ImGuiKey(k), false)) continue;
          result = ImGuiKey(k);
          capturing_.clear();
          break;
        }
      }
    }
  } else if (ImGui::Button(shown.empty() ? "(none)" : shown.c_str(), ImVec2(width, 0))) {
    capturing_ = id;
    capture_frame_ = ImGui::GetFrameCount();
  }
  ImGui::PopID();
  return result;
}

std::optional<ImGuiKey> Ui::KeyBindRow(const char* label, const char* help, const std::string& shown) {
  Row(label, help);
  return KeyCapture(label, shown);
}

void Ui::StatusCard(Status status, const char* title, const char* detail, float progress) {
  LeaveRows();
  const Theme& t = theme();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float w = ImGui::GetContentRegionAvail().x, h = 76 * scale_;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec4 fill = status == Status::kReady ? t.good : status == Status::kBusy ? t.frame : t.warn;
  dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Col(fill), 12 * scale_);
  const ImVec2 dot(p.x + 34 * scale_, p.y + h * 0.5f);
  if (status == Status::kBusy) {
    const float a = float(ImGui::GetTime()) * 5.0f;
    dl->PathArcTo(dot, 11 * scale_, a, a + 4.2f, 24);
    dl->PathStroke(Col(t.accent), 0, 3.5f * scale_);
  } else {
    dl->AddCircleFilled(dot, 12 * scale_, Col(status == Status::kReady ? t.good_dot : t.warn_dot), 24);
  }
  ImGui::SetCursorScreenPos(ImVec2(p.x + 64 * scale_, p.y + 14 * scale_));
  ImGui::BeginGroup();
  ImGui::PushFont(fonts().semibold, 0.0f);
  ImGui::TextUnformatted(title);
  ImGui::PopFont();
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.88f, 0.92f, 0.96f, 1));
  if (progress >= 0.0f) {
    ImGui::ProgressBar(progress, ImVec2(w - 100 * scale_, 6 * scale_), "");
  } else if (detail && *detail) {
    ImGui::PushTextWrapPos(p.x + w - 16 * scale_ - ImGui::GetWindowPos().x);
    ImGui::TextUnformatted(detail);
    ImGui::PopTextWrapPos();
  }
  ImGui::PopStyleColor();
  ImGui::EndGroup();
  ImGui::SetCursorScreenPos(ImVec2(p.x, std::max(p.y + h, ImGui::GetItemRectMax().y + 10 * scale_) + 14 * scale_));
  ImGui::Dummy(ImVec2(0, 0));
}

bool Ui::TaskProgress(Task& task) {
  if (!task.running()) return false;
  const float f = task.fraction();
  char label[160];
  if (f >= 0.0f)
    std::snprintf(label, sizeof label, "%s %.0f%%", task.label().c_str(), double(f) * 100.0);
  else
    std::snprintf(label, sizeof label, "%s", task.label().c_str());
  const float cancel_w = 120 * scale_;
  ImGui::ProgressBar(f >= 0.0f ? f : -1.0f * float(ImGui::GetTime()),
                     ImVec2(ImGui::GetContentRegionAvail().x - cancel_w - ImGui::GetStyle().ItemSpacing.x, 0), label);
  ImGui::SameLine();
  ImGui::BeginDisabled(task.cancelled());
  if (ImGui::Button("Cancel##task", ImVec2(-FLT_MIN, 0))) task.Cancel();
  ImGui::EndDisabled();
  return true;
}

void Ui::Heading(const char* text) {
  LeaveRows();
  ImGui::Dummy(ImVec2(0, 4 * scale_));
  ImGui::PushFont(fonts().semibold, fonts().size * 1.1f);
  ImGui::TextUnformatted(text);
  ImGui::PopFont();
}

void Ui::Paragraph(const char* text) {
  LeaveRows();
  ImGui::PushStyleColor(ImGuiCol_Text, theme().dim);
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(text);
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
}

void Ui::Help(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, theme().dim);
  ImGui::PushFont(nullptr, fonts().size * 0.84f);
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(text);
  ImGui::PopTextWrapPos();
  ImGui::PopFont();
  ImGui::PopStyleColor();
}

void Ui::Spacer(float height) {
  LeaveRows();
  ImGui::Dummy(ImVec2(0, height * scale_));
}

bool Ui::Section(const char* title, bool open_by_default) {
  LeaveRows();
  ImGui::Dummy(ImVec2(0, 4 * scale_));
  ImGui::PushFont(fonts().semibold, 0.0f);
  const bool open = ImGui::CollapsingHeader(title, open_by_default ? ImGuiTreeNodeFlags_DefaultOpen : 0);
  ImGui::PopFont();
  return open;
}

void Ui::AchievementSummary(int unlocked, int total, int points, int total_points) {
  LeaveRows();
  ImGui::PushFont(fonts().semibold, 0.0f);
  if (total_points > 0)
    ImGui::Text("%d / %d unlocked     %d / %d G", unlocked, total, points, total_points);
  else
    ImGui::Text("%d / %d unlocked", unlocked, total);
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, 4 * scale_));
}

void Ui::AchievementCardView(const AchievementCard& a, float width) {
  LeaveRows();
  const Theme& t = theme();
  const Fonts& f = fonts();
  const float s = scale_, base = ImGui::GetFontSize();
  const float card_w = width > 0 ? width : ImGui::GetContentRegionAvail().x;
  const float card_h = 86 * s, icon = 60 * s;
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(card_w, card_h));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, ImVec2(p.x + card_w, p.y + card_h), Col(a.unlocked ? t.frame_hot : t.frame), 10 * s);
  if (a.unlocked)
    dl->AddRectFilled(p, ImVec2(p.x + 4 * s, p.y + card_h), Col(t.accent), 10 * s, ImDrawFlags_RoundCornersLeft);
  float tx = p.x + 18 * s;
  if (a.icon != ImTextureID{}) {
    const ImVec2 i0(p.x + 14 * s, p.y + (card_h - icon) * 0.5f);
    dl->AddImageRounded(ImTextureRef(a.icon), i0, ImVec2(i0.x + icon, i0.y + icon), ImVec2(0, 0), ImVec2(1, 1),
                        a.unlocked ? IM_COL32_WHITE : IM_COL32(105, 110, 125, 200), 8 * s);
    tx = i0.x + icon + 14 * s;
  }
  const float right = p.x + card_w - 14 * s;
  dl->AddText(f.semibold, base, ImVec2(tx, p.y + 12 * s), Col(a.unlocked ? t.text : t.dim), a.title.c_str());
  if (a.points > 0) {
    const std::string g = std::to_string(a.points) + " G";
    const ImVec2 gs = f.semibold->CalcTextSizeA(base * 0.9f, FLT_MAX, 0, g.c_str());
    dl->AddText(f.semibold, base * 0.9f, ImVec2(right - gs.x, p.y + 13 * s), Col(a.unlocked ? t.accent : t.dim),
                g.c_str());
  }
  const std::string& desc = a.unlocked || a.locked_description.empty() ? a.description : a.locked_description;
  dl->AddText(f.regular, base * 0.84f, ImVec2(tx, p.y + 38 * s), Col(t.dim), desc.c_str(), nullptr, right - tx);
  if (a.unlocked)
    dl->AddText(f.semibold, base * 0.72f, ImVec2(tx, p.y + card_h - 22 * s), Col(t.accent), "UNLOCKED");
}

void Ui::AchievementGrid(const std::vector<AchievementCard>& cards, int max_columns) {
  LeaveRows();
  const float avail = ImGui::GetContentRegionAvail().x;
  const int cols = std::max(1, std::min(max_columns, avail > 700 * scale_ ? 2 : 1));
  const float gap = 12 * scale_;
  const float card_w = (avail - gap * float(cols - 1)) / float(cols);
  for (size_t i = 0; i < cards.size(); ++i) {
    if (i % cols) ImGui::SameLine(0, gap);
    ImGui::PushID(int(i));
    AchievementCardView(cards[i], card_w);
    ImGui::PopID();
    if (i % cols == size_t(cols - 1) || i + 1 == cards.size()) ImGui::Dummy(ImVec2(0, gap * 0.5f));
  }
}

// ---------------------------------------------------------------- launcher --
Launcher::Launcher(LauncherConfig config) : config_(std::move(config)) {
  ui_.launcher_ = this;
  if (config_.fonts.size <= 0.0f) config_.fonts.size = 18.0f;
  page_ = std::clamp(config_.start_page, 0, std::max(0, int(config_.pages.size()) - 1));
  if (config_.settings)
    for (const std::string& key : config_.restart_keys) restart_baseline_.push_back(config_.settings->Get(key));
  // Testing aid: TRG_LAUNCHER_AUTOPLAY=<frames> (or 1 for the default 120)
  // presses PLAY on its own, skipping any before_play prompt.
  if (const char* v = std::getenv("TRG_LAUNCHER_AUTOPLAY"); v && *v && *v != '0') {
    const int frames = std::atoi(v);
    autoplay_frames_ = frames > 1 ? frames : 120;
  }
}

void Launcher::ShowPrompt(PlayPrompt prompt) {
  bool has_back = false;
  for (const PromptButton& b : prompt.buttons) has_back |= !b.play;
  if (!has_back) prompt.buttons.push_back({"Back", false, false, {}});
  prompt_ = std::move(prompt);
  open_prompt_ = true;
}

bool Launcher::restart_needed() const {
  for (size_t i = 0; i < restart_baseline_.size(); ++i)
    if (config_.settings->Get(config_.restart_keys[i]) != restart_baseline_[i]) return true;
  return false;
}

void Launcher::DrawPrompt() {
  if (!prompt_) return;
  const Theme& t = config_.theme;
  const char* id = "##trg_prompt";
  if (open_prompt_) {
    ImGui::OpenPopup(id);
    open_prompt_ = false;
  }
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(std::min(600 * s_, vp->Size.x - 40 * s_), 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24 * s_, 20 * s_));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12 * s_);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::PushStyleColor(ImGuiCol_PopupBg, t.panel);
  ImGui::PushStyleColor(ImGuiCol_Border, t.warn);
  const bool open = ImGui::BeginPopupModal(id, nullptr,
                                           ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(3);
  if (!open) {
    prompt_.reset();  // closed some other way
    return;
  }
  const PlayPrompt& p = *prompt_;
  ImGui::PushFont(config_.fonts.bold, ImGui::GetFontSize() * 1.2f);
  ImGui::TextUnformatted(p.title.c_str());
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, 6 * s_));
  ImGui::PushTextWrapPos(0.0f);
  for (const std::string& para : p.paragraphs) {
    ImGui::TextUnformatted(para.c_str());
    ImGui::Dummy(ImVec2(0, 6 * s_));
  }
  if (!p.footnote.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, t.dim);
    ImGui::TextUnformatted(p.footnote.c_str());
    ImGui::PopStyleColor();
  }
  ImGui::PopTextWrapPos();
  ImGui::Dummy(ImVec2(0, 10 * s_));
  // Buttons that start the game share the first line; the others go below, full width.
  std::vector<const PromptButton*> play_buttons, other;
  for (const PromptButton& b : p.buttons) (b.play ? play_buttons : other).push_back(&b);
  const PromptButton* chosen = nullptr;
  const float gap = 8 * s_;
  const float avail = ImGui::GetContentRegionAvail().x;
  for (size_t i = 0; i < play_buttons.size(); ++i) {
    if (i) ImGui::SameLine(0, gap);
    const float bw = (avail - gap * float(play_buttons.size() - 1)) / float(play_buttons.size());
    const PromptButton& b = *play_buttons[i];
    ImGui::PushID(int(i));
    if (b.accent ? ui_.AccentButton(b.label.c_str(), ImVec2(bw, 0)) : ImGui::Button(b.label.c_str(), ImVec2(bw, 0)))
      chosen = &b;
    ImGui::PopID();
  }
  for (size_t i = 0; i < other.size(); ++i) {
    ImGui::PushID(int(100 + i));
    if (other[i]->accent ? ui_.AccentButton(other[i]->label.c_str(), ImVec2(-FLT_MIN, 0))
                         : ImGui::Button(other[i]->label.c_str(), ImVec2(-FLT_MIN, 0)))
      chosen = other[i];
    ImGui::PopID();
  }
  if (!chosen && !other.empty() && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) chosen = other.front();
  if (chosen) {
    const PromptButton b = *chosen;  // copied: the prompt is reset below
    ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    prompt_.reset();
    if (b.action) b.action();
    if (b.play) {
      prompt_confirmed_ = true;
      want_play_ = true;
    }
    return;
  }
  ImGui::EndPopup();
}

Launcher::~Launcher() {
  if (nav_suspended_) ImGui::GetIO().ConfigFlags |= saved_nav_flags_;
}

int Launcher::FindPage(std::string_view name) const {
  for (size_t i = 0; i < config_.pages.size(); ++i)
    if (config_.pages[i].name == name) return int(i);
  return -1;
}

void Launcher::GoToPage(int index) {
  if (index < 0 || index >= int(config_.pages.size())) return;
  page_ = index;
  ui_.capturing_.clear();
}

void Launcher::SetStatus(std::string text, double seconds) {
  status_ = std::move(text);
  status_until_ = ImGui::GetTime() + seconds;
}

void Launcher::DropFile(const std::string& path) {
  if (config_.on_file_drop) config_.on_file_drop(path);
}

bool Launcher::Save() {
  bool ok = config_.settings && config_.settings->Save();
  if (config_.on_save) ok = config_.on_save() && ok;
  SetStatus(ok ? "Settings saved." : "Could not save the settings.", ok ? 3.0 : 6.0);
  return ok;
}

Result Launcher::Frame() {
  IM_ASSERT(config_.settings && "LauncherConfig::settings is required");
  Fonts& fonts = config_.fonts;
  if (!fonts.regular) fonts.regular = ImGui::GetFont();
  if (!fonts.bold) fonts.bold = fonts.semibold ? fonts.semibold : fonts.regular;
  if (!fonts.semibold) fonts.semibold = fonts.bold;

  const ImGuiStyle saved_style = ImGui::GetStyle();
  ImGui::PushFont(fonts.regular, fonts.size);
  s_ = ImGui::GetFontSize() / fonts.size;
  ui_.scale_ = s_;
  ApplyTheme(config_.theme, s_);

  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->Pos);
  ImGui::SetNextWindowSize(vp->Size);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::Begin("##trg_launcher", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar();
  const float w = vp->Size.x, h = vp->Size.y;
  const float header = std::clamp(h * 0.2f, 120 * s_, 190 * s_);
  const float footer = 78 * s_;
  const float margin = 22 * s_;

  DrawHeader(vp->Pos, w, header);

  const float body_top = header + margin * 0.6f;
  const float body_h = std::max(100 * s_, h - body_top - footer);
  const float sidebar_w = std::clamp(w * 0.17f, 170 * s_, 230 * s_);
  ImGui::SetCursorPos(ImVec2(margin, body_top));
  DrawSidebar(ImVec2(sidebar_w, body_h));
  ImGui::SetCursorPos(ImVec2(margin * 2 + sidebar_w, body_top));
  DrawContent(ImVec2(w - sidebar_w - margin * 3, body_h));

  DrawFooter(ImVec2(vp->Pos.x + margin, vp->Pos.y + h - footer), w - margin * 2, footer);
  HandleHotkeys();
  DrawPrompt();
  ImGui::End();
  ImGui::PopFont();
  ImGui::GetStyle() = saved_style;

  // Arrow keys must not move the keyboard focus while a key is being bound.
  ImGuiIO& io = ImGui::GetIO();
  if (capturing_key() && !nav_suspended_) {
    saved_nav_flags_ = io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    nav_suspended_ = true;
  } else if (!capturing_key() && nav_suspended_) {
    io.ConfigFlags |= saved_nav_flags_;
    nav_suspended_ = false;
  }

  if (want_quit_) {
    want_quit_ = want_play_ = false;
    return Result::kQuit;
  }
  if (autoplay_frames_ > 0 && --autoplay_frames_ == 0) {
    prompt_confirmed_ = true;
    want_play_ = true;
  }
  if (want_play_) {
    want_play_ = false;
    return TryPlay();
  }
  return Result::kNone;
}

Result Launcher::TryPlay() {
  const PlayCheck check = config_.can_play ? config_.can_play() : PlayCheck{};
  if (!check.ok) {
    if (!check.reason.empty()) SetStatus(check.reason, 5.0);
    GoToPage(check.page);
    return Result::kNone;
  }
  if (!prompt_confirmed_ && config_.before_play) {
    if (auto prompt = config_.before_play()) {
      ShowPrompt(std::move(*prompt));
      return Result::kNone;
    }
  }
  prompt_confirmed_ = false;
  if (config_.save_on_play) {
    bool ok = config_.settings->Save();
    if (config_.on_save) ok = config_.on_save() && ok;
    if (!ok) std::fprintf(stderr, "[trg-launcher] could not save the settings\n");
  }
  return Result::kPlay;
}

void Launcher::DrawHeader(ImVec2 origin, float w, float h) {
  const Theme& t = config_.theme;
  const Branding& b = config_.branding;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p1(origin.x + w, origin.y + h);
  dl->PushClipRect(origin, p1, true);
  const HeaderContext ctx{dl, origin, p1, float(ImGui::GetTime()), s_, t};
  if (b.background) {
    b.background(ctx);
  } else {
    dl->AddRectFilledMultiColor(origin, p1, Col(t.panel), Col(t.bg), Col(t.bg), Col(t.panel));
  }
  if (b.shade_title_side)
    dl->AddRectFilledMultiColor(origin, ImVec2(origin.x + w * 0.65f, p1.y), Col(t.bg, 0.82f), Col(t.bg, 0.0f),
                                Col(t.bg, 0.0f), Col(t.bg, 0.82f));
  if (b.fade_into_page)
    dl->AddRectFilledMultiColor(ImVec2(origin.x, p1.y - h * 0.35f), p1, Col(t.bg, 0.0f), Col(t.bg, 0.0f),
                                Col(t.bg, 1.0f), Col(t.bg, 1.0f));

  float x = origin.x + 30 * s_;
  if (b.icon != ImTextureID{}) {
    const float icon = h * 0.46f;
    const ImVec2 i0(x, origin.y + (h - icon) * 0.45f);
    dl->AddImageRounded(ImTextureRef(b.icon), i0, ImVec2(i0.x + icon, i0.y + icon), ImVec2(0, 0), ImVec2(1, 1),
                        IM_COL32_WHITE, 10 * s_);
    x += icon + 20 * s_;
  }
  const float title_size = std::clamp(h * 0.27f, 30 * s_, 46 * s_);
  const float title_y = origin.y + h * 0.5f - title_size * 0.85f;
  const char* title = b.title.c_str();
  dl->AddText(config_.fonts.bold, title_size, ImVec2(x + 2 * s_, title_y + 3 * s_), b.title_shadow, title);
  dl->AddText(config_.fonts.bold, title_size, ImVec2(x, title_y), IM_COL32_WHITE, title);
  if (!b.subtitle.empty())
    dl->AddText(config_.fonts.semibold, 16 * s_, ImVec2(x + 2 * s_, title_y + title_size * 1.15f),
                Col(b.subtitle_color.value_or(t.accent)), b.subtitle.c_str());
  dl->PopClipRect();
}

void Launcher::DrawSidebar(ImVec2 size) {
  const Theme& t = config_.theme;
  ImGui::BeginChild("##sidebar", size, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
  const float pad = 10 * s_;
  const float item_h = 46 * s_;
  ImGui::SetCursorPos(ImVec2(pad, pad));
  ImGui::PushFont(config_.fonts.semibold, 0.0f);
  for (int i = 0; i < int(config_.pages.size()); ++i) {
    const Page& page = config_.pages[size_t(i)];
    const bool selected = page_ == i;
    ImGui::SetCursorPosX(pad);
    ImGui::PushID(i);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 item(size.x - pad * 2, item_h);
    if (ImGui::InvisibleButton("##page", item)) GoToPage(i);
    const bool hot = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (selected || hot)
      dl->AddRectFilled(pos, ImVec2(pos.x + item.x, pos.y + item.y), Col(selected ? t.accent : t.frame), 8 * s_);
    dl->AddText(ImVec2(pos.x + 16 * s_, pos.y + (item_h - ImGui::GetFontSize()) * 0.5f),
                Col(selected ? t.on_accent : t.text), page.name.c_str());
    if (page.needs_attention && page.needs_attention())
      dl->AddCircleFilled(ImVec2(pos.x + item.x - 18 * s_, pos.y + item_h * 0.5f), 4.5f * s_,
                          Col(selected ? t.on_accent : t.warn_dot));
    ImGui::PopID();
    ImGui::Dummy(ImVec2(0, 2 * s_));
  }
  ImGui::PopFont();
  ImGui::EndChild();
}

void Launcher::DrawContent(ImVec2 size) {
  ImGui::BeginChild("##content", size, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
  if (config_.pages.empty()) {
    ImGui::EndChild();
    return;
  }
  const Page& page = config_.pages[size_t(page_)];
  const float pad = 26 * s_;
  ImGui::SetCursorPos(ImVec2(pad, pad * 0.8f));
  ImGui::BeginGroup();
  ImGui::PushFont(config_.fonts.semibold, config_.fonts.size * 1.45f);
  ImGui::TextUnformatted(page.name.c_str());
  ImGui::PopFont();
  if (!page.blurb.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, config_.theme.dim);
    ImGui::TextUnformatted(page.blurb.c_str());
    ImGui::PopStyleColor();
  }
  ImGui::EndGroup();

  ImGui::SetCursorPos(ImVec2(pad, ImGui::GetCursorPosY() + 8 * s_));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
  ImGui::BeginChild("##page_body", ImVec2(size.x - pad * 2, size.y - ImGui::GetCursorPosY() - pad * 0.6f),
                    ImGuiChildFlags_None);
  ImGui::PushID(page_);
  ui_.BeginPage();
  if (page.draw) page.draw(ui_);
  ui_.EndPage();
  ImGui::PopID();
  ImGui::Dummy(ImVec2(0, 8 * s_));
  ImGui::EndChild();
  ImGui::PopStyleColor();
  ImGui::EndChild();
}

void Launcher::DrawFooter(ImVec2 origin, float w, float h) {
  const Theme& t = config_.theme;
  const float bw = 130 * s_, play_w = 210 * s_, bh = 48 * s_;
  const float cy = origin.y + h * 0.5f;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float small = config_.fonts.size * 0.84f * s_;
  dl->AddText(config_.fonts.regular, small, ImVec2(origin.x, cy - 20 * s_), Col(t.dim), config_.hints.c_str());
  const bool show_status = !status_.empty() && ImGui::GetTime() < status_until_;
  dl->AddText(show_status ? config_.fonts.semibold : config_.fonts.regular, small, ImVec2(origin.x, cy + 2 * s_),
              Col(show_status ? t.accent : t.dim), show_status ? status_.c_str() : config_.note.c_str());

  const float right = origin.x + w;
  ImGui::SetCursorScreenPos(ImVec2(right - play_w - bw * 2 - 24 * s_, cy - bh * 0.5f));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12 * s_);
  if (ImGui::Button("Quit", ImVec2(bw, bh))) want_quit_ = true;
  ImGui::SameLine(0, 12 * s_);
  if (ImGui::Button("Save", ImVec2(bw, bh))) Save();
  ImGui::SameLine(0, 12 * s_);
  ImGui::PopStyleVar();
  const PlayCheck check = config_.can_play ? config_.can_play() : PlayCheck{};
  DrawPlayButton(ImVec2(play_w, bh), check.ok);
}

void Launcher::DrawPlayButton(ImVec2 size, bool can_play) {
  const Theme& t = config_.theme;
  const ImVec2 p = ImGui::GetCursorScreenPos();
  // Still clickable when the game cannot start: that explains why.
  const bool clicked = ImGui::InvisibleButton("##play", size);
  const bool hot = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float r = size.y * 0.5f;
  if (can_play) {
    const float pulse = 0.5f + 0.5f * std::sin(float(ImGui::GetTime()) * 3.0f);
    dl->AddRectFilled(ImVec2(p.x - 3 * s_, p.y - 3 * s_), ImVec2(p.x + size.x + 3 * s_, p.y + size.y + 3 * s_),
                      Col(t.accent, hot ? 0.35f : 0.18f), r + 3 * s_);
    const float grow = (3 + 4 * pulse) * s_;
    dl->AddRect(ImVec2(p.x - grow, p.y - grow), ImVec2(p.x + size.x + grow, p.y + size.y + grow),
                Col(t.accent, 0.45f * (1.0f - pulse)), r + grow, 0, 2 * s_);
  }
  dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y),
                    can_play ? Col(hot ? t.accent_hot : t.accent) : Col(hot ? t.frame_hot : t.frame), r);
  const float fs = 22 * s_;
  const ImVec2 ts = config_.fonts.bold->CalcTextSizeA(fs, FLT_MAX, 0, "PLAY");
  const float tri = fs * 0.5f;
  const float total = ts.x + 12 * s_ + tri;
  const float tx = p.x + (size.x - total) * 0.5f, ty = p.y + (size.y - ts.y) * 0.5f;
  const ImU32 ink = can_play ? Col(t.on_accent) : Col(t.dim);
  dl->AddText(config_.fonts.bold, fs, ImVec2(tx, ty), ink, "PLAY");
  const float ax = tx + ts.x + 12 * s_, ay = p.y + size.y * 0.5f;
  dl->AddTriangleFilled(ImVec2(ax, ay - tri * 0.6f), ImVec2(ax, ay + tri * 0.6f), ImVec2(ax + tri, ay), ink);
  if (clicked) want_play_ = true;
}

void Launcher::HandleHotkeys() {
  const ImGuiIO& io = ImGui::GetIO();
  if (capturing_key() || io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
  if (ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false)) {
    want_play_ = true;
  } else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
    // Enter activates the focused control while navigating with the keyboard.
    if (!io.NavVisible && !ImGui::IsAnyItemActive()) want_play_ = true;
  } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    want_quit_ = true;
  } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
    Save();
  }
}

// ------------------------------------------------------------------ helpers --
bool ShouldShowLauncher(const SettingsStore& settings, std::string_view key, bool game_ready, bool force) {
  if (force || !game_ready || ShiftHeld()) return true;
  return settings.GetBool(key, true);
}

int ImGuiKeyToVirtualKey(ImGuiKey k) {
  if (k >= ImGuiKey_A && k <= ImGuiKey_Z) return 'A' + (k - ImGuiKey_A);
  if (k >= ImGuiKey_0 && k <= ImGuiKey_9) return '0' + (k - ImGuiKey_0);
  if (k >= ImGuiKey_F1 && k <= ImGuiKey_F24) return 0x70 + (k - ImGuiKey_F1);
  if (k >= ImGuiKey_Keypad0 && k <= ImGuiKey_Keypad9) return 0x60 + (k - ImGuiKey_Keypad0);
  switch (k) {
    case ImGuiKey_Tab: return 0x09;
    case ImGuiKey_LeftArrow: return 0x25;
    case ImGuiKey_RightArrow: return 0x27;
    case ImGuiKey_UpArrow: return 0x26;
    case ImGuiKey_DownArrow: return 0x28;
    case ImGuiKey_PageUp: return 0x21;
    case ImGuiKey_PageDown: return 0x22;
    case ImGuiKey_Home: return 0x24;
    case ImGuiKey_End: return 0x23;
    case ImGuiKey_Insert: return 0x2D;
    case ImGuiKey_Delete: return 0x2E;
    case ImGuiKey_Backspace: return 0x08;
    case ImGuiKey_Space: return 0x20;
    case ImGuiKey_Enter:
    case ImGuiKey_KeypadEnter: return 0x0D;
    case ImGuiKey_Escape: return 0x1B;
    case ImGuiKey_CapsLock: return 0x14;
    case ImGuiKey_LeftShift: return 0xA0;
    case ImGuiKey_RightShift: return 0xA1;
    case ImGuiKey_LeftCtrl: return 0xA2;
    case ImGuiKey_RightCtrl: return 0xA3;
    case ImGuiKey_LeftAlt: return 0xA4;
    case ImGuiKey_RightAlt: return 0xA5;
    case ImGuiKey_LeftSuper: return 0x5B;
    case ImGuiKey_RightSuper: return 0x5C;
    case ImGuiKey_Menu: return 0x5D;
    case ImGuiKey_Semicolon: return 0xBA;
    case ImGuiKey_Equal: return 0xBB;
    case ImGuiKey_Comma: return 0xBC;
    case ImGuiKey_Minus: return 0xBD;
    case ImGuiKey_Period: return 0xBE;
    case ImGuiKey_Slash: return 0xBF;
    case ImGuiKey_GraveAccent: return 0xC0;
    case ImGuiKey_LeftBracket: return 0xDB;
    case ImGuiKey_Backslash: return 0xDC;
    case ImGuiKey_RightBracket: return 0xDD;
    case ImGuiKey_Apostrophe: return 0xDE;
    case ImGuiKey_KeypadDecimal: return 0x6E;
    case ImGuiKey_KeypadDivide: return 0x6F;
    case ImGuiKey_KeypadMultiply: return 0x6A;
    case ImGuiKey_KeypadSubtract: return 0x6D;
    case ImGuiKey_KeypadAdd: return 0x6B;
    default: return 0;
  }
}

}  // namespace trg
