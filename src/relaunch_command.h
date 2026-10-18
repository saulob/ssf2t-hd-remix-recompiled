#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ssf2t {

struct RelaunchCommand {
  std::string executable;
  std::vector<std::string> argv;
};

// Inside an AppImage the AppImage file is started again: this process runs from a mount that closes when it exits.
// `args` is this process's own command line.
inline std::optional<RelaunchCommand> MakeRelaunchCommand(std::vector<std::string> args, const char* appimage,
                                                          std::string_view self_executable,
                                                          std::string_view extra_arg) {
  const std::string executable = appimage && *appimage ? std::string(appimage) : std::string(self_executable);
  if (executable.empty() || args.empty()) {
    return std::nullopt;
  }
  args.front() = executable;
  args.emplace_back(extra_arg);
  return RelaunchCommand{executable, std::move(args)};
}

}  // namespace ssf2t
