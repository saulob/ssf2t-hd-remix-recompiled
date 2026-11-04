import assert from 'node:assert/strict';
import { test } from 'node:test';

import { ADMIN_HTML } from '../src/admin_page.js';
import { CHARACTERS, SECRET_CHARACTER, characterName } from '../src/characters.js';
import { Hub } from '../src/hub.js';
import { CAPABILITIES, PROTOCOL, WORKER_BUILD, effectiveSecret, normalizeSecret, parseHello, secretEstablishment,
  secretFromMask, secretMask } from '../src/protocol.js';
import { openDashboard } from './dom.js';
import { SECRET_ON, characters, daily, dimension, fightEvent, playFight, send, startFightMatch } from './fight_support.js';
import { befriend, identity, makeHub } from './harness.js';

const caps = (supports, enabled = supports) => ({
  supports: { akuma: supports.includes('akuma'), evil_ryu: supports.includes('evil_ryu') },
  enabled: { akuma: enabled.includes('akuma'), evil_ryu: enabled.includes('evil_ryu') },
});
const NONE = caps([]);
const SUPPORTED_OFF = caps(['akuma', 'evil_ryu'], []);
const FALSE_ALL = { supports: { akuma: false, evil_ryu: false }, enabled: { akuma: false, evil_ryu: false } };
const KEYS = ['akuma', 'evil_ryu'];
const attOf = (client) => client.conn.attachment();
const maskOf = (env, column) => env.host.sql.get(`SELECT ${column} AS n FROM matches`).n;

function countWrites(env) {
  const writes = [];
  const run = env.host.sql.run;
  env.host.sql.run = (query, ...args) => {
    if (/^\s*(INSERT|UPDATE|DELETE)/i.test(query)) writes.push(query.replace(/\s+/g, ' ').trim());
    return run(query, ...args);
  };
  return writes;
}

// Two friends paired into a match; the host's match.created event and the joiner's answer are kept.
function pair(env, secretA, secretB, { nA = 1, nB = 2, ipA = '203.0.113.1', ipB = '203.0.113.2' } = {}) {
  const a = env.connect(identity(nA), { ip: ipA });
  const b = env.connect(identity(nB), { ip: ipB });
  assert.equal(a.hello(secretA ? { secret: secretA } : {}).code, 'ok');
  assert.equal(b.hello(secretB ? { secret: secretB } : {}).code, 'ok');
  befriend(a, b);
  const hosted = a.call('pm.host', {});
  const joined = b.call('pm.join', { session: hosted.session });
  assert.equal(joined.code, 'ok');
  const created = a.events('match.created')[0];
  return { a, b, match: joined.match, joined, created };
}

const establish = (m) => {
  m.a.call('match.ready', { match: m.match, ready: true });
  m.b.call('match.ready', { match: m.match, ready: true });
};

// ---- the wire ----
test('the service announces secret characters and the protocol number stays 3', () => {
  assert.equal(CAPABILITIES.secret_characters, true);
  assert.equal(CAPABILITIES.protocol_version, 3);
  assert.equal(PROTOCOL, 3);
  assert.equal(WORKER_BUILD, '2026-11-04-secret1');
});

test('hello stores the advertised capabilities and answers secret_characters', () => {
  const env = makeHub();
  const a = env.connect(identity(1));
  const reply = a.hello({ secret: caps(['akuma', 'evil_ryu'], ['evil_ryu']) });
  assert.equal(reply.code, 'ok');
  assert.equal(reply.capabilities.secret_characters, true);
  assert.equal(reply.capabilities.protocol_version, 3);
  assert.deepEqual(attOf(a).secret, caps(['akuma', 'evil_ryu'], ['evil_ryu']));
  assert.deepEqual(JSON.parse(JSON.stringify(attOf(a).secret)), attOf(a).secret);
});

test('a hello without the field is an old client: accepted, protocol 3, everything false', () => {
  const env = makeHub();
  const a = env.connect(identity(1));
  const reply = a.hello();
  assert.equal(reply.code, 'ok');
  assert.deepEqual(attOf(a).secret, FALSE_ALL);
  assert.deepEqual(parseHello({ proto: 3, game: 'ssf2t-hd-remix-pc', guest: 'base013', build: 1, ...identity(1) }).secret, FALSE_ALL);
});

