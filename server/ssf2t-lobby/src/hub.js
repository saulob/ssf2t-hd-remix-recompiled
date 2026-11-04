// The online hub: players, friends, presence, Player Match advertisements and the private 1v1 matches two friends are
// paired into.
// Signalling passes through it; fights run directly between the two PCs, or, when no direct path exists, as binary
// relay frames between the two sockets of one match. State lives in SQL tables and in each socket's attachment, so a
// hibernated Durable Object resumes as if it had never stopped; relay routes are a memory cache rebuilt from them.
// The same class runs in the development server (local/server.js).
import { PROTECTION, RateLimiter } from './abuse.js';
import { SECRET_CHARACTER, SECRET_KEYS } from './characters.js';
import { Metrics, SAMPLE_MS, countryCode, platformCategory, sampleBucket, versionCategory } from './metrics.js';
import { sha256Hex, toHex } from './sha256.js';
import {
  BUCKETS, CAPABILITIES, LIMITS, ProtocolError, RELAY, compatibleGuests, fail, ipGroup, isLocalAddress,
  effectiveSecret, normalizeFriendCode, normalizeSecret, parseEndpoint, parseFightEvent, parseHello, parseRelayFrame,
  secretEstablishment, secretFromMask, secretMask,
} from './protocol.js';

const DAY_MS = 24 * 3600 * 1000;
const SOCIAL_OPS = new Set(['friends.add', 'pm.host', 'pm.join', 'invite.send', 'match.resume', 'secret.update']);
const RULE_KEY = /^[a-z_]{1,16}$/;
const QOS_DATA = /^(?:[0-9a-f]{2}){0,256}$/;
const ROLES = ['host', 'join'];
const GROWING_OPS = new Set(['hello', 'pm.host', 'pm.join', 'match.resume']);
const REJECT_CODES = new Set(['proto', 'bad_game', 'bad_guest', 'version_mismatch']);
const MATCH_LIST_MAX = 50;

const SCHEMA = [
  `CREATE TABLE IF NOT EXISTS meta (k TEXT PRIMARY KEY, v TEXT NOT NULL)`,
  `CREATE TABLE IF NOT EXISTS players (player TEXT PRIMARY KEY, token_hash TEXT NOT NULL, name TEXT NOT NULL,
     created INTEGER NOT NULL, seen INTEGER NOT NULL, session INTEGER NOT NULL DEFAULT 0)`,
  `CREATE TABLE IF NOT EXISTS friends (player TEXT NOT NULL, friend TEXT NOT NULL, since INTEGER NOT NULL,
     PRIMARY KEY (player, friend))`,
  `CREATE TABLE IF NOT EXISTS friend_requests (sender TEXT NOT NULL, target TEXT NOT NULL, ms INTEGER NOT NULL,
     PRIMARY KEY (sender, target))`,
  `CREATE TABLE IF NOT EXISTS registrations (ip TEXT NOT NULL, ms INTEGER NOT NULL)`,
  `DROP TABLE IF EXISTS challenges`,
  `CREATE TABLE IF NOT EXISTS player_matches (id TEXT PRIMARY KEY, owner TEXT NOT NULL UNIQUE, guest TEXT NOT NULL,
     build INTEGER NOT NULL, slots INTEGER NOT NULL, rules TEXT NOT NULL, qos TEXT NOT NULL DEFAULT '',
     created INTEGER NOT NULL, refreshed INTEGER NOT NULL)`,
  `CREATE TABLE IF NOT EXISTS invites (id TEXT PRIMARY KEY, session TEXT NOT NULL, sender TEXT NOT NULL,
     target TEXT NOT NULL, created INTEGER NOT NULL, UNIQUE (session, target))`,
  `CREATE TABLE IF NOT EXISTS matches (id TEXT PRIMARY KEY, host TEXT NOT NULL, joiner TEXT NOT NULL,
     guest TEXT NOT NULL, build INTEGER NOT NULL, created INTEGER NOT NULL, introduced INTEGER NOT NULL DEFAULT 0,
     host_cid TEXT, join_cid TEXT, host_grace INTEGER, join_grace INTEGER,
     host_ip TEXT NOT NULL DEFAULT '', join_ip TEXT NOT NULL DEFAULT '',
     host_pub_ip TEXT NOT NULL DEFAULT '', join_pub_ip TEXT NOT NULL DEFAULT '',
     host_pub_port INTEGER NOT NULL DEFAULT 0, join_pub_port INTEGER NOT NULL DEFAULT 0,
     host_port INTEGER NOT NULL DEFAULT 0, join_port INTEGER NOT NULL DEFAULT 0,
     host_lan TEXT NOT NULL DEFAULT '[]', join_lan TEXT NOT NULL DEFAULT '[]',
     host_ready INTEGER NOT NULL DEFAULT 0, join_ready INTEGER NOT NULL DEFAULT 0,
     relay_token INTEGER NOT NULL DEFAULT 0, transport TEXT NOT NULL DEFAULT '',
     host_secret INTEGER NOT NULL DEFAULT 0, join_secret INTEGER NOT NULL DEFAULT 0)`,
  `CREATE TABLE IF NOT EXISTS match_telemetry (match TEXT PRIMARY KEY, guest TEXT NOT NULL, host_platform TEXT NOT NULL,
     join_platform TEXT NOT NULL, reporting INTEGER NOT NULL, transport TEXT NOT NULL DEFAULT '',
     transport_counted INTEGER NOT NULL DEFAULT 0, established INTEGER, fights_started INTEGER NOT NULL DEFAULT 0,
     fights_completed INTEGER NOT NULL DEFAULT 0, secret_akuma INTEGER NOT NULL DEFAULT 0,
     secret_evil_ryu INTEGER NOT NULL DEFAULT 0)`,
  `CREATE TABLE IF NOT EXISTS match_fights (match TEXT NOT NULL, fight INTEGER NOT NULL, p1 INTEGER NOT NULL,
     p2 INTEGER NOT NULL, completed INTEGER NOT NULL DEFAULT 0, PRIMARY KEY (match, fight))`,
];
// Added after the table first shipped: CREATE TABLE above only runs on a fresh database.
const ADDED_COLUMNS = [
  ['players', 'session', 'INTEGER NOT NULL DEFAULT 0'],
  ['matches', 'host_pub_ip', "TEXT NOT NULL DEFAULT ''"], ['matches', 'join_pub_ip', "TEXT NOT NULL DEFAULT ''"],
  ['matches', 'host_pub_port', 'INTEGER NOT NULL DEFAULT 0'], ['matches', 'join_pub_port', 'INTEGER NOT NULL DEFAULT 0'],
  ['matches', 'relay_token', 'INTEGER NOT NULL DEFAULT 0'], ['matches', 'transport', "TEXT NOT NULL DEFAULT ''"],
  ['matches', 'host_secret', 'INTEGER NOT NULL DEFAULT 0'], ['matches', 'join_secret', 'INTEGER NOT NULL DEFAULT 0'],
  ['match_telemetry', 'secret_akuma', 'INTEGER NOT NULL DEFAULT 0'],
  ['match_telemetry', 'secret_evil_ryu', 'INTEGER NOT NULL DEFAULT 0'],
];

const isObject = (v) => v !== null && typeof v === 'object' && !Array.isArray(v);
const isInt = (v, lo, hi) => Number.isInteger(v) && v >= lo && v <= hi;

function takeToken(bucket, spec, now, cost = 1) {
  const [tokens, last] = bucket ?? [spec.burst, now];
  const refilled = Math.min(spec.burst, tokens + ((now - last) / 1000) * spec.perSecond);
  if (refilled < cost) return null;
  return [refilled - cost, now];
}

// Packets and bytes both have to fit; nothing is taken from either when one does not.
function takeRelayBudget(budget, spec, now, bytes) {
  const packets = takeToken(budget.packets, spec.packets, now);
  const volume = packets && takeToken(budget.bytes, spec.bytes, now, bytes);
  if (!volume) return false;
  budget.packets = packets;
  budget.bytes = volume;
  return true;
}

