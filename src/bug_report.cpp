#include "bug_report.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <system_error>

#include "localization/ui_text.h"

namespace ssf2t {
namespace fs = std::filesystem;
namespace {

constexpr std::string_view kLogPrefix = "ssf2t_hd_remix_";
constexpr std::string_view kLogSuffix = ".log";
constexpr size_t kFewestLogDigits = 3, kMostLogDigits = 9;
constexpr size_t kLogsLookedAt = 200;
constexpr size_t kSessionHeadBytes = 128 * 1024;
// More than the report keeps, so that scrubbing cannot leave it short.
constexpr size_t kLogTailBytes = 256 * 1024;

// The game process writes this when its guest memory is mapped; the launcher writes the other when it closes.
constexpr std::string_view kGameplayMark = "Guest memory arena mapped";
constexpr std::string_view kLauncherClosedMark = "Launcher closed";

constexpr char kDescriptionKey[] = "description";
constexpr char kLogTextKey[] = "log_text";
constexpr size_t kReceiptHexDigits = 64;
constexpr size_t kTicketLength = 11;

struct FieldName {
  std::string_view key;
  std::string_view label;
};
constexpr FieldName kFieldNames[] = {
    {kDescriptionKey, "Description"},
    {"launcher_version", "Port version"},
    {"guest", "Game image"},
    {"platform", "Platform"},
    {"operating_system", "Operating system"},
    {"runtime", "Runtime"},
    {"log_file", "Selected log"},
    {"log_clipped", "Log shortened to its end"},
    {kLogTextKey, "Log after privacy filtering"},
};

constexpr const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

std::string ReadPart(const fs::path& file, uintmax_t offset, size_t size) {
  std::ifstream in(file, std::ios::binary);
  std::string text(size, '\0');
  in.seekg(std::streamoff(offset));
  in.read(text.data(), std::streamsize(size));
  text.resize(size_t(std::max<std::streamsize>(in.gcount(), 0)));
  return text;
}

struct Session {
  LogActivity activity = LogActivity::kLauncher;
  bool closed_in_launcher = false;
};

Session ReadSession(const fs::path& file) {
  const std::string head = ReadPart(file, 0, kSessionHeadBytes);
  const auto holds = [&](std::string_view mark) { return head.find(mark) != std::string::npos; };
  if (holds(kGameplayMark)) return {LogActivity::kGameplay, false};
  return {LogActivity::kLauncher, holds(kLauncherClosedMark)};
}

std::time_t SystemTimeOf(fs::file_time_type written) {
  const auto system = std::chrono::system_clock::now() +
                      std::chrono::duration_cast<std::chrono::system_clock::duration>(written - fs::file_time_type::clock::now());
  return std::chrono::system_clock::to_time_t(system);
}

struct Candidate {
  fs::path path;
  std::string name;
  fs::file_time_type written;
  uintmax_t bytes = 0;
  bool running = false;
};

std::string NameOf(const fs::path& path) {
  const std::u8string text = path.filename().u8string();
  return std::string(text.begin(), text.end());
}

bool IsRunningLog(const fs::path& file, const fs::path& running_log) {
  std::error_code ec;
  return !running_log.empty() && (file == running_log || fs::equivalent(file, running_log, ec));
}

// A directory listing lags behind a file that is still open, so the running log is asked for its own size and time.
std::vector<Candidate> LogCandidates(const fs::path& folder, const fs::path& running_log) {
  std::vector<Candidate> found;
  std::error_code ec;
  for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code entry;
    const std::string name = NameOf(it->path());
    if (!IsGameLogName(name) || !it->is_regular_file(entry) || it->is_symlink(entry)) continue;
    const bool running = IsRunningLog(it->path(), running_log);
    const uintmax_t bytes = running ? fs::file_size(it->path(), entry) : it->file_size(entry);
    const fs::file_time_type written = running ? fs::last_write_time(it->path(), entry) : it->last_write_time(entry);
    if (entry || (bytes == 0 && !running)) continue;
    found.push_back({it->path(), name, written, bytes, running});
  }
  std::sort(found.begin(), found.end(), [](const Candidate& a, const Candidate& b) {
    if (a.running != b.running) return a.running;
    return a.written != b.written ? a.written > b.written : a.name > b.name;
  });
  return found;
}

void MarkRoles(std::vector<GameLog>& logs) {
  const auto played = [](const GameLog& log) { return log.activity == LogActivity::kGameplay && log.role == LogRole::kNone; };
  const auto last = std::find_if(logs.begin(), logs.end(), played);
  if (last != logs.end()) last->role = LogRole::kLastGameplay;
  for (GameLog& log : logs) {
    if (log.role != LogRole::kNone) continue;
    log.role = log.activity == LogActivity::kGameplay ? LogRole::kGameplay : LogRole::kLauncherOnly;
  }
}

std::string TrimmedDescription(std::string_view text) {
  constexpr std::string_view kSpace = " \t\r\n";
  const size_t first = text.find_first_not_of(kSpace);
  if (first == std::string_view::npos) return {};
  return std::string(text.substr(first, text.find_last_not_of(kSpace) - first + 1));
}

// A metadata field the service accepts: no more than its limit, no spaces at either end.
std::string Clamped(std::string text) {
  text.resize(BytesOfCharacters(text, kMostMetadataCharacters));
  return TrimmedDescription(text);
}

}  // namespace