test('a malformed secret field is read as false and never an error', () => {
  const bad = ['true', 1, true, null, [], [true], {}, { supports: 'x', enabled: 5 }, { supports: [], enabled: [] },
    { supports: { akuma: 'true', evil_ryu: 1 }, enabled: { akuma: 'yes', evil_ryu: [] } },
    { supports: { akuma: 1, evil_ryu: {} }, enabled: { akuma: 1, evil_ryu: {} } }];
  bad.forEach((value, i) => {
    const env = makeHub();
    const a = env.connect(identity(i + 1));
    assert.equal(a.hello({ secret: value }).code, 'ok', JSON.stringify(value));
    assert.deepEqual(attOf(a).secret, FALSE_ALL, JSON.stringify(value));
  });
  const env = makeHub();
  const a = env.connect(identity(1));
  assert.equal(a.hello({ secret: { supports: { akuma: true, evil_ryu: 'true' }, enabled: 'on' } }).code, 'ok');
  assert.deepEqual(attOf(a).secret, caps(['akuma'], []));
});

test('enabled without supports is false, and unknown keys are ignored', () => {
  const env = makeHub();
  const a = env.connect(identity(1));
  a.hello({ secret: { supports: { akuma: false, evil_ryu: true, violent_ken: true, extra: true },
    enabled: { akuma: true, evil_ryu: true, violent_ken: true }, other: true } });
  assert.deepEqual(attOf(a).secret, caps(['evil_ryu']));
  assert.ok(!('violent_ken' in attOf(a).secret.supports) && !('extra' in attOf(a).secret.supports));
});

test('the mask layout is supports akuma, supports evil_ryu, enabled akuma, enabled evil_ryu', () => {
  assert.equal(secretMask(caps(['akuma'], [])), 1);
  assert.equal(secretMask(caps(['evil_ryu'], [])), 2);
  assert.equal(secretMask(caps(['akuma'])), 5);
  assert.equal(secretMask(caps(['evil_ryu'])), 10);
  assert.equal(secretMask(SECRET_ON), 15);
  assert.equal(secretMask({ supports: { akuma: false }, enabled: { akuma: true } }), 0);
  for (let mask = 0; mask < 16; mask++) {
    const secret = secretFromMask(mask);
    assert.deepEqual(secret, normalizeSecret(secret));
    const reachable = (mask & 4 ? mask & 1 : 1) && (mask & 8 ? mask & 2 : 1);
    if (reachable) assert.equal(secretMask(secret), mask);
  }
  assert.deepEqual(secretFromMask(undefined), FALSE_ALL);
  assert.deepEqual(secretFromMask(null), FALSE_ALL);
});

test('violent_ken is reserved: it is a character name but never a capability', () => {
  assert.equal(characterName(17), 'Evil Ryu');
  assert.equal(characterName(18), 'Violent Ken');
  assert.equal(CHARACTERS.length, 19);
  assert.deepEqual(SECRET_CHARACTER, { 16: 'akuma', 17: 'evil_ryu', 18: 'violent_ken' });
  assert.deepEqual(Object.keys(effectiveSecret(SECRET_ON, SECRET_ON)), KEYS);
});

// ---- effective capabilities ----
test('effective is true only when both seats support and enabled the character, for each character', () => {
  for (const key of KEYS) {
    for (const [a, b] of [[true, true], [true, false], [false, true], [false, false]]) {
      const env = makeHub();
      const side = (on) => caps([key], on ? [key] : []);
      const m = pair(env, side(a), side(b));
      const want = a && b;
      const other = KEYS.find((k) => k !== key);
      assert.equal(m.joined.secret.effective[key], want, `${key} ${a}/${b}`);
      assert.equal(m.created.secret.effective[key], want, `${key} ${a}/${b}`);
      assert.equal(m.joined.secret.effective[other], false);
      assert.deepEqual(m.joined.secret.peer, side(a));
      assert.deepEqual(m.created.secret.peer, side(b));
    }
  }
});

test('an old client and a new client never get a secret character', () => {
  for (const [sa, sb] of [[SECRET_ON, undefined], [undefined, SECRET_ON]]) {
    const env = makeHub();
    const m = pair(env, sa, sb);
    for (const view of [m.joined.secret, m.created.secret]) {
      assert.deepEqual(view.effective, { akuma: false, evil_ryu: false });
    }
    assert.deepEqual(m.joined.secret.peer, sa ?? FALSE_ALL);
    assert.deepEqual(m.created.secret.peer, sb ?? FALSE_ALL);
  }
});

test('the match row stores both masks and the views never carry a player id', () => {
  const env = makeHub();
  const m = pair(env, caps(['akuma']), caps(['akuma', 'evil_ryu'], ['evil_ryu']));
  assert.equal(maskOf(env, 'host_secret'), 5);
  assert.equal(maskOf(env, 'join_secret'), 11);
  const dump = JSON.stringify([m.joined.secret, m.created.secret]);
  for (const id of [m.a.who.player, m.b.who.player, m.match]) assert.ok(!dump.includes(id));
  assert.deepEqual(Object.keys(m.joined.secret).sort(), ['effective', 'peer']);
  assert.deepEqual(Object.keys(m.joined.secret.peer).sort(), ['enabled', 'supports']);
});

