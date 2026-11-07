#include "bug_report_page.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <thread>
#include <utility>

#include <imgui.h>

#include "localization/active_language.h"
#include "localization/ui_text.h"
#include "online/identity.h"
#include "platform.h"
#include "trg/platform.h"

namespace ssf2t {
namespace {

constexpr float kDescriptionHeight = 112.0f;
constexpr float kPreviewHeight = 300.0f;
constexpr float kSentReportHeight = 300.0f;
constexpr auto kFreshCheck = std::chrono::seconds(30);
constexpr char kDot[] = " \xC2\xB7 ";

constexpr char kPrivacyHeadline[] = "No personal user data is intentionally included.";
constexpr char kPrivacyNote[] =
    "Before anything is sent, the launcher takes addresses, user and computer names, private folders, passwords, "
    "online secrets and other players' details out of the selected log. Automatic filtering cannot promise to catch "
    "everything, so open the preview if you want to check what goes out.";

int KeepDescriptionShort(ImGuiInputTextCallbackData* data) {
  const size_t keep = BytesOfCharacters(std::string_view(data->Buf, size_t(data->BufTextLen)), kMostDescriptionCharacters);
  if (keep < size_t(data->BufTextLen)) data->DeleteChars(int(keep), data->BufTextLen - int(keep));
  return 0;
}

std::string DateText(int64_t milliseconds) {
  return LogDateText(std::time_t(milliseconds / 1000), localization::PortugueseBrazilSelected());
}

std::string LogDetails(const GameLog& log) {
  return LogDateText(log.written, localization::PortugueseBrazilSelected()) + kDot + LogSizeText(log.bytes);
}

void WarningText(trg::Ui& ui, const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ui.theme().warn_dot);
  ImGui::TextWrapped("%s", Tr(text));
  ImGui::PopStyleColor();
}

void AccentText(trg::Ui& ui, const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ui.theme().accent);
  ImGui::TextUnformatted(Tr(text));
  ImGui::PopStyleColor();
}

void Fact(trg::Ui& ui, const char* label, const std::string& value) {
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  ImGui::PushFont(ui.fonts().semibold, 0.0f);
  ImGui::TextUnformatted(Tr(label));
  ImGui::PopFont();
  ImGui::TableSetColumnIndex(1);
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(value.c_str());
  ImGui::PopTextWrapPos();
}

// Edited-in-place text that cannot change: ImGui never writes to a read-only buffer.
void ReadOnlyBox(const char* id, std::string& text, float height) {
  ImGui::InputTextMultiline(id, text.data(), text.size() + 1, ImVec2(-FLT_MIN, height),
                            ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_WordWrap);
}

std::string GuestLabel(const std::string& guest) {
  return guest == online::kGuestTu023 ? "Title Update 0.0.2.3" : guest == online::kGuestBase013 ? "Original 0.0.1.3" : guest;
}

}  // namespace

// ---- My reports ----

MyReports::MyReports(std::filesystem::path history_file) : history_(std::move(history_file)) { history_.Load(); }

void MyReports::Refresh(const BugReportContext& context, bool asked_by_player) {
  Collect();
  if (check_ || history_.reports().empty()) return;
  check_ = std::make_shared<Check>();
  last_check_ = std::chrono::steady_clock::now();
  asked_by_player_ = asked_by_player;
  std::thread([check = check_, service = context.service, reports = history_.reports()] {
    check->result = online::FetchReportStatuses(service, reports);
    check->done = true;
  }).detach();
}

void MyReports::RefreshIfStale(const BugReportContext& context) {
  if (!last_check_ || std::chrono::steady_clock::now() - *last_check_ >= kFreshCheck) Refresh(context);
}