function relayToken() {
  return crypto.getRandomValues(new Uint32Array(1))[0] || 1;
}

function playerOf(value) {
  if (typeof value !== 'string' || !/^[A-Z0-9]{4}-[A-Z0-9]{4}$/.test(value)) fail('bad_request', 'player expected');
  return value;
}

const transportOf = (row) =>
  (row.transport === 'direct' ? 'direct' : row.transport === 'relay' && row.relay_token ? 'relay' : 'pending');

// What one seat is told about the characters of its match: the peer's stored capabilities and what both allow.
function secretView(row, role) {
  const peer = secretFromMask(row[`${role === 'host' ? 'join' : 'host'}_secret`]);
  return { peer, effective: effectiveSecret(secretFromMask(row[`${role}_secret`]), peer) };
}

// One match as the dashboard shows it: a short id, no player, address, connection or token.
function matchReport(row, now) {
  return {
    id: row.id.slice(0, 6),
    state: row.host_ready && row.join_ready ? 'in_game' : 'connecting',
    guest: row.guest,
    transport: transportOf(row),
    relay_available: row.relay_token !== 0,
    endpoints: (row.host_pub_port ? 1 : 0) + (row.join_pub_port ? 1 : 0),
    seats: ROLES.filter((r) => row[`${r}_cid`] !== null).length,
    age_ms: Math.max(0, now - row.created),
  };
}

export class Hub {
  // host: { sql: { all(q, ...a), get(q, ...a), run(q, ...a) }, sockets(): Conn[], now(): ms, setAlarm(ms), log(text) }
  // options: { local, alarmAt } - alarmAt is the alarm the host already has stored, if any.
  // Conn: { send(text), close(code, reason), attachment(), setAttachment(object) }
  constructor(host, options = {}) {
    this.host = host;
    this.local = Boolean(options.local);
    // Never persisted: cid -> the route its relay frames take, and per match its budget.
    this.relayRoutes = new Map();
    this.relayMatches = new Map();
    this.relayDrops = new Map();
    this.relayTold = new Set();
    this.relayFaults = new Map();
    this.alarmAt = options.alarmAt ?? null;
    this.guards = {
      hello: new RateLimiter(PROTECTION.hello),
      advertiseAccount: new RateLimiter(PROTECTION.advertiseAccount),
      advertiseAddress: new RateLimiter(PROTECTION.advertiseAddress),
      joinAccount: new RateLimiter(PROTECTION.joinAccount),
      resumeAccount: new RateLimiter(PROTECTION.resumeAccount),
      inviteAccount: new RateLimiter(PROTECTION.inviteAccount),
      inviteAddress: new RateLimiter(PROTECTION.inviteAddress),
      rejects: new RateLimiter({ burst: 1, perSecond: 1000 / PROTECTION.rejectWindowMs }),
    };
    for (const statement of SCHEMA) host.sql.run(statement);
    this.metrics = new Metrics(host.sql, () => host.now());
    for (const [table, column, definition] of ADDED_COLUMNS) {
      try { host.sql.run(`ALTER TABLE ${table} ADD COLUMN ${column} ${definition}`); } catch { /* already there */ }
    }
    if (!this.meta('salt')) this.setMeta('salt', this.randomHex(16));
    this.reconcileSeats();
  }

  // Seats whose socket is gone (the object or the server restarted) get the usual grace instead of staying occupied.
  reconcileSeats() {
    const live = new Set(this.host.sockets().map((c) => c.attachment()?.cid).filter(Boolean));
    const until = this.host.now() + LIMITS.graceMs;
    for (const row of this.host.sql.all('SELECT * FROM matches')) {
      for (const role of ROLES) {
        const cid = row[`${role}_cid`];
        if (cid && !live.has(cid)) {
          this.host.sql.run(`UPDATE matches SET ${role}_cid = NULL, ${role}_grace = ?, relay_token = 0 WHERE id = ?`,
            until, row.id);
        }
      }
    }
    const present = new Set(this.host.sockets().map((c) => c.attachment()).filter((a) => a?.hello && a.presence).map((a) => a.player));
    for (const row of this.host.sql.all('SELECT * FROM player_matches')) {
      if (!present.has(row.owner)) this.dropPlayerMatch(row, 'host_left');
    }
    for (const row of this.host.sql.all('SELECT * FROM invites')) {
      if (!present.has(row.target)) this.host.sql.run('DELETE FROM invites WHERE id = ?', row.id);
    }
    this.scheduleAlarm();
  }

  stats() {
    return {
      players: this.host.sql.get('SELECT COUNT(*) AS n FROM players').n,
      friendships: this.host.sql.get('SELECT COUNT(*) AS n FROM friends').n / 2,
      matches: this.host.sql.get('SELECT COUNT(*) AS n FROM matches').n,
      player_matches: this.host.sql.get('SELECT COUNT(*) AS n FROM player_matches').n,
    };
  }

  // ---- connection lifecycle ----
  admit(ip, country = '') {
    const group = ipGroup(ip);
    if (!(this.local && isLocalAddress(ip))) {
      const open = this.host.sockets().filter((c) => ipGroup(c.attachment()?.ip) === group).length;
      if (open >= LIMITS.socketsPerIp) {
        this.diagnose('ws_upgrade_ip_cap');
        return null;
      }
    }
    this.diagnose('ws_upgrade_accepted');
    return { cid: this.randomHex(8), ip, opened: this.host.now(), country: countryCode(country) };
  }

  // Upgrades the Worker refused before they reached this object.
  edgeLimited(count) {
    if (count > 0) this.diagnose('ws_upgrade_limited', count);
  }

  message(conn, text) {
    const att = conn.attachment();
    if (!att) return;
    let seq = null;
    this.current = att;
    try {
      if (typeof text !== 'string') fail('bad_request', 'text frames only');
      if (text.length > LIMITS.frameBytes) fail('too_large');
      let msg;
      try {
        msg = JSON.parse(text);
      } catch {
        fail('bad_request', 'invalid JSON');
      }
      if (!isObject(msg) || typeof msg.op !== 'string' || !isInt(msg.seq, 0, 0x7fffffff)) fail('bad_request', 'envelope');
      seq = msg.seq;
      const now = this.host.now();
      const any = takeToken(att.ra, BUCKETS.any, now);
      if (!any) fail('limit', 'slow down');
      att.ra = any;
      if (SOCIAL_OPS.has(msg.op)) {
        const social = takeToken(att.rs, BUCKETS.social, now);
        if (!social) fail('limit', 'slow down');
        att.rs = social;
      }
      if (!Object.hasOwn(HANDLERS, msg.op)) fail('bad_request', 'unknown op');
      if (msg.op !== 'hello' && !att.hello) fail('no_session');
      this.index = null;
      const body = HANDLERS[msg.op].call(this, att, msg, conn) ?? {};
      conn.setAttachment(att);
      conn.send(JSON.stringify({ re: seq, code: 'ok', ...body }));
      this.runDeferred();
      if (GROWING_OPS.has(msg.op)) this.observe();
    } catch (error) {
      this.deferred = null;
      if (error instanceof ProtocolError && REJECT_CODES.has(error.code) && !att.rejected) {
        att.rejected = true;
        this.countReject(error.code, ipGroup(att.ip));
      }
      conn.setAttachment(att);
      if (!(error instanceof ProtocolError)) {
        this.host.log(`hub: internal error in request ${seq}: ${error?.stack ?? error}`);
        conn.send(JSON.stringify({ re: seq, code: 'internal' }));
      } else {
        conn.send(JSON.stringify({ re: seq, code: error.code }));
      }
    } finally {
      this.current = null;
      this.index = null;
    }
  }

  runDeferred() {
    while (this.deferred?.length) {
      const batch = this.deferred;
      this.deferred = null;
      for (const fn of batch) fn();
    }
  }

