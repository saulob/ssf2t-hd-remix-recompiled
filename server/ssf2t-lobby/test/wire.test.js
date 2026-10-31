import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';

import { PROTOCOL, RELAY, encodeRelayFrame, parseRelayFrame } from '../src/protocol.js';

// The relay frames and constants the game clients use (tests/wire-vectors.txt, src/netplay/relay_frame.h,
// src/online/protocol.h), so the Worker and every client platform agree on one layout.
const repo = new URL('../../../', import.meta.url);
const read = (path) => readFileSync(new URL(path, repo), 'utf8');

// SSF2T_WIRE_VECTORS names the vectors another platform's build printed (tools/test.ps1 runs the Worker on each).
const vectors = new Map(
  (process.env.SSF2T_WIRE_VECTORS ? readFileSync(process.env.SSF2T_WIRE_VECTORS, 'utf8') : read('tests/wire-vectors.txt'))
    .split('\n')
    .filter((line) => line && !line.startsWith('#'))
    .map((line) => line.trim().split(/\s+/))
    .map(([name, hex]) => [name, Uint8Array.from(hex.match(/../g).map((byte) => parseInt(byte, 16)))]),
);

function cppConstant(path, name) {
  const match = read(path).match(new RegExp(`constexpr \\w+ ${name} = (\\d+);`));
  assert.ok(match, `${name} not found in ${path}`);
  return Number(match[1]);
}

test('the Worker parses the relay frames every client platform sends', () => {
  const data = parseRelayFrame(vectors.get('relay.data'));
  assert.deepEqual(data, { type: RELAY.data, token: 0x89abcdef, seq: 7, length: 5 });
  const rollback = parseRelayFrame(vectors.get('relay.rollback'));
  assert.equal(rollback.token, 1);
  assert.equal(rollback.seq, 0xffffffff);
  assert.equal(rollback.length, vectors.get('netplay.checksum').length);
});

test('the Worker encodes relay frames byte-for-byte like the clients', () => {
  const frame = vectors.get('relay.data');
  assert.deepEqual(encodeRelayFrame(0x89abcdef, 7, frame.subarray(RELAY.headerBytes)), frame);
  const rollback = vectors.get('relay.rollback');
  assert.deepEqual(encodeRelayFrame(1, 0xffffffff, vectors.get('netplay.checksum')), rollback);
});

test('relay and lobby protocol constants match the client', () => {
  assert.equal(RELAY.version, cppConstant('src/netplay/relay_frame.h', 'kRelayVersion'));
  assert.equal(RELAY.data, cppConstant('src/netplay/relay_frame.h', 'kRelayData'));
  assert.equal(RELAY.headerBytes, cppConstant('src/netplay/relay_frame.h', 'kRelayHeaderBytes'));
  assert.equal(RELAY.maxPayload, cppConstant('src/netplay/relay_frame.h', 'kRelayMaxPayload'));
  assert.equal(PROTOCOL, cppConstant('src/online/protocol.h', 'kLobbyProtocol'));
});
