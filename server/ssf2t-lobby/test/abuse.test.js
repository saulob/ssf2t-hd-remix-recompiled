import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { test } from 'node:test';

import { PROTECTION, RateLimiter } from '../src/abuse.js';
import { COUNTRY_HEADER, LIMITED_HEADER, limitedCount } from '../src/admin.js';
import { handleUpgrade, upgradeAllowed } from '../src/edge.js';
import { Hub } from '../src/hub.js';
import { SAMPLE_MS, metricsDay } from '../src/metrics.js';
import { CAPABILITIES, PROTOCOL, RELAY, encodeRelayFrame, ipGroup } from '../src/protocol.js';
import { befriend, identity, makeHub } from './harness.js';

const IP = '203.0.113.7';
const payload = Uint8Array.from([7, 1, 2, 3, 4, 5, 6, 7, 8]);

function countWrites(env) {
  const writes = [];
  const run = env.host.sql.run;
  env.host.sql.run = (query, ...args) => {
    if (/^\s*(INSERT|UPDATE|DELETE)/i.test(query)) writes.push(query.replace(/\s+/g, ' ').trim());
    return run(query, ...args);
  };
  return writes;
}

const writesTo = (writes, table) => writes.filter((q) => q.includes(table));

const abuse = (env) => Object.fromEntries(env.host.sql.all(
  `SELECT value, count FROM metrics_daily_dimensions WHERE dimension = 'abuse' AND day = ?`, metricsDay(env.clock.now))
  .map((r) => [r.value, r.count]));

const sessions = (env) => env.hub.metricsSummary().today.sessions;

function signedIn(env, n, options = {}, extra = {}) {
  const client = env.connect(identity(n), { ip: IP, ...options });
  assert.equal(client.hello(extra).code, 'ok');
  return client;
}

function reconnect(env, n, options = {}, extra = {}) {
  const client = env.connect(identity(n), { ip: IP, ...options });
  return { client, reply: client.hello(extra) };
}

function link(env, a, b) {
  for (const [x, y] of [[a, b], [b, a]]) {
    env.host.sql.run('INSERT INTO friends (player, friend, since) VALUES (?, ?, ?)', x.who.player, y.who.player, env.clock.now);
  }
}

function relayPair(env) {
  const a = signedIn(env, 1, { ip: IP }, { relay: true });
  const b = signedIn(env, 2, { ip: '198.51.100.4' }, { relay: true });
  befriend(a, b);
  const hosted = a.call('pm.host', {});
  const joined = b.call('pm.join', { session: hosted.session });
  assert.equal(joined.code, 'ok');
  a.inbox = [];
  b.inbox = [];
  return { a, b, match: joined.match, token: joined.relay_token };
}

const frame = (token, seq = 1) => encodeRelayFrame(token, seq, payload);

function upgradeRequest(headers = {}) {
  return new Request('https://ssf2t-lobby.example/', { headers: { Upgrade: 'websocket', 'CF-Connecting-IP': IP, ...headers } });
}

function fakeLobby() {
  const seen = [];
  return { seen, fetch: async (request) => { seen.push(request); return new Response(null, { status: 200 }); } };
}

function fakeLimiter(allow = () => true) {
  const keys = [];
  return { keys, limit: async ({ key }) => { keys.push(key); return { success: allow(key) }; } };
}

// ---- edge ----
test('the edge groups IPv4 and compressed or expanded IPv6 addresses by /64', () => {
  assert.equal(ipGroup('203.0.113.7'), '203.0.113.7');
  assert.equal(ipGroup('::ffff:203.0.113.7'), '203.0.113.7');
  assert.equal(ipGroup('2001:db8:1:2::5'), ipGroup('2001:0DB8:0001:0002:aaaa:bbbb:cccc:dddd'));
  assert.equal(ipGroup('2001:db8::1'), ipGroup('2001:db8:0:0:9::1'));
  assert.notEqual(ipGroup('2001:db8:1:2::5'), ipGroup('2001:db8:1:3::5'));
  assert.equal(ipGroup('::1'), '0:0:0:0');
});

