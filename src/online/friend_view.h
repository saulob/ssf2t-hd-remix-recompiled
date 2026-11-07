#pragma once

#include <string>
#include <vector>

#include "online/online_service.h"

namespace ssf2t::online {

// "TU023" for tu023, "Base013" for base013.
std::string GuestLabel(const std::string& guest);

struct FriendRowView {
  std::string status;  // the line under the friend's name
  bool incompatible = false;
};

// What one friend's row shows, given what this player is doing right now. Matches start from the game's own
// Multiplayer > Player Match, so a row offers no match controls. A latency appears only once the gameplay connection
// has measured one.
FriendRowView DescribeFriend(const Snapshot& state, const Friend& entry);

// What the Lobby page does with a listed player, from this player's own friends and requests: Add friend only while no
// relationship exists, so the Lobby and the Friends tab never disagree.
enum class LobbyRelation { kNone, kFriend, kOutgoing, kIncoming };

struct LobbyRowView {
  std::string status;  // the line under the player's name
  bool incompatible = false;
  LobbyRelation relation = LobbyRelation::kNone;
  bool can_add = false;
};

LobbyRowView DescribeLobbyPlayer(const Snapshot& state, const LobbyPlayer& player);

// What the Lobby shows in place of a list; empty when it shows one.
std::string LobbyMessage(const LobbySnapshot& lobby);

// A row of the in-game Invite Player list: every friend, with only the ones who can receive an invite selectable.
struct InviteChoice {
  std::string player;
  std::string name;
  std::string status;  // Online, Online Match, In game, Offline, or why it cannot be chosen
  bool selectable = false;
};
std::vector<InviteChoice> InviteChoices(const std::vector<Friend>& friends);

}  // namespace ssf2t::online
