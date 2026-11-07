// The Bug Report page's work without a window: the logs it offers, the report it builds and what the service's answers mean.
//
// A report is built into a BugReportPackage, whether or not the player previews it. The preview and the request both read
// that package's fields, so what the player can read is what goes out.
#pragma once

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "log_scrubber.h"
#include "online/protocol.h"
#include "report_history.h"

namespace ssf2t {

inline constexpr size_t kMostGameLogs = 5;
inline constexpr size_t kFewestDescriptionCharacters = 10;
inline constexpr size_t kMostDescriptionCharacters = 500;
inline constexpr size_t kMostMetadataCharacters = 64;
// Matches INCIDENT.logBytes in server/ssf2t-lobby/src/incidents.js: the service refuses more.
inline constexpr size_t kMostLogBytes = 64 * 1024;

// kGameplay: the session reached the game's own code. kLauncher: it never left the launcher.
enum class LogActivity { kLauncher, kGameplay };

enum class LogRole { kNone, kCurrent, kLastGameplay, kGameplay, kLauncherOnly };

struct GameLog {
  std::filesystem::path path;
  std::string name;
  std::time_t written = 0;
  uintmax_t bytes = 0;
  LogActivity activity = LogActivity::kLauncher;
  LogRole role = LogRole::kNone;
};

// True for ssf2t_hd_remix_<3 to 9 digits>.log, the only names the port writes.
bool IsGameLogName(std::string_view name);

// The logs in `folder`: `running_log` (this launcher's own session) first, then newest first. Left out: other files, empty
// files and sessions that closed in the launcher without starting the game.
std::vector<GameLog> FindGameLogs(const std::filesystem::path& folder, const std::filesystem::path& running_log,
                                  size_t most = kMostGameLogs);

// The log a report is most likely about: the newest gameplay session that is not the running one, else the newest log
// that is not the running one, else the first.
size_t DefaultGameLog(const std::vector<GameLog>& logs);

// English, for Tr; nullptr for kNone.
const char* LogRoleText(LogRole role);
const char* PreviewButtonText(bool preview_open);

// "Nov 5, 2026 03:51", or "05/11/2026 03:51" in Portuguese; local time.
std::string LogDateText(std::time_t written, bool portuguese);
// "184 KB", "1.2 MB".
std::string LogSizeText(uintmax_t bytes);

// The bytes of `text` that hold its first `most` UTF-8 characters.
size_t BytesOfCharacters(std::string_view text, size_t most);
size_t CountCharacters(std::string_view text);

enum class DraftProblem { kNone, kDescriptionTooShort, kNoLog };
// Spaces around the description do not count toward its length.
DraftProblem CheckDraft(std::string_view description, bool has_log);

// The fields of this port and PC that go with every report.
struct ReportFacts {
  std::string launcher_version;
  std::string guest;  // base013 or tu023
  std::string platform;
  std::string operating_system;
  std::string runtime;
};

struct BugReportField {
  std::string_view key;    // the request's member
  std::string_view label;  // English; the preview translates it
  std::string value;
};

struct BugReportPackage {
  std::vector<BugReportField> fields;
  const std::string& Value(std::string_view key) const;
};

// The end of `text`, at most `most` bytes, starting on a whole line.
std::string LastBytes(std::string_view text, size_t most);

enum class BuildFailure { kNone, kLogUnreadable, kDescriptionTooShort };

struct BuildResult {
  std::optional<BugReportPackage> package;
  BuildFailure failure = BuildFailure::kNone;
};

// Scrubs the log's text and bounds it. Nothing when the log cannot be read or holds nothing, or when the description is
// too short once private details are taken out of it.
BuildResult BuildBugReport(std::string_view description, const GameLog& log, const ReportFacts& facts,
                           const PrivateFacts& private_facts);

std::string BugReportPreviewText(const BugReportPackage& package);
online::IncidentFields BugReportRequestFields(const BugReportPackage& package);
// The stored report as the same fields a preview shows.
BugReportPackage SentReportOf(const std::map<std::string, std::string>& report);

enum class BugReportOutcome { kSent, kRecentLimit, kDailyLimit, kUnreachable, kRefused, kServiceError, kNotSupported };

// What a service answer code means for the page.
BugReportOutcome BugReportOutcomeOf(const std::string& code);

// English, for Tr; nullptr unless `outcome` is one of the two limits.
const char* BugReportLimitTitle(BugReportOutcome outcome);

bool IsTicket(std::string_view text);
bool IsReceipt(std::string_view text);

struct BugReportReceipt {
  BugReportOutcome outcome = BugReportOutcome::kServiceError;
  std::string ticket;
  // Proves ownership of this one report. Never logged.
  std::string credential;
  int64_t submitted_at = 0;
};

// What My reports keeps of a sent report.
TrackedReport TrackedReportOf(const BugReportPackage& package, const BugReportReceipt& receipt);

}  // namespace ssf2t