test('the pure helpers agree with the truth table', () => {
  assert.deepEqual(effectiveSecret(SECRET_ON, SECRET_ON), { akuma: true, evil_ryu: true });
  assert.deepEqual(effectiveSecret(SECRET_ON, SUPPORTED_OFF), { akuma: false, evil_ryu: false });
  assert.deepEqual(effectiveSecret(SECRET_ON, caps(['akuma'])), { akuma: true, evil_ryu: false });
  assert.deepEqual(effectiveSecret(SECRET_ON, undefined), { akuma: false, evil_ryu: false });
  assert.deepEqual(effectiveSecret('x', 5), { akuma: false, evil_ryu: false });
});

// ---- secret.update ----
test('secret.update before hello is refused', () => {
  const env = makeHub();
  const a = env.connect(identity(1));
  assert.equal(a.call('secret.update', { secret: SECRET_ON }).code, 'no_session');
});

test('secret.update outside a match only changes what the next match starts with', () => {
  const env = makeHub();
  const a = env.connect(identity(1), { ip: '203.0.113.1' });
  const b = env.connect(identity(2), { ip: '203.0.113.2' });
  a.hello();
  b.hello({ secret: SECRET_ON });
  befriend(a, b);
  const writes = countWrites(env);
  const reply = a.call('secret.update', { secret: SECRET_ON });
  assert.deepEqual(reply, { re: a.seq, code: 'ok' });
  assert.deepEqual(attOf(a).secret, SECRET_ON);
  assert.deepEqual(writes, []);
  const hosted = a.call('pm.host', {});
  const joined = b.call('pm.join', { session: hosted.session });
  assert.deepEqual(joined.secret.effective, { akuma: true, evil_ryu: true });
});

test('a changed secret.update updates the stored mask and tells both seats the same effective view', () => {
  const env = makeHub();
  const m = pair(env, SECRET_ON, SECRET_ON);
  m.a.inbox = [];
  m.b.inbox = [];
  const writes = countWrites(env);
  const next = caps(['akuma', 'evil_ryu'], ['akuma']);
  const reply = m.a.call('secret.update', { secret: next });
  assert.equal(reply.code, 'ok');
  assert.deepEqual(reply.secret.effective, { akuma: true, evil_ryu: false });
  assert.deepEqual(reply.secret.peer, SECRET_ON);
  assert.equal(maskOf(env, 'host_secret'), secretMask(next));
  assert.equal(maskOf(env, 'join_secret'), 15);
  assert.deepEqual(writes.filter((q) => q.includes('_secret')), ['UPDATE matches SET host_secret = ? WHERE id = ?']);
  assert.equal(writes.length, 1);
  const toA = m.a.events('match.secret');
  const toB = m.b.events('match.secret');
  assert.equal(toA.length, 1);
  assert.equal(toB.length, 1);
  assert.equal(toA[0].match, m.match);
  assert.deepEqual(toA[0].secret.effective, toB[0].secret.effective);
  assert.deepEqual(toA[0].secret.effective, { akuma: true, evil_ryu: false });
  assert.deepEqual(toA[0].secret.peer, SECRET_ON);
  assert.deepEqual(toB[0].secret.peer, next);
  assert.deepEqual(Object.keys(toB[0]).sort(), ['ev', 'match', 'secret']);
  const dump = JSON.stringify([toA, toB]);
  for (const id of [m.a.who.player, m.b.who.player]) assert.ok(!dump.includes(id));
  m.b.call('secret.update', { secret: caps(['akuma', 'evil_ryu'], ['evil_ryu']) });
  assert.equal(maskOf(env, 'join_secret'), 11);
  assert.deepEqual(m.a.events('match.secret')[0].secret.effective, { akuma: false, evil_ryu: false });
});

test('an unchanged secret.update writes nothing and sends no event', () => {
  const env = makeHub();
  const m = pair(env, SECRET_ON, caps(['akuma']));
  m.a.inbox = [];
  m.b.inbox = [];
  const writes = countWrites(env);
  for (let i = 0; i < 5; i++) {
    const reply = m.a.call('secret.update', { secret: SECRET_ON });
    assert.equal(reply.code, 'ok');
    assert.deepEqual(reply.secret.effective, { akuma: true, evil_ryu: false });
  }
  assert.equal(m.b.call('secret.update', { secret: { ...caps(['akuma']), junk: 1 } }).code, 'ok');
  assert.deepEqual(writes, []);
  assert.deepEqual([...m.a.inbox, ...m.b.inbox], []);
});