void MyReports::Collect() {
  if (reading_ && reading_->done) {
    if (reading_->ticket == sent_ticket_) {
      sent_failed_ = !reading_->result.reached;
      sent_gone_ = reading_->result.reached && !reading_->result.package;
      sent_text_ = reading_->result.package ? BugReportPreviewText(*reading_->result.package) : std::string();
    }
    reading_.reset();
  }
  if (!check_ || !check_->done) return;
  check_failed_ = asked_by_player_ && !check_->result.reached;
  no_support_ = check_->result.reached && !check_->result.supported;
  if (check_->result.reached && history_.Apply(check_->result.statuses)) SaveHistory();
  check_.reset();
}

void MyReports::SaveHistory() { history_.Save(); }

bool MyReports::Remember(const BugReportPackage& package, const BugReportReceipt& receipt) {
  if (receipt.credential.empty()) return false;
  return history_.Add(TrackedReportOf(package, receipt));
}

bool MyReports::HasUpdates() {
  Collect();
  return history_.HasUnseen();
}

void MyReports::ForgetSentReport() {
  sent_ticket_.clear();
  sent_text_.clear();
  sent_gone_ = false;
  sent_failed_ = false;
}

void MyReports::Open(const std::string& ticket) {
  ForgetSentReport();
  open_ticket_ = open_ticket_ == ticket ? std::string() : ticket;
}

void MyReports::Draw(trg::Ui& ui, const BugReportContext& context) {
  Collect();
  ui.Heading("My reports");
  if (std::exchange(bring_into_view_, false)) ImGui::SetScrollHereY(0.0f);
  const std::vector<TrackedReport>& reports = history_.reports();
  if (reports.empty()) {
    ui.Paragraph("No reports sent from this PC");
    return;
  }
  ui.Help("Only reports sent from this PC are listed here.");
  ImGui::BeginDisabled(check_ != nullptr);
  if (ImGui::Button(Tr(check_ ? "Checking..." : "Refresh"))) Refresh(context, true);
  ImGui::EndDisabled();
  if (check_failed_) WarningText(ui, "Could not check for updates. The last known status is shown.");
  if (no_support_) WarningText(ui, "The online service cannot update report statuses yet.");

  std::string toggled, removed;
  for (const TrackedReport& report : reports) {
    const bool open = report.ticket == open_ticket_;
    DrawRow(ui, report, open, toggled);
    if (open) DrawDetails(ui, report, context, removed);
  }
  ui.EndRows();
  if (!toggled.empty()) Open(toggled);
  if (removed.empty()) return;
  if (removed == open_ticket_) Open(removed);
  history_.Remove(removed);
  SaveHistory();
}

void MyReports::DrawRow(trg::Ui& ui, const TrackedReport& report, bool open, std::string& toggled) {
  std::string summary = ReportPreview(report.description) + "\n" + ReportStatusLine(report);
  if (report.unseen) summary += kDot + std::string(Tr("Updated"));
  summary += kDot + DateText(report.submitted_at);
  ui.Row(report.ticket.c_str(), summary.c_str());
  ImGui::PushID(report.ticket.c_str());
  if (ImGui::Button(Tr(open ? "Hide details" : "View details"), ImVec2(-FLT_MIN, 0))) toggled = report.ticket;
  ImGui::PopID();
}

