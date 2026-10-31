#pragma once

#include <array>
#include <cstddef>

#if defined(_WIN32)
#include "pad_slot_scan.h"
#else
#include <vector>

#include <SDL3/SDL_gamepad.h>
#endif

struct ImGuiIO;

namespace ssf2t {

// Feeds controllers to Dear ImGui, so the launcher and the in-game overlays navigate with a pad. The runtime's input
// system cannot be used for this: it starts only with the game, and an open overlay keeps the game from reading it.
//
// On Windows the pads are read with XInput, synchronously on the calling thread: no SDL instance of the program's
// own, no thread, no hidden window. Elsewhere it uses the one SDL shared with the runtime and starts and quits the
// gamepad subsystem on its own; SDL counts the starts, so the runtime's input is unaffected. Every connected
// controller is read and their inputs combined, so any of them navigates. Nothing is fed while another program is in
// the foreground.
//
// Call Update() once per frame while the dialog is open and Release() when it closes. Release() lets go of every
// key, so nothing read here is left held in the game. A pad unplugged while a button is held has its key released
// on the next Update.
class ImGuiGamepad {
 public:
  static constexpr size_t kButtonCount = 10;
  static constexpr size_t kStickCount = 4;

  ImGuiGamepad() = default;
  ~ImGuiGamepad();
  ImGuiGamepad(const ImGuiGamepad&) = delete;
  ImGuiGamepad& operator=(const ImGuiGamepad&) = delete;

  void Update(ImGuiIO& io);
  void Release(ImGuiIO& io);

 private:
  struct Input {
    std::array<bool, kButtonCount> down{};
    std::array<float, kStickCount> amount{};
  };

  bool Read(Input& input);

#if defined(_WIN32)
  PadSlotScan scan_;
#else
  void DrainEvents();
  void CloseAll();

  std::vector<SDL_Gamepad*> pads_;
  bool subsystem_ = false;
#endif
};

}  // namespace ssf2t
