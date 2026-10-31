#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#include "guest_memory.h"
#include "input_owner.h"
#include "invite_lifecycle.h"
#include "match_input.h"
#include "online_runtime.h"
#include "player_match.h"
#include "player_match_guest.h"
#include "player_match_invite.h"
#include "player_match_lobby.h"
#include "player_match_net.h"
#include "title_update_image.h"

REXCVAR_DECLARE(bool, ssf2t_online_enabled);

namespace {

using ssf2t::GuestHost;
using ssf2t::LoadBE32;
using ssf2t::StoreBE32;
using ssf2t::StoreBE64;

// Guest pointers read at runtime may point into the physical heap.
uint8_t* GuestPtr(uint8_t* base, uint32_t address) { return const_cast<uint8_t*>(GuestHost(base, address)); }

struct LiveProfile {
  bool available = false;
  uint64_t xuid = 0;
  std::string name;
};

LiveProfile CurrentLive() {
  LiveProfile live;
  if (!REXCVAR_GET(ssf2t_online_enabled)) return live;
  const ssf2t::online::OnlineProfile profile = ssf2t::Online().Profile();
  live.available = ssf2t::LiveAvailable(true, profile.signed_in) && !profile.friend_code.empty();
  if (!live.available) return live;
  live.xuid = ssf2t::VirtualXuid(profile.friend_code);
  live.name = profile.name;
  static bool logged = false;
  static uint64_t logged_xuid = 0;
  if (!logged || logged_xuid != live.xuid) {
    logged = true;
    logged_xuid = live.xuid;
    REXLOG_INFO("DEBUG_SAULO online runtime LIVE state active xuid={:016X}", live.xuid);
  }
  return live;
}

constexpr std::chrono::seconds kSearchWaitLimit(7);

double NowSeconds() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// One line per distinct value, so per-frame callers do not flood the log.
void LogOnChange(const char* what, uint32_t value) {
  struct Seen {
    const char* what;
    uint32_t value;
  };
  static Seen seen[16] = {};
  for (Seen& s : seen) {
    if (s.what == what) {
      if (s.value == value) return;
      s.value = value;
      REXLOG_INFO("DEBUG_SAULO {} value={}", what, value);
      return;
    }
    if (!s.what) {
      s = {what, value};
      REXLOG_INFO("DEBUG_SAULO {} value={}", what, value);
      return;
    }
  }
}

template <typename G>
uint32_t LiveUser(const uint8_t* base) {
  return ssf2t::LiveUserIndex(LoadBE32(base, G::kUserIndex));
}

template <typename G>
bool AcceptedInviteFlag(const uint8_t* base) {
  return LoadBE32(base, G::kNetManager + ssf2t::kAcceptedInviteOffset) != 0;
}

template <size_t TitleBytes, size_t MessageBytes>
void ShowPopup(PPCContext& ctx, uint8_t* base, PPCFunc* set_error, uint32_t title_address, uint32_t message_address,
               const char (&title)[TitleBytes], const char (&message)[MessageBytes]) {
  static_assert(TitleBytes <= ssf2t::kErrorTitleBytes && MessageBytes <= ssf2t::kErrorMessageBytes);
  ctx.r3.u64 = ssf2t::kPopupErrorCode;
  ctx.r4.u64 = ssf2t::kPopupErrorLevel;
  set_error(ctx, base);
  std::memset(base + title_address, 0, ssf2t::kErrorTitleBytes);
  std::memset(base + message_address, 0, ssf2t::kErrorMessageBytes);
  std::memcpy(base + title_address, title, TitleBytes);
  std::memcpy(base + message_address, message, MessageBytes);
}

template <typename G>
void ForcePlayerMatchSlots(uint8_t* base) {
  const uint32_t players_at = G::kCreateMatchSettings + ssf2t::kCreateMatchPlayersOffset;
  const uint32_t private_at = G::kCreateMatchSettings + ssf2t::kCreateMatchPrivateSlotsOffset;
  const uint32_t players = LoadBE32(base, players_at);
  const uint32_t private_slots = LoadBE32(base, private_at);
  if (players == ssf2t::kPlayerMatchSlots && private_slots == 0) return;
  StoreBE32(base, players_at, ssf2t::kPlayerMatchSlots);
  StoreBE32(base, private_at, 0);
  REXLOG_INFO("DEBUG_SAULO online_match slots forced players={} private=0 was players={} private={}",
              ssf2t::kPlayerMatchSlots, players, private_slots);
}

// The popup the guest raised during the current sign-in gate (SetError code), 0 for none.
uint32_t g_gate_error = 0;

std::atomic<uint32_t> g_live_user{0};
std::mutex g_invite_mutex;
std::function<void()> g_open_invite_picker;
std::optional<ssf2t::online::PlayerMatchAd> g_delivered_invite;
ssf2t::InviteLifecycle g_invite;
uint32_t g_confirm_result = 0;
std::atomic<bool> g_in_online_zone{false};

std::mutex g_owner_mutex;
ssf2t::InputActivity g_activity;
ssf2t::MatchInputOwner g_owner;
double g_invite_owner_at = -1.0;
constexpr double kInviteOwnerSeconds = 60.0;

void SetMatchInputOwner(ssf2t::OwnerChoice choice) {
  {
    std::lock_guard<std::mutex> lock(g_owner_mutex);
    g_owner.Set(choice);
  }
  REXLOG_INFO("DEBUG_SAULO player_match input owner set user={} source={}", choice.user, choice.source);
}

// The guest's own way out of its lobby ends the online match, so nothing restarts a session for that peer.
void EndOnlineMatch(const char* reason) {
  const bool had_match = ssf2t::Online().Assignment().has_value();
  ssf2t::Online().LeaveMatch();
  ssf2t::EndMatchInput(reason);
  if (had_match) REXLOG_INFO("DEBUG_SAULO player_match match left reason={}", reason);
}

void NoteInviteTeardown(const char* cause) {
  bool begun = false;
  uint32_t generation = 0;
  {
    std::lock_guard<std::mutex> lock(g_invite_mutex);
    begun = g_invite.BeginTeardown();
    generation = g_invite.generation();
  }
  if (begun) REXLOG_INFO("DEBUG_SAULO invite transition teardown begin generation={} cause={}", generation, cause);
}

void NoteInviteTeardownComplete() {
  bool completed = false;
  uint32_t generation = 0;
  {
    std::lock_guard<std::mutex> lock(g_invite_mutex);
    completed = g_invite.CompleteTeardown();
    generation = g_invite.generation();
  }
  if (completed) REXLOG_INFO("DEBUG_SAULO invite transition teardown complete generation={}", generation);
}

bool OnlineErrorPopup(uint32_t code) {
  return code == ssf2t::kNetErrorCode || code == ssf2t::kPlayerLeftErrorCode || code == ssf2t::kNoConnectionErrorCode;
}

std::string PopupName(uint32_t code) {
  if (code == 0) return "none";
  if (code == ssf2t::kConnectionRequiredError) return "connection_required";
  return "error_" + std::to_string(code);
}

template <typename G>
struct PlayerMatchHooks {
  static void SigninState(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const uint32_t user = ctx.r3.u32;
    g_live_user = LiveUser<G>(base);
    const bool live_user = user == g_live_user;
    if (live_user && CurrentLive().available) {
      ctx.r3.u64 = ssf2t::kSignedInToLive;
    } else {
      original(ctx, base);
    }
    if (live_user) LogOnChange("live signin state", ctx.r3.u32);
  }