test('a malformed secret.update clears the capabilities instead of failing', () => {
  const env = makeHub();
  const m = pair(env, SECRET_ON, SECRET_ON);
  m.b.inbox = [];
  assert.equal(m.a.call('secret.update', { secret: 'yes' }).code, 'ok');
  assert.deepEqual(attOf(m.a).secret, FALSE_ALL);
  assert.equal(maskOf(env, 'host_secret'), 0);
  assert.deepEqual(m.b.events('match.secret')[0].secret.effective, { akuma: false, evil_ryu: false });
  assert.equal(m.a.call('secret.update', {}).code, 'ok');
  assert.equal(m.a.call('secret.update', { secret: { enabled: { akuma: true } } }).code, 'ok');
  assert.deepEqual(attOf(m.a).secret, FALSE_ALL);
});

test('secret.update shares the social bucket and cannot flood writes', () => {
  const env = makeHub();
  const m = pair(env, SECRET_ON, SECRET_ON);
  const writes = countWrites(env);
  let limited = 0;
  let accepted = 0;
  for (let i = 0; i < 30; i++) {
    const reply = m.b.call('secret.update', { secret: i % 2 ? SECRET_ON : SUPPORTED_OFF });
    if (reply.code === 'limit') limited++;
    else if (reply.code === 'ok') accepted++;
  }
  assert.ok(limited > 0);
  assert.ok(accepted <= 10);
  assert.ok(writes.length <= 10);
  assert.equal(m.b.call('ping').code, 'ok');
  assert.equal(m.b.call('secret.update', { secret: NONE }).code, 'limit');
  env.advance(2000);
  assert.equal(m.b.call('secret.update', { secret: NONE }).code, 'ok');
});

test('leaving a match then updating touches no match row', () => {
  const env = makeHub();
  const m = pair(env, SECRET_ON, SECRET_ON);
  m.a.call('match.leave', { match: m.match });
  const writes = countWrites(env);
  assert.deepEqual(m.a.call('secret.update', { secret: NONE }), { re: m.a.seq, code: 'ok' });
  assert.deepEqual(writes, []);
});

// ---- resume and later matches ----
function reconnect(env, n, ip, secret) {
  const client = env.connect(identity(n), { ip });
  assert.equal(client.hello(secret ? { secret } : {}).code, 'ok');
  return client;
}

test('match.resume refreshes the resuming seat from the new socket and tells the peer', () => {
  const env = makeHub();
  const m = pair(env, SECRET_ON, SECRET_ON);
  m.b.inbox = [];
  m.a.drop();
  env.advance(5000);
  const back = reconnect(env, 1, '203.0.113.1', caps(['akuma', 'evil_ryu'], ['akuma']));
  const view = back.call('match.resume', { match: m.match });
  assert.equal(view.code, 'ok');
  assert.deepEqual(view.secret.effective, { akuma: true, evil_ryu: false });
  assert.deepEqual(view.secret.peer, SECRET_ON);
  assert.equal(maskOf(env, 'host_secret'), secretMask(caps(['akuma', 'evil_ryu'], ['akuma'])));
  const told = m.b.events('match.secret');
  assert.equal(told.length, 1);
  assert.deepEqual(told[0].secret.effective, { akuma: true, evil_ryu: false });
  assert.deepEqual(told[0].secret.peer, caps(['akuma', 'evil_ryu'], ['akuma']));
});

test('a resume with unchanged capabilities writes no mask and sends no secret event', () => {
  const env = makeHub();
  const m = pair(env, SECRET_ON, SECRET_ON);
  m.b.inbox = [];
  m.a.drop();
  env.advance(5000);
  const back = reconnect(env, 1, '203.0.113.1', SECRET_ON);
  const writes = countWrites(env);
  assert.equal(back.call('match.resume', { match: m.match }).code, 'ok');
  assert.deepEqual(writes.filter((q) => q.includes('_secret')), []);
  assert.deepEqual(m.b.events('match.secret'), []);
  assert.equal(m.b.events('match.peer_back').length, 1);
});

test('a resuming old client replaces the stored capabilities with none', () => {
  const env = makeHub();
  const m = pair(env, SECRET_ON, SECRET_ON);
  m.a.drop();
  env.advance(5000);
  const back = reconnect(env, 1, '203.0.113.1');
  const view = back.call('match.resume', { match: m.match });
  assert.deepEqual(view.secret.effective, { akuma: false, evil_ryu: false });
  assert.equal(maskOf(env, 'host_secret'), 0);
});

