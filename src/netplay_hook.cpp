#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>

#include "guest_memory.h"
#include "match_input.h"
#include "netplay/determinism.h"
#include "netplay/guest_netplay.h"
#include "netplay/guest_simulation.h"
#include "netplay/input_model.h"
#include "netplay/input_route.h"
#include "netplay/lobby_mux.h"
#include "netplay/rollback_core.h"
#include "netplay/session.h"
#include "netplay_runtime.h"
#include "netplay_setting.h"
#include "online_runtime.h"
#include "online_setting.h"
#include "player_match_guest.h"
#include "player_match_net.h"
#include "title_update_image.h"

REXCVAR_DEFINE_BOOL(ssf2t_netplay_determinism_test, false, "SSF2T",
                    "Developer: replays offline fight frames and logs DEBUG_SAULO determinism checks");
REXCVAR_DEFINE_STRING(ssf2t_netplay_role, ssf2t::kDefaultNetplayRole, "SSF2T",
                      "Developer: off, host or join. Plays one Local Versus fight against another instance of "
                      "the game over UDP (see Peer Address)");
REXCVAR_DEFINE_STRING(ssf2t_netplay_peer_address, ssf2t::kDefaultNetplayPeerAddress, "SSF2T",
                      "Developer: IPv4 address of the Host instance, used when Netplay Test is Join");

REX_EXTERN(__imp__base013_sub_823E90C0);
REX_EXTERN(__imp__base013_sub_8223DDB8);
REX_EXTERN(__imp__base013_sub_8241CC48);
REX_EXTERN(__imp__tu023_sub_823E97F8);
REX_EXTERN(__imp__tu023_sub_8223DEE0);
REX_EXTERN(__imp__tu023_sub_8241D320);
REX_EXTERN(__imp__base013_sub_823E1CF0);
REX_EXTERN(__imp__base013_sub_82318618);
REX_EXTERN(__imp__tu023_sub_823E23C8);
REX_EXTERN(__imp__tu023_sub_82318558);
REX_EXTERN(__imp__base013_sub_8241DEC8);
REX_EXTERN(__imp__tu023_sub_8241E618);
REX_EXTERN(__imp__base013_sub_8241CC08);
REX_EXTERN(__imp__tu023_sub_8241D2E0);
REX_EXTERN(__imp__base013_sub_82252B90);
REX_EXTERN(__imp__tu023_sub_82252BD8);