  // A socket closed: a match seat is held for the grace period; presence ends with the player's last present socket.
  closed(conn) {
    const att = conn.attachment();
    if (att) this.forgetRelayConnection(att.cid);
    if (!att?.hello) return;
    this.index = null;
    if (att.match) {
      const row = this.matchRow(att.match);
      const role = row && this.roleOf(row, att.player);
      if (role && row[`${role}_cid`] === att.cid) {
        this.endRelay(row, 'peer_disconnected', role);
        this.host.sql.run(`UPDATE matches SET ${role}_cid = NULL, ${role}_grace = ? WHERE id = ?`,
          this.host.now() + LIMITS.graceMs, row.id);
        this.sendToRole(row, this.otherRole(role), { ev: 'match.peer_lost', match: row.id });
        this.scheduleAlarm();
      }
    }
    att.match = null;
    att.hello = false;
    conn.setAttachment(att);
    this.index = null;
    if (att.presence && !this.presentSockets(att.player, att.cid).length) {
      const advertised = this.playerMatchOf(att.player);
      if (advertised) this.dropPlayerMatch(advertised, 'host_left');
      this.host.sql.run('DELETE FROM invites WHERE target = ?', att.player);
      this.notifyFriends(att.player, { ev: 'friend.offline', player: att.player });
    }
    this.runDeferred();
    this.index = null;
  }

  alarm() {
    this.alarmAt = null;
    this.index = null;
    const now = this.host.now();
    for (const row of this.host.sql.all('SELECT * FROM player_matches WHERE refreshed + ? <= ?', LIMITS.playerMatchMs, now)) {
      this.dropPlayerMatch(row, 'expired');
      this.later(() => this.notifyStatus(row.owner));
    }
    for (const row of this.host.sql.all('SELECT * FROM invites WHERE created + ? <= ?', LIMITS.inviteMs, now)) {
      this.endInvite(row, 'expired');
    }
    for (const row of this.host.sql.all('SELECT * FROM matches')) {
      const reason = ROLES.some((r) => row[`${r}_grace`] !== null && row[`${r}_grace`] <= now) ? 'disconnect'
        : row.created + LIMITS.matchMaxMs <= now ? 'expired' : null;
      if (reason) this.endMatch(row, reason);
    }
    this.runDeferred();
    this.index = null;
    this.track(() => this.metrics.flushDiagnostics());
    this.observe();
  }

  // An alarm that is already set for the same moment or earlier stays: it fires early at worst, and alarm() sets the next.
  scheduleAlarm() {
    const times = [];
    const advertised = this.host.sql.get('SELECT MIN(refreshed) AS t FROM player_matches');
    if (advertised?.t) times.push(advertised.t + LIMITS.playerMatchMs);
    const invited = this.host.sql.get('SELECT MIN(created) AS t FROM invites');
    if (invited?.t) times.push(invited.t + LIMITS.inviteMs);
    for (const row of this.host.sql.all('SELECT * FROM matches')) {
      times.push(row.created + LIMITS.matchMaxMs);
      for (const role of ROLES) if (row[`${role}_grace`] !== null) times.push(row[`${role}_grace`]);
    }
    if (this.host.sockets().length || this.metrics.pending.size) times.push(sampleBucket(this.host.now()) + SAMPLE_MS);
    if (!times.length) return;
    const at = Math.min(...times);
    if (this.alarmAt !== null && this.alarmAt <= at) return;
    this.alarmAt = at;
    this.host.setAlarm(at);
  }

  // Metrics never fail a hub operation.
  track(fn) {
    try {
      fn();
    } catch (error) {
      this.host.log(`hub: metrics error: ${error?.stack ?? error}`);
    }
  }

  diagnose(name, amount = 1) {
    if (this.metrics.diagnose(name, amount)) this.scheduleAlarm();
  }

  // One rejection per reason and address per window: a reconnect loop must not write on every turn.
  countReject(reason, key) {
    if (this.guards.rejects.take(`${reason}|${key}`, this.host.now())) this.track(() => this.metrics.reject(reason));
  }

  exempt(att) {
    return this.local && isLocalAddress(att.ip);
  }

  observe() {
    this.track(() => this.metrics.observe(this.liveMetrics()));
    this.scheduleAlarm();
  }

  // What is online now, from the open sockets and the match tables: a player with several connections counts once.
  liveMetrics() {
    this.index = null;
    const sockets = this.sockets();
    const players = new Map();
    for (const { att } of sockets) if (att.presence && !players.has(att.player)) players.set(att.player, att);
    const tally = (pick) => {
      const counts = new Map();
      for (const att of players.values()) counts.set(pick(att), (counts.get(pick(att)) ?? 0) + 1);
      return counts;
    };
    const now = this.host.now();
    const rows = this.host.sql.all('SELECT * FROM matches ORDER BY created DESC');
    const transport = new Map();
    let seated = 0;
    for (const row of rows) {
      const kind = transportOf(row);
      transport.set(kind, (transport.get(kind) ?? 0) + 1);
      seated += ROLES.filter((r) => row[`${r}_cid`] !== null).length;
    }
    const advertised = this.host.sql.all('SELECT owner, refreshed FROM player_matches')
      .filter((row) => row.refreshed + LIMITS.playerMatchMs > now && players.has(row.owner)).length;
    return {
      online: players.size,
      in_matches: seated,
      advertised,
      connections: sockets.length,
      matches: rows.length,
      direct: transport.get('direct') ?? 0,
      relay: transport.get('relay') ?? 0,
      pending: transport.get('pending') ?? 0,
      platform: tally((att) => platformCategory(att.platform)),
      version: tally((att) => versionCategory(att.version)),
      country: tally((att) => countryCode(att.country)),
      transport,
      list: rows.slice(0, MATCH_LIST_MAX).map((row) => matchReport(row, now)),
    };
  }

  metricsSummary() {
    return this.metrics.summary(this.liveMetrics());
  }

  metricsHistory(days, recent) {
    return this.metrics.history(days, recent);
  }

  // ---- helpers ----
  meta(key) {
    return this.host.sql.get('SELECT v FROM meta WHERE k = ?', key)?.v ?? null;
  }

  setMeta(key, value) {
    this.host.sql.run('INSERT INTO meta (k, v) VALUES (?, ?) ON CONFLICT(k) DO UPDATE SET v = excluded.v', key, String(value));
  }

  randomHex(bytes) {
    return toHex(crypto.getRandomValues(new Uint8Array(bytes)));
  }

  ipKey(ip) {
    return sha256Hex(`${this.meta('salt')}|${ipGroup(ip)}`).slice(0, 32);
  }

  // Greeted sockets; the one whose message is being handled carries its live attachment, not a stored copy.
  sockets() {
    if (!this.index) {
      this.index = [];
      for (const conn of this.host.sockets()) {
        const stored = conn.attachment();
        const att = stored && this.current && stored.cid === this.current.cid ? this.current : stored;
        if (att?.hello) this.index.push({ conn, att });
      }
    }
    return this.index;
  }

  socketByCid(cid) {
    return cid ? this.sockets().find((s) => s.att.cid === cid) ?? null : null;
  }

  presentSockets(player, exceptCid = null) {
    return this.sockets().filter((s) => s.att.player === player && s.att.presence && s.att.cid !== exceptCid);
  }

  send(conn, event) {
    try {
      conn.send(JSON.stringify(event));
    } catch {
      // A socket that is closing; its close handler runs next.
    }
  }

  later(fn) {
    (this.deferred ??= []).push(fn);
  }

  notifyPlayer(player, event) {
    for (const s of this.sockets()) if (s.att.player === player) this.send(s.conn, event);
  }

  // ---- players and presence ----
  playerRow(player) {
    return this.host.sql.get('SELECT * FROM players WHERE player = ?', player) ?? null;
  }

