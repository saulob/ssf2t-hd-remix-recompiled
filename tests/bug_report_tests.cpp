// The Bug Report page's logic without its window: log discovery, the report and its preview, tickets and receipts, My
// reports on disk, and the service calls over a scripted channel. Headless; nothing is sent anywhere.
// Run: ssf2t_bug_report_tests

#include <chrono>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "bug_report.h"
#include "localization/active_language.h"
#include "log_scrubber.h"
#include "online/identity.h"
#include "online/incident_client.h"
#include "online/protocol.h"
#include "report_history.h"
#include "updater/json.h"

using namespace ssf2t;
namespace fs = std::filesystem;
namespace json = trg::update::json;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

bool Has(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }

std::string ReadFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

void WriteFile(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

fs::path FreshDir(const char* name) {
  const fs::path dir = fs::temp_directory_path() / name;
  fs::remove_all(dir);
  fs::create_directories(dir);
  return dir;
}

void AgeBy(const fs::path& path, int hours) {
  fs::last_write_time(path, fs::file_time_type::clock::now() - std::chrono::hours(hours));
}

const std::string kGameplayLog =
    "[2026-11-06 23:27:23.619] [info] [core] [t1] Windows crash reporting enabled for current log.\n"
    "[2026-11-06 23:27:24.003] [info] [sys] [t2] Guest memory arena mapped: virtual base 0x0000000100000000\n";
const std::string kLauncherLog = "[2026-11-06 23:27:23.619] [info] [core] [t1] Launcher shown\n";
const std::string kQuitLog = kLauncherLog + "[2026-11-06 23:27:30.000] [info] [core] [t1] Launcher closed\n";

ReportFacts Facts() {
  ReportFacts facts;
  facts.launcher_version = "0.4.0";
  facts.guest = "tu023";
  facts.platform = "Windows";
  facts.operating_system = "Windows 11 x64 (build 26200)";
  facts.runtime = "ReXGlue SDK 0.10.0";
  return facts;
}

GameLog LogAt(const fs::path& path) {
  GameLog log;
  log.path = path;
  const std::u8string name = path.filename().u8string();
  log.name = std::string(name.begin(), name.end());
  return log;
}

PrivateFacts PrivateOnes() {
  PrivateFacts facts;
  facts.user_names = {"SauloB"};
  facts.profile_folders = {"C:\\Users\\SauloB"};
  facts.secrets = {std::string(64, 'a')};
  return facts;
}

// ---- log discovery ----

void TestLogNames() {
  CHECK(IsGameLogName("ssf2t_hd_remix_001.log"));
  CHECK(IsGameLogName("ssf2t_hd_remix_123456789.log"));
  CHECK(!IsGameLogName("ssf2t_hd_remix_01.log"));
  CHECK(!IsGameLogName("ssf2t_hd_remix_1234567890.log"));
  CHECK(!IsGameLogName("ssf2t_hd_remix_00a.log"));
  CHECK(!IsGameLogName("dragons_crown_pro_001.log"));
  CHECK(!IsGameLogName("ssf2t_hd_remix_001.log.old"));
  CHECK(!IsGameLogName("ssf2t_hd_remix_001.txt"));
  CHECK(!IsGameLogName("sub/ssf2t_hd_remix_001.log"));
}

void TestLogDiscovery() {
  const fs::path dir = FreshDir("ssf2t_bug_report_logs");
  WriteFile(dir / "ssf2t_hd_remix_001.log", kGameplayLog);
  WriteFile(dir / "ssf2t_hd_remix_002.log", kQuitLog);
  WriteFile(dir / "ssf2t_hd_remix_003.log", kLauncherLog);
  WriteFile(dir / "ssf2t_hd_remix_004.log", kGameplayLog);
  WriteFile(dir / "ssf2t_hd_remix_005.log", "");
  WriteFile(dir / "ssf2t_hd_remix_006.log", kLauncherLog);
  WriteFile(dir / "other.log", kGameplayLog);
  WriteFile(dir / "ssf2t_hd_remix_12.log", kGameplayLog);
  WriteFile(dir / "sub" / "ssf2t_hd_remix_007.log", kGameplayLog);
  AgeBy(dir / "ssf2t_hd_remix_001.log", 5);
  AgeBy(dir / "ssf2t_hd_remix_002.log", 4);
  AgeBy(dir / "ssf2t_hd_remix_003.log", 3);
  AgeBy(dir / "ssf2t_hd_remix_004.log", 2);
  AgeBy(dir / "ssf2t_hd_remix_006.log", 1);

  const std::vector<GameLog> logs = FindGameLogs(dir, dir / "ssf2t_hd_remix_006.log");
  CHECK(logs.size() == 4);
  if (logs.size() == 4) {
    CHECK(logs[0].name == "ssf2t_hd_remix_006.log" && logs[0].role == LogRole::kCurrent);
    CHECK(logs[1].name == "ssf2t_hd_remix_004.log" && logs[1].role == LogRole::kLastGameplay);
    CHECK(logs[2].name == "ssf2t_hd_remix_003.log" && logs[2].role == LogRole::kLauncherOnly);
    CHECK(logs[3].name == "ssf2t_hd_remix_001.log" && logs[3].role == LogRole::kGameplay);
    CHECK(logs[1].activity == LogActivity::kGameplay && logs[2].activity == LogActivity::kLauncher);
    CHECK(logs[1].bytes == kGameplayLog.size());
    CHECK(DefaultGameLog(logs) == 1);
  }
  CHECK(FindGameLogs(dir, dir / "ssf2t_hd_remix_006.log", 2).size() == 2);
  const std::vector<GameLog> idle = FindGameLogs(dir, {});
  CHECK(!idle.empty() && idle[0].name == "ssf2t_hd_remix_006.log" && idle[0].role == LogRole::kLauncherOnly);
  CHECK(FindGameLogs(dir / "missing", {}).empty());

  std::vector<GameLog> only_launcher(2);
  only_launcher[0].role = LogRole::kCurrent;
  only_launcher[1].role = LogRole::kLauncherOnly;
  CHECK(DefaultGameLog(only_launcher) == 1);
  only_launcher.resize(1);
  CHECK(DefaultGameLog(only_launcher) == 0);
  CHECK(DefaultGameLog({}) == 0);
  fs::remove_all(dir);
}

void TestDraftRules() {
  CHECK(CheckDraft("too short", true) == DraftProblem::kDescriptionTooShort);
  CHECK(CheckDraft("   123456789   ", true) == DraftProblem::kDescriptionTooShort);
  CHECK(CheckDraft("1234567890", true) == DraftProblem::kNone);
  CHECK(CheckDraft("1234567890", false) == DraftProblem::kNoLog);
  CHECK(CheckDraft("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", true) == DraftProblem::kDescriptionTooShort);
  CHECK(CheckDraft("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", true) == DraftProblem::kNone);
  CHECK(CheckDraft("", true) == DraftProblem::kDescriptionTooShort);
  CHECK(CountCharacters("caf\xC3\xA9") == 4);
  CHECK(BytesOfCharacters("caf\xC3\xA9 au lait", 4) == 5);
  CHECK(BytesOfCharacters("short", 20) == 5);
  CHECK(LastBytes("a\nb\nc\n", 100) == "a\nb\nc\n");
  CHECK(LastBytes("first\nsecond\nthird\n", 10) == "third\n");
  CHECK(LastBytes("abcdefghij", 4) == "ghij");
}

// ---- the report ----

void TestReportIsBuiltScrubbedAndMatchesItsPreview() {
  const fs::path dir = FreshDir("ssf2t_bug_report_build");
  const std::string token(64, 'a');
  WriteFile(dir / "ssf2t_hd_remix_010.log",
            "[2026-11-06 23:27:23.619] [info] [core] [t1] Hello SauloB from C:\\Users\\SauloB\\AppData\\x\r\n"
            "[2026-11-06 23:27:24.003] [info] [core] [t1] peer 203.0.113.9:7001 token=" + token + "\n"
            "\x01[2026-11-06 23:27:25.003] [info] [core] [t1] caf\xC3\xA9 ok\n");
  const GameLog log = LogAt(dir / "ssf2t_hd_remix_010.log");
  const BuildResult built = BuildBugReport("  The game froze at the title screen, C:\\Users\\SauloB\\x  ", log, Facts(), PrivateOnes());
  CHECK(built.package && built.failure == BuildFailure::kNone);
  if (!built.package) return;
  const BugReportPackage& package = *built.package;
  CHECK(package.fields.size() == 9);
  CHECK(package.Value("launcher_version") == "0.4.0" && package.Value("guest") == "tu023");
  CHECK(package.Value("platform") == "Windows" && package.Value("runtime") == "ReXGlue SDK 0.10.0");
  CHECK(package.Value("operating_system") == "Windows 11 x64 (build 26200)");
  CHECK(package.Value("log_file") == "ssf2t_hd_remix_010.log" && package.Value("log_clipped") == "no");
  const std::string& text = package.Value("log_text");
  CHECK(!Has(text, "SauloB") && !Has(text, "203.0.113.9") && !Has(text, token) && !Has(text, "\r") && !Has(text, "\x01"));
  CHECK(Has(text, "<home>") && Has(text, "<ipv4>:7001") && Has(text, "token=<secret>") && Has(text, "caf\xC3\xA9 ok"));
  CHECK(package.Value("description") == "The game froze at the title screen, <home>\\x");
  CHECK(!package.Value("description").starts_with(" ") && !package.Value("description").ends_with(" "));

  const std::string preview = BugReportPreviewText(package);
  for (const BugReportField& field : package.fields) {
    CHECK(field.key == "log_text" || field.key == "description" || Has(preview, field.value));
  }
  CHECK(Has(preview, package.Value("description")) && Has(preview, text));
  CHECK(!Has(preview, "SauloB"));

  const online::IncidentFields request = BugReportRequestFields(package);
  CHECK(request.size() == package.fields.size());
  const std::string frame = online::EncodeIncidentFile(7, request);
  const std::optional<json::Value> parsed = json::Parse(frame);
  CHECK(parsed && parsed->IsObject());
  if (parsed && parsed->IsObject()) {
    CHECK(parsed->Find("op") && parsed->Find("op")->AsString() == "incident.file");
    CHECK(parsed->Find("seq") && parsed->Find("seq")->AsInt64() == 7);
    CHECK(parsed->AsObject().size() == package.fields.size() + 2);
    for (const BugReportField& field : package.fields) {
      const json::Value* sent = parsed->Find(field.key);
      CHECK(sent && sent->IsString() && sent->AsString() == field.value);
    }
  }
  CHECK(frame.find("\"op\":\"incident.file\"") == 1);
  fs::remove_all(dir);
}

void TestLongLogIsClippedToItsEnd() {
  const fs::path dir = FreshDir("ssf2t_bug_report_long");
  std::string big;
  for (int i = 0; i < 30000; ++i) big += "line " + std::to_string(i) + " of a very long session log\n";
  CHECK(big.size() > 1024 * 1024);
  WriteFile(dir / "ssf2t_hd_remix_011.log", big);
  const BuildResult built = BuildBugReport("The log is long and the problem is at its end", LogAt(dir / "ssf2t_hd_remix_011.log"), Facts(), PrivateFacts{});
  CHECK(built.package.has_value());
  if (!built.package) return;
  const std::string& text = built.package->Value("log_text");
  CHECK(text.size() <= kMostLogBytes && text.size() > kMostLogBytes / 2);
  CHECK(built.package->Value("log_clipped") == "yes");
  CHECK(text.starts_with("line ") && text.ends_with("of a very long session log\n"));
  CHECK(Has(text, "line 29999 of") && !Has(text, "line 0 of") && !Has(text, "\nline 1 of"));
  const size_t first_newline = text.find('\n');
  CHECK(first_newline != std::string::npos && text.substr(0, first_newline).ends_with("session log"));

  const std::string small = "a short log\n";
  WriteFile(dir / "ssf2t_hd_remix_012.log", small);
  const BuildResult whole = BuildBugReport("The log is short and complete here", LogAt(dir / "ssf2t_hd_remix_012.log"), Facts(), PrivateFacts{});
  CHECK(whole.package && whole.package->Value("log_clipped") == "no" && whole.package->Value("log_text") == small);

  WriteFile(dir / "ssf2t_hd_remix_013.log", std::string(kMostLogBytes + 10, 'x') + "\n");
  const BuildResult one_line = BuildBugReport("One enormous line without any break", LogAt(dir / "ssf2t_hd_remix_013.log"), Facts(), PrivateFacts{});
  CHECK(one_line.package && one_line.package->Value("log_text").size() <= kMostLogBytes && one_line.package->Value("log_clipped") == "yes");
  fs::remove_all(dir);
}

void TestDescriptionAndMetadataBounds() {
  const fs::path dir = FreshDir("ssf2t_bug_report_bounds");
  WriteFile(dir / "ssf2t_hd_remix_014.log", "a log line\n");
  const GameLog log = LogAt(dir / "ssf2t_hd_remix_014.log");
  std::string long_description;
  for (size_t i = 0; i < 600; ++i) long_description += "\xC3\xA9";
  const BuildResult built = BuildBugReport(long_description, log, Facts(), PrivateFacts{});
  CHECK(built.package && CountCharacters(built.package->Value("description")) == kMostDescriptionCharacters);

  ReportFacts facts = Facts();
  facts.operating_system = std::string(100, 'x');
  facts.runtime = "  padded  ";
  const BuildResult clamped = BuildBugReport("A fine description of the issue", log, facts, PrivateFacts{});
  CHECK(clamped.package && clamped.package->Value("operating_system").size() == kMostMetadataCharacters);
  CHECK(clamped.package && clamped.package->Value("runtime") == "padded");

  const BuildResult private_only = BuildBugReport("C:\\Users\\Bob\\x", log, Facts(), PrivateFacts{});
  CHECK(!private_only.package && private_only.failure == BuildFailure::kDescriptionTooShort);
  const BuildResult missing = BuildBugReport("A fine description of the issue", LogAt(dir / "gone.log"), Facts(), PrivateFacts{});
  CHECK(!missing.package && missing.failure == BuildFailure::kLogUnreadable);
  WriteFile(dir / "ssf2t_hd_remix_015.log", "");
  CHECK(BuildBugReport("A fine description of the issue", LogAt(dir / "ssf2t_hd_remix_015.log"), Facts(), PrivateFacts{}).failure == BuildFailure::kLogUnreadable);
  WriteFile(dir / "ssf2t_hd_remix_016.log", "  \n\t\n");
  CHECK(BuildBugReport("A fine description of the issue", LogAt(dir / "ssf2t_hd_remix_016.log"), Facts(), PrivateFacts{}).failure == BuildFailure::kLogUnreadable);
  fs::remove_all(dir);
}

void TestOutcomesAndIdentifiers() {
  CHECK(BugReportOutcomeOf("ok") == BugReportOutcome::kSent);
  CHECK(BugReportOutcomeOf("incident_hourly_limit") == BugReportOutcome::kRecentLimit);
  CHECK(BugReportOutcomeOf("limit") == BugReportOutcome::kRecentLimit);
  CHECK(BugReportOutcomeOf("incident_daily_limit") == BugReportOutcome::kDailyLimit);
  CHECK(BugReportOutcomeOf("bad_request") == BugReportOutcome::kRefused);
  CHECK(BugReportOutcomeOf("too_large") == BugReportOutcome::kRefused);
  CHECK(BugReportOutcomeOf("timeout") == BugReportOutcome::kUnreachable);
  CHECK(BugReportOutcomeOf("link_lost") == BugReportOutcome::kUnreachable);
  CHECK(BugReportOutcomeOf("internal") == BugReportOutcome::kServiceError);
  CHECK(BugReportLimitTitle(BugReportOutcome::kRecentLimit) != nullptr && BugReportLimitTitle(BugReportOutcome::kDailyLimit) != nullptr);
  CHECK(BugReportLimitTitle(BugReportOutcome::kSent) == nullptr);

  CHECK(IsTicket("SSF2-ABCDEF") && IsTicket("SSF2-234679"));
  CHECK(!IsTicket("DCP-ABCDEF") && !IsTicket("SSF2-ABCDE0") && !IsTicket("SSF2-ABCDE") && !IsTicket("ssf2-abcdef") && !IsTicket(""));
  CHECK(IsReceipt(std::string(64, 'a')) && !IsReceipt(std::string(63, 'a')) && !IsReceipt(std::string(64, 'A')) && !IsReceipt(std::string(64, 'g')));
}

// ---- My reports on disk ----

TrackedReport Tracked(const std::string& ticket, int64_t at, char receipt_digit = 'b') {
  TrackedReport report;
  report.ticket = ticket;
  report.receipt = std::string(64, receipt_digit);
  report.submitted_at = at;
  report.status_updated_at = at;
  report.description = "It froze \"hard\"\nat the title \xC3\xA9";
  report.log_file = "ssf2t_hd_remix_010.log";
  report.launcher_version = "0.4.0";
  report.guest = "tu023";
  report.platform = "Windows";
  report.operating_system = "Windows 11";
  return report;
}

void TestHistoryPersistsReceiptsAndStatuses() {
  const fs::path dir = FreshDir("ssf2t_bug_report_history");
  const fs::path file = dir / kReportHistoryFile;
  ReportHistory history(file);
  history.Load();
  CHECK(history.reports().empty() && !history.HasUnseen());
  CHECK(history.Add(Tracked("SSF2-AAAAAA", 1000)));
  CHECK(history.Add(Tracked("SSF2-BBBBBB", 2000, 'c')));
  CHECK(history.reports().size() == 2 && history.reports()[0].ticket == "SSF2-BBBBBB");

  ReportHistory again(file);
  again.Load();
  CHECK(again.reports().size() == 2);
  CHECK(again.reports()[0].receipt == std::string(64, 'c') && again.reports()[1].receipt == std::string(64, 'b'));
  CHECK(again.reports()[0].description == "It froze \"hard\"\nat the title \xC3\xA9");
  CHECK(again.reports()[1].guest == "tu023" && again.reports()[1].submitted_at == 1000 && again.reports()[1].status == "new");
  CHECK(!fs::exists(dir / (std::string(kReportHistoryFile) + ".new")));
  CHECK(Has(ReadFile(file), std::string(64, 'c')));

  online::IncidentStatus same{"SSF2-AAAAAA", "new", 1000, "", ""};
  CHECK(!again.Apply({same}));
  CHECK(!again.HasUnseen());
  online::IncidentStatus fixed{"SSF2-AAAAAA", "fixed", 5000, "0.4.1", "Update the port."};
  CHECK(again.Apply({fixed}));
  CHECK(again.HasUnseen());
  CHECK(!again.Apply({fixed}));
  CHECK(!again.Apply({online::IncidentStatus{"SSF2-ZZZZZZ", "closed", 9000, "", ""}}));
  CHECK(!again.Apply({online::IncidentStatus{"SSF2-AAAAAA", "mystery", 9999, "", ""}}));
  CHECK(again.Save());

  ReportHistory third(file);
  third.Load();
  const auto found = std::find_if(third.reports().begin(), third.reports().end(), [](const TrackedReport& r) { return r.ticket == "SSF2-AAAAAA"; });
  CHECK(found != third.reports().end() && found->status == "fixed" && found->fixed_version == "0.4.1" &&
        found->player_message == "Update the port." && found->unseen && found->status_updated_at == 5000);
  CHECK(third.MarkSeen("SSF2-AAAAAA") && !third.MarkSeen("SSF2-AAAAAA") && !third.HasUnseen());
  CHECK(third.Remove("SSF2-AAAAAA") && !third.Remove("SSF2-AAAAAA") && third.reports().size() == 1);
  fs::remove_all(dir);
}

void TestDeveloperReplyReadState() {
  const fs::path dir = FreshDir("ssf2t_bug_report_reply_state");
  const fs::path file = dir / kReportHistoryFile;
  ReportHistory history(file);
  history.Load();
  CHECK(history.Add(Tracked("SSF2-AAAAAA", 1000)));
  CHECK(history.Add(Tracked("SSF2-BBBBBB", 2000, 'c')));
  CHECK(!history.HasUnseenMessage());

  CHECK(history.Apply({online::IncidentStatus{"SSF2-AAAAAA", "investigating", 3000, "", ""}}));
  CHECK(history.HasUnseen() && !history.HasUnseenMessage());
  CHECK(history.MarkSeen("SSF2-AAAAAA") && !history.HasUnseen());

  CHECK(history.Apply({online::IncidentStatus{"SSF2-AAAAAA", "fixed", 4000, "0.4.1", "Update the port."}}));
  CHECK(history.HasUnseen() && history.HasUnseenMessage());
  CHECK(history.Save());
  ReportHistory reloaded(file);
  reloaded.Load();
  CHECK(reloaded.HasUnseenMessage() && reloaded.HasUnseen());

  CHECK(history.Apply({online::IncidentStatus{"SSF2-BBBBBB", "investigating", 3500, "", "We are looking."}}));
  CHECK(history.MarkSeen("SSF2-AAAAAA"));
  CHECK(history.HasUnseenMessage() && history.HasUnseen());
  CHECK(history.MarkSeen("SSF2-BBBBBB") && !history.HasUnseenMessage() && !history.HasUnseen());

  CHECK(history.Apply({online::IncidentStatus{"SSF2-AAAAAA", "closed", 5000, "", "Update the port."}}));
  CHECK(history.HasUnseen() && !history.HasUnseenMessage());
  CHECK(history.MarkSeen("SSF2-AAAAAA"));
  CHECK(history.Apply({online::IncidentStatus{"SSF2-AAAAAA", "closed", 6000, "", "Updated text."}}));
  CHECK(history.HasUnseenMessage());
  CHECK(history.Apply({online::IncidentStatus{"SSF2-AAAAAA", "closed", 7000, "", ""}}));
  CHECK(history.HasUnseen() && !history.HasUnseenMessage());
  fs::remove_all(dir);

  const fs::path legacy_dir = FreshDir("ssf2t_bug_report_reply_state_legacy");
  const std::string receipt(64, 'a');
  WriteFile(legacy_dir / kReportHistoryFile,
            "{\"version\":1,\"reports\":["
            "{\"ticket\":\"SSF2-AAAAAA\",\"receipt\":\"" + receipt + "\",\"status\":\"fixed\",\"submitted_at\":2,\"player_message\":\"Hi\",\"unseen\":true},"
            "{\"ticket\":\"SSF2-BBBBBB\",\"receipt\":\"" + receipt + "\",\"status\":\"fixed\",\"submitted_at\":1,\"player_message\":\"Hi\",\"unseen\":false},"
            "{\"ticket\":\"SSF2-CCCCCC\",\"receipt\":\"" + receipt + "\",\"status\":\"closed\",\"submitted_at\":0,\"unseen\":true}]}");
  ReportHistory legacy(legacy_dir / kReportHistoryFile);
  legacy.Load();
  CHECK(legacy.reports().size() == 3);
  CHECK(legacy.reports()[0].message_unseen && !legacy.reports()[1].message_unseen && !legacy.reports()[2].message_unseen);
  CHECK(legacy.MarkSeen("SSF2-AAAAAA") && !legacy.HasUnseenMessage() && legacy.HasUnseen());
  fs::remove_all(legacy_dir);
}

void TestHistoryIsBoundedAndRobust() {
  const fs::path dir = FreshDir("ssf2t_bug_report_history_bounds");
  const fs::path file = dir / kReportHistoryFile;
  ReportHistory history(file);
  const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  for (int i = 0; i < 40; ++i) {
    const std::string ticket = std::string("SSF2-AAAA") + alphabet[i / 32] + alphabet[i % 32];
    history.Add(Tracked(ticket, 1000 + i));
  }
  CHECK(history.reports().size() == kMostTrackedReports);
  CHECK(history.reports().front().submitted_at == 1039);

  WriteFile(file, "{\"version\":1,\"reports\":[{\"ticket\":\"SSF2-AAAAAA\"},{\"ticket\":\"SSF2-BBBBBB\",\"receipt\":\"short\",\"status\":\"new\"},"
                  "{\"ticket\":\"nonsense\",\"receipt\":\"" + std::string(64, 'a') + "\",\"status\":\"new\"},"
                  "{\"ticket\":\"SSF2-CCCCCC\",\"receipt\":\"" + std::string(64, 'a') + "\",\"status\":\"new\",\"submitted_at\":5}]}");
  ReportHistory partial(file);
  partial.Load();
  CHECK(partial.reports().size() == 1 && partial.reports()[0].ticket == "SSF2-CCCCCC");

  WriteFile(file, "{ this is not json");
  ReportHistory damaged(file);
  damaged.Load();
  CHECK(damaged.reports().empty());
  CHECK(fs::exists(dir / (std::string(kReportHistoryFile) + ".damaged")) && !fs::exists(file));
  CHECK(damaged.Add(Tracked("SSF2-DDDDDD", 1)));
  CHECK(fs::exists(file));
  fs::remove_all(dir);
}

void TestStatusTextAndPreview() {
  localization::SetActiveLanguage("en-US");
  TrackedReport report = Tracked("SSF2-AAAAAA", 1);
  CHECK(ReportStatusLine(report) == "Open");
  report.status = "investigating";
  CHECK(ReportStatusLine(report) == "Investigating");
  report.status = "fixed";
  CHECK(ReportStatusLine(report) == "Fixed");
  report.fixed_version = "0.4.1";
  CHECK(ReportStatusLine(report) == "Fixed in version 0.4.1");
  report.status = "closed";
  CHECK(ReportStatusLine(report) == "Closed");
  CHECK(ReportStatusText("cannot_reproduce") != nullptr && ReportStatusText("duplicate") != nullptr && ReportStatusText("not_a_bug") != nullptr);
  CHECK(ReportStatusText("mystery") == nullptr);
  CHECK(ReportPreview("line one\nline two") == "line one line two");
  CHECK(ReportPreview(std::string(100, 'x')) == std::string(60, 'x') + "...");

  const BugReportPackage package = SentReportOf({{"description", "d"}, {"guest", "base013"}, {"log_text", "t\n"}});
  CHECK(package.fields.size() == 9 && package.Value("description") == "d" && package.Value("guest") == "base013");
  CHECK(package.Value("runtime").empty());
  BugReportReceipt receipt;
  receipt.outcome = BugReportOutcome::kSent;
  receipt.ticket = "SSF2-AAAAAA";
  receipt.credential = std::string(64, 'e');
  receipt.submitted_at = 1234;
  const TrackedReport remembered = TrackedReportOf(package, receipt);
  CHECK(remembered.ticket == "SSF2-AAAAAA" && remembered.receipt == receipt.credential && remembered.description == "d");
  CHECK(remembered.submitted_at == 1234 && remembered.status_updated_at == 1234 && remembered.status == "new");
}

// ---- the service client over a scripted channel ----

struct Wire {
  std::vector<std::string> sent;
  int opened = 0;
};

using Responder = std::function<std::string(const std::string& op, int seq, const json::Value& frame)>;

class FakeChannel final : public online::LobbyChannel {
 public:
  FakeChannel(std::shared_ptr<Wire> wire, Responder responder, std::string closed)
      : wire_(std::move(wire)), responder_(std::move(responder)), closed_(std::move(closed)) {}

  bool Poll(online::ChannelEvent& out) override {
    if (!greeted_) {
      greeted_ = true;
      out.kind = closed_.empty() ? online::ChannelEvent::Kind::kOpen : online::ChannelEvent::Kind::kClosed;
      out.text = closed_;
      return true;
    }
    if (inbox_.empty()) return false;
    out = inbox_.front();
    inbox_.pop_front();
    return true;
  }

  void Send(const std::string& text) override {
    wire_->sent.push_back(text);
    const std::optional<json::Value> frame = json::Parse(text);
    if (!frame || !frame->IsObject() || !responder_) return;
    const std::string reply = responder_(frame->Find("op")->AsString(), int(frame->Find("seq")->AsInt64().value_or(0)), *frame);
    if (!reply.empty()) inbox_.push_back({online::ChannelEvent::Kind::kText, reply});
  }

  void Close() override {}

 private:
  std::shared_ptr<Wire> wire_;
  Responder responder_;
  std::string closed_;
  bool greeted_ = false;
  std::deque<online::ChannelEvent> inbox_;
};

std::string Answer(int seq, const std::string& code, const std::string& more = {}) {
  return "{\"re\":" + std::to_string(seq) + ",\"code\":\"" + code + "\"" + more + "}";
}

const char kCapabilities[] = ",\"capabilities\":{\"protocol_version\":3,\"worker_build\":\"2026-11-07-bug-reports\",\"bug_reports\":true}";

struct Harness {
  std::shared_ptr<Wire> wire = std::make_shared<Wire>();
  online::IncidentServiceConfig config;
  std::vector<std::string> log;
  Responder responder;
  std::string closed;

  Harness() {
    config.url = "ws://127.0.0.1:1/";
    config.guest = "tu023";
    config.build = 3;
    config.version = "0.4.0";
    config.platform = "Windows";
    config.name = "Ryu_Fan";
    config.connect_timeout_ms = 200;
    config.call_timeout_ms = 200;
    config.log = [this](const std::string& line) { log.push_back(line); };
    config.open_channel = [this](const std::string&) -> std::unique_ptr<online::LobbyChannel> {
      ++wire->opened;
      return std::make_unique<FakeChannel>(wire, responder, closed);
    };
  }

  std::string AllLogs() const {
    std::string all;
    for (const std::string& line : log) all += line + "\n";
    return all;
  }
};

online::Identity TestIdentity() {
  online::Identity identity;
  identity.token = std::string(64, 'd');
  identity.player_id = online::DerivePlayerId(identity.token);
  return identity;
}

BugReportPackage SamplePackage() {
  BugReportPackage package;
  const std::string values[] = {"The game froze at the title screen", "0.4.0", "tu023", "Windows", "Windows 11", "ReXGlue SDK 0.10.0",
                                "ssf2t_hd_remix_010.log", "no", "a private looking line\n"};
  const BugReportPackage names = SentReportOf({});
  for (size_t i = 0; i < names.fields.size(); ++i) package.fields.push_back({names.fields[i].key, names.fields[i].label, values[i]});
  return package;
}

const std::string kReceipt(64, 'e');

Responder Accepting() {
  return [](const std::string& op, int seq, const json::Value&) -> std::string {
    if (op == "hello") return Answer(seq, "ok", kCapabilities);
    if (op == "incident.file") {
      return Answer(seq, "ok", ",\"ticket\":\"SSF2-QWERTY\",\"receipt\":\"" + kReceipt + "\",\"submitted_at\":1700000000000");
    }
    return Answer(seq, "bad_request");
  };
}

void TestFilingAReport() {
  Harness h;
  h.responder = Accepting();
  const BugReportReceipt receipt = online::FileBugReport(h.config, TestIdentity(), SamplePackage());
  CHECK(receipt.outcome == BugReportOutcome::kSent && receipt.ticket == "SSF2-QWERTY");
  CHECK(receipt.credential == kReceipt && receipt.submitted_at == 1700000000000LL);
  CHECK(h.wire->sent.size() == 2 && h.wire->opened == 1);
  if (h.wire->sent.size() != 2) return;
  const std::optional<json::Value> hello = json::Parse(h.wire->sent[0]);
  CHECK(hello && hello->Find("op")->AsString() == "hello" && hello->Find("presence") && !hello->Find("presence")->AsBool());
  CHECK(hello && hello->Find("guest")->AsString() == "tu023" && hello->Find("name")->AsString() == "Ryu_Fan");
  CHECK(hello && hello->Find("relay") == nullptr && hello->Find("telemetry") == nullptr);
  const std::optional<json::Value> file = json::Parse(h.wire->sent[1]);
  CHECK(file && file->Find("op")->AsString() == "incident.file");
  CHECK(file && file->Find("description")->AsString() == "The game froze at the title screen");
  CHECK(file && file->Find("log_text")->AsString() == "a private looking line\n");
  CHECK(h.wire->sent[1].starts_with("{\"op\":\"incident.file\""));
  const std::string logs = h.AllLogs();
  CHECK(Has(logs, "SSF2-QWERTY") && !Has(logs, kReceipt) && !Has(logs, "private looking") && !Has(logs, TestIdentity().token));
}

void TestEveryRefusalHasItsOutcome() {
  const struct {
    const char* op;
    const char* code;
    BugReportOutcome outcome;
  } cases[] = {
      {"hello", "limit", BugReportOutcome::kRecentLimit},
      {"hello", "proto", BugReportOutcome::kServiceError},
      {"incident.file", "incident_hourly_limit", BugReportOutcome::kRecentLimit},
      {"incident.file", "incident_daily_limit", BugReportOutcome::kDailyLimit},
      {"incident.file", "limit", BugReportOutcome::kRecentLimit},
      {"incident.file", "too_large", BugReportOutcome::kRefused},
      {"incident.file", "bad_request", BugReportOutcome::kRefused},
      {"incident.file", "internal", BugReportOutcome::kServiceError},
  };
  for (const auto& test : cases) {
    Harness h;
    const std::string refused_op = test.op;
    const std::string code = test.code;
    h.responder = [refused_op, code](const std::string& op, int seq, const json::Value& frame) {
      if (op == refused_op) return Answer(seq, code);
      return Accepting()(op, seq, frame);
    };
    const BugReportReceipt receipt = online::FileBugReport(h.config, TestIdentity(), SamplePackage());
    CHECK(receipt.outcome == test.outcome);
    CHECK(receipt.credential.empty() && receipt.ticket.empty());
  }
}

void TestAnOldServiceTakesNoReport() {
  Harness h;
  h.responder = [](const std::string& op, int seq, const json::Value&) {
    if (op == "hello") return Answer(seq, "ok", ",\"capabilities\":{\"protocol_version\":3,\"worker_build\":\"2026-11-06-lobby\",\"public_lobby\":true}");
    return Answer(seq, "bad_request", ",\"detail\":\"unknown op\"");
  };
  const BugReportReceipt receipt = online::FileBugReport(h.config, TestIdentity(), SamplePackage());
  CHECK(receipt.outcome == BugReportOutcome::kNotSupported);
  CHECK(h.wire->sent.size() == 1);

  Harness older;
  older.responder = [](const std::string& op, int seq, const json::Value&) { return op == "hello" ? Answer(seq, "ok") : Answer(seq, "bad_request"); };
  CHECK(online::FileBugReport(older.config, TestIdentity(), SamplePackage()).outcome == BugReportOutcome::kNotSupported);
  CHECK(older.wire->sent.size() == 1);
}

void TestUnreachableAndSilentServices() {
  Harness gone;
  gone.closed = "unreachable";
  CHECK(online::FileBugReport(gone.config, TestIdentity(), SamplePackage()).outcome == BugReportOutcome::kUnreachable);
  CHECK(gone.wire->sent.empty());

  Harness silent;
  silent.responder = [](const std::string&, int, const json::Value&) { return std::string(); };
  CHECK(online::FileBugReport(silent.config, TestIdentity(), SamplePackage()).outcome == BugReportOutcome::kUnreachable);

  Harness no_channel;
  no_channel.config.open_channel = [](const std::string&) -> std::unique_ptr<online::LobbyChannel> { return nullptr; };
  CHECK(online::FileBugReport(no_channel.config, TestIdentity(), SamplePackage()).outcome == BugReportOutcome::kUnreachable);

  Harness anonymous;
  anonymous.responder = Accepting();
  CHECK(online::FileBugReport(anonymous.config, online::Identity{}, SamplePackage()).outcome == BugReportOutcome::kServiceError);
  CHECK(anonymous.wire->opened == 0);

  Harness odd;
  odd.responder = [](const std::string& op, int seq, const json::Value&) {
    if (op == "hello") return Answer(seq, "ok", kCapabilities);
    return Answer(seq, "ok", ",\"ticket\":\"nonsense\",\"receipt\":\"x\"");
  };
  CHECK(online::FileBugReport(odd.config, TestIdentity(), SamplePackage()).outcome == BugReportOutcome::kServiceError);
}

void TestStatusChecksNeedOnlyReceipts() {
  Harness h;
  h.responder = [](const std::string& op, int seq, const json::Value& frame) {
    if (op != "incident.status") return Answer(seq, "bad_request");
    const json::Value* reports = frame.Find("reports");
    std::string out;
    if (reports && reports->IsArray() && !reports->AsArray().empty()) {
      const std::string ticket = reports->AsArray()[0].Find("ticket")->AsString();
      out = "{\"ticket\":\"" + ticket + "\",\"status\":\"fixed\",\"status_updated_at\":5000,\"fixed_version\":\"0.4.1\",\"player_message\":\"Done\"}";
    }
    return Answer(seq, "ok", ",\"reports\":[" + out + "]");
  };
  std::vector<TrackedReport> reports;
  for (int i = 0; i < 40; ++i) reports.push_back(Tracked("SSF2-AAAAA" + std::string(1, char('A' + i % 20)), 1000 + i, 'c'));
  const online::StatusCheck check = online::FetchReportStatuses(h.config, reports);
  CHECK(check.reached && check.supported && check.statuses.size() == 1);
  CHECK(check.statuses.size() == 1 && check.statuses[0].status == "fixed" && check.statuses[0].updated_at == 5000 &&
        check.statuses[0].fixed_version == "0.4.1" && check.statuses[0].player_message == "Done");
  CHECK(h.wire->sent.size() == 1);
  if (h.wire->sent.size() == 1) {
    const std::optional<json::Value> frame = json::Parse(h.wire->sent[0]);
    CHECK(frame && frame->Find("op")->AsString() == "incident.status");
    CHECK(frame && frame->Find("reports")->AsArray().size() == kMostTrackedReports);
    CHECK(!Has(h.wire->sent[0], "\"token\"") && !Has(h.wire->sent[0], "hello") && !Has(h.wire->sent[0], "player"));
    CHECK(frame && frame->Find("reports")->AsArray()[0].Find("receipt")->AsString() == std::string(64, 'c'));
    CHECK(h.wire->sent[0].size() < 4096);
  }
  const std::string logs = h.AllLogs();
  CHECK(!Has(logs, std::string(64, 'c')));

  Harness idle;
  idle.responder = Accepting();
  const online::StatusCheck nothing = online::FetchReportStatuses(idle.config, {});
  CHECK(!nothing.reached && idle.wire->opened == 0);

  Harness old;
  old.responder = [](const std::string&, int seq, const json::Value&) { return Answer(seq, "bad_request", ",\"detail\":\"unknown op\""); };
  const online::StatusCheck unsupported = online::FetchReportStatuses(old.config, {Tracked("SSF2-AAAAAA", 1)});
  CHECK(unsupported.reached && !unsupported.supported && unsupported.statuses.empty());

  Harness down;
  down.closed = "unreachable";
  const online::StatusCheck failed = online::FetchReportStatuses(down.config, {Tracked("SSF2-AAAAAA", 1)});
  CHECK(!failed.reached && failed.supported);

  Harness limited;
  limited.responder = [](const std::string&, int seq, const json::Value&) { return Answer(seq, "limit"); };
  const online::StatusCheck refused = online::FetchReportStatuses(limited.config, {Tracked("SSF2-AAAAAA", 1)});
  CHECK(refused.reached && refused.supported && refused.statuses.empty());
}

void TestReadingASentReport() {
  Harness h;
  h.responder = [](const std::string& op, int seq, const json::Value& frame) {
    if (op != "incident.read") return Answer(seq, "bad_request");
    if (frame.Find("receipt")->AsString() != std::string(64, 'b')) return Answer(seq, "not_found");
    return Answer(seq, "ok",
                  ",\"report\":{\"ticket\":\"SSF2-AAAAAA\",\"description\":\"It froze\",\"guest\":\"tu023\",\"log_file\":\"ssf2t_hd_remix_010.log\","
                  "\"log_clipped\":\"yes\",\"log_text\":\"line\\n\",\"submitted_at\":42}");
  };
  const online::SentReportRead read = online::FetchSentReport(h.config, Tracked("SSF2-AAAAAA", 1, 'b'));
  CHECK(read.reached && read.package.has_value());
  if (read.package) {
    CHECK(read.package->Value("description") == "It froze" && read.package->Value("log_text") == "line\n");
    CHECK(read.package->Value("log_clipped") == "yes" && read.package->Value("platform").empty());
    CHECK(Has(BugReportPreviewText(*read.package), "It froze") && Has(BugReportPreviewText(*read.package), "line"));
  }
  const online::SentReportRead gone = online::FetchSentReport(h.config, Tracked("SSF2-AAAAAA", 1, 'f'));
  CHECK(gone.reached && !gone.package);
  Harness down;
  down.closed = "unreachable";
  CHECK(!online::FetchSentReport(down.config, Tracked("SSF2-AAAAAA", 1)).reached);
}

void TestProtocolEncodingAndDecoding() {
  const std::string status = online::EncodeIncidentStatus(3, {{"SSF2-AAAAAA", std::string(64, 'a')}, {"SSF2-BBBBBB", std::string(64, 'b')}});
  const std::optional<json::Value> parsed = json::Parse(status);
  CHECK(parsed && parsed->Find("reports") && parsed->Find("reports")->AsArray().size() == 2);
  const std::string read = online::EncodeIncidentRead(4, {"SSF2-AAAAAA", std::string(64, 'a')});
  const std::optional<json::Value> parsed_read = json::Parse(read);
  CHECK(parsed_read && parsed_read->Find("op")->AsString() == "incident.read" && parsed_read->Find("ticket")->AsString() == "SSF2-AAAAAA");
  CHECK(online::JsonQuote("a\"b\\c\n\x01") == "\"a\\\"b\\\\c\\n\\u0001\"");

  const std::optional<online::ServerMessage> hello =
      online::DecodeServerMessage(Answer(1, "ok", kCapabilities));
  CHECK(hello && hello->capabilities && hello->capabilities->bug_reports && hello->capabilities->protocol_version == 3);
  const std::optional<online::ServerMessage> older = online::DecodeServerMessage(
      Answer(1, "ok", ",\"capabilities\":{\"protocol_version\":3,\"public_lobby\":true}"));
  CHECK(older && older->capabilities && !older->capabilities->bug_reports && older->capabilities->public_lobby);
  const std::optional<online::ServerMessage> filed = online::DecodeServerMessage(
      Answer(2, "ok", ",\"ticket\":\"SSF2-QWERTY\",\"receipt\":\"" + kReceipt + "\",\"submitted_at\":99"));
  CHECK(filed && filed->incident_ticket == "SSF2-QWERTY" && filed->incident_receipt == kReceipt && filed->incident_submitted_at == 99);
  CHECK(online::DecodeServerMessage(Answer(3, "ok", ",\"reports\":[{\"ticket\":\"\"}]"))->incident_statuses == std::nullopt);
  CHECK(online::DecodeServerMessage(Answer(3, "ok", ",\"reports\":[]"))->incident_statuses.has_value());
}

// ---- the page and the launcher ----

std::string Source(const char* relative) { return ReadFile(fs::path(SSF2T_SOURCE_DIR) / relative); }

size_t Count(const std::string& text, std::string_view part) {
  size_t count = 0;
  for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + part.size())) ++count;
  return count;
}

