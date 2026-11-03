import assert from 'node:assert/strict';
import { test } from 'node:test';

import { CAPABILITIES } from '../src/protocol.js';
import { identity, makeHub } from './harness.js';
import { characters, daily, dimension, fightEvent, playFight, send, startFightMatch } from './fight_support.js';

test('the service announces fight telemetry and the protocol number is unchanged', () => {
  assert.equal(CAPABILITIES.fight_telemetry, true);
  assert.equal(CAPABILITIES.protocol_version, 3);
});

test('a fight start is accepted and counted with both picks', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  const reply = send(m.a, fightEvent(m, 'start', 0, { p1: 0, p2: 4, transport: 'direct' }));
  assert.equal(reply.code, 'ok');
  assert.equal(reply.counted, true);
  assert.equal(daily(env, 'fights_started'), 1);
  assert.equal(daily(env, 'fights_completed'), 0);
  assert.deepEqual(characters(env), { 0: { picks: 1, wins: 0, losses: 0, draws: 0 }, 4: { picks: 1, wins: 0, losses: 0, draws: 0 } });
});

test('a fight completion is accepted and credits the winner and the loser', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  const { done } = playFight(m, 0, { p1: 0, p2: 4, result: 'p1' });
  assert.equal(done.code, 'ok');
  assert.equal(done.counted, true);
  assert.equal(daily(env, 'fights_completed'), 1);
  assert.deepEqual(characters(env), { 0: { picks: 1, wins: 1, losses: 0, draws: 0 }, 4: { picks: 1, wins: 0, losses: 1, draws: 0 } });
});

test('player two winning credits the second character, not the first', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  playFight(m, 0, { p1: 2, p2: 9, result: 'p2' });
  assert.deepEqual(characters(env)[9], { picks: 1, wins: 1, losses: 0, draws: 0 });
  assert.deepEqual(characters(env)[2], { picks: 1, wins: 0, losses: 1, draws: 0 });
});

test('a draw is counted apart and is neither a win nor a loss', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  playFight(m, 0, { p1: 0, p2: 4, result: 'draw' });
  assert.equal(daily(env, 'fight_draws'), 1);
  assert.equal(daily(env, 'fights_completed'), 1);
  assert.deepEqual(characters(env), { 0: { picks: 1, wins: 0, losses: 0, draws: 1 }, 4: { picks: 1, wins: 0, losses: 0, draws: 1 } });
});

test('a duplicate start and a duplicate completion change nothing', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  playFight(m, 0);
  const again = send(m.a, fightEvent(m, 'start', 0));
  const doneAgain = send(m.a, fightEvent(m, 'complete', 0));
  assert.deepEqual([again.code, again.counted, again.reason], ['ok', false, 'duplicate']);
  assert.deepEqual([doneAgain.code, doneAgain.counted, doneAgain.reason], ['ok', false, 'duplicate']);
  assert.equal(daily(env, 'fights_started'), 1);
  assert.equal(daily(env, 'fights_completed'), 1);
  assert.deepEqual(characters(env)[0], { picks: 1, wins: 1, losses: 0, draws: 0 });
});

test('the joiner repeating the host reports cannot double count', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  playFight(m, 0);
  assert.equal(send(m.b, fightEvent(m, 'start', 0)).counted, false);
  assert.equal(send(m.b, fightEvent(m, 'complete', 0)).counted, false);
  assert.equal(daily(env, 'fights_started'), 1);
  assert.equal(daily(env, 'fights_completed'), 1);
});

test('an unknown match, or one the sender is not in, is rejected', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  assert.equal(send(m.a, { ...fightEvent(m, 'start', 0), match: 'ffffffffffffffff' }).code, 'not_found');
  assert.equal(send(m.a, { ...fightEvent(m, 'start', 0), match: undefined }).code, 'not_found');
  const stranger = env.connect(identity(30), { ip: '203.0.113.30' });
  stranger.hello({ telemetry: true });
  assert.equal(send(stranger, fightEvent(m, 'start', 0)).code, 'not_found');
  assert.equal(daily(env, 'fights_started'), 0);
});

test('a telemetry event before sign-in is refused', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  const raw = env.connect(identity(31), { ip: '203.0.113.31' });
  assert.equal(send(raw, fightEvent(m, 'start', 0)).code, 'no_session');
});

test('an invalid character is rejected', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  for (const bad of [17, 255, -1, 1.5, '4', null, undefined]) {
    assert.equal(send(m.a, fightEvent(m, 'start', 0, { p1: bad })).code, 'bad_request', String(bad));
    assert.equal(send(m.a, fightEvent(m, 'start', 0, { p2: bad })).code, 'bad_request', String(bad));
  }
  assert.equal(daily(env, 'fights_started'), 0);
});

