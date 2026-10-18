#pragma once

namespace ssf2t {

constexpr double kReleaseTimeoutSeconds = 1.0;

// Whether the modal may release the game's input: nothing it received is still held, or the wait ran out.
inline bool InputReleased(bool anything_held, double waited_seconds, double timeout_seconds) {
  return !anything_held || waited_seconds >= timeout_seconds;
}

// Closing a modal: asking to close starts a wait, and the game gets its input back only after what was pressed
// in the dialog is released, so the closing press never also reaches the game's menu.
class CloseGate {
 public:
  void Request() { requested_ = true; }
  bool requested() const { return requested_; }

  // Once per frame. True when the dialog may go and the game may read its input again.
  bool Step(bool anything_held, double now) {
    if (!requested_) return false;
    if (since_ < 0.0) since_ = now;
    return InputReleased(anything_held, now - since_, kReleaseTimeoutSeconds);
  }

 private:
  bool requested_ = false;
  double since_ = -1.0;
};

}  // namespace ssf2t
