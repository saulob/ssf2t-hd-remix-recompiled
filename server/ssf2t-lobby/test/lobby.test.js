import assert from 'node:assert/strict';
import { after, before, test } from 'node:test';

import { startLobbyServer } from '../local/server.js';
import { CAPABILITIES, GAME_ID, LIMITS, PROTOCOL, WORKER_BUILD, parseLobbyPublish, parseLobbyRevision, tokenHash }
  from '../src/protocol.js';
import { befriend, greeted, identity, makeHub } from './harness.js';

const ROW_KEYS = ['compatible', 'guest', 'name', 'platform', 'player', 'state', 'version'];

function countWrites(env) {
  const writes = [];
  const run = env.host.sql.run;
  env.host.sql.run = (query, ...args) => {
    if (/^\s*(INSERT|UPDATE|DELETE)/i.test(query)) writes.push(query.replace(/\s+/g, ' ').trim());
    return run(query, ...args);
  };
  return writes;
}

const visible = (client, on = true) => client.call('lobby.publish', { visible: on });
const names = (reply) => reply.players.map((p) => p.name);

test('the service announces the public Lobby and the protocol number stays 3', () => {
  assert.equal(CAPABILITIES.public_lobby, true);
  assert.equal(CAPABILITIES.protocol_version, 3);
  assert.equal(PROTOCOL, 3);
  assert.equal(WORKER_BUILD, '2026-11-07-bug-reports');
  const env = makeHub();
  const reply = env.connect(identity(1)).hello();
  assert.equal(reply.capabilities.public_lobby, true);
  assert.equal(reply.capabilities.secret_characters, true);
  assert.equal(reply.capabilities.fight_telemetry, true);
});

test('nobody is listed until they publish, and the list starts empty', () => {
  const env = makeHub();
  const a = greeted(env, 1, { name: 'Alice' });
  const b = greeted(env, 2, { name: 'Bob' });
  assert.deepEqual(a.call('lobby.list').players, []);
  assert.equal(a.call('lobby.list').visible, false);
  assert.deepEqual(b.call('lobby.list').players, []);
});

test('publishing On lists the player for others and Off removes them again', () => {
  const env = makeHub();
  const a = greeted(env, 1, { name: 'Alice' });
  const b = greeted(env, 2, { name: 'Bob' });
  assert.deepEqual(visible(a), { re: a.seq, code: 'ok', visible: true });
  const seen = b.call('lobby.list');
  assert.equal(seen.code, 'ok');
  assert.deepEqual(names(seen), ['Alice']);
  assert.equal(seen.players[0].player, identity(1).player);
  assert.equal(seen.visible, false);
  assert.deepEqual(visible(a, false), { re: a.seq, code: 'ok', visible: false });
  assert.deepEqual(b.call('lobby.list').players, []);
});

test('the viewer never sees their own row, only that they are visible', () => {
  const env = makeHub();
  const a = greeted(env, 1, { name: 'Alice' });
  visible(a);
  const own = a.call('lobby.list');
  assert.deepEqual(own.players, []);
  assert.equal(own.visible, true);
});

test('a player who disconnects disappears from the list', () => {
  const env = makeHub();
  const a = greeted(env, 1, { name: 'Alice' });
  const b = greeted(env, 2, { name: 'Bob' });
  visible(a);
  assert.deepEqual(names(b.call('lobby.list')), ['Alice']);
  a.drop();
  assert.deepEqual(b.call('lobby.list').players, []);
});

test('visibility belongs to the socket: a second connection of the same player does not keep a ghost', () => {
  const env = makeHub();
  const a1 = greeted(env, 1, { name: 'Alice' });
  const a2 = greeted(env, 1, { name: 'Alice' });
  const b = greeted(env, 2, { name: 'Bob' });
  visible(a1);
  assert.deepEqual(names(b.call('lobby.list')), ['Alice']);
  visible(a2);
  assert.equal(b.call('lobby.list').players.length, 1);
  a1.drop();
  assert.deepEqual(names(b.call('lobby.list')), ['Alice']);
  a2.drop();
  assert.deepEqual(b.call('lobby.list').players, []);
});

test('visibility survives a hibernated object because it lives in the attachment', () => {
  const env = makeHub();
  const a = greeted(env, 1, { name: 'Alice' });
  visible(a);
  const stored = JSON.parse(JSON.stringify(a.conn.attachment()));
  assert.equal(stored.lobby, true);
});