void MyReports::DrawDetails(trg::Ui& ui, const TrackedReport& report, const BugReportContext& context, std::string& removed) {
  ui.EndRows();
  ImGui::PushID(report.ticket.c_str());
  if (history_.MarkSeen(report.ticket)) SaveHistory();
  if (ImGui::BeginTable("##facts", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 0.3f);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.7f);
    const char* status = ReportStatusText(report.status);
    Fact(ui, "Report ID", report.ticket);
    Fact(ui, "Submitted", DateText(report.submitted_at));
    Fact(ui, "Description", report.description);
    Fact(ui, "Selected log", report.log_file);
    Fact(ui, "Game image", GuestLabel(report.guest));
    Fact(ui, "Port version", report.launcher_version);
    Fact(ui, "Status", status ? Tr(status) : report.status);
    Fact(ui, "Status updated", DateText(report.status_updated_at));
    if (report.status == "fixed" && !report.fixed_version.empty()) Fact(ui, "Fixed in version", report.fixed_version);
    if (!report.player_message.empty()) Fact(ui, "Message from developer", report.player_message);
    ImGui::EndTable();
  }
  ui.Spacer(4);
  const bool shown = sent_ticket_ == report.ticket;
  ImGui::BeginDisabled(reading_ != nullptr);
  const char* label = reading_ ? "Loading..." : shown ? "Hide report" : "What was sent";
  if (ImGui::Button(Tr(label))) {
    if (shown) {
      ForgetSentReport();
    } else {
      ForgetSentReport();
      sent_ticket_ = report.ticket;
      reading_ = std::make_shared<Read>();
      reading_->ticket = report.ticket;
      std::thread([read = reading_, service = context.service, report] {
        read->result = online::FetchSentReport(service, report);
        read->done = true;
      }).detach();
    }
  }
  ImGui::EndDisabled();
  if (shown && sent_failed_) WarningText(ui, "Could not load the report. Try again in a moment.");
  if (shown && sent_gone_) WarningText(ui, "This report is no longer stored online.");
  if (shown && !sent_text_.empty()) ReadOnlyBox("##sent", sent_text_, kSentReportHeight * ui.scale());
  if (ui.ConfirmRow("Remove from this PC", "Removes it from this list only. The report stays with the developer.",
                    "Remove from this PC", "Remove this report from this PC?", "Remove", "Cancel")) {
    removed = report.ticket;
  }
  ui.EndRows();
  ImGui::PopID();
}

// ---- The page ----

BugReportPage::BugReportPage(Places places)
    : places_(std::move(places)), my_reports_(places_.user_dir / kReportHistoryFile) {}

void BugReportPage::Draw(trg::Ui& ui, const BugReportContext& context) {
  if (!logs_found_) FindLogs();
  CollectDelivery();
  if (ImGui::GetFrameCount() != std::exchange(last_drawn_frame_, ImGui::GetFrameCount()) + 1) my_reports_.RefreshIfStale(context);
  const bool sending = delivery_ != nullptr;
  if (CheckDraft(description_, !logs_.empty()) != problem_) problem_ = DraftProblem::kNone;
  AccentText(ui, kPrivacyHeadline);
  ui.Paragraph(kPrivacyNote);
  if (ServiceTakesNoReports(context)) {
    WarningText(ui, "The online service you use cannot take reports yet, so this version cannot send one.");
  }
  if (my_reports_.HasUpdates() && ui.ButtonRow("A report has an update", "Its status changed since you last looked.", "Show")) {
    my_reports_.BringIntoView();
  }
  ui.Heading("1. Report the problem");
  DrawDescription(ui, sending);
  ui.Heading("2. Choose one game log");
  DrawLogs(ui, sending);
  ui.Heading("3. Preview what will be sent");
  DrawPreview(ui, context, sending);
  ui.Heading("4. Send");
  DrawSend(ui, context, sending);
  ui.EndRows();
  my_reports_.Draw(ui, context);
  ui.EndRows();
}

bool BugReportPage::ServiceTakesNoReports(const BugReportContext& context) const {
  return service_takes_no_reports_ || context.service_takes_reports == false || my_reports_.ServiceTakesNoReports();
}

void BugReportPage::FindLogs() {
  logs_ = FindGameLogs(places_.logs_dir, places_.running_log);
  logs_found_ = true;
  selected_ = DefaultGameLog(logs_);
}

void BugReportPage::CollectDelivery() {
  if (!delivery_ || !delivery_->done) return;
  result_ = delivery_->receipt;
  if (result_->outcome == BugReportOutcome::kNotSupported) service_takes_no_reports_ = true;
  if (result_->outcome == BugReportOutcome::kSent) sent_report_remembered_ = my_reports_.Remember(*delivery_->package, *result_);
  delivery_.reset();
  bring_result_into_view_ = true;
  if (result_->outcome == BugReportOutcome::kSent) {
    description_[0] = '\0';
    ForgetPreview();
  }
}