test('a new match recomputes from the current sockets, nothing is carried over', () => {
  const env = makeHub();
  const m = pair(env, SECRET_ON, SECRET_ON);
  assert.deepEqual(m.joined.secret.effective, { akuma: true, evil_ryu: true });
  m.a.call('match.leave', { match: m.match });
  m.a.drop();
  const old = reconnect(env, 1, '203.0.113.1');
  const hosted = old.call('pm.host', {});
  const joined = m.b.call('pm.join', { session: hosted.session });
  assert.equal(joined.code, 'ok');
  assert.deepEqual(joined.secret.effective, { akuma: false, evil_ryu: false });
  assert.deepEqual(joined.secret.peer, FALSE_ALL);
  assert.equal(maskOf(env, 'host_secret'), 0);
  assert.equal(maskOf(env, 'join_secret'), 15);
  old.call('match.leave', { match: joined.match });
  old.call('secret.update', { secret: SECRET_ON });
  const again = old.call('pm.host', {});
  const third = m.b.call('pm.join', { session: again.session });
  assert.deepEqual(third.secret.effective, { akuma: true, evil_ryu: true });
});

test('databases from before the secret columns gain them and keep working', () => {
  const env = makeHub();
  env.host.sql.run('ALTER TABLE matches DROP COLUMN host_secret');
  env.host.sql.run('ALTER TABLE matches DROP COLUMN join_secret');
  env.host.sql.run('ALTER TABLE match_telemetry DROP COLUMN secret_akuma');
  env.host.sql.run('ALTER TABLE match_telemetry DROP COLUMN secret_evil_ryu');
  const hub = new Hub(env.host);
  assert.ok(hub);
  const columns = (table) => env.host.sql.all(`PRAGMA table_info(${table})`).map((c) => c.name);
  assert.ok(columns('matches').includes('host_secret') && columns('matches').includes('join_secret'));
  assert.ok(columns('match_telemetry').includes('secret_akuma') && columns('match_telemetry').includes('secret_evil_ryu'));
});

// ---- fight telemetry ----
test('a secret character is refused unless both seats have it available, and nothing is recorded', () => {
  const cases = [['two old clients', undefined, undefined], ['one old client', SECRET_ON, undefined],
    ['option off on one side', SECRET_ON, SUPPORTED_OFF], ['both off', SUPPORTED_OFF, SUPPORTED_OFF]];
  for (const [label, sa, sb] of cases) {
    const env = makeHub();
    const m = startFightMatch(env, { secretA: sa, secretB: sb });
    let fight = 0;
    for (const id of [16, 17, 18]) {
      for (const slots of [{ p1: id, p2: 0 }, { p1: 0, p2: id }]) {
        const reply = send(m.a, fightEvent(m, 'start', fight++, slots));
        assert.equal(reply.code, 'bad_request', `${label} ${id}`);
      }
    }
    assert.equal(env.hub.metrics.pending.get('fight_secret_rejected'), 6, label);
    assert.equal(daily(env, 'fights_started'), 0);
    assert.deepEqual(characters(env), {});
    assert.deepEqual(dimension(env, 'secret_fight'), {});
    assert.deepEqual(dimension(env, 'secret_match'), {});
    assert.equal(env.host.sql.all('SELECT * FROM match_fights').length, 0);
    assert.equal(env.host.sql.get('SELECT fights_started AS n FROM match_telemetry').n, 0);
    assert.equal(send(m.a, fightEvent(m, 'start', 40, { p1: 0, p2: 4 })).code, 'ok');
  }
});

test('a secret character is accepted when both seats support and enabled it, and violent_ken never is', () => {
  const env = makeHub();
  const m = startFightMatch(env, { secret: SECRET_ON });
  assert.equal(send(m.a, fightEvent(m, 'start', 0, { p1: 16, p2: 0 })).counted, true);
  assert.equal(send(m.a, fightEvent(m, 'start', 1, { p1: 4, p2: 17 })).counted, true);
  assert.equal(send(m.a, fightEvent(m, 'start', 2, { p1: 16, p2: 17 })).counted, true);
  assert.equal(send(m.a, fightEvent(m, 'start', 3, { p1: 18, p2: 0 })).code, 'bad_request');
  assert.equal(send(m.a, fightEvent(m, 'start', 4, { p1: 0, p2: 18 })).code, 'bad_request');
  assert.equal(send(m.b, fightEvent(m, 'start', 5, { p1: 17, p2: 17 })).counted, true);
  assert.equal(daily(env, 'fights_started'), 4);
  assert.equal(send(m.a, fightEvent(m, 'complete', 0, { p1: 16, p2: 0 })).counted, true);
  assert.equal(env.hub.metrics.pending.get('fight_secret_rejected'), 2);
});