test('a connection without presence cannot be listed', () => {
  const env = makeHub();
  const probe = env.connect(identity(1), { presence: false });
  assert.equal(probe.hello().code, 'ok');
  assert.equal(visible(probe).code, 'bad_request');
  const b = greeted(env, 2);
  assert.deepEqual(b.call('lobby.list').players, []);
});

test('malformed Lobby requests are refused and change nothing', () => {
  const env = makeHub({ local: true });
  const a = greeted(env, 1, { ip: '127.0.0.1' });
  const b = greeted(env, 2, { ip: '127.0.0.1' });
  for (const bad of [{}, { visible: 'true' }, { visible: 1 }, { visible: null }, { visible: [true] }, { visible: {} }]) {
    assert.equal(a.call('lobby.publish', bad).code, 'bad_request');
  }
  for (const bad of [{ rev: 'x' }, { rev: -1 }, { rev: 1.5 }, { rev: null }, { rev: [] }, { rev: Number.MAX_VALUE }]) {
    assert.equal(b.call('lobby.list', bad).code, 'bad_request');
  }
  assert.deepEqual(b.call('lobby.list').players, []);
  assert.equal(a.call('lobby.list').visible, false);
  assert.throws(() => parseLobbyPublish({}), { code: 'bad_request' });
  assert.equal(parseLobbyRevision({}), null);
  assert.equal(parseLobbyRevision({ rev: 7 }), 7);
});

test('Lobby operations need a greeted connection', () => {
  const env = makeHub();
  const a = env.connect(identity(1));
  assert.equal(a.call('lobby.publish', { visible: true }).code, 'no_session');
  assert.equal(a.call('lobby.list').code, 'no_session');
});

test('rows hold exactly the minimum a launcher shows, whatever the client sent', () => {
  const env = makeHub();
  const a = greeted(env, 1, { name: 'Alice', version: '0.4.0', platform: 'Linux', guest: 'tu023' });
  const b = greeted(env, 2, { guest: 'tu023' });
  visible(a);
  const [row] = b.call('lobby.list').players;
  assert.deepEqual(Object.keys(row).sort(), ROW_KEYS);
  assert.deepEqual(row, { player: identity(1).player, name: 'Alice', state: 'online', platform: 'Linux', version: '0.4.0',
    guest: 'tu023', compatible: true });
});

test('the public list never carries a token, address, endpoint, relay or match detail', () => {
  const env = makeHub();
  const a = env.connect(identity(1), { name: 'Alice', ip: '198.51.100.20' });
  a.hello({ relay: true, telemetry: true });
  const b = env.connect(identity(2), { name: 'Bob', ip: '198.51.100.21' });
  b.hello({ relay: true });
  befriend(a, b);
  const hosted = a.call('pm.host', { rules: { rounds: 2 }, qos: 'aabbcc' });
  b.call('pm.join', { session: hosted.session });
  a.call('match.endpoint', { match: a.conn.attachment().match, port: 7001, lan: ['192.168.1.9'], public_ip: '203.0.113.9',
    public_port: 55555 });
  visible(a);
  const c = greeted(env, 3, { ip: '198.51.100.22' });
  const raw = JSON.stringify(c.call('lobby.list'));
  for (const secret of [identity(1).token, tokenHash(identity(1).token), '198.51.100.20', '192.168.1.9', '203.0.113.9', '55555',
    'relay', 'lan', 'qos', 'rules', 'session', 'match', 'ip', 'token', 'hash', 'cid', 'endpoint', 'port', 'country']) {
    assert.equal(raw.toLowerCase().includes(secret.toLowerCase()), false, `leaked ${secret}`);
  }
  for (const key of Object.keys(JSON.parse(raw))) assert.ok(['re', 'code', 'players', 'rev', 'visible'].includes(key), key);
});

test('status follows what the player is doing', () => {
  const env = makeHub();
  const a = greeted(env, 1, { name: 'Alice' });
  const b = greeted(env, 2, { name: 'Bob' });
  const c = greeted(env, 3, { name: 'Cat' });
  visible(a);
  const state = () => c.call('lobby.list').players.find((p) => p.name === 'Alice').state;
  assert.equal(state(), 'online');
  befriend(a, b);
  const hosted = a.call('pm.host', {});
  assert.equal(state(), 'player_match');
  const joined = b.call('pm.join', { session: hosted.session });
  assert.equal(state(), 'connecting');
  a.call('match.ready', { match: joined.match, ready: true });
  b.call('match.ready', { match: joined.match, ready: true });
  assert.equal(state(), 'in_game');
});