namespace {

namespace np = ssf2t::netplay;

constexpr uint32_t kFirstSampleFrame = 120;
constexpr uint32_t kSampleInterval = 300;
constexpr uint32_t kMaxSamples = 8;
constexpr size_t kWatchedArenaByte = 0x3F0;
constexpr size_t kRunMergeGap = 16;
constexpr size_t kMaxRuns = 16;
constexpr size_t kMaxClassifiedRuns = 256;

using np::GuestCalls;

constexpr uint32_t kTraceCallers = 6;

struct PassTrace {
  uint32_t rand_calls = 0;
  uint32_t side_setups = 0;
  uint32_t gate_queries = 0;
  uint32_t rand_callers[kTraceCallers] = {};
};

PassTrace* g_trace = nullptr;
np::SampleSchedule g_schedule(kFirstSampleFrame, kSampleInterval, kMaxSamples);
uint32_t g_failures = 0;

using np::PointerUsable;
template <typename G>
using GuestSimulation = np::GuestSimulation<G>;

void Log(const std::string& line) { REXLOG_INFO("{}", line); }

uint32_t BlobWordBE(const std::vector<uint8_t>& blob, size_t offset) {
  return ssf2t::LoadBE32(blob.data(), static_cast<uint32_t>(offset));
}

void LogInputState(np::Frame frame, const std::vector<uint8_t>& state) {
  for (uint32_t player = 0; player < np::kPlayers; ++player) {
    const size_t entry = np::kGuestStateSize + player * np::kInputEntryStride;
    REXLOG_INFO("DEBUG_SAULO netplay frame={} table{} state={} cur={:04X} prev={:04X} pressed={:04X} released={:04X}",
                frame, player, BlobWordBE(state, entry), BlobWordBE(state, entry + np::kInputCurrentOffset),
                BlobWordBE(state, entry + np::kInputPreviousOffset), BlobWordBE(state, entry + 16),
                BlobWordBE(state, entry + 20));
  }
}

template <typename G>
const char* ClassifyAddress(uint32_t address) {
  const auto inside = [address](uint32_t start, uint32_t size) { return address >= start && address < start + size; };
  if (inside(G::kRngState, np::kRngStateBytes)) return "rng";
  if (inside(G::kSideOrder, np::kSideOrderBytes)) return "side";
  if (inside(G::kPaceCounter, 4)) return "pace";
  if (address >= G::kDrawListLow && address < G::kDrawListHigh) return "draw";
  return "other";
}

template <typename G>
void LogLeak(np::Frame frame, const char* label, const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
  const np::DiffRuns all = np::FindRuns(a, b, kRunMergeGap, kMaxClassifiedRuns);
  uint32_t rng = 0, side = 0, pace = 0, draw = 0;
  np::DiffRuns other;
  for (const np::DiffRun& run : all.runs) {
    const std::string kind = ClassifyAddress<G>(np::kDiagWindowLow + static_cast<uint32_t>(run.offset));
    if (kind == "rng") ++rng;
    else if (kind == "side") ++side;
    else if (kind == "pace") ++pace;
    else if (kind == "draw") ++draw;
    else other.runs.push_back(run);
  }
  other.total = other.runs.size();
  if (other.runs.size() > kMaxRuns) other.runs.resize(kMaxRuns);
  const std::string tag = std::string(label) + "(rng=" + std::to_string(rng) + " side=" + std::to_string(side) +
                          " pace=" + std::to_string(pace) + " draw=" + std::to_string(draw) + ") other";
  Log(np::FormatRuns(frame, tag.c_str(), np::kDiagWindowLow, other));
}

void LogTrace(np::Frame frame, const char* pass, const PassTrace& trace) {
  std::string callers;
  char text[16];
  for (uint32_t i = 0; i < trace.rand_calls && i < kTraceCallers; ++i) {
    std::snprintf(text, sizeof text, "%s%08X", i ? "," : "", trace.rand_callers[i]);
    callers += text;
  }
  REXLOG_INFO("DEBUG_SAULO netplay frame={} pass={} rand={} side_setup={} gate={} lr={}", frame, pass,
              trace.rand_calls, trace.side_setups, trace.gate_queries, callers);
}

// Developer only: a value saved while the toggle was shown to everyone must not keep sampling a player's fights.
bool DeterminismTestOn() {
  static const bool developer = ssf2t::DeveloperControlsEnabled();
  return developer && REXCVAR_GET(ssf2t_netplay_determinism_test);
}

template <typename G>
void RunDeterminismSample(PPCContext& ctx, uint8_t* base, const GuestCalls& calls, np::Frame frame) {
  GuestSimulation<G> sim(ctx, base, calls);
  np::InputWord current[np::kPlayers];
  np::InputWord previous[np::kPlayers];
  for (uint32_t player = 0; player < np::kPlayers; ++player) {
    current[player] = sim.TableWord(player, np::kInputCurrentOffset);
    previous[player] = sim.TableWord(player, np::kInputPreviousOffset);
  }
  sim.SetPrevious(previous);

  std::vector<uint8_t> a, a_again, speculative, normal, native;
  g_schedule.Taken(frame);
  if (!sim.Save(a)) {
    Log("DEBUG_SAULO netplay determinism save failed");
    sim.Step(false);
    return;
  }
  const std::vector<uint8_t> window_start = sim.DiagWindow();
  const np::InputWord inputs[np::kPlayers] = {current[0], current[1]};

  PassTrace trace_speculative, trace_normal, trace_native;
  g_trace = &trace_speculative;
  sim.Advance(inputs, true);
  g_trace = nullptr;
  sim.Save(speculative);
  const uint32_t speculative_checksum = sim.Checksum();
  const std::vector<uint8_t> window_after_first = sim.DiagWindow();

  sim.Load(a);
  sim.Save(a_again);
  const np::BlobDiff roundtrip = np::CompareBlobs(a, a_again);
  const std::vector<uint8_t> window_second_start = sim.DiagWindow();

  g_trace = &trace_normal;
  sim.Advance(inputs, false);
  g_trace = nullptr;
  sim.Save(normal);
  const uint32_t normal_checksum = sim.Checksum();
  const np::BlobDiff resim = np::CompareBlobs(speculative, normal);
  const std::vector<uint8_t> window_after_second = sim.DiagWindow();

  sim.Load(a);
  sim.SetPrevious(previous);
  g_trace = &trace_native;
  sim.Step(false);
  g_trace = nullptr;
  sim.Save(native);
  const np::BlobDiff native_diff = np::CompareBlobs(normal, native, np::kGuestStateSize);

  const bool ok = roundtrip.equal && resim.equal && speculative_checksum == normal_checksum;
  if (!ok) ++g_failures;
  Log(np::FormatCheck(frame, "roundtrip", roundtrip));
  Log(np::FormatCheck(frame, "resim", resim));
  Log(np::FormatCheck(frame, "native", native_diff));
  REXLOG_INFO("DEBUG_SAULO netplay frame={} checksum={:08X}/{:08X} inputs={:04X},{:04X}", frame, speculative_checksum,
              normal_checksum, current[0], current[1]);
  if (!ok || !native_diff.equal || g_schedule.taken() == 1) {
    REXLOG_INFO("DEBUG_SAULO netplay frame={} arena3F0 A={:02X} B1={:02X} A2={:02X} B2={:02X} B0={:02X}", frame,
                a[kWatchedArenaByte], speculative[kWatchedArenaByte], a_again[kWatchedArenaByte],
                normal[kWatchedArenaByte], native[kWatchedArenaByte]);
    LogInputState(frame, a);
    LogTrace(frame, "B1", trace_speculative);
    LogTrace(frame, "B2", trace_normal);
    LogTrace(frame, "B0", trace_native);
    LogLeak<G>(frame, "leak", window_start, window_second_start);
    LogLeak<G>(frame, "post", window_after_first, window_after_second);
  }
  if (g_schedule.taken() >= kMaxSamples) {
    REXLOG_INFO("DEBUG_SAULO netplay determinism samples={} failures={}", g_schedule.taken(), g_failures);
  }
}

template <typename G>
bool CodeMatches(const uint8_t* base) {
  static const bool matches = [base] {
    const bool ok = np::NetplayCodeMatches<G>(base);
    if (!ok) {
      REXLOG_INFO("DEBUG_SAULO netplay determinism test off: {} code differs from the analysed image", G::kName);
    }
    return ok;
  }();
  return matches;
}

std::mutex g_lobby_mutex;
np::LobbyMux* g_lobby = nullptr;

np::LobbyHold g_lobby_hold;

void PublishLobby(np::LobbyMux* mux, uint32_t tag = 0) {
  std::lock_guard<std::mutex> lock(g_lobby_mutex);
  g_lobby = mux;
  if (!mux) {
    g_lobby_hold.Clear();
    return;
  }
  for (const np::LobbyDatagram& held : g_lobby_hold.Take(tag)) mux->SendLobby(held);
}

uint64_t NetplayNowMs() {
  static const auto start = std::chrono::steady_clock::now();
  return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
                      .count());
}

np::PadSample RawPad(uint32_t user, np::InputWord word) {
  np::PadSample sample;
  sample.word = word;
  rex::Runtime* runtime = REX_KERNEL_STATE() ? REX_KERNEL_STATE()->emulator() : nullptr;
  auto* input = runtime ? static_cast<rex::input::InputSystem*>(runtime->input_system()) : nullptr;
  rex::input::X_INPUT_STATE state{};
  if (input && input->GetState(user, &state) == 0) {
    sample.buttons = state.gamepad.buttons;
    sample.stick_x = np::StickDirection(state.gamepad.thumb_lx);
    sample.stick_y = np::StickDirection(state.gamepad.thumb_ly);
  }
  return sample;
}

bool FightActive(const uint8_t* base, uint32_t net_active, uint32_t state_function_addr, uint32_t fight_loop,
                 uint32_t arena_pointer_addr) {
  return base[net_active] == 0 && ssf2t::LoadBE32(base, state_function_addr) == fight_loop &&
         PointerUsable(ssf2t::LoadBE32(base, arena_pointer_addr));
}

