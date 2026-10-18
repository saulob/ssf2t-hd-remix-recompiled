// HTTPS downloads for launcher pages (shader packs, updates, ...). Windows uses
// WinHTTP; Linux and macOS use the system's curl. Nothing is contacted unless
// the game calls these, so a launcher stays offline until the player asks.
#pragma once

#include <string>

namespace trg {

class Task;

// GETs `url` (following redirects) into `out`. When `task` is given it reports
// progress and stops on Cancel. Returns "" on success, else an error message.
std::string HttpGet(const std::string& url, std::string& out, Task* task = nullptr);

// Downloads `url` to `utf8_path` on a Task, through `<path>.part`.
std::string DownloadFile(Task& task, const std::string& url, const std::string& utf8_path);

}  // namespace trg