test('an upgrade over the edge limit is a 429 that never reaches the lobby', async () => {
  const lobby = fakeLobby();
  const limiter = fakeLimiter((key) => !key.includes('198.51.100.9'));
  const env = { WS_UPGRADE_LIMITER: limiter };
  const refused = await handleUpgrade(upgradeRequest({ 'CF-Connecting-IP': '198.51.100.9' }), env, lobby);
  assert.equal(refused.status, 429);
  assert.equal(refused.headers.get('Retry-After'), '60');
  assert.equal(lobby.seen.length, 0);
  const allowed = await handleUpgrade(upgradeRequest(), env, lobby);
  assert.equal(allowed.status, 200);
  assert.equal(lobby.seen.length, 1);
  assert.equal(limiter.keys[0], 'ws:198.51.100.9');
});

test('every address of one /64 shares an edge limit key', async () => {
  const limiter = fakeLimiter();
  await upgradeAllowed(limiter, '2001:db8:1:2::5');
  await upgradeAllowed(limiter, '2001:db8:1:2:ffff::1');
  await upgradeAllowed(limiter, '2001:db8:1:3::5');
  assert.equal(limiter.keys[0], limiter.keys[1]);
  assert.notEqual(limiter.keys[0], limiter.keys[2]);
});

test('refused upgrades are counted on the next one forwarded, and a client cannot forge the count', async () => {
  const lobby = fakeLobby();
  let allow = false;
  const env = { WS_UPGRADE_LIMITER: fakeLimiter(() => allow) };
  for (let i = 0; i < 3; i++) await handleUpgrade(upgradeRequest(), env, lobby);
  allow = true;
  await handleUpgrade(upgradeRequest(), env, lobby);
  assert.equal(limitedCount(lobby.seen[0].headers), 3);
  await handleUpgrade(upgradeRequest({ [LIMITED_HEADER]: '999999' }), env, lobby);
  assert.equal(lobby.seen[1].headers.get(LIMITED_HEADER), null);
  assert.equal(lobby.seen[1].headers.get(COUNTRY_HEADER), 'Unknown');
  for (const bad of ['-4', '1.5', 'x', '']) assert.equal(limitedCount(new Headers({ [LIMITED_HEADER]: bad })), 0);
});

test('without the binding, or if it fails, upgrades pass', async () => {
  assert.equal(await upgradeAllowed(undefined, IP), true);
  assert.equal(await upgradeAllowed({ limit: async () => { throw new Error('down'); } }, IP), true);
  assert.equal(await upgradeAllowed(fakeLimiter(), ''), true);
  assert.equal((await handleUpgrade(upgradeRequest(), {}, fakeLobby())).status, 200);
});

test('the Worker gates upgrades at the edge, leaves the other routes alone and declares the limiter', () => {
  const root = join(import.meta.dirname, '..');
  const worker = readFileSync(join(root, 'src/worker.js'), 'utf8');
  assert.ok(worker.includes('handleUpgrade(request, env, lobby())'));
  assert.ok(!worker.includes('withEdgeCountry'));
  assert.ok(worker.indexOf('isAdminPath(url.pathname)') < worker.indexOf('handleUpgrade(request'));
  assert.ok(worker.indexOf('handleUpgrade(request') < worker.indexOf("url.pathname === '/health'"));
  const toml = readFileSync(join(root, 'wrangler.toml'), 'utf8');
  assert.match(toml, /name = "WS_UPGRADE_LIMITER"/);
  assert.match(toml, /simple = \{ limit = 30, period = 60 \}/);
});

test('the lobby counts what the edge refused and the upgrades it accepted, in memory until the alarm', () => {
  const env = makeHub();
  const writes = countWrites(env);
  env.hub.edgeLimited(7);
  env.hub.edgeLimited(0);
  env.connect(identity(1));
  assert.equal(writesTo(writes, 'metrics_daily_dimensions').length, 0);
  env.advance(SAMPLE_MS);
  assert.deepEqual(abuse(env), { ws_upgrade_accepted: 1, ws_upgrade_limited: 7 });
});

test('sockets of one /64 share the per-address cap whatever the spelling, and the refusal is counted', () => {
  const env = makeHub();
  const spellings = ['2001:db8:1:2::5', '2001:0db8:0001:0002:0:0:0:6', '2001:DB8:1:2:ffff::7', '2001:db8:1:2::8'];
  for (let i = 0; i < 16; i++) assert.ok(env.connect(identity(i + 1), { ip: spellings[i % 4] }).admitted);
  assert.equal(env.connect(identity(40), { ip: '2001:db8:1:2:1::9' }).admitted, false);
  assert.ok(env.connect(identity(41), { ip: '2001:db8:1:3::9' }).admitted);
  env.advance(SAMPLE_MS);
  assert.deepEqual([abuse(env).ws_upgrade_accepted, abuse(env).ws_upgrade_ip_cap], [17, 1]);
});

