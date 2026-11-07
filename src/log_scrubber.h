// Removes what would identify a player or this PC from text that is about to leave it. Everything else stays: frame
// counts, addresses in guest memory, hashes of game files and driver versions are what a report is for. Filtering cannot
// promise to catch every private value, which is why the Bug Report page shows the result before anything is sent.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ssf2t {

// What only this PC knows. A name may be listed twice: logs can hold it in different spellings.
struct PrivateFacts {
  std::vector<std::string> user_names;
  std::vector<std::string> machine_names;
  std::vector<std::string> profile_folders;
  std::vector<std::string> install_folders;
  std::vector<std::string> secrets;      // the online token and the hash the service keeps of it
  std::vector<std::string> own_codes;    // this player's Friend Code
  std::vector<std::string> player_names; // the Online name
};

// The running system's facts, the online identity in `user_dir` when there is one (none is created), the Online name,
// the Local Match player names and `install_folders`.
PrivateFacts GatherPrivateFacts(const std::vector<std::filesystem::path>& install_folders,
                                const std::filesystem::path& user_dir, const std::string& online_name = {},
                                const std::vector<std::string>& local_player_names = {});

// Valid UTF-8 with LF line ends and no other control characters than tabs, whatever `text` held.
std::string ScrubText(std::string_view text, const PrivateFacts& facts);

}  // namespace ssf2t
