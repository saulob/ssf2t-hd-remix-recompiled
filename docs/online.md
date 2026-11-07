# Online play

Play Super Street Fighter II Turbo HD Remix against a friend over the internet. Players meet through a small online
service (`server/ssf2t-lobby`); the fight itself runs directly between the two PCs over UDP with rollback netplay.
Nothing in the fight passes through the service.

The launcher only handles friends and presence. A match starts from the game itself: **Multiplayer > Online Match**.
The Multiplayer menu lists only Local Match and Online Match. Online Match is the game's own Player Match, one against
one with an online friend; Ranked Match, Tournament Match and the larger lobbies (Quarter Match) are not offered.

Status (v0.4.0): the friend and Player Match flow, the XNet / QoS / VDP bridge and rollback netplay work on the original
Base013 and Title Update 1 (TU023) game images. Direct UDP between two PCs on different internet connections has been
tested with real players: Online Match, Invite, several fights in one session and agreeing frame-zero state, with no
gameplay desync observed. The relay fallback is implemented, tested by script (Node and C++ wire tests) and selected
automatically; it has not been tried between real players over the internet.

## Playing

1. Launcher > **Online** > **Online play** On. The **Settings**, **Friends** and **Lobby** tabs appear only then, and
   turning it on always opens **Settings**. **Online name** is what other players see (up to 16 letters, digits, `-` or
   `_`; the field takes nothing else). **Settings** also holds Your friend code, the Connection test, the Ping display
   and the Secret characters status. A Play refused for the missing Connection test returns to it.
2. Share **Your friend code** (`XXXX-XXXX`, **Copy** puts exactly that on the clipboard). Paste or type a friend's code
   in the **Friends** tab; the field shows it as `XXXX-XXXX` whatever case, spacing or hyphen it came with. **Send
   request** is enabled once it is a valid code (letters except I and O, digits 2 to 9). They **Accept** or **Decline**
   the card in their **Friends** tab, which shows the number of waiting requests; the sidebar's dot marks them too.
   The **Lobby** tab lists players who chose to be seen; see [Public Lobby](#public-lobby).
3. A friend row shows `Online · Windows · v0.3.0`, `Online Match · ...` while they host one, or `In game · ...`.
   There are no match buttons in the launcher.
4. Both press **Play**. The game signs in to the online service again by itself; nothing has to stay open.
5. In the game: **Multiplayer > Online Match** lists **Find Match** and **Create Match**. One player creates a match
   (always two players, both slots public), the other uses Find Match, which joins an online friend's match. The
   creator is Player 1, the joiner Player 2.
6. Or, in the created lobby, the game's own **Invite Player** opens a list of online friends (A invites, B goes back).
   The friend sees "<Name> invited you to an Online Match." with Accept / Decline, from the main menu or the
   Multiplayer menus; Accept joins that same lobby through the game's own accepted-invite path, for the device that
   pressed Accept (the controller last used in the game, or the keyboard; a mouse click keeps the controller). The game then asks "Accept invitation" itself: D-pad, stick or arrow keys
   move, A or Enter chooses, B or Escape keeps the current session. An invite ends when it is declined, the host
   leaves or starts, the lobby closes, or after a minute.
7. The controller (or keyboard) that chose Online Match or accepted the invite owns the match until it ends: it
   plays the local fighter whichever side it is on, through every screen of the match (pause, menus, results); the
   other fighter is the remote player.
8. The match ends only when a player leaves the lobby or the connection drops, never on a menu screen. The game then
   shows `PLAYER LEFT` or `NETWORK ERROR` / `Connection lost.` once; the owning controller's A (or Enter for the
   keyboard) returns to the menus, and a new Online Match can be created, joined or accepted from an invite, without
   restarting; each match and each fight starts from a clean rollback session.

Both players must run the same game image (the original game or Title Update 1) and the same netplay build. A friend
on another image shows `Game versions do not match`, and their Player Matches are never listed.

While Online play is signed in, the game sees a title-local Xbox LIVE profile: signed in, multiplayer allowed, the
Online name as gamer name and a stable XUID derived from the friend code (never from the private token). The profile
belongs to the controller that chose Online Match. Create Player Match has no Number of Players setting; the lobby is
always two players. With Online play Off the original `CONNECTION REQUIRED` message stays; with Online play On but the
service not signed in, Online Match shows `ONLINE UNAVAILABLE` / `Unable to connect to the online service.`