  stateOf(player) {
    const match = this.matchOfPlayer(player);
    if (match) return match.host_ready && match.join_ready ? 'in_game' : 'connecting';
    if (this.playerMatchOf(player)) return 'player_match';
    return 'online';
  }

  friendView(player, viewer) {
    const row = this.playerRow(player);
    const view = { player, name: row?.name ?? '', online: false, state: 'offline' };
    const present = this.presentSockets(player);
    if (!present.length) return view;
    const att = present[0].att;
    return {
      ...view, online: true, state: this.stateOf(player), platform: att.platform, version: att.version, guest: att.guest,
      build: att.build, compatible: viewer ? compatibleGuests(viewer, att) : true,
    };
  }

  friendsOf(player) {
    return this.host.sql.all('SELECT friend FROM friends WHERE player = ? ORDER BY friend', player).map((r) => r.friend);
  }

  areFriends(a, b) {
    return Boolean(this.host.sql.get('SELECT 1 AS x FROM friends WHERE player = ? AND friend = ?', a, b));
  }

  notifyFriends(player, event) {
    for (const friend of this.friendsOf(player)) this.notifyPlayer(friend, event);
  }

  notifyStatus(player) {
    for (const friend of this.friendsOf(player)) {
      for (const s of this.presentSockets(friend)) {
        this.send(s.conn, { ev: 'friend.status', friend: this.friendView(player, s.att) });
      }
    }
  }

  // ---- friends ----
  friendCount(player) {
    return this.host.sql.get('SELECT COUNT(*) AS n FROM friends WHERE player = ?', player).n;
  }

  expireRequests() {
    this.host.sql.run('DELETE FROM friend_requests WHERE ms < ?', this.host.now() - LIMITS.requestDays * DAY_MS);
  }

  makeFriends(a, b) {
    const now = this.host.now();
    this.host.sql.run('INSERT OR IGNORE INTO friends (player, friend, since) VALUES (?, ?, ?)', a, b, now);
    this.host.sql.run('INSERT OR IGNORE INTO friends (player, friend, since) VALUES (?, ?, ?)', b, a, now);
    this.host.sql.run('DELETE FROM friend_requests WHERE (sender = ? AND target = ?) OR (sender = ? AND target = ?)', a, b, b, a);
  }

  // Counted once the token matched, so nobody can lock out another player by guessing at the account.
  limitHello(att, player, now) {
    if (this.exempt(att) || this.guards.hello.take(player, now)) return;
    this.diagnose('hello_limited');
    fail('limit', 'too many sign-ins');
  }

  touchPlayer(row, name, now, newSession) {
    if (newSession) {
      this.host.sql.run('UPDATE players SET name = ?, seen = ?, session = ? WHERE player = ?', name, now, now, row.player);
    } else if (row.name !== name || now - row.seen >= PROTECTION.seenRefreshMs) {
      this.host.sql.run('UPDATE players SET name = ?, seen = ? WHERE player = ?', name, now, row.player);
    }
  }

  limitAdvertise(att) {
    if (this.exempt(att)) return;
    const now = this.host.now();
    const address = ipGroup(att.ip);
    const { advertiseAccount, advertiseAddress } = this.guards;
    if (!advertiseAccount.allows(att.player, now) || !advertiseAddress.allows(address, now)) {
      this.diagnose('pm_host_limited');
      fail('limit', 'hosting too fast');
    }
    advertiseAccount.take(att.player, now);
    advertiseAddress.take(address, now);
  }

  limitJoin(att) {
    if (this.exempt(att) || this.guards.joinAccount.take(att.player, this.host.now())) return;
    this.diagnose('pm_join_limited');
    fail('limit', 'joining too fast');
  }

  limitResume(att) {
    if (this.exempt(att) || this.guards.resumeAccount.take(att.player, this.host.now())) return;
    this.diagnose('match_resume_limited');
    fail('limit', 'resuming too fast');
  }

  limitInvite(att) {
    if (this.exempt(att)) return;
    const now = this.host.now();
    const address = ipGroup(att.ip);
    const { inviteAccount, inviteAddress } = this.guards;
    if (!inviteAccount.allows(att.player, now) || !inviteAddress.allows(address, now)) {
      this.diagnose('invite_limited');
      fail('limit', 'inviting too fast');
    }
    inviteAccount.take(att.player, now);
    inviteAddress.take(address, now);
  }

  requireFriendRoom(a, b) {
    if (this.friendCount(a) >= LIMITS.friends || this.friendCount(b) >= LIMITS.friends) fail('limit', 'friend list full');
  }

  // ---- Player Match advertisements ----
  playerMatchOf(owner) {
    return this.host.sql.get('SELECT * FROM player_matches WHERE owner = ?', owner) ?? null;
  }

  // What a friend's search shows; never the owner's address.
  playerMatchView(row) {
    const owner = this.presentSockets(row.owner)[0]?.att;
    return { session: row.id, player: row.owner, name: this.playerRow(row.owner)?.name ?? '', guest: row.guest,
      build: row.build, slots: row.slots, rules: JSON.parse(row.rules), qos: row.qos, platform: owner?.platform ?? '',
      version: owner?.version ?? '' };
  }

  // Invites to an advertisement end with it.
  dropPlayerMatch(row, reason) {
    this.host.sql.run('DELETE FROM player_matches WHERE id = ?', row.id);
    for (const invite of this.host.sql.all('SELECT * FROM invites WHERE session = ?', row.id)) this.endInvite(invite, reason);
  }

  endInvite(invite, reason) {
    this.host.sql.run('DELETE FROM invites WHERE id = ?', invite.id);
    this.notifyPlayer(invite.target, { ev: 'invite.ended', invite: invite.id, session: invite.session, reason });
  }

  // Live while its owner is present and keeps refreshing it.
  livePlayerMatch(row) {
    return row.refreshed + LIMITS.playerMatchMs > this.host.now() && this.presentSockets(row.owner).length > 0;
  }

  // Both sockets are known here, so a match between two relay-capable clients has its relay from the start, while
  // they still try to connect directly.
  createMatch(host, joiner) {
    const id = this.randomHex(8);
    const token = host.att.relay && joiner.att.relay ? relayToken() : 0;
    this.host.sql.run(`INSERT INTO matches (id, host, joiner, guest, build, created, host_cid, join_cid, relay_token,
      host_secret, join_secret) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`, id, host.att.player, joiner.att.player,
    joiner.att.guest, joiner.att.build, this.host.now(), host.att.cid, joiner.att.cid, token, secretMask(host.att.secret),
    secretMask(joiner.att.secret));
    this.track(() => this.metrics.count('matches_started'));
    this.host.sql.run(`INSERT INTO match_telemetry (match, guest, host_platform, join_platform, reporting) VALUES (?, ?, ?, ?, ?)`,
      id, joiner.att.guest, platformCategory(host.att.platform), platformCategory(joiner.att.platform),
      host.att.telemetry || joiner.att.telemetry ? 1 : 0);
    joiner.att.match = id;
    host.att.match = id;
    host.conn.setAttachment(host.att);
    if (token) {
      for (const role of ROLES) this.host.log(`relay attach match=${id} role=${role}`);
      this.host.log(`relay ready match=${id}`);
    }
    this.scheduleAlarm();
    return this.matchRow(id);
  }

  // ---- matches ----
  matchRow(id) {
    return this.host.sql.get('SELECT * FROM matches WHERE id = ?', id) ?? null;
  }

  matchOfPlayer(player) {
    return this.host.sql.get('SELECT * FROM matches WHERE host = ? OR joiner = ?', player, player) ?? null;
  }

  roleOf(row, player) {
    return row.host === player ? 'host' : row.joiner === player ? 'join' : null;
  }

  otherRole(role) {
    return role === 'host' ? 'join' : 'host';
  }

  playerOfRole(row, role) {
    return role === 'host' ? row.host : row.joiner;
  }

