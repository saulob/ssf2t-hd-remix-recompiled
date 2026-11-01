#include "online/friend_view.h"

#include <algorithm>
#include <cctype>

#include "localization/ui_text.h"

namespace ssf2t::online {

std::string GuestLabel(const std::string& guest) {
  std::string label;
  for (const char c : guest) label += char(std::toupper(static_cast<unsigned char>(c)));
  if (guest == kGuestBase013 && label.size() > 1) {
    label = "B" + guest.substr(1);
  }
  return label;
}

namespace {

std::string MatchStatus(const MatchSnapshot& match) {
  const std::string guest = GuestLabel(match.guest);
  if (match.peer_lost) return Tr("Reconnecting");
  if (!match.endpoints_exchanged) return Tr("Connecting");
  const std::string latency = match.rtt_ms ? Tr(" \xC2\xB7 ") + std::to_string(*match.rtt_ms) + Tr(" ms") : std::string();
  switch (match.link) {
    case NetplayLink::kFighting: return Tr("In game \xC2\xB7 ") + guest + latency;
    case NetplayLink::kConnected: return Tr("Connected \xC2\xB7 ") + guest + latency;
    case NetplayLink::kConnecting: return Tr("Connecting \xC2\xB7 ") + guest;
    case NetplayLink::kNone: break;
  }
  return Tr("Connected \xC2\xB7 ") + guest;
}

}  // namespace

std::vector<InviteChoice> InviteChoices(const std::vector<Friend>& friends) {
  std::vector<InviteChoice> choices;
  for (const Friend& f : friends) {
    InviteChoice choice{f.player, f.name, Tr("Offline"), false};
    if (f.online) {
      const bool busy = f.state == FriendState::kConnecting || f.state == FriendState::kInGame;
      choice.status = f.state == FriendState::kPlayerMatch ? Tr("Online Match")
                      : busy                               ? Tr("In game")
                                                           : Tr("Online");
      if (!f.compatible) choice.status = Tr("Game versions do not match");
      choice.selectable = f.compatible && !busy;
    }
    choices.push_back(std::move(choice));
  }
  std::stable_partition(choices.begin(), choices.end(), [](const InviteChoice& c) { return c.selectable; });
  return choices;
}

FriendRowView DescribeFriend(const Snapshot& state, const Friend& entry) {
  FriendRowView view;
  view.status = FriendStatusLine(entry);
  if (state.match && state.match->peer_player == entry.player) {
    view.status = MatchStatus(*state.match);
    return view;
  }
  view.incompatible = entry.online && !entry.compatible;
  return view;
}

}  // namespace ssf2t::online
