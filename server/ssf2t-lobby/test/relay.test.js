import assert from 'node:assert/strict';
import { test } from 'node:test';

import { CAPABILITIES, PROTOCOL, RELAY, encodeRelayFrame, parseRelayFrame } from '../src/protocol.js';
import { befriend, identity, makeHub } from './harness.js';

const bytes = (...values) => Uint8Array.from(values);

function relayClient(env, n, relay = true) {
  const client = env.connect(identity(n));
  client.hello({ relay });
  return client;
}

function relayMatch(env, { nA = 1, nB = 2, relayA = true, relayB = true } = {}) {
  const a = relayClient(env, nA, relayA);
  const b = relayClient(env, nB, relayB);
  befriend(a, b);
  const hosted = a.call('pm.host', {});
  const joined = b.call('pm.join', { session: hosted.session });
  const created = a.events('match.created')[0];
  a.inbox = [];
  b.inbox = [];
  return { a, b, match: joined.match, token: joined.relay_token, created, joined };
}

const lobbyPacket = (token, seq = 1) => encodeRelayFrame(token, seq, bytes(7, 1, 2, 3, 4, 5, 6, 7, 8));

test('hello answers with the service capabilities', () => {
  const env = makeHub();
  const reply = env.connect(identity(1)).hello({ relay: true });
  assert.deepEqual(reply.capabilities, CAPABILITIES);
  assert.equal(reply.capabilities.protocol_version, PROTOCOL);
  assert.equal(reply.capabilities.public_endpoints, true);
  assert.equal(reply.capabilities.relay_supported, true);
  assert.equal(reply.capabilities.relay_max_payload, RELAY.maxPayload);
});

test('relay frames encode and decode; malformed ones say why', () => {
  const frame = encodeRelayFrame(0xdeadbeef, 7, bytes(1, 2, 3));
  assert.deepEqual(parseRelayFrame(frame), { type: RELAY.data, token: 0xdeadbeef, seq: 7, length: 3 });
  const bad = (edit) => {
    const copy = Uint8Array.from(frame);
    edit(copy);
    return parseRelayFrame(copy).error;
  };
  assert.equal(bad((f) => (f[0] = 0x7b)), 'bad_magic');
  assert.equal(bad((f) => (f[2] = 9)), 'bad_version');
  assert.equal(bad((f) => (f[3] = 3)), 'bad_type');
  assert.equal(parseRelayFrame(frame.subarray(0, 10)).error, 'truncated');
  assert.equal(parseRelayFrame(frame.subarray(0, 3)).error, 'truncated');
  assert.equal(parseRelayFrame(frame.subarray(0, frame.length - 1)).error, 'bad_length');
  assert.equal(parseRelayFrame(encodeRelayFrame(1, 1, new Uint8Array(RELAY.maxPayload + 1))).error, 'oversized');
  assert.equal(parseRelayFrame(encodeRelayFrame(1, 1, new Uint8Array(0))).error, 'bad_length');
  assert.equal(parseRelayFrame(encodeRelayFrame(1, 1, new Uint8Array(RELAY.maxPayload))).length, RELAY.maxPayload);
  assert.equal(parseRelayFrame('text').error, 'not_binary');
});

test('a match between relay-capable clients has its relay from the start, for both roles', () => {
  const env = makeHub();
  const { match, token, created } = relayMatch(env);
  assert.ok(Number.isInteger(token) && token > 0);
  assert.equal(created.relay_token, token);
  assert.ok(env.logs.includes(`relay attach match=${match} role=host`));
  assert.ok(env.logs.includes(`relay attach match=${match} role=join`));
  assert.ok(env.logs.includes(`relay ready match=${match}`));
});

test('a match with an older client has no relay', () => {
  const env = makeHub();
  const { a, joined, created, match, token } = relayMatch(env, { relayB: false });
  assert.equal(token, undefined);
  assert.equal(created.relay_token, undefined);
  assert.equal(joined.code, 'ok');
  a.relay(lobbyPacket(1));
  assert.equal(a.call('match.transport', { match, transport: 'relay' }).code, 'relay_unavailable');
});