// Drives one guest's real two-instance fight: connects, waits for the fight-ready barrier, then hands every
// simulated frame to the existing Session/RollbackCore. Off by default; a wrong peer address or a bind failure
// just logs and leaves gameplay untouched (fails closed, like the determinism gate's fingerprint check).
template <typename G>
class FightRuntime {
 public:
  static FightRuntime& Instance() {
    static FightRuntime instance;
    return instance;
  }

  void PumpNetwork(uint64_t now_ms, const uint8_t* base) {
    EnsureStarted(now_ms);
    if (session_) {
      EndFightOutsideFightLoop(base, now_ms);
      PunchIfWaiting(now_ms);
      session_->Poll(now_ms);
      LogGameState(base);
      LogPreFight(base);
    }
    LogTransitions();
    ReportLink();
  }

  bool PlayerMatchFight(const uint8_t* base) const {
    return session_ && online_driven_ && base[G::kGameMode] == kOnlineGameMode;
  }

  // The game's own NetSync start: the point both consoles started their rollback from (in a Player Match, the
  // character select's init, so the remote player's selections reach this console).
  void FightInitReached() {
    if (phase_ == FightPhase::kIdle) EnterPhase(FightPhase::kBarrier);
  }

  // From NetSync start on the rollback core runs every simulated frame through the game's step command, which already
  // does the frame tail's simulation work; the tail must not run it again once per real frame, held or stalled.
  bool OwnsFrameTail(const uint8_t* base) const {
    if (!session_ || !LocalFightActive(base)) return false;
    return phase_ != FightPhase::kIdle || session_->state() == np::SessionState::kRunning;
  }

  // DEBUG_SAULO: which frame tails the net-active answer covers, once per kind and fight.
  void LogFrameTail(bool owns, bool in_pump) {
    if (!owns) return;
    bool& logged = in_pump ? logged_pump_tail_ : logged_tail_override_;
    if (logged) return;
    logged = true;
    REXLOG_INFO("DEBUG_SAULO netplay frame_tail net_active_override={} caller={} generation={} guest={}",
                in_pump ? "false" : "true", in_pump ? "frame_pump" : "fight_loop", generation_, G::kName);
  }

  bool Wanted() const {
    StartPlan plan;
    return session_ != nullptr || ResolvePlan(plan);
  }

  // Called from the sim-step hook. True when it fully replaced the normal step this frame.
  bool Takeover(PPCContext& ctx, uint8_t* base, const GuestCalls& calls, uint64_t now_ms) {
    if (!EnsureStarted(now_ms)) return false;

    const bool fight_active = LocalFightActive(base);
    if (online_driven_ && fight_active && !takeover_logged_) {
      takeover_logged_ = true;
      REXLOG_INFO("DEBUG_SAULO player_match rollback takeover reachable guest={}", G::kName);
    }
    session_->SetLocalReady(fight_active, now_ms);
    LogTransitions();
    if (!fight_active) {
      ResetFight();
      return false;
    }
    if (session_->state() != np::SessionState::kRunning) return BeginWhenBothReady(ctx, base, calls, now_ms);
    if (!live_sim_) live_sim_ = MakeSimulation(ctx, base, calls);
    const np::InputWord local_sample =
        online_driven_ ? OnlineLocalInput(base)
                       : live_sim_->TableWord(uint32_t(session_->local_player()), np::kInputCurrentOffset);
    const uint32_t rollbacks_before = session_->core() ? session_->core()->stats().rollbacks : 0;
    const np::TickStatus status = session_->RunFrame(local_sample, now_ms);
    if (session_->core()) {
      fight_stats_ = session_->core()->stats();
      if (fight_stats_.rollbacks != rollbacks_before) {
        REXLOG_INFO("DEBUG_SAULO netplay rollback to={} depth={}", session_->core()->confirmed_frame(),
                    fight_stats_.max_depth);
      }
    }
    LogTransitions();
    if (status == np::TickStatus::kFault) {
      REXLOG_INFO("DEBUG_SAULO netplay fault frame={}", session_->core() ? session_->core()->current_frame() : 0);
      Shutdown(now_ms, "fault");
      return false;
    }
    return true;
  }

 private:
  // kBarrier: the game's NetSync start passed. kAgreement: frame 0 is offered and the peers agree on its setup and
  // state. kRefused: they could not; the fight holds until it is left. kRollback: the rollback core runs the fight.
  enum class FightPhase { kIdle, kBarrier, kAgreement, kRefused, kRollback };

  static const char* PhaseName(FightPhase phase) {
    switch (phase) {
      case FightPhase::kBarrier: return "barrier";
      case FightPhase::kAgreement: return "agreement";
      case FightPhase::kRefused: return "refused";
      case FightPhase::kRollback: return "rollback";
      default: return "idle";
    }
  }

  void EnterPhase(FightPhase phase) {
    if (phase == phase_) return;
    REXLOG_INFO("DEBUG_SAULO netplay fight phase={} was={} generation={} fight={} guest={}", PhaseName(phase),
                PhaseName(phase_), generation_, session_ ? session_->fight_index() : 0, G::kName);
    phase_ = phase;
  }

  struct StartPlan {
    bool host = false;
    bool online = false;
    bool relay = false;
    uint32_t generation = 0;
    ssf2t::netplay::Endpoint peer{};
    bool bad_address = false;
    std::string raw_peer_address;  // only for the bad_peer_address log; empty for the dev-cvar role
  };

  static const char* OnlineGuestName() {
    return G::kGuest == np::GuestId::kTitleUpdate023 ? ssf2t::online::kGuestTu023 : ssf2t::online::kGuestBase013;
  }