Names: **Gameplay > Local player names** (Player 1, Player 2) are used only in Local Match. The **Online name** is used
for Online Match, friends, friend requests and what the other player sees. Neither changes the other.

**Connection test** checks that this PC can reach the online service and how other players will reach it. It signs in
to the service (health check, WebSocket, sign-in; the round trip is shown in ms), asks two STUN servers which public
address and port a UDP socket gets (the same mapping from both means a plain router; different ports mean a symmetric
NAT) and, if needed, asks the router to open the gameplay port through UPnP and closes it again. The result is one
verdict (`Ready for online play`, or `Limited: ...` / `Not ready: ...`) with the lines Online service, Direct
connections and, when something is wrong, Router configuration. The verdict and rows are the same as Dragon's Crown
Pro's: the STUN result decides Direct connections, a UPnP mapping makes it ready when STUN alone does not, and a
router that reports no public address (double NAT or carrier-grade NAT) is only noted in the log unless direct
connections also failed. It is a check of this PC's network, not of a friend. When the online service offers its relay
(see Relay below) a **Relay: Available** line is added and the verdict is `Ready for online play` even when direct
connections are blocked or uncertain; those lines then become notes instead of problems. The relay still needs the
gameplay UDP port to open, so a closed port stays `Limited`. Addresses and errors go to the log as
`online connection test ...`.

The test opens the gameplay UDP port the game itself would (7001, or 7002 when another instance holds 7001) and runs
STUN and UPnP from it; if neither can be opened it says so. When Online play is turned on, PLAY waits until one
Connection test has finished, whatever its result; it is saved, so it is not asked again while Online play stays on.
Turning Online play off resets it. On Windows the test's network traffic also gives Windows Defender Firewall its
chance to ask for network access before playing (choose Allow access).

**Ping display** shows the round trip to the other player in the corner, under the frame counter when both are on;
**F3** shows or hides it.
The value is measured on the gameplay connection from the sequence numbers the peer acknowledges (lowest of the last
two seconds). `Ping: --` means no peer is connected or nothing has been measured yet. No extra packets are sent.

With Online play Off nothing connects: no sign-in, no presence, no signalling, no netplay.

## While Online play is on

**Infinite Time is disabled.** It changes the simulation both players run. Turning Online play on switches it Off with a
status message; the Gameplay page shows "Infinite Time is disabled while Online play is enabled."; F10 shows the same
line; the saved value is ignored at runtime. Visual settings (aspect ratio, resolution, render scale, anti-aliasing,
graphics adapter, frame counter, title text, fight area fill) and input settings stay available.

Netplay Test (Off / Host / Join, Peer Address) and the Netplay Determinism Test are developer controls: they only
show, and only take effect, with the environment variable `SSF2T_DEVELOPER=1`. Values saved in an earlier developer
session are ignored without it.

## Identity

`online_identity.ini` in the user folder holds a private 32-byte token, made on first use and never overwritten. It is
only ever sent to the online service, never shown or logged. The public friend code is derived from it, so it is stable
and cannot be chosen. Two instances on the same user folder cannot share an identity: the second one signs in under a
temporary identity.

## Architecture

- `src/online/protocol.*`: the JSON messages. `online_service.*`: sign-in, friends, Player Match advertisement,
  search and join, the match, reconnect.
  `friend_view.*`: what a friend row shows. `connection_test.*`, `stun.*`, `upnp.*`, `net_addr.*`: the Connection test.
  `lobby_link.*`, `websocket_*`: transport.
  `src/online_page.*`, `src/online_tabs.h`, `src/online_runtime.*`, `src/ping_display.*`: launcher page and its tabs,
  process-wide service, overlay.
- `src/player_match*`: the game side. The title's own wrappers for XamUserGetSigninState, XUserCheckPrivilege,
  XamUserGetSigninInfo, XUserGetXUID and XNetGetTitleXnAddr report the title-local LIVE profile; the
  `RequestOnlineSignIn` script function (the Online Match gate) runs as the game wrote it, or shows
  `ONLINE UNAVAILABLE` when the service is down; the create-match lobby size is held at two. `online_menu*` edits the
  UIScreenSet XML as the game parses it (Multiplayer rows, Create Player Match without Number of Players) and the
  matching help lines; `menu_text.h` renames the Player Match labels. XSessionCreate (host) advertises,
  XSessionSearchEx returns friends' advertisements, XSessionCreate (join) with a listed session joins it. Every XSession call is logged. Addresses exist
  per image (`player_match_guest.h`).
  A guest search whose cache is empty and not fresh stays pending (the overlapped completes later) until the service
  answers that search, or 5 s pass; an answer from an older search epoch (left, joined or restarted) is ignored.
  A `match.peer` without `public_endpoints` from another network ends the match at once as
  `service_capability_mismatch` (the deployed Worker predates public endpoints), without waiting for STUN or the 10 s
  endpoint wait.
