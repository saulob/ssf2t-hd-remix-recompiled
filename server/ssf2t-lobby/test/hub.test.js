import assert from 'node:assert/strict';
import { test } from 'node:test';

import { Hub } from '../src/hub.js';
import { LIMITS, PROTOCOL, normalizeFriendCode, playerIdFor } from '../src/protocol.js';
import { befriend, greeted, identity, makeHub } from './harness.js';

test('hello verifies the identity and returns the friend code', () => {
  const env = makeHub();
  const a = env.connect(identity(1));
  const reply = a.hello();
  assert.equal(reply.code, 'ok');
  assert.equal(reply.friend_code, identity(1).player);
  assert.match(reply.friend_code, /^[A-HJ-NP-Z2-9]{4}-[A-HJ-NP-Z2-9]{4}$/);
  assert.equal(reply.token, undefined);
});

test('friend codes are stable and distinct per identity', () => {
  assert.equal(playerIdFor(identity(1).token), playerIdFor(identity(1).token));
  assert.notEqual(identity(1).player, identity(2).player);
  assert.equal(normalizeFriendCode('abcd 2345'), 'ABCD-2345');
});

test('a wrong player id, a different token for a known player and a bad protocol are refused', () => {
  const env = makeHub();
  const a = env.connect(identity(1));
  assert.equal(a.hello({ player: identity(2).player }).code, 'unauthorized');
  assert.equal(a.hello({ proto: PROTOCOL + 1 }).code, 'proto');
  assert.equal(a.hello({ game: 'other' }).code, 'bad_game');
  assert.equal(a.hello({ guest: 'base099' }).code, 'bad_guest');
  assert.equal(a.hello({ token: 'zz' }).code, 'bad_request');
  assert.equal(a.hello().code, 'ok');
  assert.equal(a.hello().code, 'bad_request');
});

test('operations before hello are refused', () => {
  const env = makeHub();
  const a = env.connect(identity(1));
  assert.equal(a.call('friends.list').code, 'no_session');
});

test('malformed messages never crash the hub', () => {
  const env = makeHub();
  const a = greeted(env, 1);
  assert.equal(a.raw('not json').code, 'bad_request');
  assert.equal(a.raw('[]').code, 'bad_request');
  assert.equal(a.raw('{"op":5,"seq":1}').code, 'bad_request');
  assert.equal(a.raw('{"op":"friends.list"}').code, 'bad_request');
  assert.equal(a.call('no.such.op').code, 'bad_request');
  assert.equal(a.call('friends.add', { code: 42 }).code, 'bad_request');
  assert.equal(a.call('friends.add', { code: 'ABCD' }).code, 'bad_request');
  assert.equal(a.call('pm.join', { session: 42 }).code, 'not_found');
  assert.equal(a.call('challenge.send', { player: 'x' }).code, 'bad_request');
  assert.equal(a.raw('x'.repeat(LIMITS.frameBytes + 1)).code, 'too_large');
  assert.equal(a.call('ping').code, 'ok');
  assert.deepEqual(env.logs, []);
});

test('unknown friend code is not found; own code is refused', () => {
  const env = makeHub();
  const a = greeted(env, 1);
  assert.equal(a.call('friends.add', { code: identity(9).player }).code, 'not_found');
  assert.equal(a.call('friends.add', { code: a.who.player }).code, 'bad_request');
});

