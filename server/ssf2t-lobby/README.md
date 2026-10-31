# ssf2t-lobby

Online service for Super Street Fighter II Turbo HD Remix (PC port): player identity, friends, presence, Player Match
advertisements and the private 1v1 match two friends are paired into. Fights run directly between the two PCs over UDP
whenever they can; a match whose players cannot reach each other is relayed through this service, between the two
sockets of that match only.

This service is separate from every other game's. It has its own Worker name, its own Durable Object class and its own
protocol (`game: "ssf2t-hd-remix-pc"`).

## Design

- `src/worker.js`: the Worker. `GET /` and `GET /health` return `{"service":"ssf2t-lobby","protocol":3,"status":"ok"}`.
  Every WebSocket goes to the single `SSF2TLobby` Durable Object (SQLite-backed, WebSocket Hibernation).
- `src/hub.js`: the whole service, independent of Cloudflare (`local/server.js` runs the same code on Node). Tables:
  `players`, `friends`, `friend_requests`, `player_matches`, `matches`. A **match** is a row, not an object: any number of
  matches run side by side, each between two players, with no per-match Worker or class.
- `src/protocol.js`: versions, limits, validation, friend code derivation.

A player is identified by a private token that never leaves their PC. The public id (`XXXX-XXXX`) is derived from it and
is also the friend code. The hub stores only a hash of the token and refuses a known id with another token.

### Flow

1. `hello` signs the player in (presence, platform, port version, guest image).
2. Friends: `friends.add` with a friend code, the other player `friends.accept`s; `friends.list` shows who is online.
3. The game's Multiplayer > Player Match drives the rest. Creating a Player Match sends `pm.host`, which advertises it
   to online friends with the same guest image and netplay build (sent again every minute; it expires after 3 minutes
   without one, and when its owner goes offline). `pm.search` lists those friends' live advertisements, nobody else's.
4. `pm.join` creates the private match: **the owner is Host / Player 1, the joiner is Join / Player 2**. Both get
   `match.created` / the join answer, report their gameplay socket (local port, LAN addresses and its STUN-mapped
   `public_ip`/`public_port`) with `match.endpoint`, and receive each other's with `match.peer`. The games then punch
   those endpoints from that same socket and play directly.
   When both clients said `relay: true` in `hello`, the match also has a relay from the start (`relay_token`). The
   host decides with `match.transport` (`direct`, or `relay` when no punch was answered in time); the joiner gets the
   same event and follows it. Relayed datagrams are binary frames (below).
5. `match.ready` tells the hub the direct connection is up (friends then see "In game"); `match.leave` ends the match.

### Protocol (version 3)

One JSON object per text frame; the literal `ping` is answered `pong`. Requests are `{"op":..., "seq":n, ...}`, answers
`{"re":n,"code":"ok"|<error>,...}`, events `{"ev":...}`. Binary frames are relay frames.

| op | fields | answer |
|---|---|---|
| `hello` | `proto`, `game`, `guest` (`base013`/`tu023`), `build`, `player`, `token`, `name`, `version`, `platform`, `presence`, `relay` | `player`, `name`, `friend_code`, `capabilities` |
| `ping` | | `time` |
| `friends.list` / `friends.requests` | | `friends[]` / `incoming[]`, `outgoing[]` |
| `friends.add` | `code` | `player`, `name`, `state` (`pending`/`friends`) |
| `friends.accept` / `.decline` / `.remove` | `player` | `friend` / - / - |
| `pm.host` | `slots` (2), `rules` (up to 8 small integers) | `session` |
| `pm.cancel` | | |
| `pm.search` | | `sessions[]`: `{session, player, name, guest, build, slots, rules, platform, version}` |
| `pm.join` | `session` | the match, plus `session` |
| `match.endpoint` | `match`, `port`, `lan[]`, optional `public_ip` + `public_port` (both or neither) | |
| `match.ready` | `match`, `ready` | |
| `match.transport` | `match`, `transport` (`direct`/`relay`); host only | |
| `match.leave` / `match.resume` | `match` | `.resume` answers with the match |

