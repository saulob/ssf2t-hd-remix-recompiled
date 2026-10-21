// Keyboard remapping: the launcher's action table (labels, ReXGlue cvars, defaults), capture into a
// ReXGlue binding (keys, modifiers, reserved keys), the Keyboard On/Off setting and the Keyboard
// defaults reset, and that port hotkeys do not depend on the setting. Pure logic plus source checks;
// the capture widget and the launcher page need the running launcher.
// Run: ssf2t_keyboard_tests

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <imgui.h>

#include "keybinds.h"

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

std::string ReadSource(const char* name) {
  std::ifstream in(std::string(SSF2T_SRC_DIR) + "/" + name, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

const ssf2t::KeyboardAction* Find(const char* key) {
  for (const ssf2t::KeyboardAction& action : ssf2t::kKeyboardActions) {
    if (std::string_view(action.key) == key) {
      return &action;
    }
  }
  return nullptr;
}

// The store the launcher uses keeps edits pending until Save ("edits stay pending until Save or
// Play"). Get reads the pending value; Save commits it. GameSettings writes true/false for toggles.
struct PendingStore : trg::SettingsStore {
  std::map<std::string, std::string, std::less<>> saved;
  std::map<std::string, std::string, std::less<>> pending;

  PendingStore() {
    true_value = "true";
    false_value = "false";
  }

  std::string Get(std::string_view key, std::string_view def = {}) const override {
    const auto it = pending.find(key);
    return it == pending.end() || it->second.empty() ? std::string(def) : it->second;
  }
  void Set(std::string_view key, std::string value) override { pending[std::string(key)] = std::move(value); }
  bool Save() override {
    saved = pending;
    return true;
  }
};

void TestTable() {
  std::printf("action table\n");
  CHECK(ssf2t::kKeyboardActions.size() == 24);
  for (const ssf2t::KeyboardAction& action : ssf2t::kKeyboardActions) {
    CHECK(std::string_view(action.key).starts_with("keybind_"));
    CHECK(action.label[0] != '\0');
    CHECK(action.default_binding[0] != '\0');
    // A default is always something the launcher can show and restore.
    CHECK(ssf2t::KeyboardBindingIsValid(action.default_binding));
    // Guide is not offered.
    CHECK(std::string_view(action.key) != "keybind_guide");
  }
  // No key or label is used twice.
  for (std::size_t a = 0; a < ssf2t::kKeyboardActions.size(); ++a) {
    for (std::size_t b = a + 1; b < ssf2t::kKeyboardActions.size(); ++b) {
      CHECK(std::string_view(ssf2t::kKeyboardActions[a].key) != ssf2t::kKeyboardActions[b].key);
      CHECK(std::string_view(ssf2t::kKeyboardActions[a].label) != ssf2t::kKeyboardActions[b].label);
    }
  }
}

void TestMapping() {
  std::printf("ReXGlue setting, label and default\n");
  const struct {
    const char* key;
    const char* label;
    const char* default_binding;
  } kExpected[] = {
      {"keybind_a", "A", "Semicolon,Space"},
      {"keybind_b", "B", "Quote,Backspace"},
      {"keybind_x", "X", "L"},
      {"keybind_y", "Y", "P"},
      {"keybind_left_shoulder", "Left bumper", "1"},
      {"keybind_right_shoulder", "Right bumper", "3"},
      {"keybind_left_trigger", "Left trigger", "Q,I"},
      {"keybind_right_trigger", "Right trigger", "E,O"},
      {"keybind_lstick_up", "Left stick up", "W"},
      {"keybind_lstick_down", "Left stick down", "S"},
      {"keybind_lstick_left", "Left stick left", "A"},
      {"keybind_lstick_right", "Left stick right", "D"},
      {"keybind_lstick_press", "Left stick press", "F"},
      {"keybind_rstick_up", "Right stick up", "Up"},
      {"keybind_rstick_down", "Right stick down", "Down"},
      {"keybind_rstick_left", "Right stick left", "Left"},
      {"keybind_rstick_right", "Right stick right", "Right"},
      {"keybind_rstick_press", "Right stick press", "K"},
      {"keybind_dpad_up", "D-pad up", "Shift+Up"},
      {"keybind_dpad_down", "D-pad down", "Shift+Down"},
      {"keybind_dpad_left", "D-pad left", "Shift+Left"},
      {"keybind_dpad_right", "D-pad right", "Shift+Right"},
      {"keybind_back", "View / Back", "Z,Tab"},
      {"keybind_start", "Menu / Start", "X,Return"},
  };
  CHECK(sizeof(kExpected) / sizeof(kExpected[0]) == ssf2t::kKeyboardActions.size());
  for (const auto& expected : kExpected) {
    const ssf2t::KeyboardAction* action = Find(expected.key);
    CHECK(action != nullptr);
    if (!action) {
      continue;
    }
    CHECK(std::string_view(action->label) == expected.label);
    CHECK(std::string_view(action->default_binding) == expected.default_binding);
  }
}

void TestCapture() {
  std::printf("captured key and modifiers\n");
  CHECK(ssf2t::KeyboardBindingFor(ImGuiKey_D, false, false, false) == "D");
  CHECK(ssf2t::KeyboardBindingFor(ImGuiKey_Space, false, false, false) == "Space");
  CHECK(ssf2t::KeyboardBindingFor(ImGuiKey_UpArrow, true, false, false) == "Shift+Up");
  CHECK(ssf2t::KeyboardBindingFor(ImGuiKey_A, false, true, false) == "Ctrl+A");
  CHECK(ssf2t::KeyboardBindingFor(ImGuiKey_S, false, false, true) == "Alt+S");
  CHECK(ssf2t::KeyboardBindingFor(ImGuiKey_F5, true, true, true) == "Shift+Ctrl+Alt+F5");
  CHECK(ssf2t::KeyboardBindingFor(ImGuiKey_Keypad0, false, false, false) == "Numpad0");
  CHECK(ssf2t::KeyboardBindingFor(ImGuiKey_Equal, false, false, false) == "Plus");
  CHECK(ssf2t::KeyboardBindingFor(ImGuiKey_Semicolon, false, false, false) == "Semicolon");
  // Every binding a capture can produce is one ReXGlue parses back.
  for (ImGuiKey key : {ImGuiKey_A, ImGuiKey_UpArrow, ImGuiKey_Space, ImGuiKey_F1, ImGuiKey_KeypadAdd}) {
    for (bool shift : {false, true}) {
      for (bool ctrl : {false, true}) {
        for (bool alt : {false, true}) {
          const std::string binding = ssf2t::KeyboardBindingFor(key, shift, ctrl, alt);
          CHECK(ssf2t::KeyboardBindingIsValid(binding));
        }
      }
    }
  }
}

void TestRejection() {
  std::printf("keys that cannot be bound\n");
  // A bare modifier can never fire, so holding it is not a binding.
  for (ImGuiKey key : {ImGuiKey_LeftShift, ImGuiKey_RightShift, ImGuiKey_LeftCtrl, ImGuiKey_RightCtrl,
                       ImGuiKey_LeftAlt, ImGuiKey_RightAlt}) {
    CHECK(ssf2t::KeyboardBindingFor(key, false, false, false).empty());
  }
  // Reserved: the port and the SDK already use these while playing.
  for (ImGuiKey key : {ImGuiKey_F2, ImGuiKey_F3, ImGuiKey_F4, ImGuiKey_F7, ImGuiKey_F11, ImGuiKey_F12,
                       ImGuiKey_Escape, ImGuiKey_GraveAccent}) {
    CHECK(ssf2t::KeyboardBindingIsReserved(key));
    CHECK(ssf2t::KeyboardBindingFor(key, false, false, false).empty());
    // A reserved key is refused with a modifier too.
    CHECK(ssf2t::KeyboardBindingFor(key, true, false, false).empty());
  }
  // Mouse, gamepad and keys ReXGlue has no name for.
  for (ImGuiKey key : {ImGuiKey_GamepadFaceDown, ImGuiKey_MouseLeft, ImGuiKey_KeypadDecimal, ImGuiKey_Oem102,
                       ImGuiKey_Menu}) {
    CHECK(ssf2t::KeyboardKeyName(key) == nullptr);
    CHECK(ssf2t::KeyboardBindingFor(key, false, false, false).empty());
  }
  // Esc cancels the capture, so it is never stored; F12 stays unbound.
  CHECK(!ssf2t::KeyboardBindingIsValid("Escape"));
  CHECK(!ssf2t::KeyboardBindingIsValid("F12"));
  CHECK(!ssf2t::KeyboardBindingIsValid("Backtick"));
}

void TestEscapeCancelsCapture() {
  std::printf("Escape cancels the capture and leaves the binding unchanged\n");
  // The capture ends on Escape and stores nothing, and Escape is reserved, so no row changes.
  CHECK(ssf2t::KeyboardBindingIsReserved(ImGuiKey_Escape));
  const std::string captured = ssf2t::KeyboardBindingFor(ImGuiKey_Escape, false, false, false);
  CHECK(captured.empty());
  PendingStore store;
  store.Set("keybind_y", "G");
  CHECK(store.Save());
  if (!captured.empty()) {
    store.Set("keybind_y", captured);
  }
  CHECK(ssf2t::SavedKeyboardBinding(store, *Find("keybind_y")) == "G");
}

void TestValidity() {
  std::printf("a stored binding\n");
  for (const char* good : {"Space", "Semicolon,Space", "Shift+Up", "Ctrl+Shift+W", "NumpadPlus", "F1",
                           "Shift+Up,Shift+Down", " Z , Tab "}) {
    CHECK(ssf2t::KeyboardBindingIsValid(good));
  }
  for (const char* bad : {"", "NotAKey", "Shift", "Shift+", "Foo+Up", "Up,", ",Up", "Up,,Down", "Shift+Escape",
                          "Ctrl+F3", "Control+", "Return+"}) {
    CHECK(!ssf2t::KeyboardBindingIsValid(bad));
  }
}

void TestLabel() {
  std::printf("label shown for a binding\n");
  CHECK(ssf2t::KeyboardBindingLabel("Semicolon,Space") == "Semicolon or Space");
  CHECK(ssf2t::KeyboardBindingLabel("Shift+Up") == "Shift+Up");
  CHECK(ssf2t::KeyboardBindingLabel("Z,Tab") == "Z or Tab");
  CHECK(ssf2t::KeyboardBindingLabel("Q,I") == "Q or I");
  CHECK(ssf2t::KeyboardBindingLabel("") == "None");
}

void TestSaved() {
  std::printf("reading the stored binding\n");
  PendingStore store;
  // Nothing stored: the default is shown.
  for (const ssf2t::KeyboardAction& action : ssf2t::kKeyboardActions) {
    CHECK(ssf2t::SavedKeyboardBinding(store, action) == action.default_binding);
  }
  // A captured binding survives a read and shows its key.
  store.Set("keybind_a", "Shift+Up");
  CHECK(ssf2t::SavedKeyboardBinding(store, *Find("keybind_a")) == "Shift+Up");
  CHECK(ssf2t::KeyboardBindingLabel(ssf2t::SavedKeyboardBinding(store, *Find("keybind_a"))) == "Shift+Up");
  // A hand-edited value ReXGlue cannot use, or a reserved key, falls back to the default.
  store.Set("keybind_b", "NotAKey");
  CHECK(ssf2t::SavedKeyboardBinding(store, *Find("keybind_b")) == "Quote,Backspace");
  store.Set("keybind_start", "F12");
  CHECK(ssf2t::SavedKeyboardBinding(store, *Find("keybind_start")) == "X,Return");
  // Alternatives in a default survive the read and the label.
  CHECK(ssf2t::KeyboardBindingLabel(ssf2t::SavedKeyboardBinding(store, *Find("keybind_back"))) == "Z or Tab");
}

void TestPendingAndReset() {
  std::printf("pending edits and Keyboard defaults\n");
  PendingStore store;
  for (const ssf2t::KeyboardAction& action : ssf2t::kKeyboardActions) {
    store.pending[action.key] = action.default_binding;
    store.saved[action.key] = action.default_binding;
  }
  store.saved["ssf2t_show_fps"] = "true";
  store.pending["ssf2t_show_fps"] = "true";

  store.Set("keybind_a", "G");
  CHECK(ssf2t::SavedKeyboardBinding(store, *Find("keybind_a")) == "G");
  CHECK(store.saved["keybind_a"] == "Semicolon,Space");  // still pending
  CHECK(store.Save());
  CHECK(store.saved["keybind_a"] == "G");  // saved on Save / Play

  ssf2t::ResetKeyboardDefaults(store);
  for (const ssf2t::KeyboardAction& action : ssf2t::kKeyboardActions) {
    CHECK(store.Get(action.key) == action.default_binding);
    CHECK(ssf2t::SavedKeyboardBinding(store, action) == action.default_binding);
  }
  // The reset only queues the keyboard keys; the other settings are untouched.
  CHECK(store.Get("ssf2t_show_fps") == "true");
  CHECK(store.saved["keybind_a"] == "G");  // reset is pending too
  CHECK(store.Save());
  CHECK(store.saved["keybind_a"] == "Semicolon,Space");
  // The full set of defaults keeps alternatives, not just the first key.
  CHECK(store.Get("keybind_a") == "Semicolon,Space");
  CHECK(store.Get("keybind_dpad_up") == "Shift+Up");
}

void TestSerialization() {
  std::printf("values a settings file can hold\n");
  // Settings are written as `key = "value"` lines: no default or captured binding may break one.
  const auto safe = [](std::string_view value) {
    return value.find('"') == std::string_view::npos && value.find('\\') == std::string_view::npos &&
           value.find('\n') == std::string_view::npos && value.find('\r') == std::string_view::npos;
  };
  for (const ssf2t::KeyboardAction& action : ssf2t::kKeyboardActions) {
    CHECK(safe(action.key));
    CHECK(safe(action.default_binding));
  }
  for (ImGuiKey key : {ImGuiKey_A, ImGuiKey_Apostrophe, ImGuiKey_Backslash, ImGuiKey_UpArrow, ImGuiKey_KeypadAdd}) {
    CHECK(safe(ssf2t::KeyboardBindingFor(key, true, true, true)));
  }
}

void TestSources() {
  std::printf("wiring in the launcher and settings\n");
  const std::string settings = ReadSource("settings.cpp");
  CHECK(Has(settings, "#include \"keybinds.h\""));
  CHECK(Has(settings, "for (const KeyboardAction& action : kKeyboardActions)"));
  // mnk_mode is an ordinary managed setting again, and nothing forces it on.
  CHECK(Has(settings, "      \"mnk_mode\",\n") || Has(settings, "      \"mnk_mode\",\r\n"));
  CHECK(Has(settings, "for (const std::string& key : kGameSettingKeys)"));
  CHECK(Has(settings, "rex::cvar::SetFlagByName(key, value)"));
  CHECK(!Has(settings, "ApplyKeyboardMode"));
  CHECK(!Has(ReadSource("settings.h"), "ApplyKeyboardMode"));
  CHECK(!Has(ReadSource("ssf2t_hd_remix_app.h"), "ApplyKeyboardMode"));

  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "ui.KeyBindRow(action.label, nullptr, shown)"));
  CHECK(Has(launcher, "ResetKeyboardDefaults(ui.settings())"));
  CHECK(Has(launcher, "KeyboardBindingFor(*key, io.KeyShift, io.KeyCtrl, io.KeyAlt)"));
  CHECK(Has(launcher, "KeyboardBindingLabel(SavedKeyboardBinding(ui.settings(), action))"));
  CHECK(Has(launcher, "Click a key, then press the new one. Esc cancels."));
  CHECK(Has(launcher, "\"Keyboard defaults\""));
  // Keyboard On/Off row, default On, with the editing rows greyed out while Off.
  CHECK(Has(launcher, "ui.Toggle(\"Keyboard\","));
  CHECK(Has(launcher, "Use the keyboard for gameplay controls together with the controller."));
  CHECK(Has(launcher, "\"mnk_mode\", true,"));
  CHECK(Has(launcher, "\"Off\", \"On\""));
  CHECK(Has(launcher, "ui.settings().GetBool(\"mnk_mode\", true)"));
  CHECK(Has(launcher, "ImGui::BeginDisabled()"));
}