test('each character is checked on its own', () => {
  const env = makeHub();
  const m = startFightMatch(env, { secret: caps(['akuma', 'evil_ryu'], ['akuma']) });
  assert.equal(send(m.a, fightEvent(m, 'start', 0, { p1: 16, p2: 16 })).code, 'ok');
  assert.equal(send(m.a, fightEvent(m, 'start', 1, { p1: 16, p2: 17 })).code, 'bad_request');
  m.a.call('secret.update', { secret: SECRET_ON });
  m.b.call('secret.update', { secret: SECRET_ON });
  assert.equal(send(m.a, fightEvent(m, 'start', 1, { p1: 16, p2: 17 })).code, 'ok');
});

test('a completion keeps the start rule even if the capabilities changed in between', () => {
  const env = makeHub();
  const m = startFightMatch(env, { secret: SECRET_ON });
  send(m.a, fightEvent(m, 'start', 0, { p1: 17, p2: 0 }));
  m.b.call('secret.update', { secret: NONE });
  const done = send(m.a, fightEvent(m, 'complete', 0, { p1: 17, p2: 0 }));
  assert.equal(done.code, 'ok');
  assert.equal(done.counted, true);
  assert.equal(send(m.a, fightEvent(m, 'complete', 0, { p1: 0, p2: 0 })).code, 'ok');
  assert.equal(send(m.a, fightEvent(m, 'start', 1, { p1: 17, p2: 0 })).code, 'bad_request');
});

test('Evil Ryu is counted apart from Ryu in the characters and the matchups', () => {
  const env = makeHub();
  const m = startFightMatch(env, { secret: SECRET_ON });
  playFight(m, 0, { p1: 17, p2: 4, result: 'p1' });
  playFight(m, 1, { p1: 0, p2: 4, result: 'p2' });
  playFight(m, 2, { p1: 16, p2: 17, result: 'p2' });
  const history = env.hub.metricsHistory(30);
  const row = (id) => history.characters.find((c) => c.id === id);
  assert.deepEqual([row(17).name, row(17).picks, row(17).wins, row(17).losses], ['Evil Ryu', 2, 2, 0]);
  assert.deepEqual([row(0).name, row(0).picks, row(0).wins, row(0).losses], ['Ryu', 1, 0, 1]);
  assert.deepEqual([row(16).name, row(16).picks, row(16).losses], ['Akuma', 1, 1]);
  const names = history.matchups.map((x) => `${x.a_name} vs ${x.b_name}`).sort();
  assert.deepEqual(names, ['Ken vs Evil Ryu', 'Ryu vs Ken', 'Akuma vs Evil Ryu'].sort());
  const against = (a, b) => history.matchups.find((x) => x.a === a && x.b === b);
  assert.equal(against(4, 17).fights, 1);
  assert.equal(against(4, 17).b_wins, 1);
  assert.equal(against(0, 4).fights, 1);
});

// ---- counters ----
test('a match counts available, option mismatch and unsupported peer once, when it is first established', () => {
  const cases = [
    ['both on', SECRET_ON, SECRET_ON, { akuma_available: 1, evil_ryu_available: 1 }],
    ['one option off', SECRET_ON, caps(['akuma', 'evil_ryu'], ['akuma']), { akuma_available: 1, evil_ryu_option_mismatch: 1 }],
    ['both mismatched', caps(['akuma', 'evil_ryu'], ['akuma']), caps(['akuma', 'evil_ryu'], ['evil_ryu']),
      { akuma_option_mismatch: 1, evil_ryu_option_mismatch: 1 }],
    ['old peer', SECRET_ON, undefined, { unsupported_peer: 1 }],
    ['old peer, option off', SUPPORTED_OFF, undefined, {}],
    ['both old', undefined, undefined, {}],
    ['both supported, both off', SUPPORTED_OFF, SUPPORTED_OFF, {}],
    ['one character known to one side', caps(['akuma']), SECRET_ON, { akuma_available: 1, unsupported_peer: 1 }],
    ['one side off, one character unknown to it', caps(['akuma'], []), SECRET_ON, { akuma_option_mismatch: 1, unsupported_peer: 1 }],
  ];
  for (const [label, sa, sb, want] of cases) {
    const env = makeHub();
    const m = startFightMatch(env, { secretA: sa, secretB: sb });
    assert.deepEqual(dimension(env, 'secret'), {}, label);
    m.a.call('match.ready', { match: m.match, ready: true });
    assert.deepEqual(dimension(env, 'secret'), {}, `${label} before the peer is ready`);
    m.b.call('match.ready', { match: m.match, ready: true });
    assert.deepEqual(dimension(env, 'secret'), want, label);
    m.b.call('match.ready', { match: m.match, ready: false });
    m.b.call('match.ready', { match: m.match, ready: true });
    m.a.call('match.ready', { match: m.match, ready: false });
    m.a.call('match.ready', { match: m.match, ready: true });
    assert.deepEqual(dimension(env, 'secret'), want, `${label} counted once`);
  }
});