  // The lobby's hand-off wins over the developer role, which may still be set from earlier tests.
  static bool ResolvePlan(StartPlan& plan) {
    if (const auto assignment = ssf2t::Online().Assignment(); assignment && assignment->guest == OnlineGuestName()) {
      plan = {};
      plan.online = true;
      plan.host = assignment->host;
      plan.relay = assignment->relay;
      plan.generation = assignment->generation;
      uint32_t address = 0;
      const bool usable = np::UsableAddress(np::ParsePeerAddress(assignment->peer_ip, address));
      plan.bad_address = !plan.host && !usable;
      plan.raw_peer_address = assignment->peer_ip;
      plan.peer = ssf2t::netplay::Endpoint{usable ? address : 0, assignment->peer_port};
      return true;
    }
    const std::string role = REXCVAR_GET(ssf2t_netplay_role);
    if (role != ssf2t::kNetplayRoleHost && role != ssf2t::kNetplayRoleJoin) return false;
    plan = {};
    plan.host = role == ssf2t::kNetplayRoleHost;
    if (!plan.host) {
      uint32_t address = 0;
      plan.bad_address = !ssf2t::netplay::ParseIPv4(REXCVAR_GET(ssf2t_netplay_peer_address), address);
      plan.peer = ssf2t::netplay::Endpoint{address, ssf2t::kNetplayHostPort};
    }
    return true;
  }

  bool EnsureStarted(uint64_t now_ms) {
    StartPlan plan;
    const bool wanted = ResolvePlan(plan);
    if (session_ && online_driven_ && plan.online && plan.generation != generation_) {
      Shutdown(now_ms, "new_match");
      retry_at_ms_ = now_ms;
    }
    if (session_) {
      const bool unfinished = session_->state() != np::SessionState::kRunning;
      if (!wanted && (!online_driven_ || unfinished)) {
        Shutdown(now_ms, "match_ended");
        return false;
      }
      const np::SessionState state = session_->state();
      if (online_driven_ && wanted && (state == np::SessionState::kTimedOut || state == np::SessionState::kClosed)) {
        // A peer that was connected and is gone ends the match; restarting would wait for it forever.
        if (peer_seen_) {
          REXLOG_INFO("DEBUG_SAULO player_match disconnect cleanup reason={} rollback=stopped lobby_mux=closed "
                      "socket=closed handoff=cleared guest={}",
                      state == np::SessionState::kTimedOut ? "timeout" : "closed", G::kName);
          ssf2t::Online().LeaveMatch();
          ssf2t::EndMatchInput("peer_timeout");
        }
        Shutdown(now_ms, state == np::SessionState::kTimedOut ? "timeout" : "closed");
        return false;
      }
      return true;
    }
    if (!wanted || now_ms < retry_at_ms_ || (faulted_ && !plan.online)) return false;
    if (plan.bad_address) {
      REXLOG_INFO("DEBUG_SAULO netplay join failed reason=bad_peer_address raw_address={}", plan.raw_peer_address);
      if (plan.online) {
        if (!bad_address_since_ms_ || plan.generation != bad_address_generation_) {
          bad_address_since_ms_ = now_ms;
          bad_address_generation_ = plan.generation;
        }
        if (now_ms - bad_address_since_ms_ >= kBadAddressGiveUpMs) {
          // The match is stuck: leaving is what lets Invite Player work again instead of refusing with no_player_match.
          REXLOG_INFO("DEBUG_SAULO netplay give up reason=bad_peer_address generation={} guest={}", plan.generation,
                      G::kName);
          ssf2t::Online().LeaveMatch();
          bad_address_since_ms_ = 0;
        }
      }
      Fault(plan, now_ms);
      return false;
    }
    bad_address_since_ms_ = 0;
    const uint16_t local_port = plan.host ? ssf2t::kNetplayHostPort : ssf2t::kNetplayJoinPort;
    np::OnlineGameSocket& shared = ssf2t::GameSocket();
    // An online match must play on the socket its endpoint was learned and proven on: another one is another NAT
    // mapping, which the peer was never told about.
    if (plan.online || (shared.is_open() && shared.bound_port() == local_port)) {
      if (!shared.is_open()) {
        REXLOG_INFO("DEBUG_SAULO netplay bind failed reason=online_socket_unbound");
        Fault(plan, now_ms);
        return false;
      }
      transport_ = &shared;
      REXLOG_INFO("DEBUG_SAULO netplay socket shared port={}", shared.bound_port());
    } else {
      own_transport_ = std::make_unique<ssf2t::netplay::UdpTransport>();
      if (!own_transport_->Open(local_port, false)) {
        REXLOG_INFO("DEBUG_SAULO netplay bind failed port={}", local_port);
        own_transport_.reset();
        Fault(plan, now_ms);
        return false;
      }
      transport_ = own_transport_.get();
    }
    ssf2t::netplay::SessionConfig config;
    config.role = plan.host ? ssf2t::netplay::Role::kHost : ssf2t::netplay::Role::kJoin;
    config.guest = G::kGuest;
    config.peer = plan.peer;
    config.session_id = ssf2t::netplay::MatchSessionId(plan.generation, now_ms);
    config.nonce = uint32_t(now_ms) ^ 0x9E3779B9u;
    config.verify_frame_zero = true;
    mux_ = std::make_unique<np::LobbyMux>(*transport_);
    session_ = std::make_unique<ssf2t::netplay::Session>(config, *mux_);
    if (plan.online) {
      if (const auto route = ssf2t::RouteFor(ssf2t::Online().Peer())) {
        mux_->SetRoute(route->tag, plan.peer);
        PublishLobby(mux_.get(), route->tag);
        REXLOG_INFO("DEBUG_SAULO player_match lobby transport begin tag={:08X} peer_port={}", route->tag, plan.peer.port);
      }
    }
    session_->Start(now_ms);
    online_driven_ = plan.online;
    generation_ = plan.generation;
    // A relayed match has no NAT mapping to keep open.
    punch_peer_ = plan.online && plan.host && !plan.relay ? plan.peer : ssf2t::netplay::Endpoint{};
    if (plan.online) {
      REXLOG_INFO("DEBUG_SAULO online session handoff role={} transport={} guest={}", plan.host ? "host" : "join",
                  plan.relay ? "relay" : "direct", G::kName);
    }
    REXLOG_INFO("DEBUG_SAULO netplay session started role={} generation={} guest={}", plan.host ? "host" : "join",
                generation_, G::kName);
    return true;
  }

  std::unique_ptr<np::GuestSimulation<G>> MakeSimulation(PPCContext& ctx, uint8_t* base, const GuestCalls& calls) {
    return std::make_unique<np::GuestSimulation<G>>(ctx, base, calls, np::FrameKind::kRollbackFrame);
  }

  void ResetFight() {
    live_sim_.reset();
    EnterPhase(FightPhase::kIdle);
    logged_tail_override_ = false;
    logged_pump_tail_ = false;
    logged_modes_ = {};
    presentation_logged_ = false;
  }