- A joined Player Match becomes an assignment (Host = owner = Player 1, Join = Player 2, the peer's address and the
  fixed gameplay ports 7001 / 7002). `netplay_hook.cpp` takes it before the developer role setting, opens the UDP
  transport and the existing `Session` / `RollbackCore` / `GuestSimulation`, and reports the link and RTT back. It only
  takes over a fight in the game's online mode, never Local Match.
- If the service connection drops, friends and the match are kept, the client reconnects and resumes its seat (the
  service holds it 30 s). A fight already running over direct UDP continues; the match is cleared when that session
  ends. A relayed match (or one still choosing, with the relay as its fallback) ends at once instead.

## Relay

Direct UDP is always tried first; the online service's relay is the automatic fallback for networks that cannot be
punched (strict or carrier-grade NAT, firewall software). There is no setting for it.

- A match between two clients that both said `relay: true` in `hello` gets a relay token from the service at once
  (`relay_token` in `match.created` / the `pm.join` answer), while the direct path is still being tried.
- Each match is `DirectProbing` first: STUN, endpoint exchange, punching. The **host** chooses: direct as soon as a
  punch is answered, the relay when no direct path appeared within `kDirectProbeWindowSeconds` (2.5 s from its own
  endpoint advertisement; a punch that only starts later still gets `kMinPunchSeconds`, 1.5 s), or at once when the
  service does not forward public endpoints. It tells the service with `match.transport`; the joiner only follows the
  `match.transport` event, so both ends always use the same transport. The choice is made before the fight session is
  told the peer and is never switched during the match.