  static void CheckPrivilege(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const uint32_t user = ctx.r3.u32;
    const uint32_t privilege = ctx.r4.u32;
    const uint32_t result = ctx.r5.u32;
    // The net manager checks its own copy of the active user, which keeps the last valid one.
    if ((ssf2t::LocalUser(user) || user == 0xFF) && result && CurrentLive().available) {
      StoreBE32(GuestPtr(base, result), 0, 1);
      ctx.r3.u64 = 0;
      LogOnChange("live privilege check granted", privilege);
      return;
    }
    original(ctx, base);
    LogOnChange("live privilege check original", privilege);
  }

  static void SigninInfo(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const uint32_t user = ctx.r3.u32;
    const uint32_t info = ctx.r5.u32;
    original(ctx, base);
    const bool live_user = user == LiveUser<G>(base);
    const LiveProfile live = live_user && info ? CurrentLive() : LiveProfile{};
    if (live.available) {
      uint8_t* out = GuestPtr(base, info);
      StoreBE64(out, 0, live.xuid);
      StoreBE32(out, 8, ssf2t::kSigninInfoOnlineEnabled);
      StoreBE32(out, 12, ssf2t::kSignedInToLive);
      std::memset(out + 24, 0, 16);
      std::memcpy(out + 24, live.name.data(), std::min<size_t>(live.name.size(), 15));
      ctx.r3.u64 = 0;
    }
    if (live_user) LogOnChange("live signin info", live.available ? ssf2t::kSignedInToLive : 0);
  }

  static void GetXuid(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const uint32_t user = ctx.r3.u32;
    const uint32_t out = ctx.r4.u32;
    const LiveProfile live = user == LiveUser<G>(base) && out ? CurrentLive() : LiveProfile{};
    if (!live.available) {
      original(ctx, base);
      return;
    }
    StoreBE64(GuestPtr(base, out), 0, live.xuid);
    ctx.r3.u64 = 0;
    LogOnChange("live xuid virtual", uint32_t(live.xuid));
  }

  static void TitleXnAddr(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const uint32_t xnaddr = ctx.r3.u32;
    original(ctx, base);
    const LiveProfile live = CurrentLive();
    if (!live.available || !xnaddr) return;
    ssf2t::WriteXnAddr(GuestPtr(base, xnaddr), live.xuid);
    constexpr uint32_t kXnAddrOnline = 0x80;
    ctx.r3.u64 = ctx.r3.u32 | kXnAddrOnline;
    LogOnChange("live xnaddr flags", ctx.r3.u32);
  }