A friend row is `{player, name, online, state, platform, version, guest, build, compatible}`; `state` is `offline`,
`online`, `player_match`, `connecting` or `in_game`.

`capabilities` is `{protocol_version, worker_build, public_endpoints, relay_supported, relay_max_payload}`; an older
service sends none, which clients read as no relay. `match.created` and the `pm.join` answer carry `relay_token` when
the match has a relay.

Events: `friend.request`, `friend.added`, `friend.removed`, `friend.status`, `friend.offline`, `match.created`
(with `session`), `match.peer`, `match.ready`,
`match.peer_lost`, `match.peer_back`, `match.ended` (`left`, `disconnect`, `expired`), `match.transport` (to the joiner),
`relay.closed` (`peer_disconnected`, `rate_limit`, `peer_backpressure`, `peer_unavailable`, `unavailable`).

Errors: `bad_request`, `too_large`, `no_session`, `proto`, `bad_game`, `bad_guest`, `unauthorized`, `not_found`,
`not_friends`, `offline`, `busy`, `version_mismatch`, `limit`, `relay_unavailable`, `internal`.

Relay frames (binary): `"SR"`, version `1`, type `1` (data), token (u32), sequence (u32), payload length (u16), payload
(1 to 1472 bytes); big-endian. A frame is forwarded unchanged to the other socket of the sender's own current match,
taken from the match row, never from the frame, and only with that match's token; anything else is dropped (the first
few of each reason are logged, the payload never). Frames are not stored. The relay ends on `match.leave`, a
disconnect (a resumed seat gets no relay), `match.transport` `direct`, or its limits.

Rules: a Player Match is two-player only and is seen and joined only by an online friend who is not in a match, with
the same guest image (`base013` or `tu023`) and netplay build; joining also drops the joiner's own advertisement. A peer's address is always the address of its own
connection; only the port comes from the client, and LAN candidates reach only a peer behind the same address. A
dropped socket keeps its match seat for 30 s (`match.resume` retakes it); after that the peer hears `match.ended`.

Limits: 4 KB frames, 16-character names, 60 messages burst then 20/s per socket (`friends.add`, `pm.host`, `pm.join`,
`match.leave`, `match.resume` 10 burst then 1/s), 16 sockets and 20 new players per day per address, 200 friends, 50 pending requests each way.
Relay: 400 frames/s (burst 800) and 128 KB/s (burst 256 KB) per socket, twice that per match; 256 dropped frames in a
match end its relay. All matches share the one Durable Object, so relayed traffic counts against its CPU and request
budget.

## Local

No dependencies and no Cloudflare account are needed (Node 22.13 or newer, for `node:sqlite`).

```
cd server/ssf2t-lobby
node --test "test/*.test.js"        # protocol, friends, Player Match, matches, real WebSockets
node local/server.js                # ws://127.0.0.1:8791/ (add --db <file> to keep players and friends)
```

To point game instances at it, set `SSF2T_LOBBY=ws://127.0.0.1:8791/` in the environment of each.

## Deploy (manual; nothing in this repository deploys)

```
cd server/ssf2t-lobby
npx wrangler whoami                 # the account must be the one that owns the workers.dev subdomain
npx wrangler deploy
```

The Durable Object class `SSF2TLobby` and its migration `v1` already exist; this change needs no new migration. Do not
edit an applied migration. The hub keeps its data in the object's SQLite tables, created on first use (later columns,
such as `relay_token`, are added on start), and drops the unused `challenges` table. Clients of protocols 1 and 2 are
refused with `proto` after the deploy. Protocol 3 clients without relay support keep working: their matches simply
have no relay.

Check it:

```
curl https://ssf2t-lobby.saulobenigno.workers.dev/health
```

The game's default address is `kDefaultServiceUrl` in `src/online/online_service.h`.
