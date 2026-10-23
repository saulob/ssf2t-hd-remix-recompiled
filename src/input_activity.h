#pragma once

#include <functional>

#include <rex/input/device.h>
#include <rex/input/input.h>

namespace rex::input {

// A passive hook added by patches/rexglue-sdk/0048-input-activity-callback.patch.
// Called on the input polling thread when a device's state changes.
void SetInputActivityCallback(
    std::function<void(DeviceId device, const X_INPUT_GAMEPAD& gamepad)> callback);

}  // namespace rex::input