  // Frame 0 must be the same state on both peers: the fight runs natively only up to NetSync start, then holds without
  // stepping (and without the frame tail's simulation work) while the peers agree, however long each side waits for
  // the other. Their setup words must be equal; their states may differ only by what each one's previous fight left
  // behind, which the joiner then takes from the host. Anything else holds the fight for good: a rollback session
  // started from it would only desync.
  bool BeginWhenBothReady(PPCContext& ctx, uint8_t* base, const GuestCalls& calls, uint64_t now_ms) {
    if (online_driven_ && phase_ == FightPhase::kIdle) return false;
    const np::SessionState state = session_->state();
    if (!session_->ReadyToBeginFight()) {
      return state == np::SessionState::kWaiting || state == np::SessionState::kConnecting ||
             state == np::SessionState::kConnected;
    }
    if (phase_ == FightPhase::kIdle || phase_ == FightPhase::kBarrier) OfferFrameZero(ctx, base, calls);
    LogFrameZeroReport();
    if (phase_ == FightPhase::kAgreement && session_->FrameZeroAdoptionReady()) {
      AdoptHostFrameZero();
      LogFrameZeroReport();
    }
    const np::FrameZeroVerdict verdict = session_->frame_zero();
    if (verdict == np::FrameZeroVerdict::kMismatch && phase_ == FightPhase::kAgreement) {
      EnterPhase(FightPhase::kRefused);
      REXLOG_INFO("DEBUG_SAULO netplay takeover refused reason={} generation={} fight={} guest={}",
                  np::RefusalName(session_->frame_zero_refusal()), generation_, session_->fight_index(), G::kName);
    }
    if (verdict != np::FrameZeroVerdict::kMatch || phase_ != FightPhase::kAgreement) return true;
    if (!session_->BeginFight(*live_sim_, now_ms)) {
      ResetFight();
      REXLOG_INFO("DEBUG_SAULO netplay takeover stopped reason=begin_fight_failed");
      return false;
    }
    EnterPhase(FightPhase::kRollback);
    fight_stats_ = {};
    const ModeWords modes = ReadModes(base);
    REXLOG_INFO("DEBUG_SAULO netplay takeover begin state={:X}/{:X}/{:X}/{:X} generation={} fight={} guest={}",
                modes.mode[0], modes.mode[1], modes.mode[2], modes.mode[3], generation_, session_->fight_index(),
                G::kName);
    if (online_driven_) {
      REXLOG_INFO("DEBUG_SAULO player_match rollback takeover guest={}", G::kName);
      LogInputRoute(base);
    }
    return true;
  }

  void OfferFrameZero(PPCContext& ctx, uint8_t* base, const GuestCalls& calls) {
    EnterPhase(FightPhase::kAgreement);
    live_sim_ = MakeSimulation(ctx, base, calls);
    live_sim_->NeutralInputs();
    std::vector<uint8_t> start;
    live_sim_->Save(start);
    std::vector<uint8_t> view = np::FrameZeroView(start);
    std::vector<uint32_t> setup = ReadSetupWords(base);
    REXLOG_INFO("DEBUG_SAULO netplay fight start frame=0 generation={} fight={} state {}", generation_,
                session_->fight_index(), np::DescribeStateHashes(view));
    LogSetupWords(setup);
    session_->OfferFrameZero(std::move(view), G::kGlobalsSnapshot, std::move(setup));
  }

  // Through the game's own load command: the same restore every rollback makes.
  void AdoptHostFrameZero() {
    std::vector<uint8_t> blob;
    if (!live_sim_->Save(blob)) return;
    const std::optional<np::FrameZeroAdoption> adopted = session_->AdoptFrameZero(blob);
    if (!adopted || !live_sim_->Load(blob)) return;
    std::vector<uint8_t> state;
    live_sim_->Save(state);
    std::vector<uint8_t> view = np::FrameZeroView(state);
    REXLOG_INFO("DEBUG_SAULO netplay agree adopted=host blocks={} bytes={} generation={} fight={} state {}",
                adopted->blocks, adopted->bytes, generation_, session_->fight_index(), np::DescribeStateHashes(view));
    session_->ReofferFrameZero(std::move(view));
  }

  static std::vector<uint32_t> ReadSetupWords(const uint8_t* base) {
    np::SetupFields fields;
    fields.mode = base[G::kGameMode];
    const ModeWords modes = ReadModes(base);
    std::copy(std::begin(modes.mode), std::end(modes.mode), fields.phase);
    fields.stage = ssf2t::LoadBE32(base, G::kOnlineStage);
    const bool flag = base[G::kButtonConfigFlag] != 0;
    for (uint32_t slot = 0; slot < 2; ++slot) {
      std::memcpy(fields.buttons[slot], base + np::ButtonConfigAddress<G>(flag, slot), np::kButtonConfigBytes);
    }
    return np::SetupWords(fields);
  }

  // DEBUG_SAULO
  void LogSetupWords(const std::vector<uint32_t>& setup) const {
    std::string words;
    char word[48];
    for (size_t i = 0; i < setup.size() && i < np::kSetupWordCount; ++i) {
      std::snprintf(word, sizeof word, " %s=%08X", np::kSetupWordNames[i], setup[i]);
      words += word;
    }
    REXLOG_INFO("DEBUG_SAULO netplay setup local fight={} generation={} guest={}{}", session_->fight_index(),
                generation_, G::kName, words);
  }

  void LogFrameZeroReport() {
    for (const std::string& line : session_->TakeFrameZeroReport()) Log(line);
  }

  // DEBUG_SAULO: the game's state function after a Player Match fight (results, quote, lobby), once per change.
  void LogGameState(const uint8_t* base) {
    if (!online_driven_) return;
    const uint32_t state = ssf2t::LoadBE32(base, G::kStateFunction);
    if (state == logged_game_state_) return;
    const char* name = state == G::kFightLoop ? "fight_loop" : state == 0 ? "frontend" : "other";
    REXLOG_INFO("DEBUG_SAULO post_fight state={:08X} name={} was={:08X} generation={} guest={}", state, name,
                logged_game_state_, generation_, G::kName);
    logged_game_state_ = state;
  }