void TestNothingIsSentWithoutThePlayer() {
  const std::string page = Source("src/bug_report_page.cpp");
  CHECK(Count(page, "FileBugReport(") == 1);
  CHECK(Has(page, "if (clicked) Send(context);"));
  const size_t send = page.find("void BugReportPage::Send(");
  const size_t thread = page.find("std::thread", send);
  CHECK(send != std::string::npos && thread != std::string::npos && page.find("FileBugReport(", send) > thread);
  CHECK(Count(page, "AcceptDraft()") >= 3);
  for (const char* file : {"src/launcher.cpp", "src/bug_report.cpp", "src/log_scrubber.cpp", "src/report_history.cpp", "src/online/online_service.cpp",
                           "src/online_runtime.cpp"}) {
    CHECK(!Has(Source(file), "FileBugReport("));
  }
  CHECK(Count(Source("src/online/incident_client.cpp"), "EncodeIncidentFile(") == 1);
  for (const char* file : {"src/launcher.cpp", "src/bug_report_page.cpp", "src/online/online_service.cpp"}) {
    CHECK(!Has(Source(file), "EncodeIncidentFile("));
  }
  CHECK(Has(Source("src/bug_report_page.cpp"), "preview_text_ = BugReportPreviewText(*previewed_)"));
  CHECK(Has(Source("src/bug_report_page.cpp"), "previewed_ ? previewed_ : Build(context)"));
}

