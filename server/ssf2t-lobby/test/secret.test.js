import assert from 'node:assert/strict';
import { test } from 'node:test';

import { ADMIN_HTML } from '../src/admin_page.js';
import { CHARACTERS, SECRET_CHARACTER, SECRET_KEYS, characterName } from '../src/characters.js';
import { Hub } from '../src/hub.js';
import { CAPABILITIES, PROTOCOL, WORKER_BUILD, effectiveSecret, normalizeSecret, parseHello, secretEstablishment,
  secretFromMask, secretMask } from '../src/protocol.js';
import { openDashboard } from './dom.js';
import { SECRET_ON, characters, daily, dimension, fightEvent, playFight, send, startFightMatch } from './fight_support.js';
import { befriend, identity, makeHub } from './harness.js';

const KEYS = ['akuma', 'evil_ryu', 'violent_ken'];
const only = (...on) => Object.fromEntries(KEYS.map((key) => [key, on.includes(key)]));
const caps = (supports, enabled = supports) => ({ supports: only(...supports), enabled: only(...enabled) });
const NONE = caps([]);
const SUPPORTED_OFF = caps(KEYS, []);
const FALSE_ALL = NONE;
// The build before Violent Ken: it advertises the two older characters and has never heard of the third.
const EVIL_BUILD = { supports: { akuma: true, evil_ryu: true }, enabled: { akuma: true, evil_ryu: true } };
const VIOLENT_OFF = caps(KEYS, ['akuma', 'evil_ryu']);
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
  assert.equal(WORKER_BUILD, '2026-11-04-secret2');
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
    { supports: { akuma: 1, evil_ryu: {} }, enabled: { akuma: 1, evil_ryu: {} } },
    { supports: { violent_ken: 'true' }, enabled: { violent_ken: 1 } }, { supports: { violent_ken: 1 }, enabled: { violent_ken: true } },
    { supports: { violent_ken: null }, enabled: { violent_ken: [] } }, { supports: { violent_ken: {} }, enabled: { violent_ken: 'on' } }];
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
  a.hello({ secret: { supports: { akuma: false, evil_ryu: true, extra: true },
    enabled: { akuma: true, evil_ryu: true, violent_ken: true, extra: true }, other: true } });
  assert.deepEqual(attOf(a).secret, caps(['evil_ryu']));
  assert.ok(!('extra' in attOf(a).secret.supports) && !('extra' in attOf(a).secret.enabled));
});

test('the mask keeps the first four bits and adds supports violent_ken then enabled violent_ken', () => {
  assert.equal(secretMask(caps(['akuma'], [])), 1);
  assert.equal(secretMask(caps(['evil_ryu'], [])), 2);
  assert.equal(secretMask(caps(['akuma'])), 5);
  assert.equal(secretMask(caps(['evil_ryu'])), 10);
  assert.equal(secretMask(caps(['violent_ken'], [])), 16);
  assert.equal(secretMask(caps(['violent_ken'])), 48);
  assert.equal(secretMask(EVIL_BUILD), 15);
  assert.equal(secretMask(SECRET_ON), 63);
  assert.equal(secretMask({ supports: { akuma: false }, enabled: { akuma: true } }), 0);
  assert.equal(secretMask({ supports: { violent_ken: false }, enabled: { violent_ken: true } }), 0);
  for (let mask = 0; mask < 64; mask++) {
    const secret = secretFromMask(mask);
    assert.deepEqual(secret, normalizeSecret(secret));
    const reachable = (mask & 4 ? mask & 1 : 1) && (mask & 8 ? mask & 2 : 1) && (mask & 32 ? mask & 16 : 1);
    if (reachable) assert.equal(secretMask(secret), mask);
  }
  assert.deepEqual(secretFromMask(undefined), FALSE_ALL);
  assert.deepEqual(secretFromMask(null), FALSE_ALL);
});