- A match is only refused for having no path when the direct path fails and the match has no relay (an older service
  or an older friend's game): `match transport summary ... selected=failed`.
- Relayed datagrams are binary WebSocket messages on the existing service connection: `"SR"`, version 1, type 1
  (data), the match's relay token (u32), a sequence number (u32), the payload length (u16), the payload; big-endian,
  14 bytes of header. The payload is at most 1472 bytes (the game's largest datagram is a 1412-byte lobby datagram;
  rollback packets are at most 256 bytes). `OnlineGameSocket` carries it below the guest: the session and the
  Player Match lobby send to and receive from `netplay::kRelayPeer` (127.0.0.2:1) exactly as they would to a UDP
  peer. Received frames are queued (at most 256) and drained by the game's normal network pump; an overflow, a frame
  the connection cannot take (512 queued) or a lost service connection ends the match cleanly.
- The service forwards a frame only from one socket of a match to the other socket of the same match, found from its
  own match row (never from the frame), and only with that match's current token. Frames are never stored or logged.
  Limits per connection: 400 frames/s (burst 800) and 128 KB/s (burst 256 KB); per match twice that. Over-limit frames
  are dropped; 256 drops in a match, a peer socket that cannot take a frame, or a disconnect end the relay
  (`relay.closed`). A fight sends about 60 to 70 frames/s each way, well under the limits.
- The relay runs over TCP, so a lost segment delays the frames after it; rollback hides most of it. The Ping display
  keeps measuring the real peer round trip from the session's own acknowledgements, relay included.
- **Network establishment budget** (`online/establishment.h`): one absolute clock per match, started when the service
  creates it (`network establishment begin ... deadline_ms=15000`). No transport by 15 s is a terminal timeout
  (`network establishment timeout`), reported and handed to the game at once; a known failure (service connection
  lost, `relay.closed`, the peer left, no direct path and no relay, queue overflow) fails the same way immediately,
  with `terminal=true`. Once a transport is selected (`network establishment ready`) the game's lobby handshake gets
  `min(selection + 10 s, start + 15 s)`. The joiner has no timer of its own: it waits for the host inside the same
  clock. Repeated peer updates, repeated or stale `match.transport` events and fights never move it.
- **The game's lobby wait**: `WAIT_FOR_PLAYER_LIST` (net manager op 6) arms its own 10.0 s timer when it starts, which
  is right after the join, so it could run out while the transport was still being chosen. The game ends the
  wait when the player-list flag (net manager +0x22DE9) is set, or when that timer expires, which it reports as
  `LOBBY_JOIN_FAILED_UNABLE_TO_CONNECT`. The shared timer check (base `8241E8E8`, TU `8241F0A8`) is hooked and, only
  while op 6 runs in a joined online match, answers from the budget instead (`GuestWaitState`); every other op keeps
  the game's timer. `guest wait complete result=connected|failed` is logged once per match from the wait's completion
  (base `824282C8`, TU `82428A28`).
- Lobby datagrams the game sends before the transport is selected (its first connection requests) are held, at most 32
  of that match, and sent when the lobby is published, instead of being lost until the game's own resend timer.
- All relays share the one global Durable Object, so it carries every relayed match's traffic: watch its CPU time and
  request count after deploying.

Logs: `online service capabilities protocol= worker_build= public_endpoints= relay_supported=`,
`network establishment begin|ready|failed|timeout`, `relay available match= role=`, `direct probe begin match=
budget_ms=2500`, `relay establishment begin ... relay_budget_remaining_ms= absolute_remaining_ms=`,
`guest wait complete result=`, `udp punch tx/rx`,
`direct path not established elapsed_ms=`, `relay fallback begin match=`, `relay ready match=`,
`transport selected=direct|relay|failed`, `relay tx/rx n= bytes=` (first four), `relay stats ...` (every 10 s),
`relay closed reason=`, and one `match transport summary` per match. The service logs `relay attach`, `relay ready`,
`relay forward` (first three per direction), `relay drop reason=` (bounded) and `relay close reason=`.

## Testing two instances on one PC

```
tools\online-instance.ps1 -Name a      # terminal 1
tools\online-instance.ps1 -Name b      # terminal 2
```

Each instance has its own user folder (`build\online-instances\<name>\user_data`: settings, online identity, saves) and
logs (`build\online-instances\<name>\logs`), on the production service (`SSF2T_LOBBY` selects another one, such as
the local server). Display and graphics settings are copied once
from `build\user_data`; the identity is not. `-PrepareOnly` sets the folders up without starting the game.
`-Akuma On|Off` and `-EvilRyu On|Off` set the instance's Enable Akuma and Enable Evil Ryu and Violent Ken options (new
instances start Off), for the secret-character matrix: `tools\online-instance.ps1 -Name a -Akuma On -EvilRyu On`.

The script prints the instance's folders, identity file and the exact log file. Every start writes a new log named after
the instance, `ssf2t_hd_remix_a_001.log`, `_002`, ... (never a shared `ssf2t_hd_remix.log`), and keeps detailed logging
on so the log survives a normal exit. A new instance's Online name is `Test-<name>` (`Test-a`, `Test-b`; `-OnlineName`
overrides it); it is only set when the instance has none, so a name chosen on the Online page is kept. The sign-in line
of each log shows `name=` and the public `code=`.

Manual two-instance check: Online play On in both, **Copy** the friend code in `a`, paste it into **Add a friend** in `b`
(Ctrl+V; lower case, a missing hyphen, spaces and a trailing newline are all fine), **Send request**, **Accept** in `a`,
then Play in both and Multiplayer > Online Match in the game. Log lines: `online friend request sent
code=...`, `player_match ...`, `XSession...`.

## Public Lobby

The **Lobby** tab is for finding people to add as friends. It does not create matches and does not change how Online
Match finds opponents: matches still start from Multiplayer > Online Match between friends.

- **Show me in Lobby** is Off by default. While it is Off you are not listed, but you can still read the Lobby, and
  friends, presence and Online play work as usual. Turning it On lists your **Online name** to other players of this game;
  they can send you a friend request. Nothing else about you is listed: no address, connection details or Player
  Match. Your friend code travels with the listing only so that Add friend can send the request; no screen shows it. The
  setting is `ssf2t_online_lobby_visible`, and the game keeps you listed while it runs.
- Each row shows the name, what the player is doing (`Online`, `Online Match`, `Connecting`, `In game`), the platform,
  the port version and the game image (`Base013` or `TU023`). A player who cannot play with you (another game image or
  netplay build) stays in the list marked **Game versions do not match**.