void BugReportPage::ForgetPreview() {
  previewed_.reset();
  preview_text_.clear();
  build_failure_ = BuildFailure::kNone;
}

bool BugReportPage::AcceptDraft() {
  problem_ = CheckDraft(description_, !logs_.empty());
  bring_problem_into_view_ = problem_ != DraftProblem::kNone;
  return problem_ == DraftProblem::kNone;
}

std::shared_ptr<const BugReportPackage> BugReportPage::Build(const BugReportContext& context) {
  build_failure_ = BuildFailure::kNone;
  const PrivateFacts private_facts = GatherPrivateFacts(places_.install_folders, places_.user_dir, context.online_name);
  BuildResult built = BuildBugReport(description_, logs_[selected_], context.facts, private_facts);
  if (!built.package) {
    build_failure_ = built.failure;
    bring_problem_into_view_ = true;
    if (built.failure == BuildFailure::kLogUnreadable) FindLogs();
    return nullptr;
  }
  return std::make_shared<const BugReportPackage>(std::move(*built.package));
}

void BugReportPage::TogglePreview(const BugReportContext& context) {
  if (previewed_) {
    ForgetPreview();
    return;
  }
  if (!AcceptDraft()) return;
  previewed_ = Build(context);
  if (previewed_) preview_text_ = BugReportPreviewText(*previewed_);
}

void BugReportPage::Send(const BugReportContext& context) {
  if (!AcceptDraft()) return;
  std::shared_ptr<const BugReportPackage> package = previewed_ ? previewed_ : Build(context);
  if (!package) return;
  result_.reset();
  delivery_ = std::make_shared<Delivery>();
  delivery_->package = package;
  bring_result_into_view_ = true;
  std::thread([delivery = delivery_, package = std::move(package), service = context.service, user_dir = places_.user_dir] {
    const online::Identity identity = online::LoadOrCreateIdentity(user_dir);
    delivery->receipt = online::FileBugReport(service, identity, *package);
    delivery->done = true;
  }).detach();
}

void BugReportPage::DrawDescription(trg::Ui& ui, bool sending) {
  ui.EndRows();
  const bool too_short = problem_ == DraftProblem::kDescriptionTooShort || build_failure_ == BuildFailure::kDescriptionTooShort;
  if (too_short && std::exchange(bring_problem_into_view_, false)) ImGui::SetKeyboardFocusHere();
  ImGui::BeginDisabled(sending);
  const ImGuiInputTextFlags flags = ImGuiInputTextFlags_WordWrap | ImGuiInputTextFlags_CallbackEdit;
  if (ImGui::InputTextMultiline("##description", description_, sizeof(description_),
                                ImVec2(-FLT_MIN, kDescriptionHeight * ui.scale()), flags, KeepDescriptionShort)) {
    ForgetPreview();
  }
  ImGui::EndDisabled();
  const size_t used = CountCharacters(description_);
  const std::string counter = std::to_string(used) + " / " + std::to_string(kMostDescriptionCharacters);
  ImGui::TextColored(used >= kMostDescriptionCharacters ? ui.theme().warn_dot : ui.theme().dim, "%s", counter.c_str());
  if (too_short) {
    ImGui::SameLine();
    ImGui::TextColored(ui.theme().warn_dot, "%s", Tr("Please describe the problem, minimum 10 characters"));
  }
  ui.Help("What were you doing, what went wrong, and does it happen every time?");
}