test('every playable character id is accepted', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  for (let id = 0; id <= 16; id++) assert.equal(send(m.a, fightEvent(m, 'start', id, { p1: id, p2: 16 - id })).code, 'ok');
  assert.equal(daily(env, 'fights_started'), 17);
});

test('an invalid or foreign build is rejected', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  assert.equal(send(m.a, fightEvent(m, 'start', 0, { build: 'nope' })).code, 'bad_request');
  assert.equal(send(m.a, fightEvent(m, 'start', 0, { build: 'tu023' })).code, 'bad_request');
  assert.equal(send(m.a, fightEvent(m, 'start', 0, { build: undefined })).code, 'bad_request');
  assert.equal(daily(env, 'fights_started'), 0);
});

test('an invalid platform is rejected', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  assert.equal(send(m.a, fightEvent(m, 'start', 0, { platform: 'Amiga' })).code, 'bad_request');
  assert.equal(send(m.a, fightEvent(m, 'start', 0, { platform: 7 })).code, 'bad_request');
  assert.equal(daily(env, 'fights_started'), 0);
});

test('an invalid transport is rejected', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  for (const bad of ['pending', 'udp', '', 3, null]) {
    assert.equal(send(m.a, fightEvent(m, 'start', 0, { transport: bad })).code, 'bad_request', String(bad));
  }
  assert.equal(daily(env, 'fights_started'), 0);
});

test('an invalid or absurd fight index is rejected', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  for (const bad of [-1, 200, 5000, 1.5, '1', null, undefined]) {
    assert.equal(send(m.a, fightEvent(m, 'start', bad)).code, 'bad_request', String(bad));
  }
  assert.equal(send(m.a, fightEvent(m, 'start', 199)).code, 'ok');
  assert.equal(daily(env, 'fights_started'), 1);
});

test('impossible event orderings are rejected', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  assert.equal(send(m.a, fightEvent(m, 'complete', 0)).code, 'bad_request');
  send(m.a, fightEvent(m, 'start', 0, { p1: 0, p2: 4 }));
  assert.equal(send(m.a, fightEvent(m, 'complete', 0, { p1: 1, p2: 4 })).code, 'bad_request');
  assert.equal(send(m.a, fightEvent(m, 'complete', 0, { result: 'p3' })).code, 'bad_request');
  assert.equal(send(m.a, fightEvent(m, 'complete', 0, { result: undefined })).code, 'bad_request');
  assert.equal(send(m.a, fightEvent(m, 'finish', 0)).code, 'bad_request');
  assert.equal(daily(env, 'fights_completed'), 0);
  assert.equal(send(m.a, fightEvent(m, 'complete', 0, { p1: 0, p2: 4 })).code, 'ok');
});

test('a fight that never completed counts as started only', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  playFight(m, 0, { complete: false });
  m.b.drop();
  assert.equal(daily(env, 'fights_started'), 1);
  assert.equal(daily(env, 'fights_completed'), 0);
  assert.deepEqual(characters(env)[0], { picks: 1, wins: 0, losses: 0, draws: 0 });
  assert.deepEqual(characters(env)[4], { picks: 1, wins: 0, losses: 0, draws: 0 });
});

test('ten fights in one match are ten starts, twenty picks and one match', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  for (let i = 0; i < 10; i++) playFight(m, i, { p1: 0, p2: 4, result: i % 2 ? 'p2' : 'p1' });
  assert.equal(daily(env, 'matches_started'), 1);
  assert.equal(daily(env, 'fights_started'), 10);
  assert.equal(daily(env, 'fights_completed'), 10);
  assert.deepEqual(characters(env), { 0: { picks: 10, wins: 5, losses: 5, draws: 0 }, 4: { picks: 10, wins: 5, losses: 5, draws: 0 } });
});

test('Direct and Relay matches and fights are counted apart', () => {
  const env = makeHub();
  const one = startFightMatch(env, { nA: 1, nB: 2 });
  playFight(one, 0, { transport: 'direct' });
  playFight(one, 1, { transport: 'direct' });
  const two = startFightMatch(env, { nA: 3, nB: 4, ipA: '203.0.113.3', ipB: '203.0.113.4' });
  assert.equal(two.a.call('match.transport', { match: two.match, transport: 'relay' }).code, 'ok');
  playFight(two, 0, { transport: 'relay' });
  assert.deepEqual(dimension(env, 'transport'), { direct: 1, relay: 1 });
  assert.deepEqual(dimension(env, 'fight_transport'), { direct: 2, relay: 1 });
});