// ---- rate limiter ----
test('the limiter refills, refuses without spending, and stays bounded', () => {
  const limiter = new RateLimiter({ burst: 2, perSecond: 1 }, 8);
  assert.ok(limiter.take('a', 0) && limiter.take('a', 0));
  assert.equal(limiter.take('a', 0), false);
  assert.equal(limiter.allows('a', 1000), true);
  assert.equal(limiter.take('a', 1000), true);
  for (let i = 0; i < 100; i++) limiter.take(`k${i}`, 5000);
  assert.ok(limiter.buckets.size <= 8);
});

// ---- hello ----
test('reconnecting does not rewrite the player row or the session metrics', () => {
  const env = makeHub();
  let client = signedIn(env, 1);
  const writes = countWrites(env);
  for (let i = 0; i < 5; i++) {
    client.drop();
    env.advance(30_000);
    const next = reconnect(env, 1);
    assert.equal(next.reply.code, 'ok');
    client = next.client;
  }
  assert.equal(writesTo(writes, 'players').length, 0);
  assert.deepEqual(writes.filter((q) => q.includes('metrics_daily') && !q.includes("'abuse'")), []);
});

test('the player row is refreshed when the name changes or the last write is old, and not otherwise', () => {
  const env = makeHub();
  const first = signedIn(env, 1, { name: 'Alice' });
  first.drop();
  const writes = countWrites(env);
  const probe = { name: 'Alicia', presence: false };
  const renamed = reconnect(env, 1, probe);
  assert.equal(renamed.reply.name, 'Alicia');
  assert.equal(writesTo(writes, 'UPDATE players').length, 1);
  assert.equal(env.hub.playerRow(first.who.player).name, 'Alicia');
  renamed.client.drop();
  env.advance(PROTECTION.seenRefreshMs - 1000);
  reconnect(env, 1, probe).client.drop();
  assert.equal(writesTo(writes, 'UPDATE players').length, 1);
  env.advance(1000);
  reconnect(env, 1, probe);
  assert.equal(writesTo(writes, 'UPDATE players').length, 2);
  assert.equal(env.hub.playerRow(first.who.player).seen, env.clock.now);
});

test('authentication is unchanged: a token that does not match the player is refused and writes nothing', () => {
  const env = makeHub();
  const owner = signedIn(env, 1);
  const writes = countWrites(env);
  const thief = env.connect({ player: owner.who.player, token: 'cd'.repeat(32) }, { ip: IP });
  assert.equal(thief.hello().code, 'unauthorized');
  assert.equal(writes.length, 0);
});

test('a rapid reconnect of one player is one session; after the window it is another', () => {
  const env = makeHub();
  const writes = countWrites(env);
  const first = signedIn(env, 1);
  const counted = writes.length;
  assert.equal(sessions(env), 1);
  let client = first;
  for (let i = 0; i < 4; i++) {
    client.drop();
    env.advance(5_000);
    client = reconnect(env, 1).client;
  }
  assert.equal(sessions(env), 1);
  assert.deepEqual(writes.slice(counted).filter((q) => !q.startsWith('DELETE FROM invites WHERE target')), []);
  client.drop();
  env.advance(PROTECTION.sessionWindowMs);
  assert.equal(reconnect(env, 1).reply.code, 'ok');
  assert.equal(sessions(env), 2);
  const dimensions = env.host.sql.all(`SELECT dimension, SUM(count) AS n FROM metrics_daily_dimensions
    WHERE dimension IN ('platform', 'version', 'country') GROUP BY dimension`);
  assert.deepEqual(dimensions.map((d) => d.n), [2, 2, 2]);
  env.advance(SAMPLE_MS);
  assert.equal(abuse(env).hello_reconnect_deduped, 4);
  assert.equal(abuse(env).hello_accepted, 6);
});

test('the Connection test does not use up the session window, and each player counts for itself', () => {
  const env = makeHub();
  const owner = env.connect(identity(1), { ip: IP, presence: false });
  assert.equal(owner.hello().code, 'ok');
  assert.equal(sessions(env), 0);
  owner.drop();
  assert.equal(reconnect(env, 1).reply.code, 'ok');
  assert.equal(sessions(env), 1);
  signedIn(env, 2);
  assert.equal(sessions(env), 2);
});