test('Violent Ken is a secret character of its own, apart from Ken', () => {
  assert.equal(characterName(17), 'Evil Ryu');
  assert.equal(characterName(18), 'Violent Ken');
  assert.equal(CHARACTERS.length, 19);
  assert.deepEqual(SECRET_CHARACTER, { 16: 'akuma', 17: 'evil_ryu', 18: 'violent_ken' });
  assert.deepEqual(SECRET_KEYS, KEYS);
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
      assert.equal(m.joined.secret.effective[key], want, `${key} ${a}/${b}`);
      assert.equal(m.created.secret.effective[key], want, `${key} ${a}/${b}`);
      for (const other of KEYS.filter((k) => k !== key)) assert.equal(m.joined.secret.effective[other], false);
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
      assert.deepEqual(view.effective, only());
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
  assert.deepEqual(effectiveSecret(SECRET_ON, SECRET_ON), only(...KEYS));
  assert.deepEqual(effectiveSecret(SECRET_ON, SUPPORTED_OFF), only());
  assert.deepEqual(effectiveSecret(SECRET_ON, caps(['akuma'])), only('akuma'));
  assert.deepEqual(effectiveSecret(SECRET_ON, undefined), only());
  assert.deepEqual(effectiveSecret('x', 5), only());
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
  assert.deepEqual(joined.secret.effective, only(...KEYS));
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
  assert.deepEqual(reply.secret.effective, only('akuma'));
  assert.deepEqual(reply.secret.peer, SECRET_ON);
  assert.equal(maskOf(env, 'host_secret'), secretMask(next));
  assert.equal(maskOf(env, 'join_secret'), 63);
  assert.deepEqual(writes.filter((q) => q.includes('_secret')), ['UPDATE matches SET host_secret = ? WHERE id = ?']);
  assert.equal(writes.length, 1);
  const toA = m.a.events('match.secret');
  const toB = m.b.events('match.secret');
  assert.equal(toA.length, 1);
  assert.equal(toB.length, 1);
  assert.equal(toA[0].match, m.match);
  assert.deepEqual(toA[0].secret.effective, toB[0].secret.effective);
  assert.deepEqual(toA[0].secret.effective, only('akuma'));
  assert.deepEqual(toA[0].secret.peer, SECRET_ON);
  assert.deepEqual(toB[0].secret.peer, next);
  assert.deepEqual(Object.keys(toB[0]).sort(), ['ev', 'match', 'secret']);
  const dump = JSON.stringify([toA, toB]);
  for (const id of [m.a.who.player, m.b.who.player]) assert.ok(!dump.includes(id));
  m.b.call('secret.update', { secret: caps(['akuma', 'evil_ryu'], ['evil_ryu']) });
  assert.equal(maskOf(env, 'join_secret'), 11);
  assert.deepEqual(m.a.events('match.secret')[0].secret.effective, only());
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
    assert.deepEqual(reply.secret.effective, only('akuma'));
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
  assert.deepEqual(m.b.events('match.secret')[0].secret.effective, only());
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
  assert.deepEqual(view.secret.effective, only('akuma'));
  assert.deepEqual(view.secret.peer, SECRET_ON);
  assert.equal(maskOf(env, 'host_secret'), secretMask(caps(['akuma', 'evil_ryu'], ['akuma'])));
  const told = m.b.events('match.secret');
  assert.equal(told.length, 1);
  assert.deepEqual(told[0].secret.effective, only('akuma'));
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
  assert.deepEqual(view.secret.effective, only());
  assert.equal(maskOf(env, 'host_secret'), 0);
});