test('compatibility follows the existing rule: same guest image and same netplay build', () => {
  const env = makeHub();
  const base = greeted(env, 1, { name: 'Alice', guest: 'base013', build: 1 });
  const tu = greeted(env, 2, { name: 'Bob', guest: 'tu023', build: 1 });
  const newer = greeted(env, 3, { name: 'Cat', guest: 'base013', build: 2 });
  const same = greeted(env, 4, { name: 'Dan', guest: 'base013', build: 1 });
  const tuToo = greeted(env, 5, { name: 'Eve', guest: 'tu023', build: 1 });
  for (const who of [base, tu, newer, same, tuToo]) visible(who);

  const fromBase = same.call('lobby.list').players;
  const compat = (rows, name) => rows.find((p) => p.name === name).compatible;
  assert.equal(compat(fromBase, 'Alice'), true);
  assert.equal(compat(fromBase, 'Bob'), false);
  assert.equal(compat(fromBase, 'Cat'), false);
  assert.equal(compat(fromBase, 'Eve'), false);

  const fromTu = tuToo.call('lobby.list').players;
  assert.equal(compat(fromTu, 'Bob'), true);
  assert.equal(compat(fromTu, 'Alice'), false);
  assert.equal(compat(fromTu, 'Dan'), false);
  assert.ok(fromBase.some((p) => !p.compatible), 'incompatible players stay in the list');
});

test('compatible players come first, then by name', () => {
  const env = makeHub();
  const viewer = greeted(env, 9, { name: 'Viewer', guest: 'base013' });
  const rows = [['Zed', 'base013'], ['Amy', 'tu023'], ['Bob', 'base013'], ['Cat', 'tu023']];
  rows.forEach(([name, guest], i) => visible(greeted(env, i + 1, { name, guest })));
  assert.deepEqual(names(viewer.call('lobby.list')), ['Bob', 'Zed', 'Amy', 'Cat']);
});

test('a Lobby player can be added with the existing friend request, and the target hears the usual event', () => {
  const env = makeHub();
  const a = greeted(env, 1, { name: 'Alice' });
  const b = greeted(env, 2, { name: 'Bob' });
  visible(a);
  const [row] = b.call('lobby.list').players;
  assert.equal(b.call('friends.add', { code: row.player }).state, 'pending');
  const [event] = a.events('friend.request');
  assert.deepEqual([event.player, event.name], [identity(2).player, 'Bob']);
  assert.deepEqual(b.call('friends.requests').outgoing.map((r) => r.player), [identity(1).player]);
  assert.equal(b.call('friends.add', { code: row.player }).state, 'pending');
  assert.equal(a.call('friends.accept', { player: identity(2).player }).code, 'ok');
  assert.equal(b.call('friends.add', { code: row.player }).state, 'friends');
  assert.deepEqual(b.call('friends.list').friends.map((f) => f.player), [identity(1).player]);
});

test('adding a Lobby player who already sent a request makes you friends', () => {
  const env = makeHub();
  const a = greeted(env, 1, { name: 'Alice' });
  const b = greeted(env, 2, { name: 'Bob' });
  visible(a);
  visible(b);
  const toB = a.call('lobby.list').players[0];
  assert.equal(a.call('friends.add', { code: toB.player }).state, 'pending');
  const toA = b.call('lobby.list').players[0];
  assert.equal(b.call('friends.add', { code: toA.player }).state, 'friends');
});

test('a player cannot add themselves from the Lobby', () => {
  const env = makeHub();
  const a = greeted(env, 1, { name: 'Alice' });
  visible(a);
  assert.deepEqual(a.call('lobby.list').players, []);
  assert.equal(a.call('friends.add', { code: identity(1).player }).code, 'bad_request');
});

test('friends stay in the list, marked by the launcher from its own friend data', () => {
  const env = makeHub();
  const a = greeted(env, 1, { name: 'Alice' });
  const b = greeted(env, 2, { name: 'Bob' });
  befriend(a, b);
  visible(a);
  const [row] = b.call('lobby.list').players;
  assert.equal(row.player, identity(1).player);
  assert.equal(Object.hasOwn(row, 'relation'), false);
  assert.equal(b.call('friends.add', { code: row.player }).state, 'friends');
});