test('a hello flood on one player is limited, and legitimate reconnects fit well inside the limit', () => {
  const env = makeHub();
  const first = signedIn(env, 1);
  first.drop();
  for (let i = 0; i < PROTECTION.hello.burst - 1; i++) {
    const next = reconnect(env, 1);
    assert.equal(next.reply.code, 'ok');
    next.client.drop();
  }
  const writes = countWrites(env);
  assert.equal(reconnect(env, 1).reply.code, 'limit');
  assert.equal(writes.length, 0);
  env.advance(15_000);
  assert.equal(reconnect(env, 1).reply.code, 'ok');
  env.advance(SAMPLE_MS);
  assert.equal(abuse(env).hello_limited, 1);
});

test('guessing at a player cannot lock its owner out', () => {
  const env = makeHub();
  const owner = signedIn(env, 1);
  owner.drop();
  for (let i = 0; i < 40; i++) {
    const thief = env.connect({ player: owner.who.player, token: 'cd'.repeat(32) }, { ip: IP });
    assert.equal(thief.hello().code, 'unauthorized');
    thief.drop();
  }
  assert.equal(reconnect(env, 1).reply.code, 'ok');
});

test('the development server is not limited on loopback', () => {
  const env = makeHub({ local: true });
  const first = signedIn(env, 1, { ip: '127.0.0.1' });
  first.drop();
  for (let i = 0; i < 30; i++) {
    const next = reconnect(env, 1, { ip: '127.0.0.1' });
    assert.equal(next.reply.code, 'ok');
    next.client.drop();
  }
  const host = signedIn(env, 2, { ip: '127.0.0.1' });
  for (let i = 0; i < PROTECTION.advertiseAccount.burst + 3; i++) {
    assert.equal(host.call('pm.host', {}).code, 'ok');
    host.call('pm.cancel');
  }
});

test('refusals of one reason are written once per window, not on every reconnect', () => {
  const env = makeHub();
  const writes = countWrites(env);
  for (let i = 0; i < 5; i++) assert.equal(env.connect(identity(1), { ip: IP }).hello({ proto: 2 }).code, 'proto');
  assert.equal(env.host.sql.get('SELECT incompatible_rejects AS n FROM metrics_daily').n, 1);
  assert.equal(writesTo(writes, 'metrics').length, 2);
  env.advance(PROTECTION.rejectWindowMs);
  assert.equal(env.connect(identity(1), { ip: IP }).hello({ proto: 2 }).code, 'proto');
  assert.equal(env.host.sql.get('SELECT incompatible_rejects AS n FROM metrics_daily').n, 2);
});

test('the wire contract is unchanged', () => {
  const env = makeHub();
  signedIn(env, 1);
  const again = reconnect(env, 1).reply;
  assert.deepEqual(Object.keys(again).sort(), ['capabilities', 'code', 'friend_code', 'name', 'player', 're', 'time']);
  assert.equal(PROTOCOL, 3);
  assert.deepEqual(again.capabilities, CAPABILITIES);
  assert.equal(again.capabilities.relay_max_payload, 1472);
  assert.deepEqual(RELAY.connection.packets, { burst: 800, perSecond: 400 });
  assert.deepEqual(RELAY.match.packets, { burst: 1600, perSecond: 800 });
  assert.equal(again.friend_code, identity(1).player);
});

test('a database from before the session column gains it and keeps its players', () => {
  const env = makeHub();
  const owner = signedIn(env, 1);
  owner.drop();
  env.host.sql.run('ALTER TABLE players DROP COLUMN session');
  const hub = new Hub(env.host);
  assert.equal(hub.playerRow(owner.who.player).session, 0);
  assert.equal(reconnect(env, 1).reply.code, 'ok');
});

// ---- Player Match ----
test('hosting and joining as usual is never limited', () => {
  const env = makeHub();
  const a = signedIn(env, 1);
  const b = signedIn(env, 2, { ip: '198.51.100.4' });
  befriend(a, b);
  for (let round = 0; round < 3; round++) {
    const hosted = a.call('pm.host', {});
    assert.equal(hosted.code, 'ok');
    const joined = b.call('pm.join', { session: hosted.session });
    assert.equal(joined.code, 'ok');
    assert.equal(b.call('match.leave', { match: joined.match }).code, 'ok');
    env.advance(60_000);
  }
});