  sendToRole(row, role, event) {
    const s = this.socketByCid(row[`${role}_cid`]);
    if (s) this.send(s.conn, event);
  }

  matchView(row, role) {
    const peer = this.playerRow(this.playerOfRole(row, this.otherRole(role)));
    return { match: row.id, role, guest: row.guest, build: row.build, peer: { player: peer?.player ?? '', name: peer?.name ?? '' },
      peer_lost: row[`${this.otherRole(role)}_cid`] === null, ...(row.relay_token ? { relay_token: row.relay_token } : {}),
      secret: secretView(row, role) };
  }

  attachMatch(cid, id) {
    const s = this.socketByCid(cid);
    if (!s) return;
    s.att.match = id;
    s.conn.setAttachment(s.att);
  }

  telemetryOf(id) {
    return this.host.sql.get('SELECT * FROM match_telemetry WHERE match = ?', id) ?? null;
  }

  // The closed match as the recent-matches list keeps it: no id, no player, no address.
  closeTelemetry(row, reason) {
    const tele = this.telemetryOf(row.id);
    if (!tele) return;
    const known = transportOf(row);
    const reported = tele.reporting === 1;
    const settled = tele.established !== null;
    let state;
    if (reason === 'disconnect') state = 'Disconnected';
    else if (reason === 'expired') state = 'Timeout';
    else if (!settled && !tele.fights_started) state = 'Failed';
    else if (!reported || !tele.fights_started) state = 'Ended';
    else if (tele.fights_completed === tele.fights_started) state = 'Completed';
    else state = 'Abandoned';
    this.track(() => this.metrics.recordMatch({
      duration_ms: Math.max(0, this.host.now() - (tele.established ?? row.created)),
      fights_started: reported ? tele.fights_started : null,
      fights_completed: reported ? tele.fights_completed : null,
      transport: tele.transport || (known === 'pending' ? 'unknown' : known),
      guest: row.guest,
      platforms: `${tele.host_platform} x ${tele.join_platform}`,
      state,
    }));
    this.host.sql.run('DELETE FROM match_fights WHERE match = ?', row.id);
    this.host.sql.run('DELETE FROM match_telemetry WHERE match = ?', row.id);
  }

  // A secret character can start a fight only when both seats support and enabled it; violent_ken never can.
  requireSecretCharacters(row, event) {
    const effective = effectiveSecret(secretFromMask(row.host_secret), secretFromMask(row.join_secret));
    for (const id of [event.p1, event.p2]) {
      const key = SECRET_CHARACTER[id];
      if (key && effective[key] !== true) {
        this.diagnose('fight_secret_rejected');
        fail('bad_request', 'character not available');
      }
    }
  }

  // Each secret character counts once per match, at the first fight that includes it.
  countSecretMatch(tele, event) {
    for (const key of SECRET_KEYS) {
      if (tele[`secret_${key}`] || ![event.p1, event.p2].some((id) => SECRET_CHARACTER[id] === key)) continue;
      this.host.sql.run(`UPDATE match_telemetry SET secret_${key} = 1 WHERE match = ?`, tele.match);
      this.track(() => this.metrics.secretMatch(key));
    }
  }

  // The match's transport counts once, whichever of the host's choice and its first fight names it first.
  countTransport(row, transport) {
    const tele = this.telemetryOf(row.id);
    if (!tele || tele.transport_counted) return;
    this.host.sql.run('UPDATE match_telemetry SET transport = ?, transport_counted = 1 WHERE match = ?', transport, row.id);
    this.track(() => this.metrics.transport(transport));
  }

  endMatch(row, reason, actor = null) {
    this.closeTelemetry(row, reason);
    if (row.relay_token) this.host.log(`relay close match=${row.id} reason=${reason}`);
    this.forgetRelayMatch(row.id);
    this.host.sql.run('DELETE FROM matches WHERE id = ?', row.id);
    for (const role of ROLES) {
      const player = this.playerOfRole(row, role);
      for (const s of this.sockets()) {
        if (s.att.match !== row.id) continue;
        s.att.match = null;
        s.conn.setAttachment(s.att);
        if (s.att.cid !== actor) this.send(s.conn, { ev: 'match.ended', match: row.id, reason });
      }
      this.later(() => this.notifyStatus(player));
    }
  }

  introduce(row) {
    if (!row.host_port || !row.join_port) return;
    if (!row.introduced) this.host.sql.run('UPDATE matches SET introduced = 1 WHERE id = ?', row.id);
    for (const role of ROLES) {
      const other = this.otherRole(role);
      const peer = this.playerRow(this.playerOfRole(row, other));
      this.sendToRole(row, role, {
        ev: 'match.peer', match: row.id, player: peer?.player, name: peer?.name, role: other,
        ip: row[`${other}_ip`], port: row[`${other}_port`], lan: JSON.parse(row[`${other}_lan`]),
        same_network: row[`${other}_ip`] === row[`${role}_ip`], public_endpoints: true,
        ...(row[`${other}_pub_port`] ? { public_ip: row[`${other}_pub_ip`], public_port: row[`${other}_pub_port`] } : {}),
      });
    }
  }

  // ---- relay ----
  // One binary frame: forwarded unchanged to the other socket of the sender's own current match, never anywhere the
  // frame could name.
  relay(conn, bytes) {
    const att = conn.attachment();
    if (!att) return this.dropRelay('unknown_connection');
    if (this.relayFaults.get(att.cid)?.abusive) return;
    if (!att.hello) return this.senderFault(conn, att, 'unknown_connection');
    const frame = parseRelayFrame(bytes);
    if (frame.error) return this.senderFault(conn, att, frame.error, att.match);
    const { route, fault } = this.relayRouteFor(att);
    if (!route) return this.senderFault(conn, att, 'no_relay', att.match, { tell: true, count: fault });
    if (frame.token !== route.token) return this.senderFault(conn, att, 'stale_token', route.match, { tell: true });
    const now = this.host.now();
    const shared = this.relayMatch(route.match);
    if (!takeRelayBudget(route.budget, RELAY.connection, now, bytes.length) ||
        !takeRelayBudget(shared.budget, RELAY.match, now, bytes.length)) {
      this.dropRelay('rate_limit', route.match);
      if (++shared.overruns >= RELAY.overrunsToClose) {
        this.diagnose('relay_abuse_closed');
        this.endRelay(this.matchRow(route.match), 'rate_limit');
      }
      return;
    }
    const buffered = route.peer.bufferedAmount;
    if (typeof buffered === 'number' && buffered > RELAY.bufferedBytes) {
      this.endRelay(this.matchRow(route.match), 'peer_backpressure');
      return;
    }
    try {
      route.peer.sendBinary(bytes);
    } catch {
      this.endRelay(this.matchRow(route.match), 'peer_unavailable');
      return;
    }
    route.forwards += 1;
    if (route.forwards <= RELAY.loggedForwards) {
      const direction = route.role === 'host' ? 'host_to_join' : 'join_to_host';
      this.host.log(`relay forward match=${route.match} direction=${direction} bytes=${bytes.length}`);
    }
  }

  // From the match row on a miss only; later frames take the cached peer socket. Without a route, `fault` says whether
  // the sender is to blame: a peer that is away (grace, resuming) or a relay that closed with it is not the sender's doing.
  relayRouteFor(att) {
    const cached = this.relayRoutes.get(att.cid);
    if (cached && cached.match === att.match) return { route: cached };
    this.relayRoutes.delete(att.cid);
    const row = att.match ? this.matchRow(att.match) : null;
    if (!row) return { fault: true };
    const role = this.roleOf(row, att.player);
    if (!role || row[`${role}_cid`] !== att.cid) return { fault: true };
    if (!row.relay_token) return { fault: ROLES.every((r) => row[`${r}_cid`] !== null) };
    this.index = null;
    const peer = this.socketByCid(row[`${this.otherRole(role)}_cid`]);
    this.index = null;
    if (!peer) return { fault: false };
    const route = { match: row.id, token: row.relay_token, role, peerCid: peer.att.cid, peer: peer.conn, budget: {},
      forwards: 0 };
    this.relayRoutes.set(att.cid, route);
    return { route };
  }