test('the classification is exclusive per character and unsupported_peer is once per match', () => {
  assert.deepEqual(secretEstablishment(SECRET_ON, SECRET_ON), ['akuma_available', 'evil_ryu_available']);
  assert.deepEqual(secretEstablishment(SECRET_ON, undefined), ['unsupported_peer']);
  assert.deepEqual(secretEstablishment(undefined, SECRET_ON), ['unsupported_peer']);
  assert.deepEqual(secretEstablishment(SECRET_ON, caps(['akuma', 'evil_ryu'], [])), ['akuma_option_mismatch', 'evil_ryu_option_mismatch']);
  assert.deepEqual(secretEstablishment(caps(['akuma'], []), caps(['akuma'], [])), []);
});

test('a second match counts again and a resumed match does not', () => {
  const env = makeHub();
  const m = startFightMatch(env, { secret: SECRET_ON });
  establish(m);
  m.a.drop();
  env.advance(5000);
  const back = reconnect(env, 1, '203.0.113.1', SECRET_ON);
  back.call('match.resume', { match: m.match });
  back.call('match.ready', { match: m.match, ready: true });
  assert.deepEqual(dimension(env, 'secret'), { akuma_available: 1, evil_ryu_available: 1 });
  back.call('match.leave', { match: m.match });
  const hosted = back.call('pm.host', {});
  const joined = m.b.call('pm.join', { session: hosted.session });
  back.call('match.ready', { match: joined.match, ready: true });
  m.b.call('match.ready', { match: joined.match, ready: true });
  assert.deepEqual(dimension(env, 'secret'), { akuma_available: 2, evil_ryu_available: 2 });
});

test('fights count Akuma, Evil Ryu, mirrors and the cross pairing once per started fight', () => {
  const env = makeHub();
  const m = startFightMatch(env, { secret: SECRET_ON });
  const play = (fight, p1, p2) => send(m.a, fightEvent(m, 'start', fight, { p1, p2 }));
  play(0, 16, 0);
  play(1, 16, 16);
  play(2, 17, 17);
  play(3, 16, 17);
  play(4, 17, 16);
  play(5, 0, 4);
  play(0, 16, 0);
  send(m.b, fightEvent(m, 'start', 1, { p1: 16, p2: 16 }));
  send(m.a, fightEvent(m, 'complete', 0, { p1: 16, p2: 0 }));
  assert.deepEqual(dimension(env, 'secret_fight'), { akuma: 4, evil_ryu: 3, akuma_mirror: 1, evil_ryu_mirror: 1, akuma_vs_evil_ryu: 2 });
});

test('a match counts each secret character once, at its first fight', () => {
  const env = makeHub();
  const m = startFightMatch(env, { secret: SECRET_ON });
  send(m.a, fightEvent(m, 'start', 0, { p1: 0, p2: 4 }));
  assert.deepEqual(dimension(env, 'secret_match'), {});
  send(m.a, fightEvent(m, 'start', 1, { p1: 16, p2: 4 }));
  send(m.a, fightEvent(m, 'start', 2, { p1: 4, p2: 16 }));
  send(m.a, fightEvent(m, 'start', 1, { p1: 16, p2: 4 }));
  assert.deepEqual(dimension(env, 'secret_match'), { akuma: 1 });
  send(m.a, fightEvent(m, 'start', 3, { p1: 17, p2: 16 }));
  send(m.a, fightEvent(m, 'start', 4, { p1: 17, p2: 17 }));
  assert.deepEqual(dimension(env, 'secret_match'), { akuma: 1, evil_ryu: 1 });
  const two = startFightMatch(env, { nA: 3, nB: 4, ipA: '203.0.113.3', ipB: '203.0.113.4', secret: SECRET_ON });
  send(two.a, fightEvent(two, 'start', 0, { p1: 16, p2: 16 }));
  send(two.a, fightEvent(two, 'start', 1, { p1: 16, p2: 0 }));
  assert.deepEqual(dimension(env, 'secret_match'), { akuma: 2, evil_ryu: 1 });
});

test('the counters hold only counts, and the history exposes the dimensions', () => {
  const env = makeHub();
  const m = startFightMatch(env, { secret: SECRET_ON });
  establish(m);
  playFight(m, 0, { p1: 16, p2: 17 });
  const columns = env.host.sql.all('PRAGMA table_info(metrics_daily_dimensions)').map((c) => c.name);
  assert.deepEqual(columns, ['day', 'dimension', 'value', 'count']);
  const history = env.hub.metricsHistory(7);
  const of = (name) => Object.fromEntries(history.dimensions[name].map((d) => [d.value, d.count]));
  assert.deepEqual(of('secret'), { akuma_available: 1, evil_ryu_available: 1 });
  assert.deepEqual(of('secret_match'), { akuma: 1, evil_ryu: 1 });
  assert.equal(of('secret_fight').akuma_vs_evil_ryu, 1);
  const dump = JSON.stringify(env.host.sql.all('SELECT * FROM metrics_daily_dimensions'));
  for (const id of [m.match, m.a.who.player, m.b.who.player, m.a.who.token]) assert.ok(!dump.includes(id));
});

