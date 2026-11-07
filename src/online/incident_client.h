// The launcher's calls for problem reports: each one opens its own short connection to the online service, so a report
// never touches the Online page's connection, its presence or its friends, and works with Online play off. All of them
// block: run them off the UI thread.
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "bug_report.h"
#include "online/identity.h"
#include "online/lobby_link.h"
#include "online/protocol.h"
#include "report_history.h"

namespace ssf2t::online {

struct IncidentServiceConfig {
  std::string url;
  std::string guest;     // base013 or tu023
  int build = 0;
  std::string version;   // the port version
  std::string platform;  // Windows or Linux
  std::string name = kDefaultOnlineName;
  ChannelFactory open_channel = OpenLobbyLink;
  int connect_timeout_ms = 8000;
  int call_timeout_ms = 20000;
  std::function<void(const std::string&)> log;  // never receives a receipt, a token or any report text
};

// One connection, one request at a time.
class IncidentSession {
 public:
  IncidentSession(ChannelFactory open, std::string url);
  ~IncidentSession();
  IncidentSession(const IncidentSession&) = delete;
  IncidentSession& operator=(const IncidentSession&) = delete;

  // False with the reason ("unreachable", "timeout" or what the link said) when the connection did not open.
  bool Open(int timeout_ms, std::string* why);
  // The response to the request `encode` made, or nothing with the service's error code, "timeout" or "link_lost".
  std::optional<ServerMessage> Call(const std::function<std::string(int)>& encode, int timeout_ms, std::string* code);
  void Close();

 private:
  ChannelFactory open_;
  std::string url_;
  std::unique_ptr<LobbyChannel> channel_;
  int next_seq_ = 1;
};

// Signs in without showing the player online, sends the report and closes. The receipt is empty from a service without
// receipts.
BugReportReceipt FileBugReport(const IncidentServiceConfig& config, const Identity& identity, const BugReportPackage& package);

struct StatusCheck {
  bool reached = false;     // the service answered
  bool supported = true;    // false: it does not know the operation
  std::vector<IncidentStatus> statuses;
};
// Needs no account and no Online play: the receipts are the only proof sent. At most kMostTrackedReports are asked about.
StatusCheck FetchReportStatuses(const IncidentServiceConfig& config, const std::vector<TrackedReport>& reports);

struct SentReportRead {
  bool reached = false;
  std::optional<BugReportPackage> package;  // empty when the service no longer has the report
};
SentReportRead FetchSentReport(const IncidentServiceConfig& config, const TrackedReport& report);

}  // namespace ssf2t::online