- **Add friend** sends the same friend request as the Friends tab, so the other player answers it there. It is offered
  only to players who are not friends and have no request pending either way; a request they already sent you shows
  **Accept** and **Decline**. You never see anyone's friend code in the Lobby.
- The list refreshes by itself every 30 seconds while the Lobby tab is open, and is read again when you open it. Nothing
  is read while another tab is shown. There is no Refresh button.
- A player disappears from the Lobby when they turn the setting Off, turn Online play Off or close the game; the service
  keeps no record of who was listed. The list shows the first 50 players, compatible ones first.
- An older service without the Lobby makes the tab say so; the other tabs and Online play are not affected. If the Lobby
  cannot be read for a moment, the tab says so and keeps the last list; the rest of the page stays online.

## Bug reports

The launcher's **Bug Report** page sends a problem report to the developer: your description and one game log. Nothing is
sent until you press **Send report**, and nothing about the log leaves your PC before you can read it.

- **What is sent.** Only these fields: your description (10 to 500 characters), the port version, the game image
  (`base013` or `tu023`), the platform, the operating system, the ReXGlue runtime version, the log's file name, whether
  the log was shortened, and the log text after the privacy filter. No game files, saves, screenshots, settings or
  account details are attached.
- **When it is sent.** Only when you press **Send report**, over its own short connection to the online service. Online
  play does not have to be On. Opening the page, writing a description and choosing a log send nothing. When the launcher
  opens, when you open the page (at most every 30 seconds) and when you press **Refresh**, it asks the service about the
  reports already listed in **My reports**, sending only their receipts (see below), never a new log.
- **Choosing the log.** The page lists the newest five `ssf2t_hd_remix_<number>.log` files of the logs folder (the one
  **Open logs folder** on About opens): this launcher's own session first, then newest first. Sessions that only opened and
  closed the launcher are left out. The newest earlier session that reached the game is chosen for you; pick another if the
  problem was in a different run. With **Detailed logging** off, only sessions that crashed keep a log.
- **Preview.** **Open preview** shows exactly what will be sent: the report is built once, and the preview and the request
  both read that same text. Change the description or the log and the preview closes, so it can never be out of date.
