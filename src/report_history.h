// My reports: the reports this PC sent, kept in the user folder. This file is the only link between a player and their
// reports; the online service holds no list of them. Each entry's receipt proves ownership of that one report.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "online/protocol.h"

namespace ssf2t {

// Matches INCIDENT.statusBatch in server/ssf2t-lobby/src/incidents.js.
inline constexpr size_t kMostTrackedReports = 30;
inline constexpr char kReportHistoryFile[] = "bug_reports.json";

struct TrackedReport {
  std::string ticket;
  std::string receipt;
  int64_t submitted_at = 0;
  std::string description;
  std::string log_file;
  std::string launcher_version;
  std::string guest;
  std::string platform;
  std::string operating_system;
  std::string status = "new";
  int64_t status_updated_at = 0;
  std::string fixed_version;
  std::string player_message;
  bool unseen = false;
  // The developer message appeared or changed and the report has not been opened since.
  bool message_unseen = false;
};

// English, for Tr; nullptr for a status this launcher does not know.
const char* ReportStatusText(std::string_view status);
// "Investigating", or "Fixed in version 0.4.1"; translated.
std::string ReportStatusLine(const TrackedReport& report);
// The start of the description on one line.
std::string ReportPreview(std::string_view description);

class ReportHistory {
 public:
  explicit ReportHistory(std::filesystem::path file) : file_(std::move(file)) {}

  // A damaged file is set aside, not overwritten, and the history starts empty.
  void Load();
  // Written to a new file and moved into place, so an interrupted save leaves the previous history.
  bool Save() const;

  const std::vector<TrackedReport>& reports() const { return reports_; }
  bool HasUnseen() const;
  bool HasUnseenMessage() const;

  // The history changes in memory even when saving fails; false says it was not saved.
  bool Add(TrackedReport report);
  // Newer facts for the reports held here; a difference marks the report unseen, and a new or changed developer message
  // also marks its message unseen. False when nothing changed.
  bool Apply(const std::vector<online::IncidentStatus>& statuses);
  bool MarkSeen(std::string_view ticket);
  bool Remove(std::string_view ticket);

  // The saved text of the history; the receipts are in it.
  std::string Dump() const;

 private:
  std::filesystem::path file_;
  std::vector<TrackedReport> reports_;
};

}  // namespace ssf2t