  static void RequestOnlineSignIn(PPCContext& ctx, uint8_t* base, PPCFunc* original, PPCFunc* set_error,
                                  PPCFunc* refresh_local_users) {
    const int argument = ctx.r3.s32;
    const uint32_t caller = uint32_t(ctx.lr);
    const bool confirm = caller == G::kInviteConfirmSignInReturn;
    if (confirm && SkipConfirmRepeat(ctx, base)) return;
    const bool online = REXCVAR_GET(ssf2t_online_enabled);
    const bool live = CurrentLive().available;
    if (argument == 1 && !confirm) {
      REXLOG_INFO("DEBUG_SAULO online_match selected guest={}", G::kName);
      REXLOG_INFO("DEBUG_SAULO online_match gate begin online={} live={} user={} live_user={}", online, live,
                  int32_t(LoadBE32(base, G::kUserIndex)), LiveUser<G>(base));
    }
    REXLOG_INFO("DEBUG_SAULO online sign-in request begin argument={} live={}", argument, int(live));
    REXLOG_INFO("DEBUG_SAULO online sign-in request caller={:08X}", caller);
    std::string original_result = "not_called";
    std::string popup;
    const ssf2t::OnlineGate gate = ssf2t::DecideOnlineGate(argument, online, live);
    if (gate == ssf2t::OnlineGate::kLive) {
      // The game caches each signed-in user's XUID and name when it starts; the controller that chose Online Match
      // may only now be the LIVE user, and a stale guest XUID makes both players look like the same person.
      refresh_local_users(ctx, base);
      ctx.r3.u64 = uint64_t(int64_t(argument));
      REXLOG_INFO("DEBUG_SAULO player_match local users refreshed live_user={} xuid={:016X} guest={}",
                  LiveUser<G>(base), CurrentLive().xuid, G::kName);
    }
    if (gate == ssf2t::OnlineGate::kServiceUnavailable) {
      ShowPopup(ctx, base, set_error, G::kErrorTitle, G::kErrorMessage, ssf2t::kOnlineUnavailableTitle,
                ssf2t::kOnlineUnavailableMessage);
      ctx.r3.u64 = 0;
      popup = "online_unavailable";
    } else {
      g_gate_error = 0;
      original(ctx, base);
      original_result = std::to_string(ctx.r3.u32);
      popup = PopupName(g_gate_error);
    }
    if (confirm) g_confirm_result = ctx.r3.u32;
    if (argument == 1 && !confirm && gate == ssf2t::OnlineGate::kLive && ssf2t::SignInAllowed(ctx.r3.u32)) {
      TakeMenuOwner(base);
    }
    REXLOG_INFO("DEBUG_SAULO online sign-in request original_result={}", original_result);
    REXLOG_INFO("DEBUG_SAULO online sign-in request override_result={}", ctx.r3.u32);
    REXLOG_INFO("DEBUG_SAULO online sign-in request branch={}", ssf2t::SignInAllowed(ctx.r3.u32) ? "allow" : "deny");
    REXLOG_INFO("DEBUG_SAULO online sign-in request popup={}", popup);
  }

  // Only the first of the accept box's per-frame gates reaches the game; the box ignores the result.
  static bool SkipConfirmRepeat(PPCContext& ctx, const uint8_t* base) {
    ssf2t::InviteLifecycle::Gate gate;
    uint32_t generation = 0;
    {
      std::lock_guard<std::mutex> lock(g_invite_mutex);
      gate = g_invite.ConfirmGate();
      generation = g_invite.generation();
    }
    if (gate == ssf2t::InviteLifecycle::Gate::kRepeat) {
      ctx.r3.u64 = g_confirm_result;
      return true;
    }
    REXLOG_INFO("DEBUG_SAULO player_match invite confirm gate generation={} pending={} accepted_flag={} online_zone={} "
                "active_match={} guest={}",
                generation, gate == ssf2t::InviteLifecycle::Gate::kFirst, AcceptedInviteFlag<G>(base),
                g_in_online_zone.load(), ssf2t::Online().Assignment().has_value(), G::kName);
    return false;
  }

  static void JoinLobbyFromInvite(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const bool before = AcceptedInviteFlag<G>(base);
    original(ctx, base);
    NoteInviteTeardownComplete();
    InviteConsumed(base, "join", before, false);
  }

  static void DeclineAcceptedInvite(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const bool before = AcceptedInviteFlag<G>(base);
    original(ctx, base);
    InviteConsumed(base, "declined", before, true);
  }

  static void InviteConsumed(const uint8_t* base, const char* how, bool flag_before, bool declined) {
    bool consumed = false;
    uint32_t generation = 0;
    uint32_t repeats = 0;
    {
      std::lock_guard<std::mutex> lock(g_invite_mutex);
      consumed = declined ? g_invite.Cancel() : g_invite.Consume();
      generation = g_invite.generation();
      repeats = g_invite.repeats();
    }
    REXLOG_INFO("DEBUG_SAULO player_match invite {} generation={} consumed={} accepted_flag={}->{} "
                "confirm_repeats_skipped={} online_zone={} guest={}",
                how, generation, consumed, flag_before, AcceptedInviteFlag<G>(base), repeats,
                g_in_online_zone.load(), G::kName);
  }

  // The accepted invite's own sign-in, and screens of a match under way, keep the owner they have.
  static void TakeMenuOwner(const uint8_t* base) {
    if (ssf2t::Online().Assignment()) return;
    ssf2t::OwnerChoice choice;
    {
      std::lock_guard<std::mutex> lock(g_owner_mutex);
      if (g_invite_owner_at >= 0.0 && NowSeconds() - g_invite_owner_at < kInviteOwnerSeconds) return;
      choice = ssf2t::ChooseMenuOwner(int32_t(LoadBE32(base, G::kUserIndex)), g_activity);
    }
    SetMatchInputOwner(choice);
  }

  // XInputGetState(user, state): the game's own per-user poll, so each press is credited to the user it belongs to.
  static void PadPoll(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const int32_t user = ctx.r3.s32;
    const uint32_t state = ctx.r4.u32;
    original(ctx, base);
    if (ctx.r3.u32 != 0 || !state) return;
    const uint8_t* pad = GuestPtr(base, state) + 4;
    ssf2t::PadReading reading;
    reading.buttons = ssf2t::LoadBE16(pad, 0);
    reading.left_trigger = pad[2];
    reading.right_trigger = pad[3];
    for (int i = 0; i < 4; ++i) reading.thumbs[i] = int16_t(ssf2t::LoadBE16(pad, 4 + i * 2));
    std::lock_guard<std::mutex> lock(g_owner_mutex);
    g_activity.Observe(user, reading);
  }