  relayMatch(id) {
    let shared = this.relayMatches.get(id);
    if (!shared) {
      shared = { budget: {}, overruns: 0 };
      this.relayMatches.set(id, shared);
    }
    return shared;
  }

  // The first few drops of each reason are logged, then one in a thousand. A sender whose match has no relay (any
  // more) hears it once per match.
  dropRelay(reason, match = null, att = null) {
    const count = (this.relayDrops.get(reason) ?? 0) + 1;
    this.relayDrops.set(reason, count);
    if (count <= 5 || count % 1000 === 0) this.host.log(`relay drop reason=${reason} match=${match ?? '-'} count=${count}`);
    if (!att?.match) return;
    const told = `${att.cid}:${att.match}`;
    if (this.relayTold.has(told)) return;
    if (this.relayTold.size >= 1024) this.relayTold.clear();
    this.relayTold.add(told);
    this.index = null;
    const s = this.socketByCid(att.cid);
    this.index = null;
    if (s) this.send(s.conn, { ev: 'relay.closed', match: att.match, reason: 'unavailable' });
  }

  // Drops the frame; a sender that keeps producing frames no well-behaved client sends loses its connection.
  senderFault(conn, att, reason, match = null, { tell = false, count = true } = {}) {
    this.dropRelay(reason, match, tell ? att : null);
    if (!count) return;
    const state = this.relayFaults.get(att.cid) ?? { bucket: null, abusive: false };
    state.bucket = takeToken(state.bucket, RELAY.invalid, this.host.now());
    this.relayFaults.set(att.cid, state);
    if (state.bucket) return;
    state.abusive = true;
    this.host.log(`relay sender closed cid=${att.cid} reason=${reason}`);
    this.diagnose('relay_abuse_closed');
    try {
      conn.close(1008, 'relay abuse');
    } catch {
      // Already closing.
    }
  }

  endRelay(row, reason, notifyExcept = null, notify = true) {
    if (!row?.relay_token) return;
    this.host.sql.run(`UPDATE matches SET relay_token = 0, transport = CASE WHEN transport = 'relay' THEN '' ELSE transport END
      WHERE id = ?`, row.id);
    this.forgetRelayMatch(row.id);
    this.host.log(`relay close match=${row.id} reason=${reason}`);
    if (!notify) return;
    for (const role of ROLES) {
      if (role !== notifyExcept) this.sendToRole(row, role, { ev: 'relay.closed', match: row.id, reason });
    }
  }

  forgetRelayMatch(id) {
    for (const [cid, route] of this.relayRoutes) if (route.match === id) this.relayRoutes.delete(cid);
    this.relayMatches.delete(id);
  }

  forgetRelayConnection(cid) {
    this.relayFaults.delete(cid);
    for (const [key, route] of this.relayRoutes) {
      if (key === cid || route.peerCid === cid) this.relayRoutes.delete(key);
    }
  }
}