  // The sim's phase (the mode words the game's phase tables index); DEBUG_SAULO: its phase timer and the presentation
  // request a waiting phase sets.
  struct ModeWords {
    uint16_t mode[4] = {};
    uint16_t timer = 0;
    uint8_t request = 0;
    bool operator==(const ModeWords& other) const {
      return std::memcmp(mode, other.mode, sizeof(mode)) == 0;
    }
  };

  static ModeWords ReadModes(const uint8_t* base) {
    ModeWords modes;
    const uint32_t work = ssf2t::LoadBE32(base, G::kFightWork);
    if (!PointerUsable(work)) return modes;
    const uint8_t* fight = ssf2t::GuestHost(base, work);
    for (uint32_t i = 0; i < 4; ++i) modes.mode[i] = ssf2t::LoadBE16(fight, i * 4);
    modes.timer = ssf2t::LoadBE16(fight, kPhaseTimerOffset);
    modes.request = fight[kPresentationRequestOffset];
    return modes;
  }

  void LogPreFight(const uint8_t* base) {
    if (!online_driven_ || presentation_logged_ || !LocalFightActive(base)) return;
    const ModeWords modes = ReadModes(base);
    if (modes == logged_modes_) return;
    logged_modes_ = modes;
    REXLOG_INFO("DEBUG_SAULO pre_fight state={:X}/{:X}/{:X}/{:X} timer={} request={} generation={} guest={}",
                modes.mode[0], modes.mode[1], modes.mode[2], modes.mode[3], modes.timer, modes.request, generation_,
                G::kName);
    if (modes.mode[1] == kFightMode && modes.mode[2] >= kRoundInitMode) {
      presentation_logged_ = true;
      REXLOG_INFO("DEBUG_SAULO pre_fight presentation complete state={:X}/{:X}/{:X}/{:X} generation={} guest={}",
                  modes.mode[0], modes.mode[1], modes.mode[2], modes.mode[3], generation_, G::kName);
    }
  }

  // A Player Match assignment never takes over a Local Match fight.
  bool LocalFightActive(const uint8_t* base) const {
    return FightActive(base, G::kNetActive, G::kStateFunction, G::kFightLoop, G::kArenaPointer) &&
           (!online_driven_ || base[G::kGameMode] == kOnlineGameMode);
  }

  // The sim-step hook runs only inside the fight loop, so it never sees a fight end; without this the finished
  // fight's core, frames and snapshots would resume in the next fight.
  void EndFightOutsideFightLoop(const uint8_t* base, uint64_t now_ms) {
    if (!session_->local_ready() || LocalFightActive(base)) {
      frames_outside_fight_ = 0;
      return;
    }
    if (++frames_outside_fight_ < kFightEndFrames) return;
    frames_outside_fight_ = 0;
    REXLOG_INFO("DEBUG_SAULO netplay fight left generation={} frame={} guest={}", generation_,
                session_->core() ? session_->core()->current_frame() : 0, G::kName);
    ResetFight();
    session_->SetLocalReady(false, now_ms);
  }

  void Fault(const StartPlan& plan, uint64_t now_ms) {
    if (plan.online) retry_at_ms_ = now_ms + kRetryDelayMs;
    else faulted_ = true;
  }

  np::InputWord OnlineLocalInput(const uint8_t* base) {
    const uint32_t user = uint32_t(ssf2t::MatchFightUser(int32_t(ssf2t::LoadBE32(base, G::kActiveUser))));
    const np::InputWord word =
        np::ControllerWord(ssf2t::LoadBE32(base, np::ControllerWordAddress(G::kControllerInputTable, user)));
    if (input_frames_ < np::InputChangeLog::kFrames) {
      const np::PadSample pad = RawPad(user, word);
      if (input_log_.ShouldLog(input_frames_, pad)) {
        REXLOG_INFO("DEBUG_SAULO netplay input frame={} user={} buttons={:04X} stick={},{} word={:04X} fight={}",
                    input_frames_, user, pad.buttons, pad.stick_x, pad.stick_y, word, np::DescribeFightBits(word));
      }
      ++input_frames_;
    }
    return word;
  }

  void LogInputRoute(const uint8_t* base) {
    const uint32_t active = ssf2t::LoadBE32(base, G::kActiveUser);
    const uint32_t ui = ssf2t::LoadBE32(base, G::kUiInputPointer);
    const int32_t mask =
        PointerUsable(ui) ? int32_t(ssf2t::LoadBE32(ssf2t::GuestHost(base, ui), ssf2t::kUiControllerMaskOffset)) : 0;
    REXLOG_INFO("DEBUG_SAULO netplay input route active_user={} match_input_user={} live_user={} "
                "controller_mask={:#x} local_input_slot={} rollback_player={} fighter_slot={} guest={}",
                int32_t(active), ssf2t::MatchInputUser(), np::LocalInputUser(active), uint32_t(mask),
                ssf2t::MatchFightUser(int32_t(active)), session_->local_player(), session_->local_player(), G::kName);
    input_log_.Reset();
    input_frames_ = 0;
  }

  // The host sends nothing until a joiner's Hello arrives, so a NAT in front of it would drop that Hello. One small
  // datagram toward the joiner opens the way; the session ignores it.
  void PunchIfWaiting(uint64_t now_ms) {
    if (punch_peer_.port == 0 || session_->state() != np::SessionState::kWaiting) return;
    if (now_ms - last_punch_ms_ < kPunchIntervalMs) return;
    last_punch_ms_ = now_ms;
    const uint8_t byte = 0;
    transport_->Send(punch_peer_, &byte, 1);
  }

  void ReportLink() {
    ssf2t::online::NetplayLink link = ssf2t::online::NetplayLink::kNone;
    std::optional<int> rtt;
    if (session_ && online_driven_) {
      switch (session_->state()) {
        case np::SessionState::kWaiting:
        case np::SessionState::kConnecting: link = ssf2t::online::NetplayLink::kConnecting; break;
        case np::SessionState::kConnected: link = ssf2t::online::NetplayLink::kConnected; break;
        case np::SessionState::kRunning: link = ssf2t::online::NetplayLink::kFighting; break;
        default: break;
      }
      if (const auto measured = session_->rtt_ms()) rtt = int(*measured);
    }
    if (rtt && !rtt_logged_) REXLOG_INFO("DEBUG_SAULO netplay rtt measured={}ms", *rtt);
    rtt_logged_ = rtt.has_value();
    if (link == ssf2t::online::NetplayLink::kNone && !reported_) return;
    ssf2t::Online().SetNetplayLink(link, rtt);
    reported_ = link != ssf2t::online::NetplayLink::kNone;
  }