// A table opened by ui.Row is closed by ui.EndRows, which pops the table's own id: a PushID in between would be popped instead.
void TestIdScopesNeverCrossATableBoundary() {
  const std::string page = Source("src/bug_report_page.cpp");
  const size_t details = page.find("void MyReports::DrawDetails(");
  CHECK(details != std::string::npos);
  const std::string body = page.substr(details, page.find("\n}\n", details) - details);
  const size_t first_end = body.find("ui.EndRows();");
  const size_t push = body.find("ImGui::PushID(");
  const size_t pop = body.rfind("ImGui::PopID();");
  const size_t last_end = body.rfind("ui.EndRows();");
  CHECK(first_end != std::string::npos && push != std::string::npos && first_end < push);
  CHECK(pop != std::string::npos && last_end < pop);
  CHECK(Count(body, "ImGui::PushID(") == 1 && Count(body, "ImGui::PopID();") == 1 && body.find("return") == std::string::npos);

  const size_t draw = page.find("void MyReports::Draw(");
  const std::string loop = page.substr(draw, page.find("\n}\n", draw) - draw);
  CHECK(loop.find("PushID") == std::string::npos && loop.find("PopID") == std::string::npos);

  CHECK(Count(page, "ImGui::PushID(") == Count(page, "ImGui::PopID();"));
  CHECK(Count(page, "ImGui::BeginDisabled(") == Count(page, "ImGui::EndDisabled();"));
  CHECK(Count(page, "ImGui::PushStyleColor(") == Count(page, "ImGui::PopStyleColor();"));
  CHECK(Count(page, "ImGui::PushFont(") == Count(page, "ImGui::PopFont();"));
}

