#include "imgui_gamepad.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>

#include <imgui.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xinput.h>
#else
#include <SDL3/SDL.h>
#endif

namespace ssf2t {
namespace {

// The left stick dead zone the launcher has always used (XInput's).
constexpr int kStickDeadZone = 7849;

float StickAmount(int value) {
  return std::clamp(float(value - kStickDeadZone) / float(32767 - kStickDeadZone), 0.0f, 1.0f);
}

#if defined(_WIN32)

struct ButtonKey {
  ImGuiKey key;
  WORD mask;
};
constexpr ButtonKey kButtons[] = {
    {ImGuiKey_GamepadStart, XINPUT_GAMEPAD_START},
    {ImGuiKey_GamepadBack, XINPUT_GAMEPAD_BACK},
    {ImGuiKey_GamepadFaceDown, XINPUT_GAMEPAD_A},
    {ImGuiKey_GamepadFaceRight, XINPUT_GAMEPAD_B},
    {ImGuiKey_GamepadFaceLeft, XINPUT_GAMEPAD_X},
    {ImGuiKey_GamepadFaceUp, XINPUT_GAMEPAD_Y},
    {ImGuiKey_GamepadDpadLeft, XINPUT_GAMEPAD_DPAD_LEFT},
    {ImGuiKey_GamepadDpadRight, XINPUT_GAMEPAD_DPAD_RIGHT},
    {ImGuiKey_GamepadDpadUp, XINPUT_GAMEPAD_DPAD_UP},
    {ImGuiKey_GamepadDpadDown, XINPUT_GAMEPAD_DPAD_DOWN},
};

// XInput's Y axis points up.
struct StickKey {
  ImGuiKey key;
  bool horizontal;
  int direction;
};
constexpr StickKey kSticks[] = {
    {ImGuiKey_GamepadLStickLeft, true, -1},
    {ImGuiKey_GamepadLStickRight, true, 1},
    {ImGuiKey_GamepadLStickUp, false, 1},
    {ImGuiKey_GamepadLStickDown, false, -1},
};

using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

XInputGetStateFn LoadXInput() {
  for (const wchar_t* name : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"}) {
    if (HMODULE dll = LoadLibraryW(name)) {
      if (FARPROC proc = GetProcAddress(dll, "XInputGetState")) {
        return reinterpret_cast<XInputGetStateFn>(reinterpret_cast<void*>(proc));
      }
    }
  }
  return nullptr;
}

DWORD GetPadState(DWORD pad, XINPUT_STATE* state) {
  static const XInputGetStateFn get_state = LoadXInput();
  return get_state ? get_state(pad, state) : ERROR_DEVICE_NOT_CONNECTED;
}

#else

// SDL names the buttons by position (south, east, ...), so a Steam Controller's or a DualSense's face buttons map
// exactly as an Xbox pad's.
struct ButtonKey {
  ImGuiKey key;
  SDL_GamepadButton button;
};
constexpr ButtonKey kButtons[] = {
    {ImGuiKey_GamepadStart, SDL_GAMEPAD_BUTTON_START},
    {ImGuiKey_GamepadBack, SDL_GAMEPAD_BUTTON_BACK},
    {ImGuiKey_GamepadFaceDown, SDL_GAMEPAD_BUTTON_SOUTH},
    {ImGuiKey_GamepadFaceRight, SDL_GAMEPAD_BUTTON_EAST},
    {ImGuiKey_GamepadFaceLeft, SDL_GAMEPAD_BUTTON_WEST},
    {ImGuiKey_GamepadFaceUp, SDL_GAMEPAD_BUTTON_NORTH},
    {ImGuiKey_GamepadDpadLeft, SDL_GAMEPAD_BUTTON_DPAD_LEFT},
    {ImGuiKey_GamepadDpadRight, SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
    {ImGuiKey_GamepadDpadUp, SDL_GAMEPAD_BUTTON_DPAD_UP},
    {ImGuiKey_GamepadDpadDown, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
};

// SDL's Y axis points down.
struct StickKey {
  ImGuiKey key;
  SDL_GamepadAxis axis;
  int direction;
};
constexpr StickKey kSticks[] = {
    {ImGuiKey_GamepadLStickLeft, SDL_GAMEPAD_AXIS_LEFTX, -1},
    {ImGuiKey_GamepadLStickRight, SDL_GAMEPAD_AXIS_LEFTX, 1},
    {ImGuiKey_GamepadLStickUp, SDL_GAMEPAD_AXIS_LEFTY, -1},
    {ImGuiKey_GamepadLStickDown, SDL_GAMEPAD_AXIS_LEFTY, 1},
};

bool Tracks(const std::vector<SDL_Gamepad*>& pads, SDL_JoystickID id) {
  for (SDL_Gamepad* pad : pads) {
    if (SDL_GetGamepadID(pad) == id) {
      return true;
    }
  }
  return false;
}

void OpenPad(std::vector<SDL_Gamepad*>& pads, SDL_JoystickID id) {
  if (Tracks(pads, id)) {
    return;
  }
  if (SDL_Gamepad* pad = SDL_OpenGamepad(id)) {
    pads.push_back(pad);
  }
}

void ClosePad(std::vector<SDL_Gamepad*>& pads, SDL_JoystickID id) {
  for (auto it = pads.begin(); it != pads.end(); ++it) {
    if (SDL_GetGamepadID(*it) == id) {
      SDL_CloseGamepad(*it);
      pads.erase(it);
      return;
    }
  }
}

#endif

static_assert(std::size(kButtons) == ImGuiGamepad::kButtonCount);
static_assert(std::size(kSticks) == ImGuiGamepad::kStickCount);

// XInput reads controllers whichever program is in front.
bool ProgramInForeground() {
#if defined(_WIN32)
  DWORD process = 0;
  const HWND window = GetForegroundWindow();
  return window != nullptr && GetWindowThreadProcessId(window, &process) != 0 && process == GetCurrentProcessId();
#else
  return true;
#endif
}

}  // namespace

ImGuiGamepad::~ImGuiGamepad() {
#if !defined(_WIN32)
  CloseAll();
#endif
}

void ImGuiGamepad::Update(ImGuiIO& io) {
  // The union of every connected pad, so any of them navigates. All-false when none is connected or another program
  // is in front, which is what releases the keys of a pad that was unplugged or is in use elsewhere.
  Input input;
  if (!Read(input)) {
    return;
  }
  io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  if (!ProgramInForeground()) {
    input.down = {};
    input.amount = {};
  }
  for (size_t i = 0; i < std::size(kButtons); ++i) {
    io.AddKeyEvent(kButtons[i].key, input.down[i]);
  }
  for (size_t i = 0; i < std::size(kSticks); ++i) {
    io.AddKeyAnalogEvent(kSticks[i].key, input.amount[i] > 0.1f, input.amount[i]);
  }
}

void ImGuiGamepad::Release(ImGuiIO& io) {
  for (const ButtonKey& b : kButtons) {
    io.AddKeyEvent(b.key, false);
  }
  for (const StickKey& s : kSticks) {
    io.AddKeyAnalogEvent(s.key, false, 0.0f);
  }
#if defined(_WIN32)
  scan_.Reset();
#else
  CloseAll();
#endif
}

#if defined(_WIN32)

bool ImGuiGamepad::Read(Input& input) {
  scan_.Poll(ImGui::GetTime(), [&input](int slot) {
    XINPUT_STATE state{};
    if (GetPadState(DWORD(slot), &state) != ERROR_SUCCESS) {
      return false;
    }
    const XINPUT_GAMEPAD& pad = state.Gamepad;
    for (size_t i = 0; i < std::size(kButtons); ++i) {
      input.down[i] = input.down[i] || (pad.wButtons & kButtons[i].mask) != 0;
    }
    for (size_t i = 0; i < std::size(kSticks); ++i) {
      const int value = kSticks[i].direction * int(kSticks[i].horizontal ? pad.sThumbLX : pad.sThumbLY);
      input.amount[i] = std::max(input.amount[i], StickAmount(value));
    }
    return true;
  });
  return true;
}

#else

bool ImGuiGamepad::Read(Input& input) {
  if (!subsystem_) {
    // Started here and quit in Release; the runtime shares this SDL and counts the same starts, so the game's own
    // input is unaffected.
    if (!(subsystem_ = SDL_InitSubSystem(SDL_INIT_GAMEPAD))) {
      return false;
    }
    // Controllers connected before this started do not arrive as events; take the ones already there.
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; ids && i < count; ++i) {
      OpenPad(pads_, ids[i]);
    }
    SDL_free(ids);
  }
  DrainEvents();
  for (SDL_Gamepad* pad : pads_) {
    for (size_t i = 0; i < std::size(kButtons); ++i) {
      input.down[i] = input.down[i] || SDL_GetGamepadButton(pad, kButtons[i].button);
    }
    for (size_t i = 0; i < std::size(kSticks); ++i) {
      const int value = kSticks[i].direction * int(SDL_GetGamepadAxis(pad, kSticks[i].axis));
      input.amount[i] = std::max(input.amount[i], StickAmount(value));
    }
  }
  return true;
}

void ImGuiGamepad::DrainEvents() {
  // The program and the runtime share one SDL, whose queue also holds the window's events (its paint requests, the
  // keys and the mouse): take only the controllers' and leave the rest for the window.
  SDL_PumpEvents();
  SDL_Event event;
  while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_JOYSTICK_AXIS_MOTION, SDL_EVENT_FINGER_DOWN - 1) > 0) {
    if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
      OpenPad(pads_, event.gdevice.which);
    } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
      ClosePad(pads_, event.gdevice.which);
    }
  }
}

void ImGuiGamepad::CloseAll() {
  for (SDL_Gamepad* pad : pads_) {
    SDL_CloseGamepad(pad);
  }
  pads_.clear();
  if (subsystem_) {
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    subsystem_ = false;
  }
}

#endif

}  // namespace ssf2t