  // OnlineOp_INVITE asks the Xbox Guide to pick friends; Online play picks from the online service's friends.
  static void ShowGameInviteUi(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    if (!CurrentLive().available) {
      original(ctx, base);
      return;
    }
    REXLOG_INFO("DEBUG_SAULO player_match invite open user={} guest={}", ctx.r3.u32, G::kName);
    std::function<void()> open;
    {
      std::lock_guard<std::mutex> lock(g_invite_mutex);
      open = g_open_invite_picker;
    }
    if (open) open();
    ctx.r3.u64 = 0;
  }

  // (user, XINVITE_INFO*): what the game's XN_LIVE_INVITE_ACCEPTED handler joins.
  static void InviteAcceptedInfo(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const uint32_t out = ctx.r4.u32;
    std::optional<ssf2t::online::PlayerMatchAd> invite;
    uint32_t generation = 0;
    {
      std::lock_guard<std::mutex> lock(g_invite_mutex);
      invite.swap(g_delivered_invite);
      generation = g_invite.generation();
    }
    const LiveProfile live = CurrentLive();
    if (!invite || !out || !live.available) {
      original(ctx, base);
      return;
    }
    ssf2t::WriteInviteInfo(GuestPtr(base, out), live.xuid, ssf2t::VirtualXuid(invite->player), invite->session);
    ctx.r3.u64 = 0;
    REXLOG_INFO("DEBUG_SAULO player_match invite delivered to game session={} host={} generation={}", invite->session,
                invite->player, generation);
  }

  static void SetError(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const uint32_t code = ctx.r3.u32;
    const int32_t level = ctx.r4.s32;
    const uint32_t caller = uint32_t(ctx.lr);
    const bool shown = level > int32_t(LoadBE32(base, G::kErrorLevel));
    original(ctx, base);
    if (shown) g_gate_error = code;
    if (shown && REXCVAR_GET(ssf2t_online_enabled)) {
      const int32_t owner = ssf2t::MatchInputUser();
      if (OnlineErrorPopup(code) && ssf2t::ValidUser(owner)) GiveMenusTo(base, owner);
      const uint32_t ui = LoadBE32(base, G::kUiInputPointer);
      const int32_t mask = ui ? int32_t(LoadBE32(GuestPtr(base, ui), ssf2t::kUiControllerMaskOffset)) : 0;
      REXLOG_INFO("DEBUG_SAULO player_match error popup code={} level={} match_input_user={} active_user={} "
                  "controller_mask={:#x} caller={:08X} guest={}",
                  code, level, owner, int32_t(LoadBE32(base, G::kUserIndex)), uint32_t(mask), caller, G::kName);
    }
    if (code == ssf2t::kConnectionRequiredError) {
      REXLOG_INFO("DEBUG_SAULO connection_required popup source={:08X} level={} shown={} guest={}", caller, level,
                  shown, G::kName);
    }
  }

  // The same as SetControllerMask(user + 1) for the menus, without its other side effects.
  static void GiveMenusTo(uint8_t* base, int32_t user) {
    StoreBE32(base, G::kUserIndex, uint32_t(user));
    if (const uint32_t ui = LoadBE32(base, G::kUiInputPointer)) {
      StoreBE32(GuestPtr(base, ui), ssf2t::kUiControllerMaskOffset, ssf2t::ControllerMaskFor(user));
    }
  }

  // The online zone is menu state only; the game leaves it for screens inside a running match, so the match's
  // lifetime follows the session (leave, delete, peer timeout) instead.
  static void SetInOnlineZone(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const bool entering = ctx.r3.u32 != 0;
    original(ctx, base);
    g_in_online_zone = entering;
    if (entering) NoteInviteTeardownComplete();
    else if (CurrentLive().available) NoteInviteTeardown("zone_left");
    const bool active_match = ssf2t::Online().Assignment().has_value();
    REXLOG_INFO("DEBUG_SAULO player_match online zone={} active_match={} teardown=false", entering, active_match);
    if (entering) {
      REXLOG_INFO("DEBUG_SAULO online_match enter player_match_fsm guest={}", G::kName);
      return;
    }
    if (active_match) return;
    ssf2t::Online().CancelPlayerMatch();
    ssf2t::Online().SetPlayerMatchSearch(false);
  }

  static void CreateMatchDefaults(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    original(ctx, base);
    ForcePlayerMatchSlots<G>(base);
  }

  // The lobby-size row is removed from the screen; this only keeps any other path at one against one.
  static void CreateMatchNumPlayers(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    original(ctx, base);
    ForcePlayerMatchSlots<G>(base);
  }

  // Whatever path filled the settings, the lobby is created with two public slots.
  static void CreateLobby(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    ForcePlayerMatchSlots<G>(base);
    original(ctx, base);
  }

  static void SessionCreate(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const uint32_t flags = ctx.r3.u32;
    const uint32_t info = ctx.r8.u32;
    REXLOG_INFO("DEBUG_SAULO XSessionCreate flags={:08X} user={} public={} private={}", flags, ctx.r4.u32,
                ctx.r5.u32, ctx.r6.u32);
    const bool live = CurrentLive().available;
    if (live) ssf2t::ResetLobbyTelemetry();
    std::string join_session;
    if (live && !(flags & ssf2t::kSessionCreateHost) && info) join_session = ssf2t::SessionIdText(GuestPtr(base, info));
    original(ctx, base);
    if (!live) return;
    if (flags & ssf2t::kSessionCreateHost) {
      const ssf2t::CreateMatchSettings settings = ssf2t::ReadCreateMatchSettings(base + G::kCreateMatchSettings);
      ssf2t::Online().HostPlayerMatch(ssf2t::PlayerMatchRulesFrom(settings));
      return;
    }
    if (join_session.empty()) return;
    bool invite_join = false;
    uint32_t generation = 0;
    {
      std::lock_guard<std::mutex> lock(g_invite_mutex);
      invite_join = g_invite.BeginJoining(join_session);
      generation = g_invite.generation();
    }
    if (invite_join) {
      REXLOG_INFO("DEBUG_SAULO invite transition joining generation={} session={}", generation, join_session);
    }
    if (!ssf2t::Online().JoinPlayerMatch(join_session) && invite_join) {
      REXLOG_INFO("DEBUG_SAULO invite transition failed generation={} reason=join_refused", generation);
    }
  }