test('friend request, accept, duplicate handling and remove', () => {
  const env = makeHub();
  const a = greeted(env, 1);
  const b = greeted(env, 2);
  const sent = a.call('friends.add', { code: b.who.player.toLowerCase() });
  assert.deepEqual([sent.code, sent.state, sent.player], ['ok', 'pending', b.who.player]);
  assert.equal(a.call('friends.add', { code: b.who.player }).state, 'pending');
  const request = b.events('friend.request');
  assert.equal(request.length, 1);
  assert.equal(request[0].player, a.who.player);
  assert.equal(b.call('friends.requests').incoming.length, 1);
  assert.equal(a.call('friends.requests').outgoing.length, 1);

  const accepted = b.call('friends.accept', { player: a.who.player });
  assert.equal(accepted.friend.player, a.who.player);
  assert.equal(a.events('friend.added').length, 1);
  assert.equal(a.call('friends.add', { code: b.who.player }).state, 'friends');
  assert.equal(b.call('friends.requests').incoming.length, 0);
  assert.equal(a.call('friends.list').friends.length, 1);
  assert.equal(b.call('friends.accept', { player: a.who.player }).code, 'not_found');

  assert.equal(a.call('friends.remove', { player: b.who.player }).code, 'ok');
  assert.equal(b.events('friend.removed').length, 1);
  assert.equal(a.call('friends.list').friends.length, 0);
  assert.equal(b.call('friends.list').friends.length, 0);
});

test('requests in both directions become a friendship; decline and cancel clear them', () => {
  const env = makeHub();
  const a = greeted(env, 1);
  const b = greeted(env, 2);
  const c = greeted(env, 3);
  a.call('friends.add', { code: b.who.player });
  assert.equal(b.call('friends.add', { code: a.who.player }).state, 'friends');
  assert.equal(a.call('friends.list').friends.length, 1);
  c.call('friends.add', { code: a.who.player });
  assert.equal(a.call('friends.decline', { player: c.who.player }).code, 'ok');
  assert.equal(a.call('friends.requests').incoming.length, 0);
  c.call('friends.add', { code: b.who.player });
  assert.equal(c.call('friends.decline', { player: b.who.player }).code, 'ok');
  assert.equal(b.call('friends.requests').incoming.length, 0);
  assert.equal(b.call('friends.decline', { player: c.who.player }).code, 'not_found');
});

test('friends persist across a hub restart and an offline friend has no presence', () => {
  const env = makeHub();
  const a = greeted(env, 1);
  const b = greeted(env, 2);
  befriend(a, b);
  b.drop();
  const list = a.call('friends.list').friends;
  assert.equal(list[0].online, false);
  assert.equal(list[0].state, 'offline');
  assert.equal(a.events('friend.offline').length, 1);
  const b2 = greeted(env, 2);
  assert.equal(b2.call('friends.list').friends.length, 1);
  assert.equal(a.call('friends.list').friends[0].online, true);
});

test('presence carries platform and application version, and goes online and offline', () => {
  const env = makeHub();
  const a = greeted(env, 1);
  const b = env.connect(identity(2), { platform: 'Linux', version: '0.3.0' });
  befriend(a, greeted(env, 3));
  b.hello();
  befriend(a, b);
  const view = a.call('friends.list').friends.find((f) => f.player === b.who.player);
  assert.deepEqual([view.online, view.state, view.platform, view.version], [true, 'online', 'Linux', '0.3.0']);
  b.drop();
  assert.equal(a.call('friends.list').friends.find((f) => f.player === b.who.player).online, false);
  b.hello;
  const probe = env.connect(identity(2), { presence: false });
  probe.hello();
  assert.equal(a.call('friends.list').friends.find((f) => f.player === b.who.player).online, false);
});

test('a status event reaches friends when someone comes online or renames', () => {
  const env = makeHub();
  const a = greeted(env, 1);
  const b = greeted(env, 2);
  befriend(a, b);
  b.drop();
  a.inbox = [];
  const b2 = env.connect(identity(2), { name: 'NewName' });
  b2.hello();
  const status = a.events('friend.status');
  assert.equal(status.at(-1).friend.name, 'NewName');
  assert.equal(status.at(-1).friend.online, true);
});