test('hosting in a loop is limited, reconnecting does not reset it, and searching and leaving still work', () => {
  const env = makeHub();
  const first = signedIn(env, 1);
  let client = first;
  const burst = PROTECTION.advertiseAccount.burst;
  for (let i = 0; i < burst; i++) {
    assert.equal(client.call('pm.host', {}).code, 'ok');
    assert.equal(client.call('pm.cancel').code, 'ok');
  }
  assert.equal(client.call('pm.host', {}).code, 'limit');
  client.drop();
  client = reconnect(env, 1).client;
  assert.equal(client.call('pm.host', {}).code, 'limit');
  assert.equal(client.call('pm.search').code, 'ok');
  assert.equal(client.call('match.leave', { match: 'none' }).code, 'ok');
  env.advance(20_000);
  assert.equal(client.call('pm.host', {}).code, 'ok');
  env.advance(SAMPLE_MS);
  assert.deepEqual([abuse(env).pm_host_accepted, abuse(env).pm_host_limited], [burst + 1, 2]);
});

test('a refused pm.host writes nothing', () => {
  const env = makeHub();
  const client = signedIn(env, 1);
  for (let i = 0; i < PROTECTION.advertiseAccount.burst; i++) {
    client.call('pm.host', {});
    client.call('pm.cancel');
  }
  const writes = countWrites(env);
  assert.equal(client.call('pm.host', {}).code, 'limit');
  assert.equal(writes.length, 0);
});

test('players behind one address share a wider advertising limit, other addresses are unaffected', () => {
  const env = makeHub();
  const results = [];
  for (let i = 0; i < PROTECTION.advertiseAddress.burst + 3; i++) signedIn(env, i + 1, { ip: `192.0.2.${i + 1}` }).drop();
  for (let i = 0; i < PROTECTION.advertiseAddress.burst + 3; i++) {
    const { client } = reconnect(env, i + 1, { ip: '203.0.113.50' });
    results.push(client.call('pm.host', {}).code);
    client.drop();
  }
  assert.equal(results.filter((c) => c === 'ok').length, PROTECTION.advertiseAddress.burst);
  assert.equal(results.at(-1), 'limit');
  assert.equal(signedIn(env, 99, { ip: '203.0.113.51' }).call('pm.host', {}).code, 'ok');
});

test('refreshing an unchanged advertisement writes only when the last write is old', () => {
  const env = makeHub();
  const host = signedIn(env, 1);
  const session = host.call('pm.host', {}).session;
  const writes = countWrites(env);
  env.advance(PROTECTION.advertiseRefreshMs - 1000);
  assert.equal(host.call('pm.host', {}).session, session);
  assert.equal(writes.length, 0);
  env.advance(1000);
  assert.equal(host.call('pm.host', {}).session, session);
  assert.equal(writesTo(writes, 'UPDATE player_matches').length, 1);
  assert.equal(host.call('pm.host', { rules: { mode: 1 } }).session, session);
  assert.equal(writesTo(writes, 'UPDATE player_matches').length, 2);
});

test('joining in a loop is limited, reconnecting does not reset it, and failed joins do not count', () => {
  const env = makeHub();
  let joiner = signedIn(env, 1, { ip: '198.51.100.200' });
  for (let i = 0; i < 30; i++) {
    assert.equal(joiner.call('pm.join', { session: 'nope' }).code, 'not_found');
    env.advance(1000);
  }
  assert.equal(env.hub.guards.joinAccount.tokens(joiner.who.player, env.clock.now), PROTECTION.joinAccount.burst);
  const codes = [];
  for (let i = 0; i < 80 && codes.at(-1) !== 'limit'; i++) {
    const host = signedIn(env, 100 + i, { ip: `192.0.2.${i + 1}` });
    link(env, host, joiner);
    if (i % 8 === 7) {
      joiner.drop();
      joiner = reconnect(env, 1, { ip: '198.51.100.200' }).client;
    }
    const joined = joiner.call('pm.join', { session: host.call('pm.host', {}).session });
    codes.push(joined.code);
    if (joined.code === 'ok') joiner.call('match.leave', { match: joined.match });
    env.advance(1000);
  }
  assert.equal(codes.at(-1), 'limit');
  const accepted = codes.filter((c) => c === 'ok').length;
  assert.ok(accepted >= PROTECTION.joinAccount.burst && accepted < PROTECTION.joinAccount.burst + 10, `accepted ${accepted}`);
  assert.equal(env.host.sql.get('SELECT COUNT(*) AS n FROM matches').n, 0);
  env.advance(SAMPLE_MS);
  assert.equal(abuse(env).pm_join_limited, 1);
});