test('a new match recomputes from the current sockets, nothing is carried over', () => {
  const env = makeHub();
  const m = pair(env, SECRET_ON, SECRET_ON);
  assert.deepEqual(m.joined.secret.effective, only(...KEYS));
  m.a.call('match.leave', { match: m.match });
  m.a.drop();
  const old = reconnect(env, 1, '203.0.113.1');
  const hosted = old.call('pm.host', {});
  const joined = m.b.call('pm.join', { session: hosted.session });
  assert.equal(joined.code, 'ok');
  assert.deepEqual(joined.secret.effective, only());
  assert.deepEqual(joined.secret.peer, FALSE_ALL);
  assert.equal(maskOf(env, 'host_secret'), 0);
  assert.equal(maskOf(env, 'join_secret'), 63);
  old.call('match.leave', { match: joined.match });
  old.call('secret.update', { secret: SECRET_ON });
  const again = old.call('pm.host', {});
  const third = m.b.call('pm.join', { session: again.session });
  assert.deepEqual(third.secret.effective, only(...KEYS));
});

test('databases from before the secret columns gain them and keep working', () => {
  const env = makeHub();
  env.host.sql.run('ALTER TABLE matches DROP COLUMN host_secret');
  env.host.sql.run('ALTER TABLE matches DROP COLUMN join_secret');
  env.host.sql.run('ALTER TABLE match_telemetry DROP COLUMN secret_akuma');
  env.host.sql.run('ALTER TABLE match_telemetry DROP COLUMN secret_evil_ryu');
  env.host.sql.run('ALTER TABLE match_telemetry DROP COLUMN secret_violent_ken');
  const hub = new Hub(env.host);
  assert.ok(hub);
  const columns = (table) => env.host.sql.all(`PRAGMA table_info(${table})`).map((c) => c.name);
  assert.ok(columns('matches').includes('host_secret') && columns('matches').includes('join_secret'));
  for (const key of KEYS) assert.ok(columns('match_telemetry').includes(`secret_${key}`));
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

test('a secret character is accepted when both seats support and enabled it', () => {
  const env = makeHub();
  const m = startFightMatch(env, { secret: SECRET_ON });
  assert.equal(send(m.a, fightEvent(m, 'start', 0, { p1: 16, p2: 0 })).counted, true);
  assert.equal(send(m.a, fightEvent(m, 'start', 1, { p1: 4, p2: 17 })).counted, true);
  assert.equal(send(m.a, fightEvent(m, 'start', 2, { p1: 16, p2: 17 })).counted, true);
  assert.equal(send(m.a, fightEvent(m, 'start', 3, { p1: 18, p2: 0 })).counted, true);
  assert.equal(send(m.a, fightEvent(m, 'start', 4, { p1: 4, p2: 18 })).counted, true);
  assert.equal(send(m.b, fightEvent(m, 'start', 5, { p1: 17, p2: 17 })).counted, true);
  assert.equal(daily(env, 'fights_started'), 6);
  assert.equal(send(m.a, fightEvent(m, 'complete', 3, { p1: 18, p2: 0 })).counted, true);
  assert.equal(env.hub.metrics.pending.get('fight_secret_rejected'), undefined);
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
const ALL_AVAILABLE = { akuma_available: 1, evil_ryu_available: 1, violent_ken_available: 1 };

test('a match counts available, option mismatch and unsupported peer once, when it is first established', () => {
  const cases = [
    ['both on', SECRET_ON, SECRET_ON, ALL_AVAILABLE],
    ['one option off', SECRET_ON, caps(KEYS, ['akuma', 'violent_ken']),
      { akuma_available: 1, evil_ryu_option_mismatch: 1, violent_ken_available: 1 }],
    ['both mismatched', caps(KEYS, ['akuma']), caps(KEYS, ['evil_ryu']),
      { akuma_option_mismatch: 1, evil_ryu_option_mismatch: 1 }],
    ['Violent Ken option off on one side', SECRET_ON, VIOLENT_OFF,
      { akuma_available: 1, evil_ryu_available: 1, violent_ken_option_mismatch: 1 }],
    ['Violent Ken unknown to one side', SECRET_ON, EVIL_BUILD,
      { akuma_available: 1, evil_ryu_available: 1, unsupported_peer: 1 }],
    ['Violent Ken off, unknown to the peer', VIOLENT_OFF, EVIL_BUILD, { akuma_available: 1, evil_ryu_available: 1 }],
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
  assert.deepEqual(secretEstablishment(SECRET_ON, SECRET_ON), ['akuma_available', 'evil_ryu_available', 'violent_ken_available']);
  assert.deepEqual(secretEstablishment(SECRET_ON, undefined), ['unsupported_peer']);
  assert.deepEqual(secretEstablishment(undefined, SECRET_ON), ['unsupported_peer']);
  assert.deepEqual(secretEstablishment(SECRET_ON, caps(KEYS, [])),
    ['akuma_option_mismatch', 'evil_ryu_option_mismatch', 'violent_ken_option_mismatch']);
  assert.deepEqual(secretEstablishment(SECRET_ON, EVIL_BUILD), ['akuma_available', 'evil_ryu_available', 'unsupported_peer']);
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
  assert.deepEqual(dimension(env, 'secret'), ALL_AVAILABLE);
  back.call('match.leave', { match: m.match });
  const hosted = back.call('pm.host', {});
  const joined = m.b.call('pm.join', { session: hosted.session });
  back.call('match.ready', { match: joined.match, ready: true });
  m.b.call('match.ready', { match: joined.match, ready: true });
  assert.deepEqual(dimension(env, 'secret'), { akuma_available: 2, evil_ryu_available: 2, violent_ken_available: 2 });
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
  assert.deepEqual(of('secret'), ALL_AVAILABLE);
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
    secretB: caps(KEYS, ['akuma', 'violent_ken']) });
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

// ---- Violent Ken ----
const KEN = 4;
const EVIL_RYU = 17;
const VIOLENT_KEN = 18;
const startViolent = (m, fight, others = {}) => send(m.a, fightEvent(m, 'start', fight, { p1: VIOLENT_KEN, p2: KEN, ...others }));

test('Violent Ken On/Off on A and B: effective, both views, counters and the fight rule agree', () => {
  const side = (on) => (on ? SECRET_ON : VIOLENT_OFF);
  for (const [a, b] of [[true, true], [true, false], [false, true], [false, false]]) {
    const label = `A ${a ? 'On' : 'Off'} B ${b ? 'On' : 'Off'}`;
    const want = a && b;
    const older = ['akuma', 'evil_ryu'];
    const m = pair(makeHub(), side(a), side(b));
    assert.deepEqual(m.joined.secret.effective, only(...older, ...(want ? ['violent_ken'] : [])), label);
    assert.deepEqual(m.created.secret.effective, m.joined.secret.effective, label);
    assert.equal(m.joined.secret.peer.enabled.violent_ken, a, label);
    assert.equal(m.created.secret.peer.enabled.violent_ken, b, label);

    const env = makeHub();
    const f = startFightMatch(env, { secretA: side(a), secretB: side(b) });
    establish(f);
    const outcome = want ? { violent_ken_available: 1 } : a !== b ? { violent_ken_option_mismatch: 1 } : {};
    assert.deepEqual(dimension(env, 'secret'), { akuma_available: 1, evil_ryu_available: 1, ...outcome }, label);
    assert.equal(startViolent(f, 0).code, want ? 'ok' : 'bad_request', label);
    assert.equal(send(f.a, fightEvent(f, 'start', 1, { p1: 0, p2: VIOLENT_KEN })).code, want ? 'ok' : 'bad_request', label);
    assert.equal(env.hub.metrics.pending.get('fight_secret_rejected'), want ? undefined : 2, label);
    assert.equal(daily(env, 'fights_started'), want ? 2 : 0, label);
    assert.deepEqual(dimension(env, 'secret_match'), want ? { violent_ken: 1 } : {}, label);
  }
});

test('Violent Ken is never available against an old client or a build that does not support it', () => {
  const claiming = { supports: EVIL_BUILD.supports, enabled: { ...EVIL_BUILD.enabled, violent_ken: true } };
  const peers = [['old client', undefined, []], ['Evil Ryu build', EVIL_BUILD, ['akuma', 'evil_ryu']],
    ['enabled without support', claiming, ['akuma', 'evil_ryu']]];
  for (const [label, peer, usable] of peers) {
    for (const [sa, sb] of [[SECRET_ON, peer], [peer, SECRET_ON]]) {
      const m = pair(makeHub(), sa, sb);
      assert.deepEqual(m.joined.secret.effective, only(...usable), label);
      assert.deepEqual(m.created.secret.effective, only(...usable), label);
      const view = (sa === peer ? m.joined : m.created).secret.peer;
      assert.equal(view.supports.violent_ken, false, label);
      assert.equal(view.enabled.violent_ken, false, label);

      const env = makeHub();
      const f = startFightMatch(env, { secretA: sa, secretB: sb });
      establish(f);
      assert.equal(dimension(env, 'secret').unsupported_peer, 1, label);
      assert.equal(dimension(env, 'secret').violent_ken_option_mismatch, undefined, label);
      assert.equal(startViolent(f, 0).code, 'bad_request', label);
      assert.equal(send(f.a, fightEvent(f, 'complete', 0, { p1: VIOLENT_KEN, p2: KEN })).code, 'bad_request', label);
      assert.equal(env.hub.metrics.pending.get('fight_secret_rejected'), 1, label);
      assert.deepEqual(characters(env), {}, label);
    }
  }
});

test('Evil Ryu availability does not imply Violent Ken, nor the other way round', () => {
  const evil = makeHub();
  const m = startFightMatch(evil, { secret: caps(['evil_ryu']) });
  establish(m);
  assert.equal(send(m.a, fightEvent(m, 'start', 0, { p1: EVIL_RYU, p2: 0 })).code, 'ok');
  assert.equal(startViolent(m, 1).code, 'bad_request');
  assert.deepEqual(dimension(evil, 'secret'), { evil_ryu_available: 1 });
  assert.deepEqual(effectiveSecret(EVIL_BUILD, EVIL_BUILD), only('akuma', 'evil_ryu'));

  const ken = makeHub();
  const v = startFightMatch(ken, { secret: caps(['violent_ken']) });
  establish(v);
  assert.equal(startViolent(v, 0).code, 'ok');
  assert.equal(send(v.a, fightEvent(v, 'start', 1, { p1: EVIL_RYU, p2: 0 })).code, 'bad_request');
  assert.deepEqual(dimension(ken, 'secret'), { violent_ken_available: 1 });
  assert.deepEqual(effectiveSecret(caps(['violent_ken']), caps(['violent_ken'])), only('violent_ken'));
});

test('a Violent Ken secret.update writes and broadcasts only when it changes something', () => {
  const env = makeHub();
  const m = pair(env, SECRET_ON, SECRET_ON);
  m.a.inbox = [];
  m.b.inbox = [];
  const writes = countWrites(env);
  const reply = m.a.call('secret.update', { secret: VIOLENT_OFF });
  assert.deepEqual(reply.secret.effective, only('akuma', 'evil_ryu'));
  assert.equal(maskOf(env, 'host_secret'), secretMask(VIOLENT_OFF));
  assert.equal(writes.length, 1);
  assert.equal(m.b.events('match.secret')[0].secret.peer.enabled.violent_ken, false);
  assert.equal(m.a.events('match.secret')[0].secret.effective.violent_ken, false);
  m.a.inbox = [];
  m.b.inbox = [];
  assert.equal(m.a.call('secret.update', { secret: VIOLENT_OFF }).code, 'ok');
  assert.equal(writes.length, 1);
  assert.deepEqual([...m.a.inbox, ...m.b.inbox], []);
  env.advance(2000);
  assert.deepEqual(m.a.call('secret.update', { secret: SECRET_ON }).secret.effective, only(...KEYS));
  assert.equal(writes.length, 2);
  assert.equal(m.b.events('match.secret')[0].secret.effective.violent_ken, true);
});

test('a resume refreshes Violent Ken from the new socket', () => {
  const env = makeHub();
  const m = pair(env, SECRET_ON, SECRET_ON);
  m.b.inbox = [];
  m.a.drop();
  env.advance(5000);
  const back = reconnect(env, 1, '203.0.113.1', EVIL_BUILD);
  assert.deepEqual(back.call('match.resume', { match: m.match }).secret.effective, only('akuma', 'evil_ryu'));
  assert.equal(maskOf(env, 'host_secret'), 15);
  assert.deepEqual(m.b.events('match.secret')[0].secret.effective, only('akuma', 'evil_ryu'));
});

test('rows and sockets stored before Violent Ken read as without it and cause no change', () => {
  const env = makeHub();
  const m = startFightMatch(env, { secret: SECRET_ON });
  env.host.sql.run('UPDATE matches SET host_secret = 15, join_secret = 15');
  assert.equal(startViolent(m, 0).code, 'bad_request');
  assert.equal(send(m.a, fightEvent(m, 'start', 1, { p1: EVIL_RYU, p2: 0 })).code, 'ok');
  assert.deepEqual(secretFromMask(15), caps(['akuma', 'evil_ryu']));

  const n = pair(makeHub(), EVIL_BUILD, EVIL_BUILD);
  const att = attOf(n.a);
  att.secret = EVIL_BUILD;
  n.a.conn.setAttachment(att);
  n.a.inbox = [];
  n.b.inbox = [];
  assert.deepEqual(n.a.call('secret.update', { secret: EVIL_BUILD }).secret.effective, only('akuma', 'evil_ryu'));
  assert.deepEqual([...n.a.inbox, ...n.b.inbox], []);
});

test('an open match from before the Violent Ken flag gains it and counts once', () => {
  const env = makeHub();
  const m = startFightMatch(env, { secret: SECRET_ON });
  env.host.sql.run('ALTER TABLE match_telemetry DROP COLUMN secret_violent_ken');
  assert.ok(new Hub(env.host));
  assert.equal(env.host.sql.get('SELECT secret_violent_ken AS n FROM match_telemetry').n, 0);
  startViolent(m, 0);
  startViolent(m, 1);
  assert.deepEqual(dimension(env, 'secret_match'), { violent_ken: 1 });
});

test('Violent Ken is counted apart from Ken in the characters and the matchups', () => {
  const env = makeHub();
  const m = startFightMatch(env, { secret: SECRET_ON });
  playFight(m, 0, { p1: VIOLENT_KEN, p2: KEN, result: 'p1' });
  playFight(m, 1, { p1: KEN, p2: 0, result: 'p2' });
  playFight(m, 2, { p1: EVIL_RYU, p2: VIOLENT_KEN, result: 'p2' });
  assert.deepEqual(characters(env)[VIOLENT_KEN], { picks: 2, wins: 2, losses: 0, draws: 0 });
  assert.deepEqual(characters(env)[KEN], { picks: 2, wins: 0, losses: 2, draws: 0 });
  const history = env.hub.metricsHistory(30);
  assert.equal(history.characters.find((c) => c.id === VIOLENT_KEN).name, 'Violent Ken');
  assert.equal(history.characters.find((c) => c.id === KEN).name, 'Ken');
  const against = (a, b) => history.matchups.find((x) => x.a === a && x.b === b);
  assert.deepEqual([against(KEN, VIOLENT_KEN).a_name, against(KEN, VIOLENT_KEN).b_name, against(KEN, VIOLENT_KEN).b_wins],
    ['Ken', 'Violent Ken', 1]);
  assert.deepEqual([against(EVIL_RYU, VIOLENT_KEN).b_name, against(EVIL_RYU, VIOLENT_KEN).b_wins], ['Violent Ken', 1]);
});

test('fights count Violent Ken, its mirror and both cross pairings, whichever side holds them', () => {
  const env = makeHub();
  const m = startFightMatch(env, { secret: SECRET_ON });
  const pairings = [[VIOLENT_KEN, KEN], [VIOLENT_KEN, VIOLENT_KEN], [EVIL_RYU, VIOLENT_KEN], [VIOLENT_KEN, EVIL_RYU],
    [16, VIOLENT_KEN], [VIOLENT_KEN, 16]];
  pairings.forEach(([p1, p2], fight) => send(m.a, fightEvent(m, 'start', fight, { p1, p2 })));
  assert.deepEqual(dimension(env, 'secret_fight'), { violent_ken: 6, violent_ken_mirror: 1, evil_ryu: 2, akuma: 2,
    evil_ryu_vs_violent_ken: 2, akuma_vs_violent_ken: 2 });
  assert.deepEqual(dimension(env, 'secret_match'), { violent_ken: 1, evil_ryu: 1, akuma: 1 });
  const dump = JSON.stringify(env.host.sql.all('SELECT * FROM metrics_daily_dimensions'));
  for (const id of [m.match, m.a.who.player, m.b.who.player, m.a.who.token]) assert.ok(!dump.includes(id));
});

test('the dashboard labels Violent Ken and shows it apart from Ken', async () => {
  const labels = ['Rooms with Violent Ken available', 'Violent Ken option mismatch', 'Fights with Violent Ken',
    'Violent Ken mirror fights', 'Akuma vs Violent Ken fights', 'Evil Ryu vs Violent Ken fights', 'Matches with Violent Ken'];
  for (const label of labels) assert.ok(ADMIN_HTML.includes(`<span class="label">${label}</span>`), label);
  const env = makeHub();
  const m = startFightMatch(env, { secret: SECRET_ON });
  establish(m);
  const off = startFightMatch(env, { nA: 3, nB: 4, ipA: '203.0.113.3', ipB: '203.0.113.4', secretA: SECRET_ON, secretB: VIOLENT_OFF });
  establish(off);
  const old = startFightMatch(env, { nA: 5, nB: 6, ipA: '203.0.113.5', ipB: '203.0.113.6', secretA: SECRET_ON, secretB: EVIL_BUILD });
  establish(old);
  playFight(m, 0, { p1: VIOLENT_KEN, p2: KEN, result: 'p1' });
  playFight(m, 1, { p1: VIOLENT_KEN, p2: VIOLENT_KEN, result: 'p1' });
  playFight(m, 2, { p1: 16, p2: VIOLENT_KEN, result: 'p2' });
  playFight(m, 3, { p1: EVIL_RYU, p2: VIOLENT_KEN, result: 'p2' });
  const page = await openDashboard({ summary: env.hub.metricsSummary(), history: env.hub.metricsHistory(30, 20) });
  const expected = { 's-violent-ken-available': '1', 's-violent-ken-mismatch': '1', 's-unsupported': '1',
    's-fights-violent-ken': '4', 's-fights-violent-ken-mirror': '1', 's-fights-akuma-vs-violent-ken': '1',
    's-fights-evil-ryu-vs-violent-ken': '1', 's-matches-violent-ken': '1' };
  for (const [id, want] of Object.entries(expected)) assert.equal(page.text(id), want, id);
  const names = page.rows('chars').slice(1).map((r) => r[0]);
  assert.equal(names.filter((n) => n === 'Ken').length, 1);
  assert.equal(names.filter((n) => n === 'Violent Ken').length, 1);
  assert.deepEqual(page.rows('chars').find((r) => r[0] === 'Violent Ken').slice(1, 4), ['5', '4', '1']);
  assert.deepEqual(page.rows('chars').find((r) => r[0] === 'Ken').slice(1, 4), ['1', '0', '1']);
});