const HANDLERS = {
  hello(att, msg) {
    if (att.hello) fail('bad_request', 'already greeted');
    const hello = parseHello(msg);
    const now = this.host.now();
    const row = this.playerRow(hello.player);
    if (row && row.token_hash !== hello.hash) fail('unauthorized');
    this.limitHello(att, hello.player, now);
    let renamed = false;
    let newSession = hello.presence;
    if (!row) {
      const ipKey = this.ipKey(att.ip);
      if (!(this.local && isLocalAddress(att.ip))) {
        this.host.sql.run('DELETE FROM registrations WHERE ms < ?', now - DAY_MS);
        const recent = this.host.sql.get('SELECT COUNT(*) AS n FROM registrations WHERE ip = ?', ipKey).n;
        if (recent >= LIMITS.registrationsPerIpPerDay) fail('limit', 'too many new players');
      }
      this.host.sql.run('INSERT INTO players (player, token_hash, name, created, seen, session) VALUES (?, ?, ?, ?, ?, ?)',
        hello.player, hello.hash, hello.name, now, now, hello.presence ? now : 0);
      this.host.sql.run('INSERT INTO registrations (ip, ms) VALUES (?, ?)', ipKey, now);
    } else {
      renamed = row.name !== hello.name;
      newSession = hello.presence && now - row.session >= PROTECTION.sessionWindowMs;
      this.touchPlayer(row, hello.name, now, newSession);
    }
    const wasOnline = hello.presence && this.presentSockets(hello.player).length > 0;
    Object.assign(att, { hello: true, player: hello.player, name: hello.name, presence: hello.presence, guest: hello.guest,
      build: hello.build, version: hello.version, platform: hello.platform, relay: hello.relay, telemetry: hello.telemetry,
      secret: hello.secret, match: null });
    if (newSession) this.track(() => this.metrics.session(att));
    else if (hello.presence) this.diagnose('hello_reconnect_deduped');
    this.diagnose('hello_accepted');
    this.index = null;
    if (hello.presence ? !wasOnline || renamed : renamed) this.later(() => this.notifyStatus(hello.player));
    return { player: hello.player, name: hello.name, friend_code: hello.player, time: now, capabilities: CAPABILITIES };
  },

  ping() {
    return { time: this.host.now() };
  },

  'friends.list'(att) {
    return { friends: this.friendsOf(att.player).map((f) => this.friendView(f, att)) };
  },

  'friends.requests'(att) {
    this.expireRequests();
    const named = (rows, key) => rows.map((r) => ({ player: r[key], name: this.playerRow(r[key])?.name ?? '', ms: r.ms }));
    return {
      incoming: named(this.host.sql.all('SELECT sender, ms FROM friend_requests WHERE target = ? ORDER BY ms', att.player), 'sender'),
      outgoing: named(this.host.sql.all('SELECT target, ms FROM friend_requests WHERE sender = ? ORDER BY ms', att.player), 'target'),
    };
  },

  'friends.add'(att, msg) {
    const code = normalizeFriendCode(msg.code);
    const target = this.playerRow(code);
    if (!target) fail('not_found');
    if (target.player === att.player) fail('bad_request', 'own code');
    const result = { player: target.player, name: target.name };
    if (this.areFriends(att.player, target.player)) return { ...result, state: 'friends' };
    this.expireRequests();
    if (this.host.sql.get('SELECT 1 AS x FROM friend_requests WHERE sender = ? AND target = ?', target.player, att.player)) {
      this.requireFriendRoom(att.player, target.player);
      this.makeFriends(att.player, target.player);
      for (const s of this.presentSockets(target.player)) {
        this.send(s.conn, { ev: 'friend.added', friend: this.friendView(att.player, s.att) });
      }
      return { ...result, state: 'friends' };
    }
    if (this.host.sql.get('SELECT 1 AS x FROM friend_requests WHERE sender = ? AND target = ?', att.player, target.player)) {
      return { ...result, state: 'pending' };
    }
    const outgoing = this.host.sql.get('SELECT COUNT(*) AS n FROM friend_requests WHERE sender = ?', att.player).n;
    const incoming = this.host.sql.get('SELECT COUNT(*) AS n FROM friend_requests WHERE target = ?', target.player).n;
    if (outgoing >= LIMITS.pendingOut || incoming >= LIMITS.pendingIn) fail('limit', 'too many pending requests');
    this.requireFriendRoom(att.player, target.player);
    this.host.sql.run('INSERT INTO friend_requests (sender, target, ms) VALUES (?, ?, ?)', att.player, target.player, this.host.now());
    this.notifyPlayer(target.player, { ev: 'friend.request', player: att.player, name: att.name });
    return { ...result, state: 'pending' };
  },

  'friends.accept'(att, msg) {
    const sender = playerOf(msg.player);
    this.expireRequests();
    if (!this.host.sql.get('SELECT 1 AS x FROM friend_requests WHERE sender = ? AND target = ?', sender, att.player)) fail('not_found');
    this.requireFriendRoom(att.player, sender);
    this.makeFriends(att.player, sender);
    for (const s of this.presentSockets(sender)) {
      this.send(s.conn, { ev: 'friend.added', friend: this.friendView(att.player, s.att) });
    }
    return { friend: this.friendView(sender, att) };
  },

  'friends.decline'(att, msg) {
    const other = playerOf(msg.player);
    const pair = [other, att.player, att.player, other];
    if (!this.host.sql.get(`SELECT 1 AS x FROM friend_requests WHERE (sender = ? AND target = ?) OR (sender = ? AND target = ?)`, ...pair)) {
      fail('not_found');
    }
    this.host.sql.run('DELETE FROM friend_requests WHERE (sender = ? AND target = ?) OR (sender = ? AND target = ?)', ...pair);
    return {};
  },

  'friends.remove'(att, msg) {
    const other = playerOf(msg.player);
    const were = this.areFriends(att.player, other);
    this.host.sql.run('DELETE FROM friends WHERE (player = ? AND friend = ?) OR (player = ? AND friend = ?)', att.player, other,
      other, att.player);
    this.host.sql.run('DELETE FROM friend_requests WHERE (sender = ? AND target = ?) OR (sender = ? AND target = ?)', other,
      att.player, att.player, other);
    if (were) this.notifyPlayer(other, { ev: 'friend.removed', player: att.player });
    return {};
  },

  // Advertises (or keeps alive) the sender's Player Match to compatible online friends.
  'pm.host'(att, msg) {
    if (!att.presence) fail('bad_request', 'presence required');
    if (this.matchOfPlayer(att.player)) fail('busy');
    const slots = msg.slots ?? 2;
    if (slots !== 2) fail('bad_request', 'two-player matches only');
    const rules = msg.rules ?? {};
    if (!isObject(rules) || Object.keys(rules).length > LIMITS.ruleFields) fail('bad_request', 'rules expected');
    for (const [key, value] of Object.entries(rules)) {
      if (!RULE_KEY.test(key) || !isInt(value, 0, 255)) fail('bad_request', 'rule expected');
    }
    const qos = msg.qos ?? '';
    if (typeof qos !== 'string' || !QOS_DATA.test(qos)) fail('bad_request', 'qos expected');
    const now = this.host.now();
    const existing = this.playerMatchOf(att.player);
    const rulesText = JSON.stringify(rules);
    if (existing) {
      const unchanged = existing.guest === att.guest && existing.build === att.build && existing.slots === slots &&
        existing.rules === rulesText && existing.qos === qos;
      if (!unchanged || now - existing.refreshed >= PROTECTION.advertiseRefreshMs) {
        this.host.sql.run('UPDATE player_matches SET guest = ?, build = ?, slots = ?, rules = ?, qos = ?, refreshed = ? WHERE id = ?',
          att.guest, att.build, slots, rulesText, qos, now, existing.id);
      }
      this.scheduleAlarm();
      return { session: existing.id };
    }
    this.limitAdvertise(att);
    const id = this.randomHex(8);
    this.host.sql.run(`INSERT INTO player_matches (id, owner, guest, build, slots, rules, qos, created, refreshed)
      VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)`, id, att.player, att.guest, att.build, slots, rulesText, qos, now, now);
    this.diagnose('pm_host_accepted');
    this.track(() => this.metrics.count('advertised'));
    this.later(() => this.notifyStatus(att.player));
    this.scheduleAlarm();
    return { session: id };
  },

  'pm.cancel'(att) {
    const row = this.playerMatchOf(att.player);
    if (!row) return {};
    this.dropPlayerMatch(row, 'cancelled');
    this.later(() => this.notifyStatus(att.player));
    return {};
  },

  // Live Player Matches owned by friends running the same guest image and netplay build.
  'pm.search'(att) {
    const friends = new Set(this.friendsOf(att.player));
    const sessions = [];
    for (const row of this.host.sql.all('SELECT * FROM player_matches ORDER BY created')) {
      if (!friends.has(row.owner) || !compatibleGuests(att, row) || !this.livePlayerMatch(row)) continue;
      sessions.push(this.playerMatchView(row));
      if (sessions.length >= LIMITS.searchResults) break;
    }
    return { sessions };
  },

  // The owner is Host / Player 1, the joiner Join / Player 2.
  'pm.join'(att, msg, conn) {
    if (!att.presence) fail('bad_request', 'presence required');
    const row = typeof msg.session === 'string'
      ? this.host.sql.get('SELECT * FROM player_matches WHERE id = ?', msg.session) ?? null : null;
    if (!row) fail('not_found');
    if (row.owner === att.player) fail('bad_request', 'own session');
    if (!this.areFriends(att.player, row.owner)) fail('not_friends');
    if (!this.livePlayerMatch(row)) {
      this.dropPlayerMatch(row, 'host_left');
      fail('offline');
    }
    if (!compatibleGuests(att, row)) fail('version_mismatch');
    if (this.matchOfPlayer(att.player) || this.matchOfPlayer(row.owner)) fail('busy');
    this.limitJoin(att);
    const owner = this.presentSockets(row.owner)[0];
    const own = this.playerMatchOf(att.player);
    if (own) this.dropPlayerMatch(own, 'cancelled');
    this.dropPlayerMatch(row, 'started');
    const created = this.createMatch(owner, { conn, att });
    this.send(owner.conn, { ev: 'match.created', session: row.id, ...this.matchView(created, 'host') });
    this.later(() => {
      this.notifyStatus(row.owner);
      this.notifyStatus(att.player);
    });
    return { session: row.id, ...this.matchView(created, 'join') };
  },

  // Invites one online friend to the sender's live Player Match; the friend joins it with pm.join.
  'invite.send'(att, msg) {
    if (!att.presence) fail('bad_request', 'presence required');
    const target = playerOf(msg.player);
    const row = this.playerMatchOf(att.player);
    if (!row || !this.livePlayerMatch(row)) fail('not_found');
    if (!this.areFriends(att.player, target)) fail('not_friends');
    const recipient = this.presentSockets(target)[0];
    if (!recipient) fail('offline');
    if (!compatibleGuests(recipient.att, row)) fail('version_mismatch');
    if (this.matchOfPlayer(target)) fail('busy');
    this.limitInvite(att);
    const now = this.host.now();
    const id = this.randomHex(8);
    this.host.sql.run('DELETE FROM invites WHERE session = ? AND target = ?', row.id, target);
    this.host.sql.run('INSERT INTO invites (id, session, sender, target, created) VALUES (?, ?, ?, ?, ?)', id, row.id,
      att.player, target, now);
    this.scheduleAlarm();
    this.notifyPlayer(target, { ev: 'invite', invite: { id, ...this.playerMatchView(row), expires: now + LIMITS.inviteMs } });
    return { invite: id, session: row.id };
  },

  'invite.decline'(att, msg) {
    const invite = typeof msg.invite === 'string'
      ? this.host.sql.get('SELECT * FROM invites WHERE id = ? AND target = ?', msg.invite, att.player) : null;
    if (!invite) return {};
    this.host.sql.run('DELETE FROM invites WHERE id = ?', invite.id);
    this.notifyPlayer(invite.sender, { ev: 'invite.ended', invite: invite.id, session: invite.session, reason: 'declined' });
    return {};
  },

  'match.endpoint'(att, msg) {
    const row = att.match && this.matchRow(att.match);
    if (!row || row.id !== msg.match) fail('not_found');
    const role = this.roleOf(row, att.player);
    const endpoint = parseEndpoint(msg);
    const lan = JSON.stringify(endpoint.lan);
    const unchanged = row[`${role}_ip`] === att.ip && row[`${role}_pub_ip`] === endpoint.publicIp &&
      row[`${role}_pub_port`] === endpoint.publicPort && row[`${role}_port`] === endpoint.port && row[`${role}_lan`] === lan;
    if (!unchanged) {
      this.host.sql.run(
        `UPDATE matches SET ${role}_ip = ?, ${role}_pub_ip = ?, ${role}_pub_port = ?, ${role}_port = ?, ${role}_lan = ? WHERE id = ?`,
        att.ip, endpoint.publicIp, endpoint.publicPort, endpoint.port, lan, row.id);
    }
    this.later(() => this.introduce(this.matchRow(row.id)));
    return {};
  },

  'match.ready'(att, msg) {
    const row = att.match && this.matchRow(att.match);
    if (!row || row.id !== msg.match) fail('not_found');
    if (typeof msg.ready !== 'boolean') fail('bad_request', 'ready expected');
    const role = this.roleOf(row, att.player);
    if (row[`${role}_ready`] !== (msg.ready ? 1 : 0)) {
      this.host.sql.run(`UPDATE matches SET ${role}_ready = ? WHERE id = ?`, msg.ready ? 1 : 0, row.id);
      if (msg.ready && row[`${this.otherRole(role)}_ready`] && this.telemetryOf(row.id)?.established === null) {
        this.host.sql.run('UPDATE match_telemetry SET established = COALESCE(established, ?) WHERE match = ?',
          this.host.now(), row.id);
        this.track(() => this.metrics.secretEstablished(secretEstablishment(secretFromMask(row.host_secret),
          secretFromMask(row.join_secret))));
      }
    }
    this.later(() => {
      this.sendToRole(row, this.otherRole(role), { ev: 'match.ready', match: row.id, ready: msg.ready });
      this.notifyStatus(row.host);
      this.notifyStatus(row.joiner);
    });
    return {};
  },

  // The host's choice, made once per match before either game is told it is connected; the joiner follows it. Relay
  // needs the match's relay and the joiner's socket; direct closes the relay.
  'match.transport'(att, msg) {
    const row = att.match && this.matchRow(att.match);
    if (!row || row.id !== msg.match) fail('not_found');
    if (this.roleOf(row, att.player) !== 'host') fail('bad_request', 'the host chooses the transport');
    if (msg.transport === 'relay') {
      if (!row.relay_token || !this.socketByCid(row.join_cid)) fail('relay_unavailable');
      this.host.log(`relay transport selected match=${row.id}`);
    } else if (msg.transport === 'direct') {
      this.later(() => this.endRelay(this.matchRow(row.id), 'direct_selected', null, false));
    } else {
      fail('bad_request', 'transport expected');
    }
    if (!row.transport) this.countTransport(row, msg.transport);
    if (row.transport !== msg.transport) this.host.sql.run('UPDATE matches SET transport = ? WHERE id = ?', msg.transport, row.id);
    this.later(() => this.sendToRole(row, 'join', { ev: 'match.transport', match: row.id, transport: msg.transport }));
    return {};
  },

  // The host's passive account of its fights; a retry, a resend or the joiner's copy of an event changes nothing.
  'telemetry.fight'(att, msg) {
    const row = att.match && this.matchRow(att.match);
    if (!row || row.id !== msg.match) fail('not_found');
    const tele = this.telemetryOf(row.id);
    if (!tele) fail('not_found');
    const event = parseFightEvent(msg);
    if (event.build !== row.guest) fail('bad_request', 'build does not match the match');
    const fight = this.host.sql.get('SELECT * FROM match_fights WHERE match = ? AND fight = ?', row.id, event.fight);
    if (event.kind === 'start') {
      if (fight) return { counted: false, reason: 'duplicate' };
      if (tele.fights_started >= LIMITS.fightsPerMatch) fail('limit', 'too many fights');
      this.requireSecretCharacters(row, event);
      const known = transportOf(row);
      const transport = known === 'pending' ? event.transport : known;
      this.host.sql.run('INSERT INTO match_fights (match, fight, p1, p2) VALUES (?, ?, ?, ?)', row.id, event.fight, event.p1,
        event.p2);
      this.host.sql.run('UPDATE match_telemetry SET fights_started = fights_started + 1, reporting = 1 WHERE match = ?', row.id);
      this.countTransport(row, transport);
      this.track(() => this.metrics.fightStarted({ p1: event.p1, p2: event.p2, transport, guest: row.guest,
        platforms: [tele.host_platform, tele.join_platform] }));
      this.countSecretMatch(tele, event);
      return { counted: true };
    }
    if (!fight) fail('bad_request', 'fight not started');
    if (fight.completed) return { counted: false, reason: 'duplicate' };
    if (fight.p1 !== event.p1 || fight.p2 !== event.p2) fail('bad_request', 'characters differ from the start');
    this.host.sql.run('UPDATE match_fights SET completed = 1 WHERE match = ? AND fight = ?', row.id, event.fight);
    this.host.sql.run('UPDATE match_telemetry SET fights_completed = fights_completed + 1 WHERE match = ?', row.id);
    this.track(() => this.metrics.fightCompleted({ p1: event.p1, p2: event.p2, result: event.result }));
    return { counted: true };
  },

  // The sender's secret character capabilities; a change inside a match reaches both seats.
  'secret.update'(att, msg) {
    const secret = normalizeSecret(msg.secret);
    const mask = secretMask(secret);
    const changed = mask !== secretMask(att.secret);
    if (changed) att.secret = secret;
    let row = att.match && this.matchRow(att.match);
    const role = row && this.roleOf(row, att.player);
    if (!role) return {};
    if (changed) {
      this.host.sql.run(`UPDATE matches SET ${role}_secret = ? WHERE id = ?`, mask, row.id);
      row = this.matchRow(row.id);
      const fresh = row;
      this.later(() => {
        for (const seat of ROLES) {
          this.sendToRole(fresh, seat, { ev: 'match.secret', match: fresh.id, secret: secretView(fresh, seat) });
        }
      });
    }
    return { secret: secretView(row, role) };
  },

  'match.leave'(att, msg) {
    const row = att.match && this.matchRow(att.match);
    if (!row || row.id !== msg.match) return {};
    this.endMatch(row, 'left', att.cid);
    att.match = null;
    return {};
  },

  // Retakes a seat held after a lost connection; the peer's endpoint is sent again.
  'match.resume'(att, msg) {
    if (att.match) fail('bad_request', 'already in a match');
    const row = typeof msg.match === 'string' ? this.matchRow(msg.match) : null;
    const role = row && this.roleOf(row, att.player);
    if (!role || !att.presence) fail('not_found');
    this.limitResume(att);
    const old = this.socketByCid(row[`${role}_cid`]);
    if (old && old.att.cid !== att.cid) {
      old.att.match = null;
      old.conn.setAttachment(old.att);
      try {
        old.conn.close(4001, 'seat taken by a new connection');
      } catch {
        // Already closing.
      }
    }
    // A relay never outlives the socket it was set up on.
    this.endRelay(row, 'resumed', role);
    this.host.sql.run(`UPDATE matches SET ${role}_cid = ?, ${role}_grace = NULL WHERE id = ?`, att.cid, row.id);
    const mask = secretMask(att.secret);
    const secretChanged = row[`${role}_secret`] !== mask;
    if (secretChanged) this.host.sql.run(`UPDATE matches SET ${role}_secret = ? WHERE id = ?`, mask, row.id);
    att.match = row.id;
    const fresh = this.matchRow(row.id);
    this.later(() => {
      this.sendToRole(fresh, this.otherRole(role), { ev: 'match.peer_back', match: fresh.id });
      if (secretChanged) {
        this.sendToRole(fresh, this.otherRole(role), { ev: 'match.secret', match: fresh.id,
          secret: secretView(fresh, this.otherRole(role)) });
      }
      this.introduce(this.matchRow(fresh.id));
      this.notifyStatus(att.player);
    });
    return this.matchView(fresh, role);
  },
};