  void Shutdown(uint64_t now_ms, const char* reason) {
    if (session_) {
      REXLOG_INFO("DEBUG_SAULO netplay match cleanup generation={} reason={} frame={} rollback=reset guest={}",
                  generation_, reason, session_->core() ? session_->core()->current_frame() : 0, G::kName);
    }
    online_driven_ = false;
    generation_ = 0;
    frames_outside_fight_ = 0;
    ResetFight();
    takeover_logged_ = false;
    punch_peer_ = {};
    retry_at_ms_ = now_ms + kRetryDelayMs;
    bad_address_since_ms_ = 0;
    logged_game_state_ = 0;
    PublishLobby(nullptr);
    session_.reset();
    mux_.reset();
    transport_ = nullptr;
    own_transport_.reset();
    logged_state_ = np::SessionState::kIdle;
    logged_local_ready_ = false;
    logged_remote_ready_ = false;
    peer_seen_ = false;
    input_frames_ = np::InputChangeLog::kFrames;
  }

  void LogTransitions() {
    if (!session_) return;
    const np::SessionState state = session_->state();
    if (state == np::SessionState::kConnected || state == np::SessionState::kRunning) peer_seen_ = true;
    if (state != logged_state_) {
      if (state == np::SessionState::kConnected && logged_state_ != np::SessionState::kRunning) {
        REXLOG_INFO("DEBUG_SAULO netplay connected role={} guest={} player={}",
                    session_->local_player() == 0 ? "host" : "join", G::kName, session_->local_player());
      } else if (state == np::SessionState::kRunning) {
        REXLOG_INFO("DEBUG_SAULO netplay takeover started player={}", session_->local_player());
      } else if (logged_state_ == np::SessionState::kRunning && state == np::SessionState::kConnected) {
        REXLOG_INFO("DEBUG_SAULO netplay takeover stopped reason=fight_ended checksums={} mismatches={}",
                    fight_stats_.checksums, fight_stats_.mismatches);
      } else if (state == np::SessionState::kTimedOut || state == np::SessionState::kClosed ||
                state == np::SessionState::kRejected) {
        REXLOG_INFO("DEBUG_SAULO netplay disconnect reason={}",
                    state == np::SessionState::kTimedOut ? "timeout"
                    : state == np::SessionState::kRejected ? "rejected"
                                                            : "closed");
      }
      logged_state_ = state;
    }
    if (session_->local_ready() != logged_local_ready_) {
      logged_local_ready_ = session_->local_ready();
      REXLOG_INFO("DEBUG_SAULO netplay local ready={}", logged_local_ready_);
    }
    if (session_->remote_ready() != logged_remote_ready_) {
      logged_remote_ready_ = session_->remote_ready();
      REXLOG_INFO("DEBUG_SAULO netplay remote ready={}", logged_remote_ready_);
    }
    if (session_->core() && session_->core()->desync() && !desync_logged_) {
      const np::Desync& d = *session_->core()->desync();
      REXLOG_INFO("DEBUG_SAULO netplay desync frame={} local={:08X} remote={:08X}", d.frame, d.local, d.remote);
      desync_logged_ = true;
    }
    if (session_->core()) {
      const np::Frame confirmed = session_->core()->confirmed_frame();
      if (confirmed / kConfirmedLogInterval != logged_confirmed_ / kConfirmedLogInterval) {
        REXLOG_INFO("DEBUG_SAULO netplay confirmed frame={}", confirmed);
      }
      logged_confirmed_ = confirmed;
    } else {
      desync_logged_ = false;
      logged_confirmed_ = 0;
    }
  }

  static constexpr np::Frame kConfirmedLogInterval = 300;
  static constexpr uint8_t kOnlineGameMode = 3;
  static constexpr uint64_t kRetryDelayMs = 2000;
  // Long enough for the STUN-measured public_ip to reach the Worker and come back in a corrected match.peer.
  static constexpr uint64_t kBadAddressGiveUpMs = 10000;
  static constexpr uint64_t kPunchIntervalMs = 500;
  static constexpr uint32_t kFightEndFrames = 30;
  // DEBUG_SAULO: fight-work offsets and the mode values of the fight phase and of its round init.
  static constexpr uint32_t kPhaseTimerOffset = 14;
  static constexpr uint32_t kPresentationRequestOffset = 1639;
  static constexpr uint16_t kFightMode = 4;
  static constexpr uint16_t kRoundInitMode = 6;

  np::Transport* transport_ = nullptr;
  std::unique_ptr<ssf2t::netplay::UdpTransport> own_transport_;  // the developer Netplay Test's, when not shared
  std::unique_ptr<np::LobbyMux> mux_;
  std::unique_ptr<ssf2t::netplay::Session> session_;
  std::unique_ptr<np::GuestSimulation<G>> live_sim_;
  bool faulted_ = false;
  bool online_driven_ = false;
  uint32_t generation_ = 0;
  uint32_t frames_outside_fight_ = 0;
  FightPhase phase_ = FightPhase::kIdle;
  uint32_t logged_game_state_ = 0;
  bool logged_tail_override_ = false;
  bool logged_pump_tail_ = false;
  ModeWords logged_modes_;
  bool presentation_logged_ = false;
  np::CoreStats fight_stats_;
  bool takeover_logged_ = false;
  bool reported_ = false;
  bool rtt_logged_ = false;
  ssf2t::netplay::Endpoint punch_peer_{};
  uint64_t last_punch_ms_ = 0;
  uint64_t retry_at_ms_ = 0;
  uint64_t bad_address_since_ms_ = 0;
  uint32_t bad_address_generation_ = 0;
  bool desync_logged_ = false;
  np::Frame logged_confirmed_ = 0;
  np::SessionState logged_state_ = np::SessionState::kIdle;
  bool logged_local_ready_ = false;
  bool logged_remote_ready_ = false;
  bool peer_seen_ = false;
  np::InputChangeLog input_log_;
  uint32_t input_frames_ = np::InputChangeLog::kFrames;
};