void TestLauncherListsThePageAndKeepsReceiptsOutOfLogs() {
  const std::string launcher = Source("src/launcher.cpp");
  const size_t bug = launcher.find("MakePage(LauncherPage::kBugReport, ");
  const size_t about = launcher.find("MakePage(LauncherPage::kAbout, ");
  CHECK(bug != std::string::npos && about != std::string::npos && bug < about);
  CHECK(Has(launcher, "bug_report_page_->HasReportUpdates()"));
  CHECK(Has(launcher, "void PageBugReport(trg::Ui& ui)"));
  for (const char* file : {"src/bug_report_page.cpp", "src/online/incident_client.cpp", "src/report_history.cpp", "src/bug_report.cpp"}) {
    const std::string text = Source(file);
    CHECK(!Has(text, "REXLOG"));
  }
  const std::string client = Source("src/online/incident_client.cpp");
  for (size_t at = client.find("Say("); at != std::string::npos; at = client.find("Say(", at + 4)) {
    const std::string line = client.substr(at, client.find('\n', at) - at);
    CHECK(!Has(line, "receipt") && !Has(line, "token") && !Has(line, "log_text") && !Has(line, "description"));
  }
}

}  // namespace

// The page keeps the shared Bug Report layout: section order, sizes, the selected-log facts and the report actions.
void TestPageFollowsTheSharedLayout() {
  const std::string page = Source("src/bug_report_page.cpp");
  size_t at = 0;
  for (const char* part : {"ui.Heading(\"1. Report the problem\")", "ui.Heading(\"2. Choose one game log\")",
                           "ui.Heading(\"3. Preview what will be sent\")", "ui.Heading(\"4. Send\")",
                           "my_reports_.Draw(ui, context)"}) {
    const size_t found = page.find(part, at);
    CHECK(found != std::string::npos);
    if (found == std::string::npos) break;
    at = found + 1;
  }
  for (const char* constant :
       {"constexpr float kDescriptionHeight = 112.0f;", "constexpr float kPreviewHeight = 300.0f;",
        "constexpr float kSentReportHeight = 300.0f;", "constexpr float kCardPadding = 12.0f;",
        "constexpr float kCardRounding = 12.0f;", "constexpr float kDetailGap = 8.0f;",
        "constexpr float kActionShare = 0.4f;", "constexpr float kRefreshPadding = 16.0f;",
        "kFactLabelShare = 0.28f, kFactLabelMin = 130.0f, kFactLabelMax = 220.0f;"}) {
    CHECK(Has(page, constant));
  }
  CHECK(kFewestDescriptionCharacters == 10 && kMostDescriptionCharacters == 500);
  for (const char* text :
       {"\"Selected log\"", "\"Last written\"", "\"Size\"", "\"View selected log\"", "\"My reports\"", "\"Refresh\"",
        "\"Checking...\"", "\"View details\"", "\"Hide details\"", "\"What was sent\"", "\"Hide report\"",
        "\"Remove from this PC\"", "\"Remove this report from this PC?\"", "\"Remove\"", "\"Cancel\"",
        "\"Fixed in version\"", "\"Message from developer\"", "\"Status updated\"", "\"Send this log\"",
        "\"No reports sent from this PC\"", "\"A report has an update\"",
        "\"What were you doing, what went wrong, and does it happen every time?\"",
        "\"This report is no longer available online.\""}) {
    CHECK(Has(page, text));
  }
  CHECK(Has(page, "KeepClearOfScrollbar(std::floor(trg::layout::kContentPadding * ui.scale()))"));
  CHECK(Has(page, "if (DrawLogFacts(ui, log)) trg::OpenUrl(Utf8(log.path));"));
  CHECK(Has(page, "bool MyReports::ConfirmRemoval(trg::Ui& ui) {"));
  CHECK(Has(page, "ImGui::BeginPopupModal(\"##remove\""));
  for (const char* file : {"src/bug_report_page.cpp", "src/bug_report_page.h", "src/launcher.cpp", "src/online_page.cpp"}) {
    const std::string text = Source(file);
    CHECK(!Has(text, "GetCurrentWindow(") && !Has(text, "GImGui"));
  }
  CHECK(!Has(page, "<imgui_internal.h>") && Has(page, "#include \"page_margin.h\""));
  const std::string margin = Source("src/page_margin.h");
  CHECK(Has(margin, "ImGui::GetCurrentContext()->CurrentWindow"));
  CHECK(!Has(margin, "ImGui::GetCurrentWindow()->"));
}

