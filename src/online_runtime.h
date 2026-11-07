#pragma once

#include <filesystem>
#include <memory>

#include "game_files.h"
#include "netplay/online_game_socket.h"
#include "online/online_service.h"

namespace ssf2t {

// The platform name the service is told: Windows or Linux.
const char* OnlinePlatformName();

// Called when the launcher opens: the service then knows the user folder and which game image will run.
void ConfigureOnline(const std::filesystem::path& user_dir, GuestVersion guest);

// The process-wide service. Valid only after ConfigureOnline; it outlives the launcher so a lobby survives Play.
online::OnlineService& Online();

// The launcher's edited (not yet saved) settings, applied right away.
void TickOnline(const online::UserSettings& settings);

// Once per presented frame while the game runs, with the saved settings. A no-op until ConfigureOnline.
void PumpOnlineService();

double OnlineNowSeconds();

// The secret characters the current online fight was started with: fixed from the start of its Character Select, so
// the rollback replays them identically, and the service's live answer otherwise.
online::SecretSet ActiveOnlineSecrets();
void LockOnlineSecrets(online::SecretSet secrets);
void UnlockOnlineSecrets();

// Online Match's UDP socket, bound as soon as Online is on and the game runs (well before the Online Match menu), so
// Windows Firewall can prompt for network access right away. It then stays bound for the whole process: every match
// learns its public endpoint from it and plays over it. False while the bind fails (retried every few seconds).
bool EnsureOnlineGameSocket();
netplay::OnlineGameSocket& GameSocket();
}  // namespace ssf2t