test('a Player Match is advertised to online friends; joining it creates the match: owner is host', () => {
  const env = makeHub();
  const a = greeted(env, 1);
  const b = greeted(env, 2);
  befriend(a, b);
  const hosted = a.call('pm.host', { slots: 2, rules: { rounds: 2, timer: 1, rule: 0 } });
  assert.equal(hosted.code, 'ok');
  assert.match(hosted.session, /^[0-9a-f]{16}$/);
  assert.equal(b.call('friends.list').friends[0].state, 'player_match');
  const found = b.call('pm.search');
  assert.equal(found.sessions.length, 1);
  assert.deepEqual([found.sessions[0].session, found.sessions[0].player, found.sessions[0].slots],
    [hosted.session, a.who.player, 2]);
  assert.deepEqual(found.sessions[0].rules, { rounds: 2, timer: 1, rule: 0 });
  assert.equal(found.sessions[0].ip, undefined);

  const joined = b.call('pm.join', { session: hosted.session });
  assert.equal(joined.code, 'ok');
  assert.equal(joined.role, 'join');
  assert.equal(joined.session, hosted.session);
  assert.equal(joined.peer.player, a.who.player);
  const created = a.events('match.created');
  assert.equal(created.length, 1);
  assert.deepEqual([created[0].role, created[0].match, created[0].session, created[0].peer.player],
    ['host', joined.match, hosted.session, b.who.player]);
  assert.equal(env.hub.stats().player_matches, 0);
  assert.equal(a.call('friends.list').friends[0].state, 'connecting');
  assert.equal(b.call('pm.search').sessions.length, 0);
});

test('only friends see a Player Match; strangers can neither find nor join it', () => {
  const env = makeHub();
  const a = greeted(env, 1);
  const stranger = greeted(env, 3);
  const hosted = a.call('pm.host', {});
  assert.equal(stranger.call('pm.search').sessions.length, 0);
  assert.equal(stranger.call('pm.join', { session: hosted.session }).code, 'not_friends');
  assert.equal(a.call('pm.join', { session: hosted.session }).code, 'bad_request');
  assert.equal(stranger.call('pm.join', { session: 'nope' }).code, 'not_found');
});

test('Base013 only meets Base013, TU023 only TU023, and a different netplay build is hidden', () => {
  const env = makeHub();
  for (const [guest, first] of [['base013', 10], ['tu023', 12]]) {
    const a = env.connect(identity(first), { guest });
    const b = env.connect(identity(first + 1), { guest });
    a.hello();
    b.hello();
    befriend(a, b);
    const hosted = a.call('pm.host', {});
    assert.equal(b.call('pm.search').sessions[0].guest, guest);
    assert.equal(b.call('pm.join', { session: hosted.session }).code, 'ok');
  }
  const base = env.connect(identity(20), { guest: 'base013' });
  const update = env.connect(identity(21), { guest: 'tu023' });
  const rebuilt = env.connect(identity(22), { guest: 'base013', build: 2 });
  for (const c of [base, update, rebuilt]) c.hello();
  befriend(base, update);
  befriend(base, rebuilt);
  const hosted = base.call('pm.host', {});
  assert.equal(update.call('pm.search').sessions.length, 0);
  assert.equal(rebuilt.call('pm.search').sessions.length, 0);
  assert.equal(update.call('pm.join', { session: hosted.session }).code, 'version_mismatch');
  assert.equal(rebuilt.call('pm.join', { session: hosted.session }).code, 'version_mismatch');
  assert.equal(env.hub.stats().matches, 2);
});

test('the host QoS data travels with the advertisement and is validated', () => {
  const env = makeHub();
  const a = greeted(env, 1);
  const b = greeted(env, 2);
  befriend(a, b);
  const blob = '0a'.repeat(112);
  assert.equal(a.call('pm.host', { qos: blob }).code, 'ok');
  assert.equal(b.call('pm.search').sessions[0].qos, blob);
  assert.equal(a.call('pm.host', { qos: 'xyz' }).code, 'bad_request');
  assert.equal(a.call('pm.host', { qos: '0'.repeat(513) }).code, 'bad_request');
  assert.equal(a.call('pm.host', {}).code, 'ok');
  assert.equal(b.call('pm.search').sessions[0].qos, '');
});