test('secret.update writes no counters, only the stored mask', () => {
  const env = makeHub();
  const m = pair(env, SECRET_ON, SECRET_ON);
  const writes = countWrites(env);
  m.a.call('secret.update', { secret: NONE });
  m.a.call('secret.update', { secret: SECRET_ON });
  assert.equal(writes.filter((q) => q.includes('metrics')).length, 0);
  assert.equal(writes.filter((q) => q.includes('host_secret')).length, 2);
});

test('secret metrics failing never fails the match or the sender', () => {
  const env = makeHub();
  const m = startFightMatch(env, { secret: SECRET_ON });
  env.hub.metrics.secretEstablished = () => { throw new Error('metrics down'); };
  env.hub.metrics.secretMatch = () => { throw new Error('metrics down'); };
  assert.equal(m.a.call('match.ready', { match: m.match, ready: true }).code, 'ok');
  assert.equal(m.b.call('match.ready', { match: m.match, ready: true }).code, 'ok');
  assert.equal(send(m.a, fightEvent(m, 'start', 0, { p1: 16, p2: 0 })).code, 'ok');
  assert.ok(env.logs.some((l) => l.includes('metrics error')));
});

// ---- dashboard ----
test('the dashboard has a Secret characters section and names Evil Ryu and Akuma', async () => {
  const env = makeHub();
  const m = startFightMatch(env, { secret: SECRET_ON });
  establish(m);
  const old = startFightMatch(env, { nA: 3, nB: 4, ipA: '203.0.113.3', ipB: '203.0.113.4', secretA: SECRET_ON });
  establish(old);
  const mismatch = startFightMatch(env, { nA: 5, nB: 6, ipA: '203.0.113.5', ipB: '203.0.113.6', secretA: SECRET_ON,
    secretB: caps(['akuma', 'evil_ryu'], ['akuma']) });
  establish(mismatch);
  playFight(m, 0, { p1: 16, p2: 0, result: 'p1' });
  playFight(m, 1, { p1: 17, p2: 4, result: 'p2' });
  playFight(m, 2, { p1: 16, p2: 16, result: 'p1' });
  playFight(m, 3, { p1: 17, p2: 17, result: 'p1' });
  playFight(m, 4, { p1: 16, p2: 17, result: 'p1' });
  playFight(m, 5, { p1: 0, p2: 4, result: 'p1' });
  assert.ok(ADMIN_HTML.includes('<h2>Secret characters</h2>'));
  const page = await openDashboard({ summary: env.hub.metricsSummary(), history: env.hub.metricsHistory(30, 20) });
  const value = (id) => page.text(id);
  assert.equal(value('s-akuma-available'), '2');
  assert.equal(value('s-evil-ryu-available'), '1');
  assert.equal(value('s-akuma-mismatch'), '0');
  assert.equal(value('s-evil-ryu-mismatch'), '1');
  assert.equal(value('s-unsupported'), '1');
  assert.equal(value('s-fights-akuma'), '3');
  assert.equal(value('s-fights-evil-ryu'), '3');
  assert.equal(value('s-fights-akuma-mirror'), '1');
  assert.equal(value('s-fights-evil-ryu-mirror'), '1');
  assert.equal(value('s-fights-akuma-vs-evil-ryu'), '1');
  assert.equal(value('s-matches-akuma'), '1');
  assert.equal(value('s-matches-evil-ryu'), '1');
  const names = page.rows('chars').slice(1).map((r) => r[0]);
  assert.ok(names.includes('Evil Ryu') && names.includes('Akuma') && names.includes('Ryu'));
  assert.equal(names.filter((n) => n === 'Ryu').length, 1);
  const evil = page.rows('chars').find((r) => r[0] === 'Evil Ryu');
  assert.deepEqual(evil.slice(1, 4), ['4', '1', '3']);
});

test('the secret section shows zeros before anything was counted', async () => {
  const page = await openDashboard({ summary: makeHub().hub.metricsSummary(), history: makeHub().hub.metricsHistory(30, 20) });
  for (const id of ['s-akuma-available', 's-evil-ryu-available', 's-unsupported', 's-fights-akuma-vs-evil-ryu', 's-matches-evil-ryu']) {
    assert.equal(page.text(id), '0');
  }
});