// ---- invites ----
function hostWithFriend(env, nHost, nTarget, ips = {}) {
  const host = signedIn(env, nHost, { ip: ips.host ?? IP });
  const target = signedIn(env, nTarget, { ip: ips.target ?? '198.51.100.4' });
  link(env, host, target);
  assert.equal(host.call('pm.host', {}).code, 'ok');
  return { host, target };
}

test('an invite reaches an online friend, who joins or declines it as usual', () => {
  const env = makeHub();
  const { host, target } = hostWithFriend(env, 1, 2);
  const sent = host.call('invite.send', { player: target.who.player });
  assert.equal(sent.code, 'ok');
  assert.equal(target.events('invite').length, 1);
  assert.equal(target.call('invite.decline', { invite: sent.invite }).code, 'ok');
  assert.equal(host.call('invite.send', { player: target.who.player }).code, 'ok');
  assert.equal(target.call('pm.join', { session: sent.session }).code, 'ok');
});

test('inviting in a loop is limited, a second connection of the player shares the limit, and refusals write nothing', () => {
  const env = makeHub();
  const { host, target } = hostWithFriend(env, 1, 2);
  const codes = [];
  for (let i = 0; i < 80 && codes.at(-1) !== 'limit'; i++) {
    env.advance(1000);
    codes.push(host.call('invite.send', { player: target.who.player }).code);
  }
  assert.equal(codes.at(-1), 'limit');
  assert.ok(codes.filter((c) => c === 'ok').length >= PROTECTION.inviteAccount.burst);
  const second = reconnect(env, 1).client;
  assert.equal(second.call('invite.send', { player: target.who.player }).code, 'limit');
  const writes = countWrites(env);
  assert.equal(host.call('invite.send', { player: target.who.player }).code, 'limit');
  assert.equal(writes.length, 0);
  env.advance(SAMPLE_MS);
  assert.ok(abuse(env).invite_limited >= 3);
});

test('players behind one address share a wider invite limit', () => {
  const env = makeHub();
  const pairs = [1, 2, 3, 4].map((n) => hostWithFriend(env, n, n + 10, { host: IP, target: `198.51.100.${n}` }));
  let limited = null;
  for (let round = 0; round < 40 && !limited; round++) {
    env.advance(1000);
    for (const { host, target } of pairs) {
      if (host.call('invite.send', { player: target.who.player }).code === 'limit') limited = host;
    }
  }
  assert.ok(limited);
  assert.equal(env.hub.guards.inviteAccount.allows(limited.who.player, env.clock.now), true);
  assert.equal(env.hub.guards.inviteAddress.allows(IP, env.clock.now), false);
});

// ---- resume ----
test('a seat is retaken as usual after a dropped connection', () => {
  const env = makeHub();
  const { a, b, match } = relayPair(env);
  b.drop();
  const b2 = signedIn(env, 2, { ip: '198.51.100.4' });
  const resumed = b2.call('match.resume', { match });
  assert.equal(resumed.code, 'ok');
  assert.equal(resumed.match, match);
  assert.equal(a.events('match.peer_back').length, 1);
});

test('two connections taking a seat from each other are limited', () => {
  const env = makeHub();
  const { b, match } = relayPair(env);
  b.drop();
  const sockets = [signedIn(env, 2, { ip: '198.51.100.4' }), signedIn(env, 2, { ip: '198.51.100.4' })];
  const codes = [];
  for (let i = 0; i < PROTECTION.resumeAccount.burst + 3; i++) codes.push(sockets[i % 2].call('match.resume', { match }).code);
  assert.deepEqual(codes.slice(0, PROTECTION.resumeAccount.burst), Array(PROTECTION.resumeAccount.burst).fill('ok'));
  assert.equal(codes.at(-1), 'limit');
  env.advance(SAMPLE_MS);
  assert.ok(abuse(env).match_resume_limited >= 1);
});

