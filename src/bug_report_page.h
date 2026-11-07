#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "bug_report.h"
#include "online/incident_client.h"
#include "report_history.h"
#include "trg/launcher.h"

namespace ssf2t {

// What the page needs from the launcher each frame.
struct BugReportContext {
  ReportFacts facts;
  online::IncidentServiceConfig service;
  std::string online_name;
  std::vector<std::string> local_player_names;
  // From the Online connection when it has announced the service's capabilities; empty before that.
  std::optional<bool> service_takes_reports;
};

// The My reports section: the list kept in the user folder and the status checks that update it.
class MyReports {
 public:
  explicit MyReports(std::filesystem::path history_file);

  void Draw(trg::Ui& ui, const BugReportContext& context);
  // Asks for the newest statuses unless a check is running. A failed check keeps the last known statuses.
  void Refresh(const BugReportContext& context, bool asked_by_player = false);
  void RefreshIfStale(const BugReportContext& context);
  // False when the report could not be added to the saved history; the report itself was sent.
  bool Remember(const BugReportPackage& package, const BugReportReceipt& receipt);
  bool HasUpdates();
  bool HasNewReply();
  bool ServiceTakesNoReports() const { return no_support_; }
  void BringIntoView() { bring_into_view_ = true; }

 private:
  struct Check {
    std::atomic<bool> done{false};
    online::StatusCheck result;
  };
  struct Read {
    std::atomic<bool> done{false};
    std::string ticket;
    online::SentReportRead result;
  };

  void Collect();
  void SaveHistory();
  void Open(const std::string& ticket);
  void Collapse();
  void ForgetSentReport();
  void ReadSentReport(const TrackedReport& report, const BugReportContext& context);
  void DrawHeader(trg::Ui& ui, const BugReportContext& context);
  void DrawReports(trg::Ui& ui, const BugReportContext& context);
  void DrawRow(trg::Ui& ui, const TrackedReport& report, size_t index, std::string& toggled);
  void DrawDetails(trg::Ui& ui, const TrackedReport& report, size_t index, const BugReportContext& context,
                   std::string& toggled, std::string& removed);
  void DrawActions(trg::Ui& ui, const TrackedReport& report, const BugReportContext& context, std::string& removed);
  void DrawSentReport(trg::Ui& ui);
  bool ConfirmRemoval(trg::Ui& ui);

  ReportHistory history_;
  std::shared_ptr<Check> check_;
  std::optional<std::chrono::steady_clock::time_point> last_check_;
  bool asked_by_player_ = false;
  bool check_failed_ = false;
  bool no_support_ = false;
  bool bring_into_view_ = false;
  bool bring_detail_into_view_ = false;
  std::string open_ticket_;
  // The open report whose developer message was unread when it was opened; keeps its "New reply" cue until it closes.
  std::string fresh_reply_;
  std::shared_ptr<Read> reading_;
  std::string sent_ticket_;
  std::string sent_text_;
  bool sent_gone_ = false;
  bool sent_failed_ = false;
};

// The launcher's Bug Report page: describe the problem, choose one game log, preview what goes out if wanted, send it.
class BugReportPage {
 public:
  struct Places {
    std::filesystem::path logs_dir;
    std::filesystem::path running_log;
    std::filesystem::path user_dir;
    std::vector<std::filesystem::path> install_folders;
  };

  explicit BugReportPage(Places places);

  void Draw(trg::Ui& ui, const BugReportContext& context);
  // Starts a status check of My reports and returns at once.
  void CheckReports(const BugReportContext& context) { my_reports_.Refresh(context); }
  bool HasReportUpdates() { return my_reports_.HasUpdates(); }

 private:
  // Shared with the sending thread, which may outlive the launcher window.
  struct Delivery {
    std::atomic<bool> done{false};
    BugReportReceipt receipt;
    std::shared_ptr<const BugReportPackage> package;
  };

  bool ServiceTakesNoReports(const BugReportContext& context) const;
  void FindLogs();
  void CollectDelivery();
  void ForgetPreview();
  bool AcceptDraft();
  std::shared_ptr<const BugReportPackage> Build(const BugReportContext& context);
  void TogglePreview(const BugReportContext& context);
  void Send(const BugReportContext& context);
  void DrawDescription(trg::Ui& ui, bool sending);
  void DrawLogs(trg::Ui& ui, bool sending);
  void DrawPreview(trg::Ui& ui, const BugReportContext& context, bool sending);
  void DrawSend(trg::Ui& ui, const BugReportContext& context, bool sending);
  void DrawResult(trg::Ui& ui);

  Places places_;
  char description_[kMostDescriptionCharacters * 4 + 1] = {};
  std::vector<GameLog> logs_;
  bool logs_found_ = false;
  size_t selected_ = 0;
  DraftProblem problem_ = DraftProblem::kNone;
  bool bring_problem_into_view_ = false;
  double problem_time_ = 0.0;
  std::shared_ptr<const BugReportPackage> previewed_;
  std::string preview_text_;
  BuildFailure build_failure_ = BuildFailure::kNone;
  std::shared_ptr<Delivery> delivery_;
  std::optional<BugReportReceipt> result_;
  bool bring_result_into_view_ = false;
  bool sent_report_remembered_ = true;
  bool service_takes_no_reports_ = false;
  MyReports my_reports_;
  int last_drawn_frame_ = -2;
};

}  // namespace ssf2t