void BugReportPage::DrawLogs(trg::Ui& ui, bool sending) {
  if (logs_.empty()) {
    ui.Paragraph(
        "No game log is available yet, so a report cannot be sent. Start the game once and try again. With Detailed "
        "logging off on the About page, only sessions that crashed keep a log.");
    if (problem_ == DraftProblem::kNoLog) WarningText(ui, "There is no game log to send.");
    return;
  }
  ui.Help("The newest logs of the game, most recent first. Only the one you choose is sent.");
  for (size_t i = 0; i < logs_.size(); ++i) {
    const std::string details = LogDetails(logs_[i]);
    const char* role = LogRoleText(logs_[i].role);
    const std::string help = role ? details + kDot + Tr(role) : details;
    ui.Row(logs_[i].name.c_str(), help.c_str());
    ImGui::PushID(int(i));
    ImGui::BeginDisabled(sending);
    ImGui::AlignTextToFramePadding();
    if (ImGui::RadioButton(Tr("Send this log"), selected_ == i) && selected_ != i) {
      selected_ = i;
      ForgetPreview();
    }
    ImGui::EndDisabled();
    ImGui::PopID();
  }
  ui.EndRows();
  if (build_failure_ == BuildFailure::kLogUnreadable) WarningText(ui, "That log could not be read. Choose another one and try again.");
  if (ui.ButtonRow("Selected log", "Opens the folder that holds it.", "Show selected log")) {
    trg::OpenInFileManager(Utf8(logs_[selected_].path));
  }
}

void BugReportPage::DrawPreview(trg::Ui& ui, const BugReportContext& context, bool sending) {
  ImGui::BeginDisabled(sending);
  const bool clicked = ui.ButtonRow("Preview", "Shows exactly what will be sent, after the privacy filter.",
                                    PreviewButtonText(previewed_ != nullptr));
  ImGui::EndDisabled();
  if (clicked) TogglePreview(context);
  ui.EndRows();
  if (previewed_) ReadOnlyBox("##preview", preview_text_, kPreviewHeight * ui.scale());
}

void BugReportPage::DrawSend(trg::Ui& ui, const BugReportContext& context, bool sending) {
  ImGui::BeginDisabled(sending || ServiceTakesNoReports(context));
  const bool clicked = ui.ButtonRow("Send report", "Sends the selected log after the privacy filter is applied.",
                                    sending ? "Sending..." : "Send report", true);
  ImGui::EndDisabled();
  if (clicked) Send(context);
  ui.EndRows();
  DrawResult(ui);
  if (std::exchange(bring_result_into_view_, false)) ImGui::SetScrollHereY(1.0f);
}

void BugReportPage::DrawResult(trg::Ui& ui) {
  if (delivery_) {
    ui.Spacer(4);
    ui.StatusCard(trg::Status::kBusy, "Sending your report...", "This takes a few seconds.");
    return;
  }
  if (!result_) return;
  ui.Spacer(4);
  switch (result_->outcome) {
    case BugReportOutcome::kSent: {
      const std::string id = std::string(Tr("Report ID:")) + " " + result_->ticket;
      ui.StatusCard(trg::Status::kReady, "Report sent successfully", id.c_str());
      ui.Spacer(4);
      if (ImGui::Button(Tr("Copy report ID"))) {
        ImGui::SetClipboardText(result_->ticket.c_str());
        ui.SetStatus("Report ID copied.");
      }
      if (!sent_report_remembered_) ui.Help("The report was sent, but it could not be added to My reports on this PC.");
      break;
    }
    case BugReportOutcome::kRecentLimit:
    case BugReportOutcome::kDailyLimit:
      ui.StatusCard(trg::Status::kAttention, BugReportLimitTitle(result_->outcome), "Please try again later");
      break;
    case BugReportOutcome::kUnreachable:
      ui.StatusCard(trg::Status::kAttention, "Cannot reach the online service",
                    "Check your connection and press Send report again; your text is kept.");
      break;
    case BugReportOutcome::kRefused:
      ui.StatusCard(trg::Status::kAttention, "The report was not accepted",
                    "Change the description or choose another log, then send it again.");
      break;
    case BugReportOutcome::kNotSupported:
      ui.StatusCard(trg::Status::kAttention, "The online service cannot take reports yet",
                    "Direct reports need a newer online service. Your text is kept; try again after the service is updated.");
      break;
    case BugReportOutcome::kServiceError:
      ui.StatusCard(trg::Status::kAttention, "The online service could not take the report",
                    "This is not your fault. Try again in a few minutes; your text is kept.");
      break;
  }
}

}  // namespace ssf2t