// ---- redundant writes ----
test('repeating an unchanged endpoint, ready flag or transport choice writes nothing', () => {
  const env = makeHub();
  const { a, b, match } = relayPair(env);
  assert.equal(a.call('match.endpoint', { match, port: 7001 }).code, 'ok');
  assert.equal(b.call('match.endpoint', { match, port: 7002 }).code, 'ok');
  assert.equal(a.call('match.ready', { match, ready: true }).code, 'ok');
  assert.equal(a.call('match.transport', { match, transport: 'relay' }).code, 'ok');
  const writes = countWrites(env);
  assert.equal(a.call('match.endpoint', { match, port: 7001 }).code, 'ok');
  assert.equal(a.call('match.ready', { match, ready: true }).code, 'ok');
  assert.equal(a.call('match.transport', { match, transport: 'relay' }).code, 'ok');
  assert.equal(writes.length, 0);
  assert.equal(a.call('match.endpoint', { match, port: 7003 }).code, 'ok');
  assert.equal(a.call('match.ready', { match, ready: false }).code, 'ok');
  assert.equal(writesTo(writes, 'UPDATE matches').length, 2);
  assert.equal(b.events('match.peer').length, 3);
});

// ---- relay ----
test('normal relay traffic passes and is never counted as a fault', () => {
  const env = makeHub();
  const { a, b, token } = relayPair(env);
  for (let i = 0; i < 100; i++) {
    a.relay(frame(token, i));
    b.relay(frame(token, i));
  }
  assert.deepEqual([a.binary.length, b.binary.length], [100, 100]);
  assert.deepEqual([a.closedWith, b.closedWith], [null, null]);
  assert.equal(env.hub.relayFaults.size, 0);
});

test('a sender of malformed frames loses its connection after a bounded number, and its peer keeps its own', () => {
  const env = makeHub();
  const { a, b, token, match } = relayPair(env);
  const junk = Uint8Array.from([1, 2, 3]);
  for (let i = 0; i < RELAY.invalid.burst; i++) a.relay(junk);
  assert.equal(a.closedWith, null);
  a.relay(junk);
  assert.deepEqual(a.closedWith, { code: 1008, reason: 'relay abuse' });
  a.relay(frame(token));
  assert.equal(b.binary.length, 0);
  b.relay(frame(token));
  assert.equal(a.binary.length, 1);
  assert.equal(b.closedWith, null);
  assert.ok(env.hub.matchRow(match));
  a.drop();
  assert.equal(env.hub.relayFaults.size, 0);
  env.advance(SAMPLE_MS);
  assert.equal(abuse(env).relay_abuse_closed, 1);
});

test('frames with a bad token, from a stranger to the match, from a taken seat, after the relay closed or before hello end the same way', () => {
  const closes = (sender, bytes) => {
    for (let i = 0; i < RELAY.invalid.burst + 1; i++) sender.relay(bytes);
    return sender.closedWith?.code;
  };
  let env = makeHub();
  let m = relayPair(env);
  assert.equal(closes(m.a, frame(m.token ^ 1)), 1008, 'bad token');
  assert.equal(m.b.closedWith, null);

  env = makeHub();
  m = relayPair(env);
  const stranger = signedIn(env, 3, { ip: '198.51.100.30' }, { relay: true });
  assert.equal(closes(stranger, frame(m.token)), 1008, 'not in a match');

  env = makeHub();
  m = relayPair(env);
  const taker = signedIn(env, 2, { ip: '198.51.100.4' }, { relay: true });
  m.b.drop();
  assert.equal(taker.call('match.resume', { match: m.match }).code, 'ok');
  assert.equal(closes(m.a, frame(m.token)), 1008, 'relay closed while both are seated');

  env = makeHub();
  m = relayPair(env);
  assert.equal(m.a.call('match.transport', { match: m.match, transport: 'direct' }).code, 'ok');
  assert.equal(closes(m.b, frame(m.token)), 1008, 'relay not in use');

  env = makeHub();
  m = relayPair(env);
  assert.equal(closes(env.connect(identity(9), { ip: '198.51.100.12' }), frame(m.token)), 1008, 'no hello');
});

test('invalid frames are budgeted over time: an occasional stray one never closes a sender', () => {
  const env = makeHub();
  const { a } = relayPair(env);
  for (let i = 0; i < 600; i++) {
    a.relay(Uint8Array.from([9]));
    env.advance(1000);
  }
  assert.equal(a.closedWith, null);
});

