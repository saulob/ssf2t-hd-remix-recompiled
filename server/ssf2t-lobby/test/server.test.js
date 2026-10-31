import assert from 'node:assert/strict';
import { after, before, test } from 'node:test';

import { startLobbyServer } from '../local/server.js';
import { GAME_ID, PROTOCOL, RELAY, SERVICE, WORKER_BUILD, encodeRelayFrame } from '../src/protocol.js';
import { identity } from './harness.js';

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
    s.ws.binaryType = 'arraybuffer';
    s.messages = [];
    s.seq = 0;
    s.ws.addEventListener('message', (e) => {
      if (e.data instanceof ArrayBuffer) s.messages.push({ binary: new Uint8Array(e.data) });
      else s.messages.push(e.data === 'pong' ? 'pong' : JSON.parse(e.data));
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

  hello(who, extra = {}) {
    return this.call('hello', { proto: PROTOCOL, game: GAME_ID, guest: 'base013', build: 1, player: who.player,
      token: who.token, name: 'Net', version: '0.2.2', platform: 'Windows', ...extra });
  }
}

test('health endpoint reports service, protocol, status and relay support only', async () => {
  const response = await fetch(`http://${base}/health`);
  assert.equal(response.status, 200);
  assert.deepEqual(await response.json(), { service: SERVICE, protocol: PROTOCOL, status: 'ok', worker_build: WORKER_BUILD,
    relay_supported: true, relay_max_payload: RELAY.maxPayload });
  assert.equal((await fetch(`http://${base}/nope`)).status, 404);
});

test('a probe-style session says whether the protocol is compatible', async () => {
  const probe = await Socket.open();
  assert.equal((await probe.hello(identity(1), { presence: false })).code, 'ok');
  const bad = await Socket.open();
  assert.equal((await bad.call('hello', { proto: 99 })).code, 'proto');
  assert.equal((await bad.call('ping')).code, 'no_session');
});

test('friends, Player Match and match over real WebSockets', async () => {
  const a = await Socket.open();
  const b = await Socket.open();
  const ida = identity(21);
  const idb = identity(22);
  assert.equal((await a.hello(ida)).code, 'ok');
  assert.equal((await b.hello(idb)).code, 'ok');

  assert.equal((await a.call('friends.add', { code: idb.player })).state, 'pending');
  assert.equal((await b.next((m) => m.ev === 'friend.request')).player, ida.player);
  assert.equal((await b.call('friends.accept', { player: ida.player })).code, 'ok');
  await a.next((m) => m.ev === 'friend.added');

  const hosted = await a.call('pm.host', { slots: 2 });
  assert.equal(hosted.code, 'ok');
  const found = await b.call('pm.search');
  assert.equal(found.sessions[0].player, ida.player);
  const accepted = await b.call('pm.join', { session: hosted.session });
  assert.equal(accepted.role, 'join');
  const created = await a.next((m) => m.ev === 'match.created');
  assert.equal(created.role, 'host');

  await a.call('match.endpoint', { match: created.match, port: 7001, lan: ['127.0.0.1'] });
  await b.call('match.endpoint', { match: created.match, port: 7002, lan: ['127.0.0.1'] });
  assert.equal((await a.next((m) => m.ev === 'match.peer')).role, 'join');
  const toJoin = await b.next((m) => m.ev === 'match.peer');
  assert.deepEqual([toJoin.role, toJoin.port, toJoin.same_network], ['host', 7001, true]);

  await a.call('match.leave', { match: created.match });
  assert.equal((await b.next((m) => m.ev === 'match.ended')).reason, 'left');
});

test('relay frames cross real WebSockets as binary, both ways, next to JSON signalling', async () => {
  const a = await Socket.open();
  const b = await Socket.open();
  const ida = identity(41);
  const idb = identity(42);
  assert.equal((await a.hello(ida, { relay: true })).capabilities.relay_supported, true);
  await b.hello(idb, { relay: true });
  await a.call('friends.add', { code: idb.player });
  await b.next((m) => m.ev === 'friend.request');
  await b.call('friends.accept', { player: ida.player });
  const hosted = await a.call('pm.host', { slots: 2 });
  const joined = await b.call('pm.join', { session: hosted.session });
  const created = await a.next((m) => m.ev === 'match.created');
  assert.ok(joined.relay_token > 0 && created.relay_token === joined.relay_token);
  assert.equal((await a.call('match.transport', { match: created.match, transport: 'relay' })).code, 'ok');
  assert.equal((await b.next((m) => m.ev === 'match.transport')).transport, 'relay');

  const lobby = encodeRelayFrame(joined.relay_token, 1, Uint8Array.from([7, 0, 0, 0, 1, 0, 0, 0, 2]));
  a.ws.send(lobby);
  assert.deepEqual((await b.next((m) => m.binary)).binary, lobby);
  const rollback = encodeRelayFrame(joined.relay_token, 1, new Uint8Array(256).fill(0x5a));
  b.ws.send(rollback);
  assert.deepEqual((await a.next((m) => m.binary)).binary, rollback);
  assert.equal((await b.call('ping')).code, 'ok');
  await a.call('match.leave', { match: created.match });
  assert.equal((await b.next((m) => m.ev === 'match.ended')).reason, 'left');
});

test('closing a socket makes the friend go offline for the other player', async () => {
  const a = await Socket.open();
  const b = await Socket.open();
  const ida = identity(31);
  const idb = identity(32);
  await a.hello(ida);
  await b.hello(idb);
  await a.call('friends.add', { code: idb.player });
  await b.call('friends.accept', { player: ida.player });
  a.ws.close();
  assert.equal((await b.next((m) => m.ev === 'friend.offline')).player, ida.player);
});