void TestKeyboardToggle() {
  std::printf("Keyboard On/Off setting\n");
  // The shipped configuration turns gameplay keyboard support on.
  CHECK(Has(ReadSource("../config/ssf2t_hd_remix.toml"), "mnk_mode = true"));

  PendingStore store;
  // Fresh configuration: On, matching the launcher row default.
  CHECK(store.Get("mnk_mode", "true") == "true");

  // Off is pending until Save, then it persists. The toggle writes true/false.
  store.SetBool("mnk_mode", false);
  CHECK(store.Get("mnk_mode") == "false");
  CHECK(store.saved.find("mnk_mode") == store.saved.end());
  CHECK(store.Save());
  CHECK(store.saved["mnk_mode"] == "false");
  CHECK(store.Get("mnk_mode") == "false");

  // Back On persists too.
  store.SetBool("mnk_mode", true);
  CHECK(store.Save());
  CHECK(store.saved["mnk_mode"] == "true");

  // Keyboard defaults resets only the bindings, never the On/Off choice.
  store.Set("keybind_a", "G");
  store.Set("mnk_mode", "false");
  CHECK(store.Save());
  ssf2t::ResetKeyboardDefaults(store);
  CHECK(store.Get("mnk_mode") == "false");
  CHECK(store.Get("keybind_a") == "Semicolon,Space");

  // A custom binding survives Off, a save, a reload and On again.
  store.Set("keybind_b", "H");
  store.Set("mnk_mode", "false");
  CHECK(store.Save());
  PendingStore reopened;
  reopened.saved = store.saved;
  reopened.pending = store.saved;
  CHECK(reopened.Get("mnk_mode") == "false");
  CHECK(ssf2t::SavedKeyboardBinding(reopened, *Find("keybind_b")) == "H");
  reopened.Set("mnk_mode", "true");
  CHECK(reopened.Save());
  CHECK(ssf2t::SavedKeyboardBinding(reopened, *Find("keybind_b")) == "H");
}

void TestHotkeySeparation() {
  std::printf("port hotkeys do not depend on mnk_mode\n");
  // Port hotkeys use the UI keybind registry or window input listeners, never the gamepad
  // keyboard emulation, so they keep working with Keyboard Off.
  CHECK(Has(ReadSource("frame_counter.cpp"), "rex::ui::RegisterBind(\"bind_ssf2t_frame_counter\", \"F2\""));
  CHECK(Has(ReadSource("window_host.cpp"), "rex::ui::VirtualKey::kF11"));
  CHECK(Has(ReadSource("ssf2t_hd_remix_app.h"), "CreateAchievementsOverlay"));
  for (const char* file : {"frame_counter.cpp", "window_host.cpp", "achievements_overlay.cpp"}) {
    CHECK(!Has(ReadSource(file), "mnk_mode"));
  }
}

}  // namespace

int main() {
  TestTable();
  TestMapping();
  TestCapture();
  TestRejection();
  TestEscapeCancelsCapture();
  TestValidity();
  TestLabel();
  TestSaved();
  TestPendingAndReset();
  TestSerialization();
  TestSources();
  TestKeyboardToggle();
  TestHotkeySeparation();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