test('a sender whose peer is away keeps its connection through the grace period and the resume', () => {
  const env = makeHub();
  const { a, b, token, match } = relayPair(env);
  b.drop();
  for (let round = 0; round < 20; round++) {
    for (let i = 0; i < 100; i++) a.relay(frame(token, round * 100 + i));
    env.advance(1000);
  }
  assert.equal(a.closedWith, null);
  const b2 = signedIn(env, 2, { ip: '198.51.100.4' }, { relay: true });
  assert.equal(b2.call('match.resume', { match }).code, 'ok');
  for (let i = 0; i < 100; i++) a.relay(frame(token, i));
  assert.equal(a.closedWith, null);
});

test('a closed sender is ignored, and one that never greeted is closed like any other', () => {
  const env = makeHub();
  const { a, b, token } = relayPair(env);
  for (let i = 0; i <= RELAY.invalid.burst; i++) a.relay(Uint8Array.from([1]));
  assert.equal(a.closedWith?.code, 1008);
  a.relay(frame(token));
  assert.equal(b.binary.length, 0);
  const silent = env.connect(identity(9), { ip: '198.51.100.30' });
  silent.relay(Uint8Array.from([1]));
  assert.equal(env.hub.relayFaults.size, 2);
  silent.drop();
  assert.equal(env.hub.relayFaults.size, 1);
});

// ---- alarms ----
test('a woken object that already knows its alarm does not set it again', () => {
  const env = makeHub();
  signedIn(env, 1);
  assert.notEqual(env.host.alarmAt, null);
  const sets = [];
  const setAlarm = env.host.setAlarm.bind(env.host);
  env.host.setAlarm = (ms) => {
    sets.push(ms);
    setAlarm(ms);
  };
  void new Hub(env.host, { alarmAt: env.host.alarmAt });
  assert.equal(sets.length, 0);
  void new Hub(env.host);
  assert.equal(sets.length, 1);
});

test('activity that does not move the next deadline earlier does not set the alarm again', () => {
  const env = makeHub();
  const host = signedIn(env, 1);
  const session = host.call('pm.host', {}).session;
  const sets = [];
  const setAlarm = env.host.setAlarm.bind(env.host);
  env.host.setAlarm = (ms) => {
    sets.push(ms);
    setAlarm(ms);
  };
  signedIn(env, 2, { ip: '198.51.100.4' });
  env.advance(60_000 - 1000);
  assert.equal(host.call('pm.host', {}).session, session);
  assert.equal(sets.length, 0);
  assert.ok(env.host.alarmAt <= env.hub.playerMatchOf(host.who.player).refreshed + 180_000);
});

test('the advertisement still expires when it is refreshed less often than the alarm fires', () => {
  const env = makeHub();
  const host = signedIn(env, 1);
  host.call('pm.host', {});
  for (let i = 0; i < 6; i++) {
    env.advance(60_000);
    assert.equal(host.call('pm.host', {}).code, 'ok');
  }
  assert.ok(env.hub.playerMatchOf(host.who.player));
  env.advance(181_000);
  assert.equal(env.hub.playerMatchOf(host.who.player), null);
});

test('diagnostics flush once per interval and an idle object schedules no further alarm', () => {
  const env = makeHub();
  env.hub.edgeLimited(3);
  assert.equal(env.host.alarmAt, env.clock.now - (env.clock.now % SAMPLE_MS) + SAMPLE_MS);
  const writes = countWrites(env);
  env.hub.edgeLimited(2);
  assert.equal(writes.length, 0);
  env.advance(SAMPLE_MS);
  assert.deepEqual(abuse(env), { ws_upgrade_limited: 5 });
  assert.equal(env.host.alarmAt, null);
});

test('abuse diagnostics hold counter names only', () => {
  const env = makeHub();
  const client = signedIn(env, 1);
  for (let i = 0; i < 9; i++) {
    client.call('pm.host', {});
    client.call('pm.cancel');
  }
  env.advance(SAMPLE_MS);
  const rows = env.host.sql.all(`SELECT * FROM metrics_daily_dimensions WHERE dimension = 'abuse'`);
  const text = JSON.stringify(rows);
  assert.ok(rows.length > 0);
  assert.ok(!text.includes(client.who.player) && !text.includes(IP) && !text.includes(client.who.token));
  assert.deepEqual(rows.map((r) => r.value).filter((v) => !/^[a-z_]+$/.test(v)), []);
});
