#pragma once

#include <filesystem>

namespace ssf2t {

// The log file this run is writing: the configured `log_file`, or the target of the logger's file
// sink. Empty when file logging is not active.
std::filesystem::path ActiveLogFile();

}  // namespace ssf2t
