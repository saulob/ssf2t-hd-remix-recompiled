#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ssf2t {

enum class OnlineTab { kSettings, kFriends, kLobby };
inline constexpr OnlineTab kOnlineTabs[] = {OnlineTab::kSettings, OnlineTab::kFriends, OnlineTab::kLobby};

inline const char* OnlineTabLabel(OnlineTab tab) {
  switch (tab) {
    case OnlineTab::kSettings: return "Settings";
    case OnlineTab::kFriends: return "Friends";
    case OnlineTab::kLobby: return "Lobby";
  }
  return "";
}

// `label` is already translated. Friends counts the requests waiting for an answer; the ID stays fixed as it changes.
inline std::string OnlineTabTitle(const std::string& label, OnlineTab tab, size_t friend_requests) {
  const bool counted = tab == OnlineTab::kFriends && friend_requests > 0;
  return label + (counted ? " (" + std::to_string(friend_requests) + ")" : "") + "###tab";
}

// Sections of the Online page, in order: with Online play off only its switch, otherwise the tabs and the selected one.
enum class OnlineSection {
  kOnlinePlay, kTabs, kOnlineName, kFriendCode, kConnectionTest, kPingDisplay, kSecretCharacters, kFriendRequests,
  kFriends, kLobby
};

inline std::vector<OnlineSection> OnlinePageSections(bool online_on, OnlineTab tab, size_t friend_requests) {
  if (!online_on) return {OnlineSection::kOnlinePlay};
  std::vector<OnlineSection> sections{OnlineSection::kOnlinePlay, OnlineSection::kTabs};
  switch (tab) {
    case OnlineTab::kSettings:
      sections.insert(sections.end(), {OnlineSection::kOnlineName, OnlineSection::kFriendCode,
                                       OnlineSection::kConnectionTest, OnlineSection::kPingDisplay,
                                       OnlineSection::kSecretCharacters});
      break;
    case OnlineTab::kFriends:
      if (friend_requests > 0) sections.push_back(OnlineSection::kFriendRequests);
      sections.push_back(OnlineSection::kFriends);
      break;
    case OnlineTab::kLobby:
      sections.push_back(OnlineSection::kLobby);
      break;
  }
  return sections;
}

// Turning Online play on always opens on Settings.
class OnlineTabSelection {
 public:
  OnlineTab selected() const { return selected_; }
  void Select(OnlineTab tab) { selected_ = tab; }
  void TrackOnlinePlay(bool on) {
    if (on && !was_on_) selected_ = OnlineTab::kSettings;
    was_on_ = on;
  }

 private:
  OnlineTab selected_ = OnlineTab::kSettings;
  bool was_on_ = false;
};

}  // namespace ssf2t
