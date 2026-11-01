#include "online_runtime.h"

#include <chrono>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "netplay/online_game_socket.h"
#include "netplay/packet.h"
#include "netplay_setting.h"
#include "online/identity.h"
#include "online/instance.h"
#include "online/local_addresses.h"
#include "online_setting.h"

REXCVAR_DEFINE_BOOL(ssf2t_online_enabled, ssf2t::kDefaultOnlineEnabled, "SSF2T",
                    "Online play: friends, and Online Match fights against them over the internet");
REXCVAR_DEFINE_STRING(ssf2t_online_name, ssf2t::kDefaultOnlineNameValue, "SSF2T",
                      "Name other players see during online play");
REXCVAR_DEFINE_BOOL(ssf2t_online_ping_display, ssf2t::kDefaultOnlinePing, "SSF2T",
                    "Shows the round trip to the other player during an online fight");
REXCVAR_DEFINE_STRING(ssf2t_online_connection_test, "", "SSF2T",
                      "UDP configuration of the last Connection test finished with Online play on, written by the "
                      "launcher; empty: none since Online play was turned on");

namespace ssf2t {
namespace {

// Declared before the service, which taps it, so it is destroyed after.
netplay::OnlineGameSocket g_game_socket;
double g_next_bind_try = 0;
bool g_bind_failure_logged = false;

std::mutex g_mutex;
std::filesystem::path g_user_dir;
GuestVersion g_guest = GuestVersion::kBase013;
bool g_configured = false;
std::unique_ptr<online::OnlineService> g_service;
std::unique_ptr<online::InstanceLock> g_lock;
bool g_game_pumped = false;
bool g_runtime_signed_in = false;

// After Play restarts the process, the launcher that holds the lock may still be exiting.
constexpr int kLockAttempts = 12;
constexpr auto kLockRetry = std::chrono::milliseconds(250);
constexpr double kBindRetrySeconds = 5.0;

const char* GuestName(GuestVersion guest) {
  return guest == GuestVersion::kTitleUpdate023 ? online::kGuestTu023 : online::kGuestBase013;
}

online::Identity ResolveIdentity() {
  for (int attempt = 0; attempt < kLockAttempts && !g_lock; ++attempt) {
    if (attempt) std::this_thread::sleep_for(kLockRetry);
    g_lock = online::InstanceLock::TryAcquire(g_user_dir);
  }
  if (!g_lock) {
    REXLOG_INFO("online second instance on this user folder: temporary identity");
    return online::MakeEphemeralIdentity();
  }
  return online::LoadOrCreateIdentity(g_user_dir);
}

const char* PlatformName() {
#if defined(_WIN32)
  return "Windows";
#else
  return "Linux";
#endif
}

online::ServiceConfig MakeConfig() {
  online::ServiceConfig config;
  if (const char* url = std::getenv(kOnlineServiceEnv); url && *url) config.url = url;
  if (const char* stun = std::getenv(kStunServersEnv); stun && *stun) config.stun = stun;
  if (const char* upnp = std::getenv(kUpnpEnv); upnp && *upnp) config.upnp = std::string_view(upnp) != "0";
  config.guest = GuestName(g_guest);
  config.build = netplay::kNetplayBuild;
  config.version = SSF2T_PORT_VERSION;
  config.platform = PlatformName();
  config.host_port = kNetplayHostPort;
  config.join_port = kNetplayJoinPort;
  config.lan = online::LocalCandidates();
  config.game_socket = &g_game_socket;
  config.identity = ResolveIdentity;
  config.log = [](const std::string& line) { REXLOG_INFO("{}", line); };
  return config;
}

online::UserSettings SavedSettings() {
  online::UserSettings settings;
  settings.enabled = REXCVAR_GET(ssf2t_online_enabled);
  settings.name = REXCVAR_GET(ssf2t_online_name);
  return settings;
}

}  // namespace

void ConfigureOnline(const std::filesystem::path& user_dir, GuestVersion guest) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_user_dir = user_dir;
  g_guest = guest;
  g_configured = true;
  if (g_service) g_service->SetGuest(GuestName(guest), netplay::kNetplayBuild);
}

online::OnlineService& Online() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_service) g_service = std::make_unique<online::OnlineService>(MakeConfig());
  return *g_service;
}

void TickOnline(const online::UserSettings& settings) {
  online::OnlineService& service = Online();
  service.Apply(settings);
  service.Update(OnlineNowSeconds());
}

void PumpOnlineService() {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_configured) return;
  }
  const online::UserSettings settings = SavedSettings();
  if (!g_game_pumped && settings.enabled) {
    g_game_pumped = true;
    REXLOG_INFO("online runtime reconnect begin guest={}", GuestName(g_guest));
  }
  if (settings.enabled) EnsureOnlineGameSocket();
  TickOnline(settings);
  const bool signed_in = Online().SignedIn();
  if (signed_in != g_runtime_signed_in) {
    g_runtime_signed_in = signed_in;
    if (signed_in) REXLOG_INFO("online runtime connected");
    REXLOG_INFO("online runtime signed in={}", signed_in);
  }
}

double OnlineNowSeconds() {
  static const auto start = std::chrono::steady_clock::now();
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

bool EnsureOnlineGameSocket() {
  if (g_game_socket.is_open()) return true;
  const double now = OnlineNowSeconds();
  if (now < g_next_bind_try) return false;
  // A second instance on this PC finds the first port taken and takes the other; the advertised endpoint says which.
  if (g_game_socket.Open({kNetplayHostPort, kNetplayJoinPort})) {
    REXLOG_INFO("udp socket bound local=0.0.0.0:{}", g_game_socket.bound_port());
    return true;
  }
  g_next_bind_try = now + kBindRetrySeconds;
  if (!g_bind_failure_logged) {
    g_bind_failure_logged = true;
    REXLOG_INFO("udp socket bind failed ports={},{}", kNetplayHostPort, kNetplayJoinPort);
  }
  return false;
}

netplay::OnlineGameSocket& GameSocket() { return g_game_socket; }

}  // namespace ssf2t