bool IsGameLogName(std::string_view name) {
  if (!name.starts_with(kLogPrefix) || !name.ends_with(kLogSuffix)) return false;
  name.remove_prefix(kLogPrefix.size());
  name.remove_suffix(kLogSuffix.size());
  return name.size() >= kFewestLogDigits && name.size() <= kMostLogDigits &&
         std::all_of(name.begin(), name.end(), [](char c) { return c >= '0' && c <= '9'; });
}

std::vector<GameLog> FindGameLogs(const fs::path& folder, const fs::path& running_log, size_t most) {
  std::vector<GameLog> logs;
  std::vector<Candidate> candidates = LogCandidates(folder, running_log);
  if (candidates.size() > kLogsLookedAt) candidates.resize(kLogsLookedAt);
  for (const Candidate& candidate : candidates) {
    if (logs.size() == most) break;
    const Session session = ReadSession(candidate.path);
    // A session that ended without closing the launcher stays: that is what a launcher crash leaves.
    if (session.closed_in_launcher && !candidate.running) continue;
    logs.push_back({candidate.path, candidate.name, SystemTimeOf(candidate.written), candidate.bytes, session.activity,
                    candidate.running ? LogRole::kCurrent : LogRole::kNone});
  }
  MarkRoles(logs);
  return logs;
}

size_t DefaultGameLog(const std::vector<GameLog>& logs) {
  const auto played = std::find_if(logs.begin(), logs.end(), [](const GameLog& log) {
    return log.activity == LogActivity::kGameplay && log.role != LogRole::kCurrent;
  });
  if (played != logs.end()) return size_t(played - logs.begin());
  const auto earlier = std::find_if(logs.begin(), logs.end(), [](const GameLog& log) { return log.role != LogRole::kCurrent; });
  return earlier == logs.end() ? 0 : size_t(earlier - logs.begin());
}

const char* LogRoleText(LogRole role) {
  switch (role) {
    case LogRole::kCurrent: return "Current session";
    case LogRole::kLastGameplay: return "Last gameplay";
    case LogRole::kGameplay: return "Gameplay session";
    case LogRole::kLauncherOnly: return "Launcher only";
    case LogRole::kNone: break;
  }
  return nullptr;
}

const char* PreviewButtonText(bool preview_open) { return preview_open ? "Close preview" : "Open preview"; }

std::string LogDateText(std::time_t written, bool portuguese) {
  const std::tm* shown = std::localtime(&written);
  if (!shown) return {};
  const std::tm local = *shown;
  char text[40];
  if (portuguese) {
    std::snprintf(text, sizeof(text), "%02d/%02d/%04d %02d:%02d", local.tm_mday, local.tm_mon + 1, local.tm_year + 1900,
                  local.tm_hour, local.tm_min);
  } else {
    std::snprintf(text, sizeof(text), "%s %d, %04d %02d:%02d", kMonths[std::clamp(local.tm_mon, 0, 11)], local.tm_mday,
                  local.tm_year + 1900, local.tm_hour, local.tm_min);
  }
  return text;
}

std::string LogSizeText(uintmax_t bytes) {
  constexpr uintmax_t kKilobyte = 1024, kMegabyte = kKilobyte * 1024;
  char text[32];
  if (bytes < kMegabyte) {
    std::snprintf(text, sizeof(text), "%llu KB", static_cast<unsigned long long>(std::max<uintmax_t>(1, (bytes + kKilobyte / 2) / kKilobyte)));
  } else {
    std::snprintf(text, sizeof(text), "%.1f MB", double(bytes) / double(kMegabyte));
  }
  return text;
}

size_t BytesOfCharacters(std::string_view text, size_t most) {
  size_t characters = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    if ((static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) continue;
    if (characters == most) return i;
    ++characters;
  }
  return text.size();
}