test('the host packet reaches only the joiner and the joiner packet only the host, unchanged', () => {
  const env = makeHub();
  const { a, b, token, match } = relayMatch(env, { nA: 1, nB: 2 });
  const c = relayClient(env, 3);
  befriend(a, c);
  const toJoin = lobbyPacket(token, 1);
  a.relay(toJoin);
  assert.deepEqual(b.binary, [toJoin]);
  assert.deepEqual(a.binary, []);
  assert.deepEqual(c.binary, []);
  const toHost = encodeRelayFrame(token, 1, bytes(0x53, 0x46, 0x52, 0x54));
  b.relay(toHost);
  assert.deepEqual(a.binary, [toHost]);
  assert.equal(b.binary.length, 1);
  assert.deepEqual(c.binary, []);
  assert.ok(env.logs.includes(`relay forward match=${match} direction=host_to_join bytes=${toJoin.length}`));
  assert.ok(env.logs.includes(`relay forward match=${match} direction=join_to_host bytes=${toHost.length}`));
  for (let i = 0; i < 10; ++i) a.relay(lobbyPacket(token, i + 2));
  assert.equal(b.binary.length, 11);
  assert.equal(env.logs.filter((l) => l.includes('direction=host_to_join')).length, RELAY.loggedForwards);
  assert.ok(!env.logs.some((l) => /payload|0x|\[/.test(l) && l.startsWith('relay forward')));
});

test('a third player, even one who knows the token, cannot inject into the match', () => {
  const env = makeHub();
  const { a, b, token } = relayMatch(env);
  const c = relayClient(env, 3);
  befriend(a, c);
  befriend(b, c);
  c.relay(lobbyPacket(token));
  assert.deepEqual([a.binary.length, b.binary.length], [0, 0]);
  assert.ok(env.logs.some((l) => l.startsWith('relay drop reason=no_relay')));
  const stranger = env.connect(identity(4));
  stranger.relay(lobbyPacket(token));
  assert.ok(env.logs.some((l) => l.startsWith('relay drop reason=unknown_connection')));
  assert.deepEqual([a.binary.length, b.binary.length], [0, 0]);
});

test('a frame with another match token is dropped and the sender told once', () => {
  const env = makeHub();
  const { a, b, token, match } = relayMatch(env);
  const told = a.relay(lobbyPacket(token ^ 1));
  a.relay(lobbyPacket(token ^ 1));
  assert.equal(b.binary.length, 0);
  assert.deepEqual(told, [{ ev: 'relay.closed', match, reason: 'unavailable' }]);
  assert.equal(a.events('relay.closed').length, 0);
  assert.ok(env.logs.some((l) => l.startsWith(`relay drop reason=stale_token match=${match}`)));
});

test('malformed and oversized frames are dropped without closing the socket', () => {
  const env = makeHub();
  const { a, b, token } = relayMatch(env);
  const wrongMagic = lobbyPacket(token);
  wrongMagic[1] = 0x46;
  a.relay(wrongMagic);
  a.relay(bytes(0x53, 0x52, 1));
  const oversized = encodeRelayFrame(token, 1, new Uint8Array(RELAY.maxPayload + 1));
  a.relay(oversized);
  const wrongVersion = lobbyPacket(token);
  wrongVersion[2] = 2;
  a.relay(wrongVersion);
  assert.equal(b.binary.length, 0);
  assert.equal(a.closedWith, null);
  for (const reason of ['bad_magic', 'truncated', 'oversized', 'bad_version']) {
    assert.ok(env.logs.some((l) => l.startsWith(`relay drop reason=${reason}`)), reason);
  }
  a.relay(lobbyPacket(token));
  assert.equal(b.binary.length, 1);
});

test('only the host chooses the transport; the joiner hears it before any relayed frame', () => {
  const env = makeHub();
  const { a, b, match, token } = relayMatch(env);
  assert.equal(b.call('match.transport', { match, transport: 'relay' }).code, 'bad_request');
  assert.equal(a.call('match.transport', { match, transport: 'udp' }).code, 'bad_request');
  assert.equal(a.call('match.transport', { match: 'other', transport: 'relay' }).code, 'not_found');
  assert.equal(a.call('match.transport', { match, transport: 'relay' }).code, 'ok');
  assert.deepEqual(b.events('match.transport'), [{ ev: 'match.transport', match, transport: 'relay' }]);
  assert.ok(env.logs.includes(`relay transport selected match=${match}`));
  a.relay(lobbyPacket(token));
  assert.equal(b.binary.length, 1);
});

test('choosing direct closes the relay quietly', () => {
  const env = makeHub();
  const { a, b, match, token } = relayMatch(env);
  assert.equal(a.call('match.transport', { match, transport: 'direct' }).code, 'ok');
  assert.deepEqual(b.events('match.transport'), [{ ev: 'match.transport', match, transport: 'direct' }]);
  assert.equal(b.events('relay.closed').length, 0);
  a.relay(lobbyPacket(token));
  assert.equal(b.binary.length, 0);
  assert.ok(env.logs.includes(`relay close match=${match} reason=direct_selected`));
});

test('match.leave clears the relay', () => {
  const env = makeHub();
  const { a, b, match, token } = relayMatch(env);
  a.relay(lobbyPacket(token));
  assert.equal(b.call('match.leave', { match }).code, 'ok');
  a.relay(lobbyPacket(token, 2));
  assert.equal(b.binary.length, 1);
  assert.ok(env.logs.includes(`relay close match=${match} reason=left`));
  assert.equal(env.hub.relayRoutes.size, 0);
  assert.equal(env.hub.relayMatches.size, 0);
});

test('a disconnect ends the relay for the peer and a resumed socket gets none', () => {
  const env = makeHub();
  const { a, b, match, token } = relayMatch(env);
  a.relay(lobbyPacket(token));
  b.drop();
  assert.deepEqual(a.events('relay.closed'), [{ ev: 'relay.closed', match, reason: 'peer_disconnected' }]);
  assert.ok(env.logs.includes(`relay close match=${match} reason=peer_disconnected`));
  a.relay(lobbyPacket(token, 2));
  const b2 = relayClient(env, 2);
  const resumed = b2.call('match.resume', { match });
  assert.equal(resumed.code, 'ok');
  assert.equal(resumed.relay_token, undefined);
  a.relay(lobbyPacket(token, 3));
  assert.deepEqual([b.binary.length, b2.binary.length], [1, 0]);
  assert.equal(a.call('match.transport', { match, transport: 'relay' }).code, 'relay_unavailable');
});

test('a stale socket of a player cannot inject after its seat was taken', () => {
  const env = makeHub();
  const { a, b, match, token } = relayMatch(env);
  const b2 = relayClient(env, 2);
  b.drop();
  assert.equal(b2.call('match.resume', { match }).code, 'ok');
  b.relay(lobbyPacket(token));
  b2.relay(lobbyPacket(token));
  assert.equal(a.binary.length, 0);
});

test('a new match never receives the old match frames', () => {
  const env = makeHub();
  const first = relayMatch(env);
  first.a.call('match.leave', { match: first.match });
  const hosted = first.a.call('pm.host', {});
  const joined = first.b.call('pm.join', { session: hosted.session });
  assert.notEqual(joined.match, first.match);
  first.b.inbox = [];
  first.a.relay(lobbyPacket(first.token));
  assert.equal(first.b.binary.length, 0);
  assert.equal(first.b.events('relay.closed').length, 0);
  first.a.relay(lobbyPacket(joined.relay_token));
  assert.equal(first.b.binary.length, 1);
});

test('rate limits drop a flood and then close the relay; normal gameplay rates pass', () => {
  const env = makeHub();
  const { a, b, match, token } = relayMatch(env);
  for (let second = 0; second < 5; ++second) {
    for (let i = 0; i < 120; ++i) {
      a.relay(lobbyPacket(token, i));
      b.relay(lobbyPacket(token, i));
    }
    env.advance(1000);
  }
  assert.deepEqual([a.binary.length, b.binary.length], [600, 600]);
  assert.ok(!env.logs.some((l) => l.includes('reason=rate_limit')));
  const big = encodeRelayFrame(token, 1, new Uint8Array(RELAY.maxPayload));
  let sent = 0;
  while (!env.logs.some((l) => l === `relay close match=${match} reason=rate_limit`) && sent < 10000) {
    a.relay(big);
    sent += 1;
  }
  assert.ok(sent < 10000);
  assert.ok(b.binary.length > 600 && b.binary.length < 600 + sent);
  assert.deepEqual(b.events('relay.closed'), [{ ev: 'relay.closed', match, reason: 'rate_limit' }]);
  const before = b.binary.length;
  a.relay(lobbyPacket(token));
  assert.equal(b.binary.length, before);
});

test('a peer socket that cannot take the frame ends the relay', () => {
  const env = makeHub();
  const { a, b, match, token } = relayMatch(env);
  b.failBinary = true;
  const told = a.relay(lobbyPacket(token));
  assert.ok(env.logs.includes(`relay close match=${match} reason=peer_unavailable`));
  assert.deepEqual(told, [{ ev: 'relay.closed', match, reason: 'peer_unavailable' }]);
});

test('signalling keeps working while frames are relayed', () => {
  const env = makeHub();
  const { a, b, match, token } = relayMatch(env);
  a.relay(lobbyPacket(token));
  assert.equal(a.call('match.endpoint', { match, port: 7001 }).code, 'ok');
  assert.equal(b.call('match.endpoint', { match, port: 7002 }).code, 'ok');
  assert.equal(a.events('match.peer').length, 1);
  assert.equal(b.call('ping').code, 'ok');
  assert.equal(a.call('friends.list').friends.length, 1);
  b.relay(lobbyPacket(token));
  assert.deepEqual([a.binary.length, b.binary.length], [1, 1]);
});