- **Privacy filtering.** Before the preview is built the launcher takes out of the log and the description: your Windows
  user name and computer name, your profile and home folders, the install and data folders, other folders on your disks
  (only the file name is kept), network share paths, IPv4 and IPv6 addresses (version numbers such as `0.0.2.3` stay),
  the online token and the hash the service keeps of it, receipts and authorization or bearer values, any password, secret or
  token written as `name=value`, match, session and invite identifiers, your Online name and every Friend Code (yours
  becomes `<own-code>`, anyone else's `<friend-code>`). Control characters are removed and line ends become LF. Automatic
  filtering cannot promise to find everything, so read the preview if you want to be certain.
- **Size.** A report holds at most 64 KiB of log. When the log is longer, the end of it is sent (it is where a problem
  usually is), starting at a whole line, and the report says that the log was shortened. A description cannot pass 500
  characters; the service refuses anything larger than these limits.
- **Ticket.** A sent report returns a ticket such as `SSF2-7KQ4WX` (the page can copy it). The service also returns a private
  receipt that proves the report is yours; it is stored only in the file `bug_reports.json` in your user folder, never in
  a log, and the service keeps just a salted hash of it.
- **My reports.** Lists the reports sent from this PC (the latest 30), with their last known status and date. **View
  details** shows the facts of a report and **What was sent** reads the stored report back from the service with its receipt.
  **Remove from this PC** only forgets it here. Reports sent from another PC or another user folder do not appear.
- **Status.** The developer can mark a report **Open**, **Investigating**, **Fixed** (optionally with the version that fixes it),
  **Cannot reproduce**, **Duplicate**, **Not a bug** or **Closed**, and leave a short message. The launcher checks when it opens, when
  you open the page and when you press **Refresh**, marks a changed report **Updated** and puts a dot on the sidebar entry.
- **Limits.** Two reports per hour and ten per day from one install, and fewer than that from one address and from everyone
  together; the page says which limit was reached.
- **An older online service** answers a report with an error the page explains ("The online service cannot take reports
  yet"); your description is kept, and Play, Friends, Lobby, Player Match and every Online Match work as before.

Files: the receipts and the list are `bug_reports.json` in the user folder, written next to `online_identity.ini`. The
service side is described in [server/ssf2t-lobby/README.md](../server/ssf2t-lobby/README.md).

## Secret characters

How the two Ultra fighters work inside the game is described in [ultra-fighters.md](ultra-fighters.md). Akuma, Evil Ryu and Violent Ken each appear in an Online Match only when both players have them on and both builds
understand them.

- Each client sends `secret: {supports: {akuma, evil_ryu, violent_ken}, enabled: {akuma, evil_ryu, violent_ken}}` in its
  hello (`supports` is what the build understands, `enabled` is Enable Akuma / Enable Evil Ryu and Violent Ken, and for those two fighters their required packages being valid here) and `secret.update` when an option
  changes while signed in. Anything missing or not exactly `true` is false, so an older client supports nothing.
- The Worker keeps both players' capabilities with the match and tells each seat `secret: {peer, effective}` (in the
  join answer, `match.created`, the resume answer and `match.secret` events). `effective.<character>` is supports and
  enabled on both sides; it is identical for both seats. The client checks that answer against its own options and can
  only shrink it. The lobby protocol stays 3 and `kNetplayBuild` is unchanged, so older clients and an older Worker keep
  working and simply never get a secret character.
- The Online page shows the options as `Enabled locally` / `Off` outside a match and, inside one, `Available`,
  `Disabled locally`, `Disabled by opponent`, `Unsupported by opponent`, `Unsupported by the service` or `Waiting for
  opponent`.
- A peer that is lost, a match that ends or is left, and a new match each recompute the result from the peer in front of
  the client; nothing carries over. A rematch against the same peer keeps it.
- The result is locked when an online Character Select starts (the game's NetSync start, where the rollback begins) and
  stays fixed until the fight is left, so changing an option mid-match applies from the next Character Select. The locked
  set is added to the frame-0 setup words only when something is on, so two clients that disagree on it refuse the fight
  instead of drifting, and clients with nothing on send exactly the words they always did.
- Character Select online uses the locked set instead of the local option for the hidden Akuma path, the Akuma
  portrait overlay and the Evil Ryu cell. Local, Arcade and Training keep following the local options.
- The selection itself is never sent: both consoles replay the same inputs through the game's own Character Select. The
  Ultra cells and the Evil Ryu and Violent Ken move trackers live outside the guest, so they are saved with every rollback snapshot
  and restored on a rollback; a mispredicted move cannot leave the cell selected. When the fighters are known, each
  console checks them against the locked set (`online fighter side=2 base=Ryu variant=EvilRyu`); an Akuma or Evil Ryu the
  match did not negotiate logs `online fighter reject ...` and leaves the match on both consoles.
- The host reports Evil Ryu as character 17 (Ryu stays 0), Violent Ken as 18 (Ken stays 4) and Akuma as 16; the Worker
  refuses a report of a secret character the match did not negotiate, and counts availability, option mismatches and usage in aggregate (see
  `server/ssf2t-lobby/README.md`).
- Logs: `online secret-cap local|peer|effective ...`, `online secret-cap locked ...`, `online fighter ...`.

## What is not covered

- A match learns its gameplay socket's public endpoint from STUN and punches the peer's candidates from that socket;
  no UPnP mapping is kept while playing. Two symmetric NATs cannot be punched; such a match is relayed. Direct play
  between two real internet connections is tested (see Status); every NAT combination is not, and a relayed match
  has not been tried with real players.
- A relayed match does not survive a lost service connection: it ends instead of moving to a new connection.
- Friend Player Matches only: no public matchmaking, Ranked Match, Tournament Match or Quarter Match. Because Ranked
  Match is not offered, the four achievements that need online ranked matches cannot be earned.

## Fight telemetry

From v0.3.1 the host of an online match reports, anonymously, each fight to the lobby service: the two internal character
ids, who won, the transport and the game build. The client only reads the running fight (the fighters' character and round
wins, and the fight work object's rounds to beat) and queues the report; it never waits, and a failure changes nothing in
the fight, the rollback or the connection. A fight is reported once when it becomes authoritative and once when a result
has held for twenty frames; a fight cut short by a disconnect counts as started only. The lobby protocol stays 3: the
client announces `telemetry` in its hello and sends reports only to a service that announces `fight_telemetry`, so v0.3.0
clients and an undeployed Worker keep working and simply add no fight numbers. Local Match, Arcade and Training never
report. Storage and privacy are described in `server/ssf2t-lobby/README.md`.