size_t CountCharacters(std::string_view text) {
  return size_t(std::count_if(text.begin(), text.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
}

DraftProblem CheckDraft(std::string_view description, bool has_log) {
  if (CountCharacters(TrimmedDescription(description)) < kFewestDescriptionCharacters) return DraftProblem::kDescriptionTooShort;
  return has_log ? DraftProblem::kNone : DraftProblem::kNoLog;
}

const std::string& BugReportPackage::Value(std::string_view key) const {
  static const std::string missing;
  const auto found = std::find_if(fields.begin(), fields.end(), [&](const BugReportField& field) { return field.key == key; });
  return found == fields.end() ? missing : found->value;
}

std::string LastBytes(std::string_view text, size_t most) {
  if (text.size() <= most) return std::string(text);
  std::string_view tail = text.substr(text.size() - most);
  const size_t line = tail.find('\n');
  if (line != std::string_view::npos && line + 1 < tail.size()) tail.remove_prefix(line + 1);
  return std::string(tail);
}

BuildResult BuildBugReport(std::string_view description, const GameLog& log, const ReportFacts& facts,
                           const PrivateFacts& private_facts) {
  std::error_code ec;
  const uintmax_t size = fs::file_size(log.path, ec);
  if (ec || size == 0) return {std::nullopt, BuildFailure::kLogUnreadable};
  const uintmax_t offset = size > kLogTailBytes ? size - kLogTailBytes : 0;
  std::string raw = ReadPart(log.path, offset, size_t(size - offset));
  if (raw.empty()) return {std::nullopt, BuildFailure::kLogUnreadable};
  if (offset > 0) {
    const size_t line = raw.find('\n');
    if (line != std::string::npos) raw.erase(0, line + 1);
  }
  const std::string scrubbed = ScrubText(raw, private_facts);
  std::string text = LastBytes(scrubbed, kMostLogBytes);
  if (text.find_first_not_of(" \t\n") == std::string::npos) return {std::nullopt, BuildFailure::kLogUnreadable};
  const bool clipped = offset > 0 || text.size() < scrubbed.size();

  std::string summary = TrimmedDescription(ScrubText(TrimmedDescription(description), private_facts));
  summary.resize(BytesOfCharacters(summary, kMostDescriptionCharacters));
  summary = TrimmedDescription(summary);
  if (CountCharacters(summary) < kFewestDescriptionCharacters) return {std::nullopt, BuildFailure::kDescriptionTooShort};

  std::string values[] = {std::move(summary),
                          Clamped(facts.launcher_version),
                          Clamped(facts.guest),
                          Clamped(facts.platform),
                          Clamped(facts.operating_system),
                          Clamped(facts.runtime),
                          log.name,
                          clipped ? "yes" : "no",
                          std::move(text)};
  BugReportPackage package;
  for (size_t i = 0; i < std::size(kFieldNames); ++i) package.fields.push_back({kFieldNames[i].key, kFieldNames[i].label, std::move(values[i])});
  return {std::move(package), BuildFailure::kNone};
}

BugReportPackage SentReportOf(const std::map<std::string, std::string>& report) {
  BugReportPackage package;
  for (const FieldName& name : kFieldNames) {
    const auto found = report.find(std::string(name.key));
    package.fields.push_back({name.key, name.label, found == report.end() ? std::string() : found->second});
  }
  return package;
}

std::string BugReportPreviewText(const BugReportPackage& package) {
  std::string out;
  for (const BugReportField& field : package.fields) {
    const std::string label = Tr(std::string(field.label));
    if (field.key == kDescriptionKey || field.key == kLogTextKey) {
      out += (out.empty() ? "" : "\n") + label + ":\n" + field.value + "\n";
      if (field.key == kDescriptionKey) out += "\n";
    } else {
      out += label + ": " + (field.value.empty() ? "-" : field.value) + "\n";
    }
  }
  return out;
}

online::IncidentFields BugReportRequestFields(const BugReportPackage& package) {
  online::IncidentFields fields;
  for (const BugReportField& field : package.fields) fields.emplace_back(std::string(field.key), field.value);
  return fields;
}

BugReportOutcome BugReportOutcomeOf(const std::string& code) {
  if (code == "ok") return BugReportOutcome::kSent;
  if (code == "incident_hourly_limit" || code == "limit") return BugReportOutcome::kRecentLimit;
  if (code == "incident_daily_limit") return BugReportOutcome::kDailyLimit;
  if (code == "bad_request" || code == "too_large") return BugReportOutcome::kRefused;
  if (code == "link_lost" || code == "unreachable" || code == "timeout") return BugReportOutcome::kUnreachable;
  return BugReportOutcome::kServiceError;
}

const char* BugReportLimitTitle(BugReportOutcome outcome) {
  if (outcome == BugReportOutcome::kRecentLimit) return "Too many reports were sent recently";
  if (outcome == BugReportOutcome::kDailyLimit) return "Daily report limit reached";
  return nullptr;
}

TrackedReport TrackedReportOf(const BugReportPackage& package, const BugReportReceipt& receipt) {
  TrackedReport report;
  report.ticket = receipt.ticket;
  report.receipt = receipt.credential;
  report.submitted_at = receipt.submitted_at;
  report.status_updated_at = receipt.submitted_at;
  report.description = package.Value(kDescriptionKey);
  report.log_file = package.Value("log_file");
  report.launcher_version = package.Value("launcher_version");
  report.guest = package.Value("guest");
  report.platform = package.Value("platform");
  report.operating_system = package.Value("operating_system");
  return report;
}

bool IsTicket(std::string_view text) {
  constexpr std::string_view kAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  if (text.size() != kTicketLength || !text.starts_with("SSF2-")) return false;
  return std::all_of(text.begin() + 5, text.end(), [&](char c) { return kAlphabet.find(c) != std::string_view::npos; });
}

bool IsReceipt(std::string_view text) {
  return text.size() == kReceiptHexDigits &&
         std::all_of(text.begin(), text.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

}  // namespace ssf2t