test('the transport a fight reports cannot contradict the one the service knows', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  assert.equal(m.a.call('match.transport', { match: m.match, transport: 'relay' }).code, 'ok');
  playFight(m, 0, { transport: 'direct' });
  assert.deepEqual(dimension(env, 'fight_transport'), { relay: 1 });
  assert.deepEqual(dimension(env, 'transport'), { relay: 1 });
});

test('a match counts its transport once however it is learned', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  playFight(m, 0, { transport: 'direct' });
  playFight(m, 1, { transport: 'direct' });
  assert.deepEqual(dimension(env, 'transport'), { direct: 1 });
});

test('Base013 and TU023 fights are counted per build', () => {
  const env = makeHub();
  playFight(startFightMatch(env, { nA: 1, nB: 2, guest: 'base013' }), 0);
  const tu = startFightMatch(env, { nA: 3, nB: 4, guest: 'tu023', ipA: '203.0.113.3', ipB: '203.0.113.4' });
  playFight(tu, 0);
  playFight(tu, 1);
  assert.deepEqual(dimension(env, 'fight_guest'), { base013: 1, tu023: 2 });
});

test('Windows and Linux fights count once per side', () => {
  const env = makeHub();
  playFight(startFightMatch(env, { nA: 1, nB: 2, platformA: 'Windows', platformB: 'Linux' }), 0);
  playFight(startFightMatch(env, { nA: 3, nB: 4, platformA: 'Windows', platformB: 'Windows', ipA: '203.0.113.3', ipB: '203.0.113.4' }), 0);
  assert.deepEqual(dimension(env, 'fight_platform'), { Windows: 3, Linux: 1 });
});

test('daily fight counters cover the day and the average is fights over matches', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  playFight(m, 0);
  playFight(m, 1);
  playFight(m, 2, { complete: false });
  const today = env.hub.metricsSummary().today;
  assert.equal(today.fights_started, 3);
  assert.equal(today.fights_completed, 2);
  assert.equal(today.matches_started, 1);
  assert.equal(today.direct_fights, 3);
  assert.equal(today.direct_matches, 1);
  assert.equal(today.relay_matches, 0);
});

test('a v0.3.0 client still plays and signs in, and contributes no fight numbers', () => {
  const env = makeHub();
  const m = startFightMatch(env, { telemetry: false });
  assert.equal(m.a.call('match.endpoint', { match: m.match, port: 7001 }).code, 'ok');
  assert.equal(m.a.call('match.ready', { match: m.match, ready: true }).code, 'ok');
  assert.equal(m.b.call('match.ready', { match: m.match, ready: true }).code, 'ok');
  assert.equal(m.a.call('match.transport', { match: m.match, transport: 'direct' }).code, 'ok');
  assert.equal(m.a.call('ping').code, 'ok');
  assert.equal(daily(env, 'fights_started'), 0);
  m.a.call('match.leave', { match: m.match });
  const recent = env.hub.metricsHistory(1).recent;
  assert.equal(recent.matches.length, 1);
  assert.equal(recent.matches[0].fights_started, null);
  assert.equal(recent.matches[0].fights_completed, null);
  assert.equal(recent.matches[0].state, 'Ended');
});

test('a hello without the telemetry flag is exactly the v0.3.0 hello', () => {
  const env = makeHub();
  const old = env.connect(identity(40), { ip: '203.0.113.40' });
  const reply = old.hello();
  assert.equal(reply.code, 'ok');
  assert.equal(reply.capabilities.fight_telemetry, true);
});

test('telemetry that fails inside the service never fails the sender or the match', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  env.hub.metrics.fightStarted = () => { throw new Error('metrics down'); };
  env.hub.metrics.fightCompleted = () => { throw new Error('metrics down'); };
  assert.equal(send(m.a, fightEvent(m, 'start', 0)).code, 'ok');
  assert.equal(send(m.a, fightEvent(m, 'complete', 0)).code, 'ok');
  assert.ok(env.logs.some((l) => l.includes('metrics error')));
  assert.equal(m.a.call('ping').code, 'ok');
});

test('fight events for a match that ended are rejected', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  playFight(m, 0, { complete: false });
  m.a.call('match.leave', { match: m.match });
  assert.equal(send(m.a, fightEvent(m, 'complete', 0)).code, 'not_found');
  assert.equal(daily(env, 'fights_completed'), 0);
});

test('the per-fight rows are removed with the match', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  playFight(m, 0);
  assert.equal(env.host.sql.all('SELECT * FROM match_fights').length, 1);
  m.a.call('match.leave', { match: m.match });
  assert.equal(env.host.sql.all('SELECT * FROM match_fights').length, 0);
  assert.equal(env.host.sql.all('SELECT * FROM match_telemetry').length, 0);
});
