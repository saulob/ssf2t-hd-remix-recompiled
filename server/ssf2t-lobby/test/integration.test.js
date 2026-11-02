import assert from 'node:assert/strict';
import { test } from 'node:test';

import { HISTORY_MAX_DAYS, metricsDay } from '../src/metrics.js';
import { identity, makeHub } from './harness.js';
import { dimension, fightEvent, playFight, send, startFightMatch } from './fight_support.js';

const MINUTE = 60 * 1000;
const RANGES = [7, 30, 90, HISTORY_MAX_DAYS];
const recentOf = (env) => env.hub.metricsHistory(30, 50).recent;

function resumeSeat(env, m, n, ip) {
  m.a.drop();
  env.advance(5 * 1000);
  const back = env.connect(identity(n), { ip, platform: 'Windows' });
  assert.equal(back.hello({ relay: true, telemetry: true }).code, 'ok');
  const view = back.call('match.resume', { match: m.match });
  assert.equal(view.code, 'ok');
  return back;
}

test('a host that drops and resumes keeps one match, one recent row and its fight counts', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  m.a.call('match.ready', { match: m.match, ready: true });
  m.b.call('match.ready', { match: m.match, ready: true });
  playFight(m, 0);
  const sessionsBefore = env.hub.metricsSummary().today.sessions;
  const back = resumeSeat(env, m, 1, '203.0.113.1');
  assert.equal(recentOf(env).total, 0);
  assert.equal(env.hub.metricsSummary().today.sessions, sessionsBefore);
  assert.equal(env.host.sql.get('SELECT COUNT(*) AS n FROM match_telemetry').n, 1);
  assert.deepEqual(send(back, fightEvent({ ...m, a: back }, 'start', 0)), { re: back.seq, code: 'ok', counted: false, reason: 'duplicate' });
  playFight({ ...m, a: back }, 1);
  env.advance(MINUTE);
  back.call('match.leave', { match: m.match });
  const recent = recentOf(env);
  assert.equal(recent.total, 1);
  assert.equal(recent.matches[0].fights_started, 2);
  assert.equal(recent.matches[0].fights_completed, 2);
  assert.equal(recent.matches[0].state, 'Completed');
  assert.equal(dimension(env, 'fight_transport').direct, 2);
});

test('a reconnect inside the session window is one session while its match telemetry still counts', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  const sessions = env.hub.metricsSummary().today.sessions;
  resumeSeat(env, m, 1, '203.0.113.1');
  assert.equal(env.hub.metricsSummary().today.sessions, sessions);
  assert.equal(env.hub.metrics.pending.get('hello_reconnect_deduped'), 1);
  assert.equal(env.host.sql.get('SELECT COUNT(*) AS n FROM match_telemetry').n, 1);
});

test('a seat that never resumes closes the match once, as Disconnected, with the fights already counted', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  m.a.call('match.ready', { match: m.match, ready: true });
  m.b.call('match.ready', { match: m.match, ready: true });
  playFight(m, 0);
  m.a.drop();
  env.advance(10 * MINUTE);
  const recent = recentOf(env);
  assert.equal(recent.total, 1);
  assert.equal(recent.matches[0].state, 'Disconnected');
  assert.equal(recent.matches[0].fights_completed, 1);
  assert.equal(send(m.b, fightEvent(m, 'start', 5)).code, 'not_found');
  assert.equal(recentOf(env).total, 1);
});

test('fight telemetry is not a way around the per-socket message limit', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  let limited = 0;
  for (let i = 0; i < 400; i++) {
    const reply = send(m.a, fightEvent(m, 'start', i % 150));
    if (reply.code === 'rate_limited' || reply.code === 'limit') limited++;
  }
  assert.ok(limited > 0);
  assert.ok(env.host.sql.get('SELECT fights_started AS n FROM match_telemetry').n <= 150);
});

test('the history keeps the abuse diagnostics next to the fight and character numbers in every range', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  playFight(m, 0, { p1: 0, p2: 4, result: 'p1' });
  m.a.call('match.transport', { match: m.match, transport: 'direct' });
  const dup = env.connect(identity(1), { ip: '203.0.113.1' });
  dup.hello({ telemetry: true });
  env.advance(16 * MINUTE);
  for (const days of RANGES) {
    const history = env.hub.metricsHistory(days, 20);
    const abuse = Object.fromEntries(history.dimensions.abuse.map((d) => [d.value, d.count]));
    assert.ok(abuse.hello_accepted >= 2, `abuse diagnostics in ${days} days`);
    assert.ok(abuse.hello_reconnect_deduped >= 1, `session dedupe in ${days} days`);
    assert.equal(history.totals.fights_started, 1);
    assert.equal(history.totals.fights_completed, 1);
    assert.equal(history.totals.direct_fights, 1);
    assert.equal(history.characters.find((c) => c.id === 0).wins, 1);
    assert.equal(history.characters.find((c) => c.id === 4).losses, 1);
    assert.deepEqual(Object.keys(history.dimensions).sort(), ['abuse', 'country', 'fight_guest', 'fight_platform',
      'fight_transport', 'platform', 'reject', 'transport', 'version']);
    assert.equal(history.daily.at(-1).day, metricsDay(env.clock.now));
  }
});

test('the pre-telemetry client plays the same match with no fight numbers', () => {
  const env = makeHub();
  const m = startFightMatch(env, { telemetry: false });
  m.a.call('match.ready', { match: m.match, ready: true });
  m.b.call('match.ready', { match: m.match, ready: true });
  m.a.call('match.leave', { match: m.match });
  const [row] = recentOf(env).matches;
  assert.equal(row.fights_started, null);
  assert.equal(row.state, 'Ended');
});
