#pragma once

#include <array>
#include <cstddef>

namespace ssf2t {

struct PlayerNameSetting {
  const char* key;
  const char* sdk_key;
  const char* label;
  const char* default_name;
};

inline constexpr char kPlayerNameHint[] = "Up to 15 characters.";
inline constexpr char kPlayerNamesTitle[] = "Local player names";
inline constexpr char kPlayerNamesHelp[] = "Names shown in Local Match. Online Match uses your Online name.";

inline constexpr std::array<PlayerNameSetting, 2> kPlayerNames = {{
    {"ssf2t_player_1_name", "local_player_1_name", "Player 1", "Player 1"},
    {"ssf2t_player_2_name", "local_player_2_name", "Player 2", "Player 2"},
}};

}  // namespace ssf2t
