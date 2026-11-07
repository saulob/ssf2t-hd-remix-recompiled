#include "online/incident_client.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace ssf2t::online {
namespace {

using Clock = std::chrono::steady_clock;
constexpr auto kPollInterval = std::chrono::milliseconds(5);

void Say(const IncidentServiceConfig& config, const std::string& line) {
  if (config.log) config.log(line);
}

BugReportReceipt NotSent(BugReportOutcome outcome) {
  BugReportReceipt receipt;
  receipt.outcome = outcome;
  return receipt;
}

}  // namespace

IncidentSession::IncidentSession(ChannelFactory open, std::string url) : open_(std::move(open)), url_(std::move(url)) {}

IncidentSession::~IncidentSession() { Close(); }

bool IncidentSession::Open(int timeout_ms, std::string* why) {
  channel_ = open_(url_);
  if (!channel_) {
    if (why) *why = "unreachable";
    return false;
  }
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
  ChannelEvent event;
  while (Clock::now() < deadline) {
    while (channel_->Poll(event)) {
      if (event.kind == ChannelEvent::Kind::kOpen) return true;
      if (event.kind == ChannelEvent::Kind::kClosed) {
        if (why) *why = event.text.empty() ? "unreachable" : event.text;
        return false;
      }
    }
    std::this_thread::sleep_for(kPollInterval);
  }
  if (why) *why = "timeout";
  return false;
}

std::optional<ServerMessage> IncidentSession::Call(const std::function<std::string(int)>& encode, int timeout_ms, std::string* code) {
  if (!channel_) {
    if (code) *code = "link_lost";
    return std::nullopt;
  }
  const int seq = next_seq_++;
  channel_->Send(encode(seq));
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
  ChannelEvent event;
  while (Clock::now() < deadline) {
    while (channel_->Poll(event)) {
      if (event.kind == ChannelEvent::Kind::kClosed) {
        if (code) *code = "link_lost";
        return std::nullopt;
      }
      if (event.kind != ChannelEvent::Kind::kText) continue;
      std::optional<ServerMessage> message = DecodeServerMessage(event.text);
      if (!message || message->kind != ServerMessage::Kind::kResponse || message->seq != seq) continue;
      if (message->code != "ok") {
        if (code) *code = message->code;
        return std::nullopt;
      }
      return message;
    }
    std::this_thread::sleep_for(kPollInterval);
  }
  if (code) *code = "timeout";
  return std::nullopt;
}

void IncidentSession::Close() {
  if (!channel_) return;
  channel_->Close();
  channel_.reset();
}

BugReportReceipt FileBugReport(const IncidentServiceConfig& config, const Identity& identity, const BugReportPackage& package) {
  if (!identity.ok()) {
    Say(config, "Bug report: no online identity (" + identity.error + ")");
    return NotSent(BugReportOutcome::kServiceError);
  }
  IncidentSession session(config.open_channel, config.url);
  std::string why;
  if (!session.Open(config.connect_timeout_ms, &why)) {
    Say(config, "Bug report: no connection (" + why + ")");
    return NotSent(BugReportOutcome::kUnreachable);
  }
  HelloFields hello;
  hello.guest = config.guest;
  hello.build = config.build;
  hello.player = identity.player_id;
  hello.token = identity.token;
  hello.name = CleanOnlineName(config.name);
  hello.version = config.version;
  hello.platform = config.platform;
  hello.presence = false;
  std::string code;
  const std::optional<ServerMessage> greeted =
      session.Call([&](int seq) { return EncodeHello(seq, hello); }, config.connect_timeout_ms, &code);
  if (!greeted) {
    Say(config, "Bug report: sign-in refused (" + code + ")");
    return NotSent(BugReportOutcomeOf(code));
  }
  if (!greeted->capabilities || !greeted->capabilities->bug_reports) {
    Say(config, "Bug report: the service takes no reports");
    return NotSent(BugReportOutcome::kNotSupported);
  }
  const IncidentFields fields = BugReportRequestFields(package);
  const std::optional<ServerMessage> answer =
      session.Call([&](int seq) { return EncodeIncidentFile(seq, fields); }, config.call_timeout_ms, &code);
  session.Close();
  if (!answer) {
    Say(config, "Bug report: not accepted (" + code + ")");
    return NotSent(BugReportOutcomeOf(code));
  }
  if (!IsTicket(answer->incident_ticket)) return NotSent(BugReportOutcome::kServiceError);
  Say(config, "Bug report: sent as " + answer->incident_ticket);
  BugReportReceipt receipt;
  receipt.outcome = BugReportOutcome::kSent;
  receipt.ticket = answer->incident_ticket;
  receipt.credential = IsReceipt(answer->incident_receipt) ? answer->incident_receipt : std::string();
  receipt.submitted_at = answer->incident_submitted_at;
  return receipt;
}

StatusCheck FetchReportStatuses(const IncidentServiceConfig& config, const std::vector<TrackedReport>& reports) {
  StatusCheck check;
  std::vector<IncidentClaim> claims;
  for (size_t i = 0; i < std::min(reports.size(), kMostTrackedReports); ++i) claims.push_back({reports[i].ticket, reports[i].receipt});
  if (claims.empty()) return check;
  IncidentSession session(config.open_channel, config.url);
  if (!session.Open(config.connect_timeout_ms, nullptr)) return check;
  std::string code;
  const std::optional<ServerMessage> answer =
      session.Call([&](int seq) { return EncodeIncidentStatus(seq, claims); }, config.call_timeout_ms, &code);
  session.Close();
  if (!answer) {
    check.reached = code != "timeout" && code != "link_lost";
    check.supported = code != "bad_request";
    return check;
  }
  check.reached = true;
  if (answer->incident_statuses) check.statuses = *answer->incident_statuses;
  return check;
}

SentReportRead FetchSentReport(const IncidentServiceConfig& config, const TrackedReport& report) {
  SentReportRead read;
  IncidentSession session(config.open_channel, config.url);
  if (!session.Open(config.connect_timeout_ms, nullptr)) return read;
  std::string code;
  const std::optional<ServerMessage> answer = session.Call(
      [&](int seq) { return EncodeIncidentRead(seq, {report.ticket, report.receipt}); }, config.call_timeout_ms, &code);
  session.Close();
  if (!answer) {
    read.reached = code == "not_found";
    return read;
  }
  read.reached = true;
  if (answer->incident_report) read.package = SentReportOf(*answer->incident_report);
  return read;
}

}  // namespace ssf2t::online
