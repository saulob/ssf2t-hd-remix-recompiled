#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ssf2t {

inline bool IsLauncherVisibilityArgument(std::string_view argument) {
  for (const std::string_view name : {"--ssf2t_skip_launcher", "--ssf2t_launcher"}) {
    if (argument == name || (argument.starts_with(name) && argument[name.size()] == '=')) {
      return true;
    }
  }
  return false;
}

// The updated program opens the launcher again, whatever the player's launcher setting or the original command line.
inline std::vector<std::string> UpdateRestartArguments(std::vector<std::string> arguments) {
  arguments.erase(std::remove_if(arguments.begin(), arguments.end(), IsLauncherVisibilityArgument), arguments.end());
  arguments.emplace_back("--ssf2t_launcher=true");
  return arguments;
}

}  // namespace ssf2t