test('Player Match advertisements are two-player only and their rules are validated', () => {
  const env = makeHub();
  const a = greeted(env, 1);
  assert.equal(a.call('pm.host', { slots: 6 }).code, 'bad_request');
  assert.equal(a.call('pm.host', { rules: { rounds: 999 } }).code, 'bad_request');
  assert.equal(a.call('pm.host', { rules: { 'Bad Key': 1 } }).code, 'bad_request');
  assert.equal(a.call('pm.host', { rules: [] }).code, 'bad_request');
  assert.equal(env.hub.stats().player_matches, 0);
  const first = a.call('pm.host', {}).session;
  assert.equal(a.call('pm.host', { rules: { rounds: 3 } }).session, first);
  assert.equal(env.hub.stats().player_matches, 1);
});

test('cancel, the owner going offline and expiry remove a Player Match', () => {
  const env = makeHub();
  const a = greeted(env, 1);
  const b = greeted(env, 2);
  befriend(a, b);
  a.call('pm.host', {});
  assert.equal(a.call('pm.cancel').code, 'ok');
  assert.equal(b.call('pm.search').sessions.length, 0);
  assert.equal(b.call('friends.list').friends[0].state, 'online');

  a.call('pm.host', {});
  a.drop();
  assert.equal(env.hub.stats().player_matches, 0);

  const a2 = greeted(env, 1);
  const stale = a2.call('pm.host', {}).session;
  env.advance(LIMITS.playerMatchMs + 1000);
  assert.equal(env.hub.stats().player_matches, 0);
  assert.equal(b.call('pm.join', { session: stale }).code, 'not_found');
});

test('a refreshed Player Match outlives the expiry window', () => {
  const env = makeHub();
  const a = greeted(env, 1);
  const b = greeted(env, 2);
  befriend(a, b);
  const session = a.call('pm.host', {}).session;
  env.advance(LIMITS.playerMatchMs - 1000);
  a.call('pm.host', {});
  env.advance(LIMITS.playerMatchMs - 1000);
  assert.equal(b.call('pm.search').sessions[0].session, session);
});

test('busy players cannot host or join', () => {
  const env = makeHub();
  const { a, b } = pairedMatch(env);
  const c = greeted(env, 3);
  befriend(c, a);
  assert.equal(a.call('pm.host', {}).code, 'busy');
  const hosted = c.call('pm.host', {});
  assert.equal(a.call('pm.join', { session: hosted.session }).code, 'busy');
  assert.ok(b);
});

test('joining a friend cancels the joiner own advertisement', () => {
  const env = makeHub();
  const a = greeted(env, 1);
  const b = greeted(env, 2);
  befriend(a, b);
  const hosted = a.call('pm.host', {});
  b.call('pm.host', {});
  assert.equal(env.hub.stats().player_matches, 2);
  b.call('pm.join', { session: hosted.session });
  assert.equal(env.hub.stats().player_matches, 0);
});

function pairedMatch(env, nA = 1, nB = 2, ips = ['203.0.113.7', '203.0.113.7']) {
  const a = env.connect(identity(nA), { ip: ips[0] });
  const b = env.connect(identity(nB), { ip: ips[1] });
  a.hello();
  b.hello();
  befriend(a, b);
  const hosted = a.call('pm.host', {});
  const joined = b.call('pm.join', { session: hosted.session });
  a.inbox = [];
  return { a, b, match: joined.match };
}

test('endpoints are exchanged when both players reported: host=P1 gets the joiner, joiner gets the host', () => {
  const env = makeHub();
  const { a, b, match } = pairedMatch(env);
  assert.equal(a.call('match.endpoint', { match, port: 7001, lan: ['127.0.0.1', '192.168.1.5'] }).code, 'ok');
  assert.equal(a.events('match.peer').length, 0);
  assert.equal(b.call('match.endpoint', { match, port: 7002, lan: ['127.0.0.1'] }).code, 'ok');
  const toHost = a.events('match.peer')[0];
  const toJoin = b.events('match.peer')[0];
  assert.deepEqual([toHost.role, toHost.port, toHost.same_network, toHost.player], ['join', 7002, true, b.who.player]);
  assert.deepEqual([toJoin.role, toJoin.port, toJoin.ip, toJoin.lan], ['host', 7001, '203.0.113.7', ['127.0.0.1', '192.168.1.5']]);
});