template <typename G>
void FightStep(PPCContext& ctx, uint8_t* base, const GuestCalls& calls) {
  if (np::InRollbackFrame()) {
    calls.step(ctx, base);
    return;
  }
  if (CodeMatches<G>(base) && FightRuntime<G>::Instance().Wanted()) {
    if (FightRuntime<G>::Instance().Takeover(ctx, base, calls, NetplayNowMs())) return;
    calls.step(ctx, base);
    return;
  }
  if (DeterminismTestOn() && CodeMatches<G>(base) &&
      FightActive(base, G::kNetActive, G::kStateFunction, G::kFightLoop, G::kArenaPointer)) {
    const np::Frame frame = ssf2t::LoadBE32(base, G::kFrameCounter);
    if (g_schedule.ShouldSample(frame)) {
      RunDeterminismSample<G>(ctx, base, calls, frame);
      return;
    }
  }
  calls.step(ctx, base);
}

// The game's own NetSync would run the fight over its lobby link; in a Player Match RollbackCore owns the fight.
template <typename G>
void NetSyncStart(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (FightRuntime<G>::Instance().PlayerMatchFight(base)) {
    FightRuntime<G>::Instance().FightInitReached();
    REXLOG_INFO("DEBUG_SAULO player_match original netsync suppressed guest={}", G::kName);
    return;
  }
  original(ctx, base);
}

// Only the fight loop's own frame tail is answered: elsewhere (the random generator, the effects gate, the frame
// pump) NetSync stays off.
template <typename G>
void NetActiveQuery(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (uint32_t(ctx.lr) == np::FrameTailNetCheckReturn<G>() && CodeMatches<G>(base)) {
    FightRuntime<G>& runtime = FightRuntime<G>::Instance();
    const bool owns = runtime.OwnsFrameTail(base);
    runtime.LogFrameTail(owns, np::InFramePump());
    if (np::AnswerFrameTailNetActive<G>(uint32_t(ctx.lr), np::InFramePump(), owns)) {
      ctx.r3.u64 = 1;
      return;
    }
  }
  original(ctx, base);
}

void FramePump(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  ++np::g_frame_pump_depth;
  original(ctx, base);
  --np::g_frame_pump_depth;
}

void EffectsGate(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (g_trace != nullptr) ++g_trace->gate_queries;
  if (np::SuppressEffects()) {
    ctx.r3.u64 = 0;
    return;
  }
  original(ctx, base);
}

void TraceRand(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (g_trace != nullptr) {
    if (g_trace->rand_calls < kTraceCallers) g_trace->rand_callers[g_trace->rand_calls] = uint32_t(ctx.lr);
    ++g_trace->rand_calls;
  }
  original(ctx, base);
}

void TraceSideSetup(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  if (g_trace != nullptr) ++g_trace->side_setups;
  original(ctx, base);
}

}  // namespace

namespace ssf2t {

bool SendPlayerMatchLobby(const netplay::LobbyDatagram& datagram) {
  std::lock_guard<std::mutex> lock(g_lobby_mutex);
  if (g_lobby) return g_lobby->SendLobby(datagram);
  const auto route = RouteFor(Online().Peer());
  return route && g_lobby_hold.Hold(route->tag, datagram);
}

bool TakePlayerMatchLobby(netplay::LobbyDatagram& out) {
  std::lock_guard<std::mutex> lock(g_lobby_mutex);
  return g_lobby && g_lobby->TakeLobby(out);
}

void PumpNetplayNetwork(const uint8_t* base) {
  PumpOnlineService();
  const uint64_t now_ms = NetplayNowMs();
  FightRuntime<np::Base013Netplay>::Instance().PumpNetwork(now_ms, base);
#if SSF2T_TITLE_UPDATE_IMAGE
  FightRuntime<np::TitleUpdate023Netplay>::Instance().PumpNetwork(now_ms, base);
#endif
}

}  // namespace ssf2t

REX_HOOK_RAW(base013_sub_823E1CF0) { TraceRand(ctx, base, __imp__base013_sub_823E1CF0); }
REX_HOOK_RAW(base013_sub_82318618) { TraceSideSetup(ctx, base, __imp__base013_sub_82318618); }
REX_HOOK_RAW(base013_sub_8223DDB8) {
  FightStep<np::Base013Netplay>(ctx, base, {__imp__base013_sub_823E90C0, __imp__base013_sub_8223DDB8});
}
REX_HOOK_RAW(base013_sub_8241CC48) { EffectsGate(ctx, base, __imp__base013_sub_8241CC48); }
REX_HOOK_RAW(base013_sub_8241DEC8) { NetSyncStart<np::Base013Netplay>(ctx, base, __imp__base013_sub_8241DEC8); }
REX_HOOK_RAW(base013_sub_8241CC08) {
  NetActiveQuery<np::Base013Netplay>(ctx, base, __imp__base013_sub_8241CC08);
}
REX_HOOK_RAW(base013_sub_82252B90) { FramePump(ctx, base, __imp__base013_sub_82252B90); }

#if SSF2T_TITLE_UPDATE_IMAGE
REX_HOOK_RAW(tu023_sub_8223DEE0) {
  FightStep<np::TitleUpdate023Netplay>(ctx, base, {__imp__tu023_sub_823E97F8, __imp__tu023_sub_8223DEE0});
}
REX_HOOK_RAW(tu023_sub_8241D320) { EffectsGate(ctx, base, __imp__tu023_sub_8241D320); }
REX_HOOK_RAW(tu023_sub_8241E618) { NetSyncStart<np::TitleUpdate023Netplay>(ctx, base, __imp__tu023_sub_8241E618); }
REX_HOOK_RAW(tu023_sub_8241D2E0) {
  NetActiveQuery<np::TitleUpdate023Netplay>(ctx, base, __imp__tu023_sub_8241D2E0);
}
REX_HOOK_RAW(tu023_sub_82252BD8) { FramePump(ctx, base, __imp__tu023_sub_82252BD8); }
REX_HOOK_RAW(tu023_sub_823E23C8) { TraceRand(ctx, base, __imp__tu023_sub_823E23C8); }
REX_HOOK_RAW(tu023_sub_82318558) { TraceSideSetup(ctx, base, __imp__tu023_sub_82318558); }
#endif