  // The guest's search stays pending until the service has answered it, so an empty cache is never taken for "no match".
  static void CompleteSearchWhenAnswered(uint8_t* base, uint32_t epoch, uint32_t header, uint32_t size_ptr,
                                         uint32_t max_results, uint32_t overlapped) {
    auto run = [=]() -> rex::X_RESULT {
      const auto limit = std::chrono::steady_clock::now() + kSearchWaitLimit;
      while (ssf2t::Online().PlayerMatchSearchPending(epoch) && std::chrono::steady_clock::now() < limit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      const uint32_t capacity = LoadBE32(GuestPtr(base, size_ptr), 0);
      const uint32_t written = ssf2t::WriteSearchResults(GuestPtr(base, header), header, capacity, max_results,
                                                         ssf2t::Online().PlayerMatchResults(epoch));
      REXLOG_INFO("DEBUG_SAULO player_match search results={} written_to_guest={} deferred=true", written, written);
      return 0;
    };
    REX_KERNEL_STATE()->CompleteOverlappedDeferred(std::function<rex::X_RESULT()>(std::move(run)), overlapped);
  }

  static void SessionSearchEx(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    const uint32_t max_results = ctx.r5.u32;
    const uint32_t stack = ctx.r1.u32;
    const uint32_t size_ptr = LoadBE32(base, stack + 84);
    const uint32_t header = LoadBE32(base, stack + 92);
    const uint32_t overlapped = LoadBE32(base, stack + 100);
    const bool live = CurrentLive().available;
    ssf2t::online::OnlineService::PlayerMatchSearchWait search;
    if (live) search = ssf2t::Online().AskPlayerMatchSearch();
    if (search.pending && header && size_ptr && overlapped && max_results &&
        LoadBE32(GuestPtr(base, size_ptr), 0) >= ssf2t::kSearchHeaderBytes + max_results * ssf2t::kSearchBytesPerResult) {
      CompleteSearchWhenAnswered(base, search.epoch, header, size_ptr, max_results, overlapped);
      ctx.r3.u64 = ssf2t::kErrorIoPending;
      return;
    }
    original(ctx, base);
    if (!live || ctx.r3.u32 == 122 || !header || !size_ptr) return;
    const uint32_t capacity = LoadBE32(GuestPtr(base, size_ptr), 0);
    const uint32_t written = ssf2t::WriteSearchResults(GuestPtr(base, header), header, capacity, max_results,
                                                       ssf2t::Online().PlayerMatchResults());
    // The game polls the search several times a second; log changes and a summary at most once a second.
    static ssf2t::SearchLogGate gate;
    if (!gate.ShouldLog(written, NowSeconds())) return;
    REXLOG_INFO("DEBUG_SAULO XSessionSearchEx procedure={} user={} max={} properties={} contexts={} calls={}",
                ctx.r3.u32, ctx.r4.u32, max_results, ctx.r7.u32 & 0xFFFF, ctx.r8.u32 & 0xFFFF,
                gate.calls_since_log());
    REXLOG_INFO("DEBUG_SAULO player_match search results={} written_to_guest={}", written, written);
  }

  static void SessionCall(PPCContext& ctx, uint8_t* base, PPCFunc* original, const char* name) {
    REXLOG_INFO("DEBUG_SAULO {} handle={:08X} arg={:08X}", name, ctx.r3.u32, ctx.r4.u32);
    original(ctx, base);
  }

  // ReXGlue fails the XGI leave message (0x000B0013), so the game never sees the lobby left: an empty created lobby
  // cannot be backed out of and a failed leave raises a lobby error. Membership lives in the online service, so
  // the leave only has to complete.
  static void SessionLeave(PPCContext& ctx, uint8_t* base, PPCFunc* original, const char* name, bool local) {
    const uint32_t overlapped = ctx.r6.u32;
    REXLOG_INFO("DEBUG_SAULO {} handle={:08X} users={} overlapped={:08X}", name, ctx.r3.u32, ctx.r4.u32, overlapped);
    if (!CurrentLive().available) {
      original(ctx, base);
      return;
    }
    NoteInviteTeardown("lobby_leave");
    if (overlapped) {
      REX_KERNEL_STATE()->CompleteOverlappedImmediate(overlapped, 0);
      ctx.r3.u64 = ssf2t::kErrorIoPending;
    } else {
      ctx.r3.u64 = 0;
    }
    REXLOG_INFO("DEBUG_SAULO {} completed xgi={:08X} result={}", name, ssf2t::kXgiSessionLeave, ctx.r3.u32);
    if (local) EndOnlineMatch("lobby_left");
  }

  static void SessionStart(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    SessionCall(ctx, base, original, "XSessionStart");
    if (CurrentLive().available) REXLOG_INFO("DEBUG_SAULO player_match start");
  }

  static void SessionDelete(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
    SessionCall(ctx, base, original, "XSessionDelete");
    if (CurrentLive().available) {
      NoteInviteTeardown("session_deleted");
      ssf2t::Online().CancelPlayerMatch();
    }
    EndOnlineMatch("session_deleted");
  }

  // Ranked and Tournament are not in the menu; these only prove nothing reached them.
  static void RankedOnly(PPCContext& ctx, uint8_t* base, PPCFunc* original, const char* name) {
    REXLOG_INFO("DEBUG_SAULO {} reached game_mode={}", name, base[G::kGameMode]);
    original(ctx, base);
  }
};

using Base = PlayerMatchHooks<ssf2t::Base013PlayerMatch>;
using Update = PlayerMatchHooks<ssf2t::TitleUpdate023PlayerMatch>;

}  // namespace

namespace ssf2t {

void SetInvitePickerOpener(std::function<void()> open) {
  std::lock_guard<std::mutex> lock(g_invite_mutex);
  g_open_invite_picker = std::move(open);
}

void DeliverAcceptedInvite(const online::PlayerMatchAd& ad, InputSource accepted_by) {
  uint32_t generation = 0;
  {
    std::lock_guard<std::mutex> lock(g_invite_mutex);
    if (!g_invite.Deliver(ad.session, Online().AcceptedInviteGeneration())) {
      REXLOG_INFO("DEBUG_SAULO player_match invite ignored session={} reason=already_delivered generation={}",
                  ad.session, g_invite.generation());
      return;
    }
    generation = g_invite.generation();
    g_delivered_invite = ad;
  }
  REXLOG_INFO("DEBUG_SAULO invite state accepted generation={} session={}", generation, ad.session);
  OwnerChoice choice;
  int32_t last_input = -1;
  int32_t last_pad = -1;
  {
    std::lock_guard<std::mutex> lock(g_owner_mutex);
    choice = ChooseInviteOwner(accepted_by, g_owner.user(), g_activity, int32_t(g_live_user.load()));
    last_input = g_activity.last_user();
    last_pad = g_activity.last_pad_user();
    g_invite_owner_at = NowSeconds();
  }
  SetMatchInputOwner(choice);
  REXLOG_INFO("DEBUG_SAULO player_match invite accepted session={} generation={} user={} source={} last_input_user={} "
              "last_pad_user={} live_user={}",
              ad.session, generation, choice.user, choice.source, last_input, last_pad, g_live_user.load());
  REX_KERNEL_STATE()->BroadcastNotification(kInviteAcceptedNotification, uint32_t(choice.user));
}

int32_t MatchInputUser() {
  std::lock_guard<std::mutex> lock(g_owner_mutex);
  return g_owner.user();
}

int32_t MatchFightUser(int32_t active_user) {
  std::lock_guard<std::mutex> lock(g_owner_mutex);
  return g_owner.FightUser(active_user);
}

void EndMatchInput(const char* reason) {
  int32_t user = -1;
  {
    std::lock_guard<std::mutex> lock(g_owner_mutex);
    if (!g_owner.active()) return;
    g_owner.End();
    g_invite_owner_at = -1.0;
    user = g_owner.user();
  }
  REXLOG_INFO("DEBUG_SAULO player_match input owner released user={} reason={}", user, reason);
}

}  // namespace ssf2t

REX_EXTERN(__imp__base013_sub_823EAEE8);
REX_EXTERN(__imp__base013_sub_8242B7E8);
REX_EXTERN(__imp__base013_sub_839C3D70);
REX_EXTERN(__imp__base013_sub_839C2E38);
REX_EXTERN(__imp__base013_sub_839C34D0);
REX_EXTERN(__imp__base013_sub_839C34E0);
REX_EXTERN(__imp__base013_sub_839C3D98);
REX_EXTERN(__imp__base013_sub_839C38A0);
REX_EXTERN(__imp__base013_sub_839C1E18);
REX_EXTERN(__imp__base013_sub_823C6DF8);
REX_EXTERN(__imp__base013_sub_823C6AD8);
REX_EXTERN(__imp__base013_sub_839C5180);
REX_EXTERN(__imp__base013_sub_823C8958);
REX_EXTERN(__imp__base013_sub_823CCA10);
REX_EXTERN(__imp__base013_sub_823CCB00);
REX_EXTERN(__imp__base013_sub_839C23E0);
REX_EXTERN(__imp__base013_sub_839C2590);
REX_EXTERN(__imp__base013_sub_839C2640);
REX_EXTERN(__imp__base013_sub_839C26E8);
REX_EXTERN(__imp__base013_sub_839C2790);
REX_EXTERN(__imp__base013_sub_839C2838);
REX_EXTERN(__imp__base013_sub_839C28E0);
REX_EXTERN(__imp__base013_sub_839C29B8);
REX_EXTERN(__imp__base013_sub_839C2A50);
REX_EXTERN(__imp__base013_sub_839C2B00);
REX_EXTERN(__imp__base013_sub_839C2BE8);
REX_EXTERN(__imp__base013_sub_839C2F68);
REX_EXTERN(__imp__base013_sub_823C7AB0);
REX_EXTERN(__imp__base013_sub_823BD588);
REX_EXTERN(__imp__base013_sub_824206A8);

REX_HOOK_RAW(base013_sub_839C34D0) { Base::SigninState(ctx, base, __imp__base013_sub_839C34D0); }
REX_HOOK_RAW(base013_sub_839C34E0) { Base::CheckPrivilege(ctx, base, __imp__base013_sub_839C34E0); }
REX_HOOK_RAW(base013_sub_839C3D98) { Base::SigninInfo(ctx, base, __imp__base013_sub_839C3D98); }
REX_HOOK_RAW(base013_sub_839C38A0) { Base::GetXuid(ctx, base, __imp__base013_sub_839C38A0); }
REX_HOOK_RAW(base013_sub_839C1E18) { Base::TitleXnAddr(ctx, base, __imp__base013_sub_839C1E18); }
REX_HOOK_RAW(base013_sub_823C6DF8) {
  Base::RequestOnlineSignIn(ctx, base, __imp__base013_sub_823C6DF8, __imp__base013_sub_823EAEE8,
                            __imp__base013_sub_8242B7E8);
}
REX_HOOK_RAW(base013_sub_839C3D70) { Base::ShowGameInviteUi(ctx, base, __imp__base013_sub_839C3D70); }
REX_HOOK_RAW(base013_sub_839C2E38) { Base::InviteAcceptedInfo(ctx, base, __imp__base013_sub_839C2E38); }
REX_HOOK_RAW(base013_sub_823EAEE8) { Base::SetError(ctx, base, __imp__base013_sub_823EAEE8); }
REX_HOOK_RAW(base013_sub_823C6AD8) { Base::SetInOnlineZone(ctx, base, __imp__base013_sub_823C6AD8); }
REX_HOOK_RAW(base013_sub_839C5180) { Base::PadPoll(ctx, base, __imp__base013_sub_839C5180); }
REX_HOOK_RAW(base013_sub_823C8958) { Base::CreateMatchDefaults(ctx, base, __imp__base013_sub_823C8958); }
REX_HOOK_RAW(base013_sub_823CCA10) { Base::CreateMatchNumPlayers(ctx, base, __imp__base013_sub_823CCA10); }
REX_HOOK_RAW(base013_sub_823CCB00) { Base::CreateMatchNumPlayers(ctx, base, __imp__base013_sub_823CCB00); }
REX_HOOK_RAW(base013_sub_839C23E0) { Base::SessionCreate(ctx, base, __imp__base013_sub_839C23E0); }
REX_HOOK_RAW(base013_sub_839C2590) { Base::SessionDelete(ctx, base, __imp__base013_sub_839C2590); }
REX_HOOK_RAW(base013_sub_839C2640) { Base::SessionCall(ctx, base, __imp__base013_sub_839C2640, "XSessionJoinLocal"); }
REX_HOOK_RAW(base013_sub_839C26E8) { Base::SessionCall(ctx, base, __imp__base013_sub_839C26E8, "XSessionJoinRemote"); }
REX_HOOK_RAW(base013_sub_839C2790) {
  Base::SessionLeave(ctx, base, __imp__base013_sub_839C2790, "XSessionLeaveLocal", true);
}
REX_HOOK_RAW(base013_sub_839C2838) {
  Base::SessionLeave(ctx, base, __imp__base013_sub_839C2838, "XSessionLeaveRemote", false);
}
REX_HOOK_RAW(base013_sub_839C28E0) {
  Base::RankedOnly(ctx, base, __imp__base013_sub_839C28E0, "XSessionArbitrationRegister");
}
REX_HOOK_RAW(base013_sub_839C29B8) { Base::SessionStart(ctx, base, __imp__base013_sub_839C29B8); }
REX_HOOK_RAW(base013_sub_839C2A50) { Base::SessionCall(ctx, base, __imp__base013_sub_839C2A50, "XSessionEnd"); }
REX_HOOK_RAW(base013_sub_839C2B00) { Base::SessionSearchEx(ctx, base, __imp__base013_sub_839C2B00); }
REX_HOOK_RAW(base013_sub_839C2BE8) { Base::RankedOnly(ctx, base, __imp__base013_sub_839C2BE8, "XSessionWriteStats"); }
REX_HOOK_RAW(base013_sub_839C2F68) {
  Base::SessionCall(ctx, base, __imp__base013_sub_839C2F68, "XSessionMigrateHost");
}
REX_HOOK_RAW(base013_sub_823C7AB0) { Base::JoinLobbyFromInvite(ctx, base, __imp__base013_sub_823C7AB0); }
REX_HOOK_RAW(base013_sub_823BD588) { Base::CreateLobby(ctx, base, __imp__base013_sub_823BD588); }
REX_HOOK_RAW(base013_sub_824206A8) { Base::DeclineAcceptedInvite(ctx, base, __imp__base013_sub_824206A8); }

#if SSF2T_TITLE_UPDATE_IMAGE
REX_EXTERN(__imp__tu023_sub_823EB5D8);
REX_EXTERN(__imp__tu023_sub_8242BF48);
REX_EXTERN(__imp__tu023_sub_839C44D0);
REX_EXTERN(__imp__tu023_sub_839C3598);
REX_EXTERN(__imp__tu023_sub_839C3C30);
REX_EXTERN(__imp__tu023_sub_839C3C40);
REX_EXTERN(__imp__tu023_sub_839C44F8);
REX_EXTERN(__imp__tu023_sub_839C4000);
REX_EXTERN(__imp__tu023_sub_839C2578);
REX_EXTERN(__imp__tu023_sub_823C73F0);
REX_EXTERN(__imp__tu023_sub_823C7100);
REX_EXTERN(__imp__tu023_sub_839C58E0);
REX_EXTERN(__imp__tu023_sub_823C8F50);
REX_EXTERN(__imp__tu023_sub_823CCFF0);
REX_EXTERN(__imp__tu023_sub_823CD0E0);
REX_EXTERN(__imp__tu023_sub_839C2B40);
REX_EXTERN(__imp__tu023_sub_839C2CF0);
REX_EXTERN(__imp__tu023_sub_839C2DA0);
REX_EXTERN(__imp__tu023_sub_839C2E48);
REX_EXTERN(__imp__tu023_sub_839C2EF0);
REX_EXTERN(__imp__tu023_sub_839C2F98);
REX_EXTERN(__imp__tu023_sub_839C3040);
REX_EXTERN(__imp__tu023_sub_839C3118);
REX_EXTERN(__imp__tu023_sub_839C31B0);
REX_EXTERN(__imp__tu023_sub_839C3260);
REX_EXTERN(__imp__tu023_sub_839C3348);
REX_EXTERN(__imp__tu023_sub_839C36C8);
REX_EXTERN(__imp__tu023_sub_823C80A8);
REX_EXTERN(__imp__tu023_sub_823BD540);
REX_EXTERN(__imp__tu023_sub_82420E68);

REX_HOOK_RAW(tu023_sub_839C3C30) { Update::SigninState(ctx, base, __imp__tu023_sub_839C3C30); }
REX_HOOK_RAW(tu023_sub_839C3C40) { Update::CheckPrivilege(ctx, base, __imp__tu023_sub_839C3C40); }
REX_HOOK_RAW(tu023_sub_839C44F8) { Update::SigninInfo(ctx, base, __imp__tu023_sub_839C44F8); }
REX_HOOK_RAW(tu023_sub_839C4000) { Update::GetXuid(ctx, base, __imp__tu023_sub_839C4000); }
REX_HOOK_RAW(tu023_sub_839C2578) { Update::TitleXnAddr(ctx, base, __imp__tu023_sub_839C2578); }
REX_HOOK_RAW(tu023_sub_823C73F0) {
  Update::RequestOnlineSignIn(ctx, base, __imp__tu023_sub_823C73F0, __imp__tu023_sub_823EB5D8,
                              __imp__tu023_sub_8242BF48);
}
REX_HOOK_RAW(tu023_sub_839C44D0) { Update::ShowGameInviteUi(ctx, base, __imp__tu023_sub_839C44D0); }
REX_HOOK_RAW(tu023_sub_839C3598) { Update::InviteAcceptedInfo(ctx, base, __imp__tu023_sub_839C3598); }
REX_HOOK_RAW(tu023_sub_823EB5D8) { Update::SetError(ctx, base, __imp__tu023_sub_823EB5D8); }
REX_HOOK_RAW(tu023_sub_823C7100) { Update::SetInOnlineZone(ctx, base, __imp__tu023_sub_823C7100); }
REX_HOOK_RAW(tu023_sub_839C58E0) { Update::PadPoll(ctx, base, __imp__tu023_sub_839C58E0); }
REX_HOOK_RAW(tu023_sub_823C8F50) { Update::CreateMatchDefaults(ctx, base, __imp__tu023_sub_823C8F50); }
REX_HOOK_RAW(tu023_sub_823CCFF0) { Update::CreateMatchNumPlayers(ctx, base, __imp__tu023_sub_823CCFF0); }
REX_HOOK_RAW(tu023_sub_823CD0E0) { Update::CreateMatchNumPlayers(ctx, base, __imp__tu023_sub_823CD0E0); }
REX_HOOK_RAW(tu023_sub_839C2B40) { Update::SessionCreate(ctx, base, __imp__tu023_sub_839C2B40); }
REX_HOOK_RAW(tu023_sub_839C2CF0) { Update::SessionDelete(ctx, base, __imp__tu023_sub_839C2CF0); }
REX_HOOK_RAW(tu023_sub_839C2DA0) { Update::SessionCall(ctx, base, __imp__tu023_sub_839C2DA0, "XSessionJoinLocal"); }
REX_HOOK_RAW(tu023_sub_839C2E48) { Update::SessionCall(ctx, base, __imp__tu023_sub_839C2E48, "XSessionJoinRemote"); }
REX_HOOK_RAW(tu023_sub_839C2EF0) {
  Update::SessionLeave(ctx, base, __imp__tu023_sub_839C2EF0, "XSessionLeaveLocal", true);
}
REX_HOOK_RAW(tu023_sub_839C2F98) {
  Update::SessionLeave(ctx, base, __imp__tu023_sub_839C2F98, "XSessionLeaveRemote", false);
}
REX_HOOK_RAW(tu023_sub_839C3040) {
  Update::RankedOnly(ctx, base, __imp__tu023_sub_839C3040, "XSessionArbitrationRegister");
}
REX_HOOK_RAW(tu023_sub_839C3118) { Update::SessionStart(ctx, base, __imp__tu023_sub_839C3118); }
REX_HOOK_RAW(tu023_sub_839C31B0) { Update::SessionCall(ctx, base, __imp__tu023_sub_839C31B0, "XSessionEnd"); }
REX_HOOK_RAW(tu023_sub_839C3260) { Update::SessionSearchEx(ctx, base, __imp__tu023_sub_839C3260); }
REX_HOOK_RAW(tu023_sub_839C3348) { Update::RankedOnly(ctx, base, __imp__tu023_sub_839C3348, "XSessionWriteStats"); }
REX_HOOK_RAW(tu023_sub_839C36C8) {
  Update::SessionCall(ctx, base, __imp__tu023_sub_839C36C8, "XSessionMigrateHost");
}
REX_HOOK_RAW(tu023_sub_823C80A8) { Update::JoinLobbyFromInvite(ctx, base, __imp__tu023_sub_823C80A8); }
REX_HOOK_RAW(tu023_sub_823BD540) { Update::CreateLobby(ctx, base, __imp__tu023_sub_823BD540); }
REX_HOOK_RAW(tu023_sub_82420E68) { Update::DeclineAcceptedInvite(ctx, base, __imp__tu023_sub_82420E68); }
#endif