test('endpoint messages are validated and tied to the sender match', () => {
  const env = makeHub();
  const { a, b, match } = pairedMatch(env);
  assert.equal(a.call('match.endpoint', { match, port: 80 }).code, 'bad_request');
  assert.equal(a.call('match.endpoint', { match, port: 7001, lan: ['8.8.8.8'] }).code, 'bad_request');
  assert.equal(a.call('match.endpoint', { match: 'nope', port: 7001 }).code, 'not_found');
  const stranger = greeted(env, 7);
  assert.equal(stranger.call('match.endpoint', { match, port: 7001 }).code, 'not_found');
  assert.equal(b.call('match.ready', { match, ready: 'yes' }).code, 'bad_request');
});

// The STUN-mapped endpoint of the sender's gameplay socket: a public IPv4 address and its own port, both or neither.
test('public_ip and public_port are validated as one endpoint', () => {
  const env = makeHub();
  const { a, match } = pairedMatch(env);
  assert.equal(a.call('match.endpoint', { match, port: 7001, public_ip: '203.0.113.7', public_port: 54114 }).code, 'ok');
  for (const bad of ['10.0.0.5', '192.168.1.5', '127.0.0.1', '2804:14d::1', 'not-an-ip', '', 1]) {
    assert.equal(a.call('match.endpoint', { match, port: 7001, public_ip: bad, public_port: 54114 }).code, 'bad_request');
  }
  for (const bad of [0, 65536, -1, 1.5, '54114', null]) {
    assert.equal(a.call('match.endpoint', { match, port: 7001, public_ip: '203.0.113.7', public_port: bad }).code,
      'bad_request');
  }
  assert.equal(a.call('match.endpoint', { match, port: 7001, public_ip: '203.0.113.7' }).code, 'bad_request');
  assert.equal(a.call('match.endpoint', { match, port: 7001, public_port: 54114 }).code, 'bad_request');
  assert.equal(a.call('match.endpoint', { match, port: 7001 }).code, 'ok');  // omitted: STUN found nothing
});

// Real test 84b7d2f1: the peer must get the mapped port (54114), not the local one (7001), and the WebSocket's observed
// address (here IPv6) is passed on untouched, never dressed up as a UDP endpoint.
test('match.peer carries the sender\'s mapped endpoint apart from its observed address', () => {
  const env = makeHub();
  const { a, b, match } = pairedMatch(env, 1, 2, ['2804:14d::1', '203.0.113.9']);
  assert.equal(a.call('match.endpoint', { match, port: 7001, public_ip: '179.113.149.63', public_port: 54114 }).code,
    'ok');
  assert.equal(b.call('match.endpoint', { match, port: 7002 }).code, 'ok');

  const toB = b.events('match.peer').at(-1);
  assert.equal(toB.ip, '2804:14d::1');
  assert.equal(toB.port, 7001);
  assert.equal(toB.public_ip, '179.113.149.63');
  assert.equal(toB.public_port, 54114);

  const toA = a.events('match.peer').at(-1);
  assert.equal(toA.ip, '203.0.113.9');
  assert.equal(toA.port, 7002);
  assert.equal(toA.public_ip, undefined);
  assert.equal(toA.public_port, undefined);

  // A later endpoint (a resumed connection) reaches the peer again with the new mapping.
  assert.equal(b.call('match.endpoint', { match, port: 7002, public_ip: '177.18.65.229', public_port: 57219 }).code,
    'ok');
  const again = a.events('match.peer').at(-1);
  assert.equal(again.public_ip, '177.18.65.229');
  assert.equal(again.public_port, 57219);
});