// The developer's reply has its own panel above the facts; unread replies mark the row, the page banner and the sidebar.
void TestDeveloperReplyIsProminent() {
  const std::string page = Source("src/bug_report_page.cpp");
  CHECK(Has(page, "void DrawDeveloperMessage(trg::Ui& ui, const std::string& message, bool fresh) {"));
  CHECK(Has(page, "void NewReplyMarker(trg::Ui& ui) {"));
  CHECK(Has(page, "Tr(\"Message from developer\")") && Has(page, "Tr(\"New reply\")"));
  CHECK(!Has(page, "Fact(ui, \"Message from developer\""));

  const size_t details = page.find("void MyReports::DrawDetails(");
  const std::string body = page.substr(details, page.find("\n}\n", details) - details);
  const size_t summary = body.find("DrawCardSummary(ui, report, toggled);");
  const size_t message = body.find("DrawDeveloperMessage(ui, report.player_message, fresh_reply_ == report.ticket);");
  const size_t facts = body.find("DrawFacts(ui, report);");
  CHECK(summary != std::string::npos && message != std::string::npos && facts != std::string::npos);
  CHECK(summary < message && message < facts);
  CHECK(Has(body, "if (!report.player_message.empty()) {"));
  const size_t remember = body.find("if (report.message_unseen) fresh_reply_ = report.ticket;");
  const size_t mark = body.find("history_.MarkSeen(report.ticket)");
  CHECK(remember != std::string::npos && mark != std::string::npos && remember < mark);

  const size_t panel = page.find("void DrawDeveloperMessage(");
  const std::string panel_body = page.substr(panel, page.find("\n}\n", panel) - panel);
  CHECK(Has(panel_body, "AddRectFilled(outer, outer_end, Tint(theme.accent, kMessageTint)"));
  CHECK(Has(panel_body, "AddRect(outer, outer_end"));
  CHECK(Has(panel_body, "ImDrawFlags_RoundCornersLeft"));
  CHECK(Has(panel_body, "if (fresh) {") && Has(panel_body, "theme.on_accent"));
  CHECK(!Has(panel_body, "InputText"));

  const size_t row = page.find("void MyReports::DrawRow(");
  const std::string row_body = page.substr(row, page.find("\n}\n", row) - row);
  CHECK(Has(row_body, "if (report.message_unseen) NewReplyMarker(ui);"));

  CHECK(Has(page, "return history_.HasUnseen() || history_.HasUnseenMessage();"));
  CHECK(Has(page, "bool MyReports::HasNewReply() {"));
  CHECK(Has(page, "ui.ButtonRow(reply ? \"A report has a new reply\" : \"A report has an update\""));
  CHECK(Has(page, "void MyReports::Collapse() {\n  open_ticket_.clear();\n  fresh_reply_.clear();"));
  CHECK(Has(Source("src/bug_report_page.h"), "bool HasReportUpdates() { return my_reports_.HasUpdates(); }"));
  CHECK(Has(Source("src/launcher.cpp"), "[this] { return bug_report_page_->HasReportUpdates(); }"));

  const std::string history = Source("src/report_history.cpp");
  CHECK(Has(history, "if (found->player_message != status.player_message) found->message_unseen = !status.player_message.empty();"));
  CHECK(Has(history, "\"message_unseen\""));

  const std::string text = Source("src/localization/pt_br_ui_text.cpp");
  for (const char* row : {"Message from developer\tMensagem do desenvolvedor\n", "New reply\tNova resposta\n",
                          "A report has a new reply\t", "The developer replied to one of your reports.\t"}) {
    CHECK(Has(text, row));
  }
}

int main() {
  TestPageFollowsTheSharedLayout();
  TestDeveloperReplyIsProminent();
  TestDeveloperReplyReadState();
  TestLogNames();
  TestLogDiscovery();
  TestDraftRules();
  TestReportIsBuiltScrubbedAndMatchesItsPreview();
  TestLongLogIsClippedToItsEnd();
  TestDescriptionAndMetadataBounds();
  TestOutcomesAndIdentifiers();
  TestHistoryPersistsReceiptsAndStatuses();
  TestHistoryIsBoundedAndRobust();
  TestStatusTextAndPreview();
  TestFilingAReport();
  TestEveryRefusalHasItsOutcome();
  TestAnOldServiceTakesNoReport();
  TestUnreachableAndSilentServices();
  TestStatusChecksNeedOnlyReceipts();
  TestReadingASentReport();
  TestProtocolEncodingAndDecoding();
  TestNothingIsSentWithoutThePlayer();
  TestIdScopesNeverCrossATableBoundary();
  TestLauncherListsThePageAndKeepsReceiptsOutOfLogs();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