test('a revision the client holds gets "unchanged" until the list changes', () => {
  const env = makeHub({ local: true });
  const a = greeted(env, 1, { name: 'Alice', ip: '127.0.0.1' });
  const b = greeted(env, 2, { name: 'Bob', ip: '127.0.0.1' });
  const first = b.call('lobby.list');
  assert.ok(Number.isSafeInteger(first.rev));
  const again = b.call('lobby.list', { rev: first.rev });
  assert.deepEqual(again, { re: b.seq, code: 'ok', unchanged: true, rev: first.rev, visible: false });
  assert.equal(Object.hasOwn(again, 'players'), false);

  visible(a);
  const changed = b.call('lobby.list', { rev: first.rev });
  assert.equal(Object.hasOwn(changed, 'players'), true);
  assert.ok(changed.rev > first.rev);
  assert.equal(b.call('lobby.list', { rev: changed.rev }).unchanged, true);

  visible(a);
  assert.equal(b.call('lobby.list', { rev: changed.rev }).unchanged, true);

  a.call('pm.host', {});
  const status = b.call('lobby.list', { rev: changed.rev });
  assert.equal(status.players[0].state, 'player_match');

  visible(a, false);
  assert.equal(b.call('lobby.list', { rev: status.rev }).players.length, 0);
  visible(a);
  const back = b.call('lobby.list');
  a.drop();
  const gone = b.call('lobby.list', { rev: back.rev });
  assert.equal(gone.unchanged, undefined);
  assert.deepEqual(gone.players, []);
});

test('an older revision, or one from before a restart, is answered with the full list', () => {
  const env = makeHub();
  const a = greeted(env, 1, { name: 'Alice' });
  const b = greeted(env, 2, { name: 'Bob' });
  env.advance(5000);
  visible(a);
  assert.equal(b.call('lobby.list', { rev: 0 }).players.length, 1);
  assert.equal(b.call('lobby.list', { rev: env.hub.lobbyRev - 1 }).players.length, 1);
  assert.equal(b.call('lobby.list', { rev: env.hub.lobbyRev + 1 }).players.length, 1);
});

test('publishing and reading the Lobby write nothing to SQL', () => {
  const env = makeHub();
  const a = greeted(env, 1, { name: 'Alice' });
  const b = greeted(env, 2, { name: 'Bob' });
  const writes = countWrites(env);
  for (let i = 0; i < 3; i++) visible(a);
  visible(a, false);
  visible(a);
  for (let i = 0; i < 4; i++) b.call('lobby.list');
  b.call('lobby.list', { rev: env.hub.lobbyRev });
  assert.deepEqual(writes, []);
});

test('reading the Lobby is limited per account and counted without a write per hit', () => {
  const env = makeHub();
  const a = greeted(env, 1);
  const codes = [];
  for (let i = 0; i < 9; i++) codes.push(a.call('lobby.list').code);
  assert.deepEqual(codes.slice(0, 6), Array(6).fill('ok'));
  assert.ok(codes.slice(6).every((c) => c === 'limit'));
  env.advance(5000);
  assert.equal(a.call('lobby.list').code, 'ok');
  assert.ok((env.hub.metrics.pending.get('lobby_list_limited') ?? 0) >= 3);
});

test('malformed reads spend nothing of the limit', () => {
  const env = makeHub();
  const a = greeted(env, 1);
  for (let i = 0; i < 20; i++) assert.equal(a.call('lobby.list', { rev: 'x' }).code, 'bad_request');
  assert.equal(a.call('lobby.list').code, 'ok');
});

test('publishing shares the social message limit and cannot be flooded', () => {
  const env = makeHub();
  const a = greeted(env, 1);
  const codes = [];
  for (let i = 0; i < 30; i++) codes.push(visible(a, i % 2 === 0).code);
  assert.ok(codes.includes('limit'));
  assert.equal(codes[0], 'ok');
});

test('the loopback development server is exempt from the Lobby read limit', () => {
  const env = makeHub({ local: true });
  const a = greeted(env, 1, { ip: '127.0.0.1' });
  for (let i = 0; i < 30; i++) assert.equal(a.call('lobby.list').code, 'ok');
});