// Real two-PC test 6d9f1c2a: whichever side finishes STUN first, neither gets match.peer until both endpoints are in,
// and each then gets the other's public endpoint; a later endpoint from either side reaches the other again.
for (const first of ['host', 'join']) {
  test(`the ${first} reporting first still gets the other's public endpoint once it arrives`, () => {
    const env = makeHub();
    const { a, b, match } = pairedMatch(env, 1, 2, ['198.51.100.1', '108.202.118.218']);
    const host = { conn: a, endpoint: { match, port: 7001, public_ip: '177.18.65.229', public_port: 7001 } };
    const join = { conn: b, endpoint: { match, port: 7001, public_ip: '108.202.118.218', public_port: 54321 } };
    const [early, late] = first === 'host' ? [host, join] : [join, host];
    assert.equal(early.conn.call('match.endpoint', early.endpoint).code, 'ok');
    assert.equal(early.conn.events('match.peer').length, 0);
    assert.equal(late.conn.events('match.peer').length, 0);
    assert.equal(late.conn.call('match.endpoint', late.endpoint).code, 'ok');
    const toHost = a.events('match.peer').at(-1);
    const toJoin = b.events('match.peer').at(-1);
    assert.equal(toHost.public_ip, '108.202.118.218');
    assert.equal(toHost.public_port, 54321);
    assert.equal(toJoin.public_ip, '177.18.65.229');
    assert.equal(toJoin.public_port, 7001);
    assert.equal(toHost.public_endpoints, true);
    assert.equal(toJoin.public_endpoints, true);
  });
}

test('an endpoint first sent without a mapping and then with one is passed on again', () => {
  const env = makeHub();
  const { a, b, match } = pairedMatch(env, 1, 2, ['198.51.100.1', '108.202.118.218']);
  a.call('match.endpoint', { match, port: 7001, public_ip: '177.18.65.229', public_port: 7001 });
  b.call('match.endpoint', { match, port: 7001 });  // e.g. an older client, or its STUN found nothing
  const before = a.events('match.peer').at(-1);
  assert.equal(before.public_ip, undefined);
  assert.equal(before.public_endpoints, true);
  b.call('match.endpoint', { match, port: 7001, public_ip: '108.202.118.218', public_port: 54321 });
  const after = a.events('match.peer').at(-1);
  assert.equal(after.public_ip, '108.202.118.218');
  assert.equal(after.public_port, 54321);
  assert.equal(after.ip, '108.202.118.218');
});

test('ready on both sides marks the players in game', () => {
  const env = makeHub();
  const { a, b, match } = pairedMatch(env);
  const watcher = greeted(env, 5);
  befriend(watcher, a);
  a.call('match.ready', { match, ready: true });
  assert.equal(b.events('match.ready')[0].ready, true);
  b.call('match.ready', { match, ready: true });
  assert.equal(watcher.call('friends.list').friends[0].state, 'in_game');
});

test('leaving a match tells the peer and frees both players', () => {
  const env = makeHub();
  const { a, b, match } = pairedMatch(env);
  assert.equal(a.call('match.leave', { match }).code, 'ok');
  assert.deepEqual(b.events('match.ended').map((e) => e.reason), ['left']);
  assert.equal(a.events('match.ended').length, 0);
  assert.equal(env.hub.stats().matches, 0);
  assert.equal(a.call('pm.host', {}).code, 'ok');
});

test('a dropped signalling socket keeps the seat; resume restores it and re-sends the peer', () => {
  const env = makeHub();
  const { a, b, match } = pairedMatch(env);
  a.call('match.endpoint', { match, port: 7001 });
  b.call('match.endpoint', { match, port: 7002 });
  a.inbox = [];
  b.inbox = [];
  a.drop();
  assert.equal(b.events('match.peer_lost').length, 1);
  env.advance(5000);
  const a2 = env.connect(identity(1));
  a2.hello();
  const resumed = a2.call('match.resume', { match });
  assert.equal(resumed.code, 'ok');
  assert.equal(resumed.role, 'host');
  assert.equal(b.events('match.peer_back').length, 1);
  assert.equal(a2.events('match.peer').length, 1);
  env.advance(LIMITS.graceMs + 1000);
  assert.equal(env.hub.stats().matches, 1);
});

