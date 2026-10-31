#pragma once

#include <cstdint>
#include <cstdlib>

namespace ssf2t {

// The keyboard and mouse feed local user 0; pads take users 1-3 (ReXGlue local player slots).
inline constexpr int32_t kKeyboardUser = 0;
inline constexpr int32_t kFirstPadUser = 1;
inline constexpr int32_t kLocalUserCount = 4;
inline constexpr int16_t kPadStickDeadzone = 7849;
inline constexpr uint8_t kPadTriggerThreshold = 30;

constexpr bool ValidUser(int32_t user) { return user >= 0 && user < kLocalUserCount; }

enum class InputSource { kNone, kController, kKeyboard, kMouse };

// What the game read from one user's pad this poll, reduced to "pressing something" and the button word.
struct PadReading {
  uint16_t buttons = 0;
  uint8_t left_trigger = 0;
  uint8_t right_trigger = 0;
  int16_t thumbs[4] = {};

  bool Meaningful() const {
    if (buttons || left_trigger > kPadTriggerThreshold || right_trigger > kPadTriggerThreshold) return true;
    for (const int16_t axis : thumbs) {
      if (std::abs(int32_t(axis)) > kPadStickDeadzone) return true;
    }
    return false;
  }
};

// Which local users last produced real input, from the game's own per-user polls.
class InputActivity {
 public:
  void Observe(int32_t user, const PadReading& reading) {
    if (!ValidUser(user)) return;
    const bool active = reading.Meaningful();
    const bool pressed = active && (!active_[user] || reading.buttons != buttons_[user]);
    active_[user] = active;
    buttons_[user] = reading.buttons;
    if (!pressed) return;
    last_user_ = user;
    if (user != kKeyboardUser) last_pad_user_ = user;
  }

  int32_t last_user() const { return last_user_; }
  int32_t last_pad_user() const { return last_pad_user_; }

 private:
  bool active_[kLocalUserCount] = {};
  uint16_t buttons_[kLocalUserCount] = {};
  int32_t last_user_ = -1;
  int32_t last_pad_user_ = -1;
};

struct OwnerChoice {
  int32_t user = kKeyboardUser;
  const char* source = "keyboard-default";
};

// The menu item that entered Online Match made the pressing user the active one (SetControllerMask).
inline OwnerChoice ChooseMenuOwner(int32_t active_user, const InputActivity& activity) {
  if (ValidUser(active_user)) return {active_user, "menu"};
  if (ValidUser(activity.last_user())) return {activity.last_user(), "menu-last-input"};
  return {};
}

// The invite prompt is a port overlay, so the game reads no input while it is up: the pad that accepted is the pad
// the player was last using.
inline OwnerChoice ChooseInviteOwner(InputSource accepted_by, int32_t current_owner, const InputActivity& activity,
                                     int32_t live_user) {
  if (accepted_by == InputSource::kKeyboard) return {kKeyboardUser, "invite-keyboard"};
  if (accepted_by == InputSource::kController) {
    if (ValidUser(activity.last_pad_user())) return {activity.last_pad_user(), "invite-controller"};
    if (ValidUser(current_owner) && current_owner != kKeyboardUser) return {current_owner, "invite-controller-owner"};
    return {kFirstPadUser, "invite-controller-first-pad"};
  }
  const char* source = "invite-mouse-fallback";
  if (ValidUser(current_owner)) return {current_owner, source};
  if (ValidUser(activity.last_pad_user())) return {activity.last_pad_user(), source};
  if (ValidUser(activity.last_user())) return {activity.last_user(), source};
  if (ValidUser(live_user)) return {live_user, source};
  return {kKeyboardUser, source};
}

// The local user that owns this online match: set when it is entered, kept through every screen of it, released
// only when the match really ends. The last owner still answers the error popup the game raises after the end.
class MatchInputOwner {
 public:
  void Set(OwnerChoice choice) {
    user_ = choice.user;
    source_ = choice.source;
    active_ = true;
  }
  void End() { active_ = false; }

  bool active() const { return active_; }
  int32_t user() const { return user_; }
  const char* source() const { return source_; }
  // The user whose pad drives the fight: the owner, else the game's active user, else the keyboard.
  int32_t FightUser(int32_t active_user) const {
    if (active_ && ValidUser(user_)) return user_;
    return ValidUser(active_user) ? active_user : kKeyboardUser;
  }

 private:
  int32_t user_ = -1;
  const char* source_ = "none";
  bool active_ = false;
};

// What the menus' input object needs so only `user` answers a popup: SetControllerMask(user + 1) sets bit user + 1.
constexpr uint32_t ControllerMaskFor(int32_t user) { return 2u << user; }

// Game and session events. Only real ends of the match release it; screens the game passes through do not.
enum class MatchEvent {
  kOnlineZoneEntered,
  kOnlineZoneLeft,
  kLobby,
  kCharacterSelect,
  kFight,
  kPause,
  kAchievements,
  kResults,
  kErrorPopup,
  kRemotePlayerLeft,
  kLocalLeave,
  kSessionDeleted,
  kPeerTimedOut,
};

constexpr bool EndsOnlineMatch(MatchEvent event) {
  return event == MatchEvent::kLocalLeave || event == MatchEvent::kSessionDeleted ||
         event == MatchEvent::kPeerTimedOut;
}

}  // namespace ssf2t