test('the list is capped and stays a few kilobytes', () => {
  const env = makeHub({ local: true });
  const viewer = greeted(env, 999, { ip: '127.0.0.1' });
  for (let i = 1; i <= LIMITS.lobbyPlayers + 25; i++) {
    visible(greeted(env, i, { ip: '127.0.0.1', name: `Player${String(i).padStart(3, '0')}` }));
  }
  const reply = viewer.call('lobby.list');
  assert.equal(reply.players.length, LIMITS.lobbyPlayers);
  assert.ok(JSON.stringify(reply).length < 12000);
  assert.equal(new Set(reply.players.map((p) => p.player)).size, LIMITS.lobbyPlayers);
});

test('the other operations answer exactly as before', () => {
  const env = makeHub();
  const a = greeted(env, 1, { name: 'Alice' });
  const b = greeted(env, 2, { name: 'Bob' });
  befriend(a, b);
  assert.equal(a.call('friends.list').friends.length, 1);
  assert.deepEqual(Object.keys(a.call('friends.list').friends[0]).sort(),
    ['build', 'compatible', 'guest', 'name', 'online', 'platform', 'player', 'state', 'version']);
  assert.equal(a.call('ping').code, 'ok');
  assert.equal(a.call('lobby.nope').code, 'bad_request');
});

let server;
let base;

before(async () => {
  server = await startLobbyServer({ port: 0 });
  base = `127.0.0.1:${server.port}`;
});

after(async () => {
  await server.close();
});

class Socket {
  static async open() {
    const s = new Socket();
    s.ws = new WebSocket(`ws://${base}/`);
    s.messages = [];
    s.seq = 0;
    s.ws.addEventListener('message', (e) => {
      if (typeof e.data === 'string' && e.data !== 'pong') s.messages.push(JSON.parse(e.data));
    });
    await new Promise((resolve, reject) => {
      s.ws.addEventListener('open', resolve, { once: true });
      s.ws.addEventListener('error', () => reject(new Error('handshake failed')), { once: true });
    });
    return s;
  }

  async next(match, timeout = 2000) {
    const deadline = Date.now() + timeout;
    for (;;) {
      const i = this.messages.findIndex(match);
      if (i >= 0) return this.messages.splice(i, 1)[0];
      if (Date.now() > deadline) throw new Error('timed out');
      await new Promise((resolve) => setTimeout(resolve, 10));
    }
  }

  async call(op, body = {}) {
    const seq = ++this.seq;
    this.ws.send(JSON.stringify({ op, seq, ...body }));
    return this.next((m) => m.re === seq);
  }

  hello(who, name, extra = {}) {
    return this.call('hello', { proto: PROTOCOL, game: GAME_ID, guest: 'base013', build: 1, player: who.player,
      token: who.token, name, version: '0.4.0', platform: 'Windows', ...extra });
  }
}

test('two local instances: visibility, listing, a friend request from the Lobby and disappearance', async () => {
  const a = await Socket.open();
  const b = await Socket.open();
  const ida = identity(61);
  const idb = identity(62);
  assert.equal((await a.hello(ida, 'Alice')).capabilities.public_lobby, true);
  await b.hello(idb, 'Bob');

  assert.deepEqual((await b.call('lobby.list')).players, []);
  assert.equal((await a.call('lobby.publish', { visible: true })).visible, true);
  const listed = await b.call('lobby.list');
  assert.deepEqual(listed.players.map((p) => [p.name, p.player, p.compatible]), [['Alice', ida.player, true]]);

  assert.equal((await b.call('friends.add', { code: listed.players[0].player })).state, 'pending');
  assert.equal((await a.next((m) => m.ev === 'friend.request')).player, idb.player);

  assert.equal((await a.call('lobby.publish', { visible: false })).visible, false);
  assert.deepEqual((await b.call('lobby.list')).players, []);

  await a.call('lobby.publish', { visible: true });
  assert.equal((await b.call('lobby.list')).players.length, 1);
  a.ws.close();
  await b.next((m) => m.ev === 'friend.offline').catch(() => null);
  for (let tries = 0; tries < 50; tries++) {
    if ((await b.call('lobby.list')).players.length === 0) return;
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  assert.fail('the closed connection stayed in the Lobby');
});