test('a match whose seat is not retaken within the grace period is cancelled', () => {
  const env = makeHub();
  const { a, b } = pairedMatch(env);
  b.drop();
  assert.equal(a.events('match.peer_lost').length, 1);
  env.advance(LIMITS.graceMs + 1000);
  assert.deepEqual(a.events('match.ended').map((e) => e.reason), ['disconnect']);
  assert.equal(env.hub.stats().matches, 0);
  const b2 = greeted(env, 2);
  assert.equal(b2.call('match.resume', { match: 'whatever' }).code, 'not_found');
});

test('a hub restart keeps friends and gives open matches the grace period', () => {
  const env = makeHub();
  const { a, match } = pairedMatch(env);
  const restarted = env.host.conns;
  restarted.clear();
  env.hub.reconcileSeats();
  env.host.alarmAt = null;
  assert.equal(env.hub.stats().matches, 1);
  const a2 = env.connect(identity(1));
  a2.hello();
  assert.equal(a2.call('match.resume', { match }).code, 'ok');
  assert.equal(a2.call('friends.list').friends.length, 1);
  assert.ok(a);
});

test('a hibernated object wakes with its advertisements and sockets intact', () => {
  const env = makeHub();
  const a = greeted(env, 1);
  const b = greeted(env, 2);
  befriend(a, b);
  const hosted = a.call('pm.host', {});
  const woke = new Hub(env.host);
  a.hub = woke;
  b.hub = woke;
  assert.equal(b.call('pm.join', { session: hosted.session }).code, 'ok');
  assert.equal(a.events('match.created').length, 1);
});

test('two independent matches run side by side without crossing', () => {
  const env = makeHub();
  const one = pairedMatch(env, 1, 2, ['203.0.113.7', '198.51.100.9']);
  const two = pairedMatch(env, 3, 4, ['192.0.2.20', '192.0.2.21']);
  assert.notEqual(one.match, two.match);
  one.a.call('match.endpoint', { match: one.match, port: 7001 });
  two.a.call('match.endpoint', { match: two.match, port: 7001 });
  assert.equal(one.a.call('match.endpoint', { match: two.match, port: 7001 }).code, 'not_found');
  one.b.call('match.endpoint', { match: one.match, port: 7002 });
  assert.equal(one.a.events('match.peer')[0].player, one.b.who.player);
  assert.equal(two.a.events('match.peer').length, 0);
  assert.equal(one.b.events('match.peer')[0].same_network, false);
  two.b.call('match.endpoint', { match: two.match, port: 7002 });
  assert.equal(two.a.events('match.peer')[0].player, two.b.who.player);
  one.a.call('match.leave', { match: one.match });
  assert.equal(two.b.call('match.ready', { match: two.match, ready: true }).code, 'ok');
  assert.equal(env.hub.stats().matches, 1);
});

test('rate limits, per-address sockets and new-player limits', () => {
  const env = makeHub();
  const a = greeted(env, 1);
  let limited = 0;
  for (let i = 0; i < 100; i++) if (a.call('ping').code === 'limit') limited++;
  assert.ok(limited > 0);
  const crowd = makeHub();
  let admitted = 0;
  for (let i = 0; i < LIMITS.socketsPerIp + 4; i++) if (crowd.connect(identity(100 + i)).admitted) admitted++;
  assert.equal(admitted, LIMITS.socketsPerIp);
  const registrations = makeHub();
  const results = [];
  for (let i = 0; i < LIMITS.registrationsPerIpPerDay + 2; i++) {
    const c = registrations.connect(identity(200 + i), { ip: '198.51.100.4' });
    results.push(c.admitted ? c.hello().code : 'refused');
    if (c.admitted) c.drop();
  }
  assert.ok(results.includes('limit') || results.includes('refused'));
});

test('no reply or event ever contains a token', () => {
  const env = makeHub();
  const { a, b, match } = pairedMatch(env);
  a.call('match.endpoint', { match, port: 7001 });
  b.call('match.endpoint', { match, port: 7002 });
  a.call('friends.list');
  b.call('friends.requests');
  b.call('pm.host', {});
  a.call('pm.search');
  const everything = JSON.stringify([...a.inbox, ...b.inbox]);
  assert.ok(!everything.includes(a.who.token));
  assert.ok(!everything.includes(b.who.token));
  assert.ok(!env.logs.join('\n').includes(a.who.token));
});

test('an invite carries the host Player Match and the friend joins that same session', () => {
  const env = makeHub();
  const a = greeted(env, 1);
  const b = greeted(env, 2);
  befriend(a, b);
  assert.equal(a.call('invite.send', { player: b.who.player }).code, 'not_found');
  const session = a.call('pm.host', { qos: 'abcd' }).session;
  const sent = a.call('invite.send', { player: b.who.player });
  assert.deepEqual([sent.code, sent.session], ['ok', session]);
  const [received] = b.events('invite');
  assert.equal(received.invite.id, sent.invite);
  assert.deepEqual([received.invite.session, received.invite.player, received.invite.qos], [session, a.who.player, 'abcd']);
  const joined = b.call('pm.join', { session: received.invite.session });
  assert.deepEqual([joined.code, joined.session, joined.role], ['ok', session, 'join']);
  assert.equal(b.events('invite.ended').at(-1).reason, 'started');
  assert.equal(env.hub.stats().player_matches, 0);
});

test('invites go only to online, compatible, free friends; a new one replaces the old', () => {
  const env = makeHub();
  const a = greeted(env, 1);
  const b = greeted(env, 2);
  const c = greeted(env, 3, { guest: 'tu023' });
  befriend(a, b);
  befriend(a, c);
  a.call('pm.host', {});
  assert.equal(a.call('invite.send', { player: identity(9).player }).code, 'not_friends');
  assert.equal(a.call('invite.send', { player: c.who.player }).code, 'version_mismatch');
  assert.equal(a.call('invite.send', { player: 'nope' }).code, 'bad_request');
  const first = a.call('invite.send', { player: b.who.player }).invite;
  const second = a.call('invite.send', { player: b.who.player }).invite;
  assert.notEqual(first, second);
  assert.equal(b.events('invite').length, 2);
  b.drop();
  assert.equal(a.call('invite.send', { player: b.who.player }).code, 'offline');
});

test('decline, cancel, host leaving and expiry end an invite', () => {
  const env = makeHub();
  const a = greeted(env, 1);
  const b = greeted(env, 2);
  befriend(a, b);
  a.call('pm.host', {});
  const declined = a.call('invite.send', { player: b.who.player }).invite;
  assert.equal(b.call('invite.decline', { invite: declined }).code, 'ok');
  assert.equal(a.events('invite.ended').at(-1).reason, 'declined');
  assert.equal(b.call('invite.decline', { invite: declined }).code, 'ok');

  a.call('invite.send', { player: b.who.player });
  a.call('pm.cancel');
  assert.equal(b.events('invite.ended').at(-1).reason, 'cancelled');

  a.call('pm.host', {});
  a.call('invite.send', { player: b.who.player });
  a.drop();
  assert.equal(b.events('invite.ended').at(-1).reason, 'host_left');

  const a2 = greeted(env, 1);
  a2.call('pm.host', {});
  a2.call('invite.send', { player: b.who.player });
  env.advance(LIMITS.inviteMs + 1000);
  assert.equal(b.events('invite.ended').at(-1).reason, 'expired');
});
